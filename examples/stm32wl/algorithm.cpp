/*
 * STM32WLE5xC (256 KiB) flash algorithm with in-circuit self tests.
 *
 * Registers are written directly (RM0461) so the example builds without a vendor SDK; in a real
 * project you would include ST's CMSIS device header (stm32wle5xx.h) or LL drivers instead and keep
 * the same structure.
 *
 * Not verified on hardware yet.
 */

#include <cstring>

#include <flashalgo/flashalgo.hpp>

using flashalgo::exec_state;

namespace
{
    inline volatile std::uint32_t &reg(std::uintptr_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    // RCC
    constexpr std::uintptr_t RCC_CR = 0x5800'0000;
    constexpr std::uint32_t RCC_CR_MSIRDY = 1u << 1;
    constexpr std::uint32_t RCC_CR_MSIRGSEL = 1u << 3;
    constexpr std::uint32_t RCC_CR_MSIRANGE_MASK = 0xfu << 4;
    constexpr std::uint32_t RCC_CR_MSIRANGE_16MHZ = 8u << 4;

    // FLASH
    constexpr std::uintptr_t FLASH_ACR = 0x5800'4000;
    constexpr std::uintptr_t FLASH_KEYR = 0x5800'4008;
    constexpr std::uintptr_t FLASH_SR = 0x5800'4010;
    constexpr std::uintptr_t FLASH_CR = 0x5800'4014;
    constexpr std::uintptr_t FLASH_OPTR = 0x5800'4020;

    constexpr std::uint32_t FLASH_ACR_PRFTEN = 1u << 8;
    constexpr std::uint32_t FLASH_ACR_ICEN = 1u << 9;
    constexpr std::uint32_t FLASH_ACR_DCEN = 1u << 10;
    constexpr std::uint32_t FLASH_ACR_ICRST = 1u << 11;
    constexpr std::uint32_t FLASH_ACR_DCRST = 1u << 12;

    constexpr std::uint32_t FLASH_SR_EOP = 1u << 0;
    constexpr std::uint32_t FLASH_SR_ERRORS = (1u << 1) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) |
                                              (1u << 9) | (1u << 14) | (1u << 15); // OPERR..FASTERR, RDERR, OPTVERR
    constexpr std::uint32_t FLASH_SR_BSY = 1u << 16;
    constexpr std::uint32_t FLASH_SR_CFGBSY = 1u << 18;

    constexpr std::uint32_t FLASH_CR_PG = 1u << 0;
    constexpr std::uint32_t FLASH_CR_PER = 1u << 1;
    constexpr std::uint32_t FLASH_CR_MER = 1u << 2;
    constexpr std::uint32_t FLASH_CR_PNB_SHIFT = 3;
    constexpr std::uint32_t FLASH_CR_PNB_MASK = 0x7fu << FLASH_CR_PNB_SHIFT;
    constexpr std::uint32_t FLASH_CR_STRT = 1u << 16;
    constexpr std::uint32_t FLASH_CR_LOCK = 1u << 31;

    constexpr std::uint32_t FLASH_KEY1 = 0x4567'0123;
    constexpr std::uint32_t FLASH_KEY2 = 0xcdef'89ab;

    // IWDG
    constexpr std::uintptr_t IWDG_KR = 0x4000'3000;
    constexpr std::uint32_t IWDG_KEY_RELOAD = 0xaaaa;

    // Unique device ID: UID96 then UID64, 20 bytes
    constexpr std::uintptr_t UID96 = 0x1fff'7590;
    constexpr std::uintptr_t UID64 = 0x1fff'7580;

    constexpr std::uint32_t flash_base = 0x0800'0000;
    constexpr std::uint32_t flash_size = 0x4'0000;
    constexpr std::uint32_t page_size = 0x800;

    // ~ a few hundred ms at 16 MHz; mass erase is the slowest operation
    constexpr std::uint32_t busy_spin_limit = 4'000'000;
}

class stm32wle5
{
public:
    static constexpr flashalgo::device_info device{
        .name = "STM32WLE5xC 256KB Flash",
        .flash_address = flash_base,
        .flash_size = flash_size,
        .page_size = page_size,
        .program_timeout_ms = 1000,
        .erase_timeout_ms = 2000,
    };

