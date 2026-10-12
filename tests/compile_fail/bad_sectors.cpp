// expect: sectors\[\] must be sorted by address with no duplicates
#include <flashalgo/flashalgo.hpp>
using flashalgo::exec_state;

struct algo {
    static constexpr flashalgo::device_info device{.name = "X", .flash_address = 0x08000000, .flash_size = 0x10000, .page_size = 0x800};
    static constexpr flashalgo::sector sectors[] = {{0x800, 0x1000}, {0x400, 0x0}};
    exec_state erase_sector(std::uint32_t) { return flashalgo::ok; }
    exec_state program_page(std::uint32_t, std::span<const std::byte>) { return flashalgo::ok; }
};
FLASHALGO_EXPORT(algo);
