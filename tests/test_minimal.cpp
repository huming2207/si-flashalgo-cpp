// Only the two required hooks: the optional entry points must not be exported at all.

#include <flashalgo/flashalgo.hpp>

#include "harness.hpp"

namespace
{
    int erase_calls = 0;
}

struct minimal_algo {
    static constexpr flashalgo::device_info device{.name = "Minimal", .flash_address = 0x0, .flash_size = 0x10000, .page_size = 0x400};

    flashalgo::exec_state erase_sector(std::uint32_t)
    {
        erase_calls += 1;
        return flashalgo::ok;
    }

    flashalgo::exec_state program_page(std::uint32_t, std::span<const std::byte> data)
    {
        return data.size() == 0x400 ? flashalgo::ok : flashalgo::err_invalid_arg;
    }
};

FLASHALGO_EXPORT(minimal_algo);

int main()
{
    using namespace flashalgo;

    CHECK(&EraseChip == nullptr);
    CHECK(&Verify == nullptr);
    CHECK(&BlankCheck == nullptr);
    CHECK(&ReadFlash == nullptr);

    CHECK_EQ(Init(0, 0, 1), ok); // no init() hook: Init only constructs the object
    CHECK_EQ(EraseSector(0x400), ok);
    CHECK_EQ(erase_calls, 1);

    std::uint8_t page[0x400]{};
    CHECK_EQ(ProgramPage(0, sizeof(page), page), ok);
    CHECK_EQ(ProgramPage(0, 4, page), err_invalid_arg);
    CHECK_EQ(UnInit(1), ok);

    // No sectors[] given: one run of page-sized sectors
    namespace off = flash_device_offset;
    CHECK_EQ(harness::u32(FlashDevice + off::sectors + 0), 0x400);
    CHECK_EQ(harness::u32(FlashDevice + off::sectors + 4), 0);
    CHECK_EQ(harness::u32(FlashDevice + off::sectors + 8), 0xffffffff);

    // Empty test table
    CHECK(std::memcmp(FlashAlgoTests, "FATS", 4) == 0);
    CHECK_EQ(harness::u16(FlashAlgoTests + 6), 0);

    return harness::result("test_minimal");
}