    exec_state init(std::uint32_t, std::uint32_t, flashalgo::function)
    {
        // 16 MHz MSI needs no flash wait states, and the algorithm runs from RAM, so the flash
        // cache and prefetch don't help: turn them off.
        while ((reg(RCC_CR) & RCC_CR_MSIRDY) == 0) {
        }
        reg(RCC_CR) = (reg(RCC_CR) & ~RCC_CR_MSIRANGE_MASK) | RCC_CR_MSIRANGE_16MHZ | RCC_CR_MSIRGSEL;
        while ((reg(RCC_CR) & RCC_CR_MSIRDY) == 0) {
        }

        reg(FLASH_ACR) &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN);
        reg(FLASH_ACR) |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
        reg(FLASH_ACR) &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);

        kick_watchdog();
        return unlock();
    }

    exec_state deinit()
    {
        reg(FLASH_CR) |= FLASH_CR_LOCK;
        return flashalgo::ok;
    }

    exec_state erase_sector(std::uint32_t addr)
    {
        if (addr < flash_base || addr - flash_base >= flash_size) {
            return flashalgo::err_invalid_addr;
        }
        const std::uint32_t page = (addr - flash_base) / page_size;

        exec_state state = begin_operation();
        if (state != flashalgo::ok) {
            return state;
        }
        reg(FLASH_CR) = (reg(FLASH_CR) & ~FLASH_CR_PNB_MASK) | FLASH_CR_PER | (page << FLASH_CR_PNB_SHIFT);
        reg(FLASH_CR) |= FLASH_CR_STRT;
        state = wait_done();
        reg(FLASH_CR) &= ~(FLASH_CR_PER | FLASH_CR_PNB_MASK);
        return state;
    }

    exec_state erase_chip()
    {
        exec_state state = begin_operation();
        if (state != flashalgo::ok) {
            return state;
        }
        reg(FLASH_CR) |= FLASH_CR_MER;
        reg(FLASH_CR) |= FLASH_CR_STRT;
        state = wait_done();
        reg(FLASH_CR) &= ~FLASH_CR_MER;
        return state;
    }

    // Standard double-word programming (RM0461 3.3.8). Fast row programming (FSTPG, 256-byte rows)
    // is faster but has more preconditions; it is left out to keep the example short.
    exec_state program_page(std::uint32_t addr, std::span<const std::byte> data)
    {
        if (addr % 8 != 0 || addr < flash_base || addr - flash_base + data.size() > flash_size) {
            return flashalgo::err_invalid_addr;
        }

        exec_state state = begin_operation();
        if (state != flashalgo::ok) {
            return state;
        }

        reg(FLASH_CR) |= FLASH_CR_PG;
        for (std::size_t off = 0; off < data.size() && state == flashalgo::ok; off += 8) {
            std::uint8_t dword[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; // pads a short tail
            const std::size_t n = data.size() - off < 8 ? data.size() - off : 8;
            std::memcpy(dword, data.data() + off, n);

            std::uint32_t lo;
            std::uint32_t hi;
            std::memcpy(&lo, dword, 4);
            std::memcpy(&hi, dword + 4, 4);
            reg(addr + off) = lo;
            reg(addr + off + 4) = hi;
            state = wait_done();
        }
        reg(FLASH_CR) &= ~FLASH_CR_PG;
        return state;
    }

    // ---- In-circuit self tests: run by Soul Injector after programming ----

    /** Copies the 20-byte device UID into the programmer's buffer. */
    [[=flashalgo::test{0xfffff001, "Read device UID"}]] exec_state read_uid(std::span<std::byte> out)
    {
        if (out.size() < 20) {
            return flashalgo::err_invalid_arg;
        }
        for (std::size_t i = 0; i < 12; i++) {
            out[i] = std::byte(reinterpret_cast<const volatile std::uint8_t *>(UID96)[i]);
        }
        for (std::size_t i = 0; i < 8; i++) {
            out[12 + i] = std::byte(reinterpret_cast<const volatile std::uint8_t *>(UID64)[i]);
        }
        return flashalgo::ok;
    }

    /** Passes while read protection is still level 0 (RDP byte 0xAA). */
    [[=flashalgo::test{0xfffff008, "RDP level 0"}]] exec_state rdp_is_level0()
    {
        return (reg(FLASH_OPTR) & 0xff) == 0xaa ? flashalgo::ok : exec_state(flashalgo::err_user_base + 1);
    }

private:
    /**
     * The algorithm does not start the IWDG, but if the option bytes start it in hardware it must
     * keep being reloaded, or the target resets in the middle of programming.
     */
    static void kick_watchdog()
    {
        reg(IWDG_KR) = IWDG_KEY_RELOAD;
    }

    static exec_state unlock()
    {
        if ((reg(FLASH_CR) & FLASH_CR_LOCK) != 0) {
            reg(FLASH_KEYR) = FLASH_KEY1;
            reg(FLASH_KEYR) = FLASH_KEY2;
        }
        return (reg(FLASH_CR) & FLASH_CR_LOCK) == 0 ? flashalgo::ok : flashalgo::err_fail;
    }

    static exec_state begin_operation()
    {
        kick_watchdog();
        for (std::uint32_t spin = 0; (reg(FLASH_SR) & FLASH_SR_BSY) != 0; spin++) {
            if (spin == busy_spin_limit) {
                return flashalgo::err_timeout;
            }
        }
        reg(FLASH_SR) = FLASH_SR_ERRORS | FLASH_SR_EOP; // write 1 to clear
        return flashalgo::ok;
    }

    static exec_state wait_done()
    {
        for (std::uint32_t spin = 0; (reg(FLASH_SR) & FLASH_SR_CFGBSY) != 0; spin++) {
            if (spin == busy_spin_limit) {
                return flashalgo::err_timeout;
            }
        }
        const std::uint32_t errors = reg(FLASH_SR) & FLASH_SR_ERRORS;
        if (errors != 0) {
            reg(FLASH_SR) = errors;
            return exec_state(flashalgo::err_user_base | static_cast<std::int32_t>(errors)); // the raw SR error bits, for debugging
        }
        return flashalgo::ok;
    }
};

FLASHALGO_EXPORT(stm32wle5);
