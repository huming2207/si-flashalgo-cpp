#pragma once

#include <cstddef>
#include <cstdint>

namespace flashalgo
{
    /**
     * Result of every hook and self test. Zero is success; the value is returned to the
     * programmer in r0 as-is, so any other int32 value can be used as a custom error code,
     * e.g. `return flashalgo::exec_state(0x70d0);`.
     */
    enum exec_state : std::int32_t {
        ok = 0,
        err_fail = 1,
        err_not_init = 2,
        err_invalid_arg = 3,
        err_invalid_addr = 4,
        err_timeout = 5,
        err_verify = 6,
        err_unsupported = 7,
        err_user_base = 0x100, // Custom codes should start here to stay clear of the generic ones
    };

    /** The `fnc` argument of CMSIS Init/UnInit: which operation the programmer is about to run. */
    enum class function : std::uint32_t {
        erase = 1,
        program = 2,
        verify = 3,
        blank_check = 4,
    };

    /** CMSIS FlashDevice.DevType */
    enum class device_type : std::uint16_t {
        unknown = 0,
        onchip = 1,
        ext_8bit = 2,
        ext_16bit = 3,
        ext_32bit = 4,
        ext_spi = 5,
    };

    /** One run of equally-sized sectors; `address` is an offset from device_info::flash_address. */
    struct sector {
        std::uint32_t size;
        std::uint32_t address;
    };

    /** What the algorithm programs. Declared as `static constexpr flashalgo::device_info device{...};` in the algorithm. */
    struct device_info {
        char name[128]{};
        device_type type = device_type::onchip;
        std::uint32_t flash_address = 0;
        std::uint32_t flash_size = 0;
        std::uint32_t page_size = 0;
        std::uint8_t empty_value = 0xff;
        std::uint32_t program_timeout_ms = 100;
        std::uint32_t erase_timeout_ms = 3000;
    };

    /**
     * Annotation for an in-circuit self test: `[[=flashalgo::test{0xfffff004}]] exec_state clock_check();`
     *
     * The programmer runs it by calling Verify with `id` as the address. The id must lie outside the
     * flash range. `name` is optional (max 31 chars); the function name is used when it is empty.
     */
    struct test {
        std::uint32_t id;
        char name[32]{};
    };

    /** Annotation for a public member function that is neither a hook nor a test. */
    struct helper {};
}
