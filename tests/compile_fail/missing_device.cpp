// expect: the algorithm needs `static constexpr flashalgo::device_info device\{\.\.\.\};`
#include <flashalgo/flashalgo.hpp>
using flashalgo::exec_state;

struct algo {
    exec_state erase_sector(std::uint32_t) { return flashalgo::ok; }
    exec_state program_page(std::uint32_t, std::span<const std::byte>) { return flashalgo::ok; }
};
FLASHALGO_EXPORT(algo);
