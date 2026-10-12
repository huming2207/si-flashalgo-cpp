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
    // Memory-mapped registers, as volatile pointers: every read and write goes to the hardware and
    // the compiler can't merge, reorder or drop them. (ST's CMSIS headers do the same with FLASH->CR.)

    // RCC
    volatile std::uint32_t *const RCC_CR = reinterpret_cast<volatile std::uint32_t *>(0x5800'0000);
    constexpr std::uint32_t RCC_CR_MSIRDY = 1u << 1;
    constexpr std::uint32_t RCC_CR_MSIRGSEL = 1u << 3;
    constexpr std::uint32_t RCC_CR_MSIRANGE_MASK = 0xfu << 4;
    constexpr std::uint32_t RCC_CR_MSIRANGE_16MHZ = 8u << 4;

    // FLASH
    volatile std::uint32_t *const FLASH_ACR = reinterpret_cast<volatile std::uint32_t *>(0x5800'4000);
    volatile std::uint32_t *const FLASH_KEYR = reinterpret_cast<volatile std::uint32_t *>(0x5800'4008);
    volatile std::uint32_t *const FLASH_SR = reinterpret_cast<volatile std::uint32_t *>(0x5800'4010);
    volatile std::uint32_t *const FLASH_CR = reinterpret_cast<volatile std::uint32_t *>(0x5800'4014);
    volatile std::uint32_t *const FLASH_OPTR = reinterpret_cast<volatile std::uint32_t *>(0x5800'4020);

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
    constexpr std::uint32_t FLASH_SR_PESD = 1u << 19;

    constexpr std::uint32_t FLASH_CR_PG = 1u << 0;
    constexpr std::uint32_t FLASH_CR_PER = 1u << 1;
    constexpr std::uint32_t FLASH_CR_MER = 1u << 2;
    constexpr std::uint32_t FLASH_CR_PNB_SHIFT = 3;
    constexpr std::uint32_t FLASH_CR_PNB_MASK = 0x7fu << FLASH_CR_PNB_SHIFT;
    constexpr std::uint32_t FLASH_CR_STRT = 1u << 16;
    constexpr std::uint32_t FLASH_CR_FSTPG = 1u << 18;
    constexpr std::uint32_t FLASH_CR_LOCK = 1u << 31;

    constexpr std::uint32_t FLASH_KEY1 = 0x4567'0123;
    constexpr std::uint32_t FLASH_KEY2 = 0xcdef'89ab;

    // IWDG
    volatile std::uint32_t *const IWDG_KR = reinterpret_cast<volatile std::uint32_t *>(0x4000'3000);
    constexpr std::uint32_t IWDG_KEY_RELOAD = 0xaaaa;

    // Unique device ID: UID96 then UID64, 20 bytes
    const volatile std::uint8_t *const UID96 = reinterpret_cast<const volatile std::uint8_t *>(0x1fff'7590);
    const volatile std::uint8_t *const UID64 = reinterpret_cast<const volatile std::uint8_t *>(0x1fff'7580);

    constexpr std::uint32_t flash_base = 0x0800'0000;
    constexpr std::uint32_t flash_size = 0x4'0000;
    constexpr std::uint32_t page_size = 0x800;
    constexpr std::uint32_t row_size = 256; // fast programming unit: 32 double words

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
        while ((*RCC_CR & RCC_CR_MSIRDY) == 0) {
        }
        *RCC_CR = (*RCC_CR & ~RCC_CR_MSIRANGE_MASK) | RCC_CR_MSIRANGE_16MHZ | RCC_CR_MSIRGSEL;
        while ((*RCC_CR & RCC_CR_MSIRDY) == 0) {
        }

        *FLASH_ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN);
        *FLASH_ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
        *FLASH_ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);

        kick_watchdog();
        return unlock();
    }

    exec_state deinit()
    {
        *FLASH_CR |= FLASH_CR_LOCK;
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
        *FLASH_CR = (*FLASH_CR & ~FLASH_CR_PNB_MASK) | FLASH_CR_PER | (page << FLASH_CR_PNB_SHIFT);
        *FLASH_CR |= FLASH_CR_STRT;
        state = wait_done();
        *FLASH_CR &= ~(FLASH_CR_PER | FLASH_CR_PNB_MASK);
        return state;
    }

    exec_state erase_chip()
    {
        exec_state state = begin_operation();
        if (state != flashalgo::ok) {
            return state;
        }
        *FLASH_CR |= FLASH_CR_MER;
        *FLASH_CR |= FLASH_CR_STRT;
        state = wait_done();
        *FLASH_CR &= ~FLASH_CR_MER;
        return state;
    }

    /*
     * Full, row-aligned 256-byte rows use fast programming (FSTPG): one high-voltage cycle per row
     * instead of one per double word. Anything else (a short tail, an unaligned start) falls back to
     * standard double-word programming. Both need the area erased first, which the programmer does.
     */
    exec_state program_page(std::uint32_t addr, std::span<const std::byte> data)
    {
        if (addr % 8 != 0 || addr < flash_base || addr - flash_base + data.size() > flash_size) {
            return flashalgo::err_invalid_addr;
        }

        exec_state state = flashalgo::ok;
        std::size_t off = 0;
        while (off < data.size() && state == flashalgo::ok) {
            const std::uint32_t dest = addr + off;
            const std::size_t left = data.size() - off;
            if (dest % row_size == 0 && left >= row_size) {
                state = program_row(dest, data.data() + off);
                off += row_size;
            } else {
                state = program_double_word(dest, data.subspan(off, left < 8 ? left : 8));
                off += 8;
            }
        }
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
            out[i] = std::byte(UID96[i]);
        }
        for (std::size_t i = 0; i < 8; i++) {
            out[12 + i] = std::byte(UID64[i]);
        }
        return flashalgo::ok;
    }

    /** Passes while read protection is still level 0 (RDP byte 0xAA). */
    [[=flashalgo::test{0xfffff008, "RDP level 0"}]] exec_state rdp_is_level0()
    {
        return (*FLASH_OPTR & 0xff) == 0xaa ? flashalgo::ok : exec_state(flashalgo::err_user_base + 1);
    }

private:
    /**
     * The algorithm does not start the IWDG, but if the option bytes start it in hardware it must
     * keep being reloaded, or the target resets in the middle of programming.
     */
    static void kick_watchdog()
    {
        *IWDG_KR = IWDG_KEY_RELOAD;
    }

    static exec_state unlock()
    {
        if ((*FLASH_CR & FLASH_CR_LOCK) != 0) {
            *FLASH_KEYR = FLASH_KEY1;
            *FLASH_KEYR = FLASH_KEY2;
        }
        return (*FLASH_CR & FLASH_CR_LOCK) == 0 ? flashalgo::ok : flashalgo::err_fail;
    }

    static exec_state begin_operation()
    {
        kick_watchdog();
        for (std::uint32_t spin = 0; (*FLASH_SR & FLASH_SR_BSY) != 0; spin++) {
            if (spin == busy_spin_limit) {
                return flashalgo::err_timeout;
            }
        }
        if ((*FLASH_SR & FLASH_SR_PESD) != 0) {
            return flashalgo::err_fail; // programming/erase suspended (the other core, on dual-core parts)
        }
        *FLASH_SR = FLASH_SR_ERRORS | FLASH_SR_EOP; // write 1 to clear
        return flashalgo::ok;
    }

    /** One 64-bit double word; a short `data` (the tail of a page) is padded with 0xff. */
    static exec_state program_double_word(std::uint32_t dest, std::span<const std::byte> data)
    {
        exec_state state = begin_operation();
        if (state != flashalgo::ok) {
            return state;
        }

        std::uint8_t dword[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        std::memcpy(dword, data.data(), data.size());
        std::uint32_t lo;
        std::uint32_t hi;
        std::memcpy(&lo, dword, 4);
        std::memcpy(&hi, dword + 4, 4);

        volatile std::uint32_t *flash = reinterpret_cast<volatile std::uint32_t *>(dest);
        *FLASH_CR |= FLASH_CR_PG;
        flash[0] = lo;
        flash[1] = hi;
        state = wait_done();
        *FLASH_CR &= ~FLASH_CR_PG;
        return state;
    }

    /**
     * One 256-byte row with fast programming (RM0461 3.3.8). The 64 word writes must follow each
     * other without a gap and without any flash access in between, or the row fails with MISSERR /
     * FASTERR. The algorithm already runs from RAM; interrupts are masked so that a pending one
     * can't fetch its vector from flash in the middle. Needs HCLK >= 8 MHz (init() sets 16 MHz).
     */
    static exec_state program_row(std::uint32_t dest, const std::byte *src)
    {
        exec_state state = begin_operation();
        if (state != flashalgo::ok) {
            return state;
        }

        std::uint32_t primask;
        asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask)::"memory");

        volatile std::uint32_t *flash = reinterpret_cast<volatile std::uint32_t *>(dest);
        *FLASH_CR |= FLASH_CR_FSTPG;
        for (std::uint32_t i = 0; i < row_size / 4; i++) {
            std::uint32_t word;
            std::memcpy(&word, src + i * 4, 4); // the programmer's buffer may be unaligned
            flash[i] = word;
        }
        state = wait_done();
        *FLASH_CR &= ~FLASH_CR_FSTPG;

        asm volatile("msr primask, %0" ::"r"(primask) : "memory");
        return state;
    }

    static exec_state wait_done()
    {
        for (std::uint32_t spin = 0; (*FLASH_SR & (FLASH_SR_BSY | FLASH_SR_CFGBSY)) != 0; spin++) {
            if (spin == busy_spin_limit) {
                return flashalgo::err_timeout;
            }
        }
        const std::uint32_t errors = *FLASH_SR & FLASH_SR_ERRORS;
        if (errors != 0) {
            *FLASH_SR = errors;
            return exec_state(flashalgo::err_user_base | static_cast<std::int32_t>(errors)); // the raw SR error bits, for debugging
        }
        return flashalgo::ok;
    }
};

FLASHALGO_EXPORT(stm32wle5);
