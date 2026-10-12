// expect: missing required hook: exec_state program_page
#include <flashalgo/flashalgo.hpp>
using flashalgo::exec_state;

struct algo {
    static constexpr flashalgo::device_info device{.name = "X", .flash_address = 0x08000000, .flash_size = 0x10000, .page_size = 0x800};
    exec_state erase_sector(std::uint32_t) { return flashalgo::ok; }
};
FLASHALGO_EXPORT(algo);
