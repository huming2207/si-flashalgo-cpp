// Every hook implemented, against a fake flash in host RAM.

#include <flashalgo/flashalgo.hpp>

#include "harness.hpp"

using flashalgo::exec_state;

namespace
{
    constexpr std::uint32_t flash_base = 0x08000000;
    constexpr std::uint32_t page_size = 0x100;
    constexpr std::uint32_t flash_size = 4 * page_size;

    std::uint8_t fake_flash[flash_size];
    int deinit_calls = 0;
    flashalgo::function last_function{};

    bool in_flash(std::uint32_t addr, std::uint32_t size)
    {
        return addr >= flash_base && addr - flash_base <= flash_size && size <= flash_size - (addr - flash_base);
    }
}

class fake_algo
{
public:
    static constexpr flashalgo::device_info device{
        .name = "Fake 1K",
        .flash_address = flash_base,
        .flash_size = flash_size,
        .page_size = page_size,
        .program_timeout_ms = 50,
        .erase_timeout_ms = 500,
    };

    // Two 0x100 sectors, then one 0x200 sector
    static constexpr flashalgo::sector sectors[] = {{0x100, 0x000}, {0x200, 0x200}};

    fake_algo() : constructed_magic(0xc0ffee)
    {
    }

    exec_state init(std::uint32_t addr, std::uint32_t, flashalgo::function fn)
    {
        if (addr != flash_base) {
            return flashalgo::err_invalid_addr;
        }
        last_function = fn;
        return flashalgo::ok;
    }

    exec_state deinit()
    {
        deinit_calls += 1;
        return flashalgo::ok;
    }

    exec_state erase_sector(std::uint32_t addr)
    {
        if (!in_flash(addr, page_size) || (addr - flash_base) % page_size != 0) {
            return flashalgo::err_invalid_addr;
        }
        std::memset(&fake_flash[addr - flash_base], 0xff, page_size);
        return flashalgo::ok;
    }

    exec_state program_page(std::uint32_t addr, std::span<const std::byte> data)
    {
        if (!in_flash(addr, data.size())) {
            return flashalgo::err_invalid_addr;
        }
        std::memcpy(&fake_flash[addr - flash_base], data.data(), data.size());
        return flashalgo::ok;
    }

    exec_state erase_chip()
    {
        std::memset(fake_flash, 0xff, sizeof(fake_flash));
        return flashalgo::ok;
    }

    std::uint32_t verify(std::uint32_t addr, std::span<const std::byte> expected)
    {
        std::uint32_t n = 0;
        while (n < expected.size() && in_flash(addr + n, 1) && std::byte{fake_flash[addr + n - flash_base]} == expected[n]) {
            n += 1;
        }
        return n;
    }

    exec_state blank_check(std::uint32_t addr, std::uint32_t size, std::uint8_t pattern)
    {
        if (!in_flash(addr, size)) {
            return flashalgo::err_invalid_addr;
        }
        for (std::uint32_t i = 0; i < size; i++) {
            if (fake_flash[addr - flash_base + i] != pattern) {
                return flashalgo::err_verify;
            }
        }
        return flashalgo::ok;
    }

    exec_state read_flash(std::uint32_t addr, std::span<std::byte> out)
    {
        if (!in_flash(addr, out.size())) {
            return flashalgo::err_invalid_addr;
        }
        std::memcpy(out.data(), &fake_flash[addr - flash_base], out.size());
        return flashalgo::ok;
    }

    [[=flashalgo::test{0xfffff001}]] exec_state read_uid(std::span<std::byte> out)
    {
        if (out.empty() || constructed_magic != 0xc0ffee) {
            return flashalgo::err_invalid_arg;
        }
        for (std::size_t i = 0; i < out.size(); i++) {
            out[i] = std::byte(0xa0 + i);
        }
        return flashalgo::ok;
    }

    [[=flashalgo::test{0xfffff004, "Clock check"}]] exec_state clock_check()
    {
        return flashalgo::ok;
    }

    [[=flashalgo::test{0xfffff003}]] exec_state fail_code()
    {
        return exec_state(0xffff0001); // a custom error code
    }

    [[=flashalgo::test{0xfffff00a}]] exec_state custom_code()
    {
        return exec_state(42);
    }

    [[=flashalgo::helper{}]] std::uint32_t pages() const
    {
        return flash_size / page_size;
    }

private:
    // Private members are not checked: no annotation needed
    std::uint32_t page_of(std::uint32_t addr) const
    {
        return (addr - flash_base) / page_size;
    }

    std::uint32_t constructed_magic = 0;
};

FLASHALGO_EXPORT(fake_algo);

