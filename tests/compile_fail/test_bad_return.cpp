// expect: test 'a' must return flashalgo::exec_state
#include <flashalgo/flashalgo.hpp>
using flashalgo::exec_state;

struct algo {
    static constexpr flashalgo::device_info device{.name = "X", .flash_address = 0x08000000, .flash_size = 0x10000, .page_size = 0x800};
    exec_state erase_sector(std::uint32_t) { return flashalgo::ok; }
    exec_state program_page(std::uint32_t, std::span<const std::byte>) { return flashalgo::ok; }
    [[=flashalgo::test{0xfffff001}]] bool a() { return true; }
};
FLASHALGO_EXPORT(algo);