int main()
{
    using namespace flashalgo;

    // Everything refuses to run before Init
    CHECK_EQ(EraseSector(flash_base), err_not_init);
    CHECK_EQ(Verify(0xfffff004, 0, nullptr), err_not_init);
    CHECK_EQ(UnInit(1), err_not_init);

    // Failing init leaves the algorithm uninitialised
    CHECK_EQ(Init(0x1234, 0, 1), err_invalid_addr);
    CHECK_EQ(EraseSector(flash_base), err_not_init);

    CHECK_EQ(Init(flash_base, 16'000'000, 2), ok);
    CHECK(last_function == function::program);
    CHECK_EQ(Init(flash_base, 16'000'000, 1), ok); // re-Init runs UnInit (deinit) first
    CHECK_EQ(deinit_calls, 1);
    CHECK(last_function == function::erase);

    // Erase / program / read back
    CHECK_EQ(EraseChip(), ok);
    CHECK_EQ(BlankCheck(flash_base, flash_size, 0xff), ok);

    std::uint8_t page[page_size];
    for (std::uint32_t i = 0; i < page_size; i++) {
        page[i] = std::uint8_t(i * 7);
    }
    CHECK_EQ(ProgramPage(flash_base + page_size, page_size, page), ok);
    CHECK_EQ(BlankCheck(flash_base + page_size, page_size, 0xff), err_verify);

    std::uint8_t back[page_size]{};
    CHECK_EQ(ReadFlash(flash_base + page_size, page_size, back), ok);
    CHECK(std::memcmp(back, page, page_size) == 0);

    // CMSIS verify: addr + size on success, first failing address otherwise
    CHECK_EQ(Verify(flash_base + page_size, page_size, page), flash_base + 2 * page_size);
    page[0x10] ^= 0xff;
    CHECK_EQ(Verify(flash_base + page_size, page_size, page), flash_base + page_size + 0x10);
    CHECK_EQ(Verify(flash_base, page_size, nullptr), flash_base);

    CHECK_EQ(EraseSector(flash_base + page_size), ok);
    CHECK_EQ(BlankCheck(flash_base + page_size, page_size, 0xff), ok);
    CHECK_EQ(EraseSector(flash_base + 1), err_invalid_addr);

    // Self tests: the address selects the test, the return value is its exec_state
    std::uint8_t uid[4]{};
    CHECK_EQ(Verify(0xfffff001, sizeof(uid), uid), ok);
    CHECK(uid[0] == 0xa0 && uid[1] == 0xa1 && uid[2] == 0xa2 && uid[3] == 0xa3);
    CHECK_EQ(Verify(0xfffff001, 0, nullptr), err_invalid_arg); // Soul Injector passes no buffer today
    CHECK_EQ(Verify(0xfffff004, 0, nullptr), ok);
    CHECK_EQ(Verify(0xfffff003, 0, nullptr), 0xffff0001);
    CHECK_EQ(Verify(0xfffff00a, 0, nullptr), 42);

    // FlashDevice, in CMSIS layout
    namespace off = flash_device_offset;
    CHECK_EQ(harness::u16(FlashDevice + off::vers), 0x0101);
    CHECK(std::strcmp(reinterpret_cast<const char *>(FlashDevice + off::dev_name), "Fake 1K") == 0);
    CHECK_EQ(harness::u16(FlashDevice + off::dev_type), 1);
    CHECK_EQ(harness::u32(FlashDevice + off::dev_addr), flash_base);
    CHECK_EQ(harness::u32(FlashDevice + off::device_size), flash_size);
    CHECK_EQ(harness::u32(FlashDevice + off::page_size), page_size);
    CHECK_EQ(FlashDevice[off::empty], 0xff);
    CHECK_EQ(harness::u32(FlashDevice + off::program_timeout), 50);
    CHECK_EQ(harness::u32(FlashDevice + off::erase_timeout), 500);
    CHECK_EQ(harness::u32(FlashDevice + off::sectors + 0), 0x100);
    CHECK_EQ(harness::u32(FlashDevice + off::sectors + 4), 0x000);
    CHECK_EQ(harness::u32(FlashDevice + off::sectors + 8), 0x200);
    CHECK_EQ(harness::u32(FlashDevice + off::sectors + 12), 0x200);
    CHECK_EQ(harness::u32(FlashDevice + off::sectors + 16), 0xffffffff);
    CHECK_EQ(harness::u32(FlashDevice + off::sectors + 20), 0xffffffff);

    // Self test table: header + 40-byte records in declaration order
    CHECK(std::memcmp(FlashAlgoTests, "FATS", 4) == 0);
    CHECK_EQ(harness::u16(FlashAlgoTests + 4), 1);
    CHECK_EQ(harness::u16(FlashAlgoTests + 6), 4);
    CHECK_EQ(harness::u16(FlashAlgoTests + 8), 40);
    struct expected_record {
        std::uint32_t id;
        std::uint8_t kind;
        const char *name;
    };
    const expected_record expected[] = {
        {0xfffff001, 1, "read_uid"},
        {0xfffff004, 0, "Clock check"},
        {0xfffff003, 0, "fail_code"},
        {0xfffff00a, 0, "custom_code"},
    };
    for (int i = 0; i < 4; i++) {
        const std::uint8_t *rec = FlashAlgoTests + 12 + 40 * i;
        CHECK_EQ(harness::u32(rec), expected[i].id);
        CHECK_EQ(rec[4], expected[i].kind);
        CHECK(std::strcmp(reinterpret_cast<const char *>(rec + 8), expected[i].name) == 0);
    }

    CHECK_EQ(UnInit(1), ok);
    CHECK_EQ(deinit_calls, 2);
    CHECK_EQ(UnInit(1), err_not_init);

    return harness::result("test_full");
}
