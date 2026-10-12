#pragma once

#include <meta>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "../hooks.hpp"
#include "../types.hpp"

/*
 * Everything in here runs at compile time only. <meta>, std::vector (inside the std::meta API) and
 * std::array never reach the target: the only run-time output is the Verify dispatch and two
 * constant tables.
 */
namespace flashalgo::detail
{
    /** A compile-time error message for static_assert (C++26 user-generated static_assert messages). */
    struct diag {
        bool ok = true;
        char text[320]{};
        std::size_t len = 0;

        consteval diag &operator<<(std::string_view s)
        {
            for (char c : s) {
                if (len < sizeof(text) - 1) {
                    text[len++] = c;
                }
            }
            return *this;
        }

        consteval diag &hex(std::uint32_t v)
        {
            *this << "0x";
            for (int shift = 28; shift >= 0; shift -= 4) {
                *this << std::string_view{&"0123456789abcdef"[(v >> shift) & 0xf], 1};
            }
            return *this;
        }

        constexpr const char *data() const
        {
            return text;
        }

        constexpr std::size_t size() const
        {
            return len;
        }
    };

    consteval diag error(std::string_view what)
    {
        diag d{.ok = false};
        d << "flashalgo: " << what;
        return d;
    }

    consteval std::meta::access_context ctx()
    {
        return std::meta::access_context::unchecked();
    }

    consteval bool is_test(std::meta::info m)
    {
        return std::meta::is_function(m) && !std::meta::annotations_of_with_type(m, ^^test).empty();
    }

    consteval test test_of(std::meta::info m)
    {
        return std::meta::extract<test>(std::meta::annotations_of_with_type(m, ^^test)[0]);
    }

    consteval bool is_helper(std::meta::info m)
    {
        return !std::meta::annotations_of_with_type(m, ^^helper).empty();
    }

    /** The members the checks look at: named, non-special member functions. */
    consteval bool is_checked_function(std::meta::info m)
    {
        return std::meta::is_function(m) && !std::meta::is_special_member_function(m) && std::meta::has_identifier(m);
    }

    /** True when the test takes the programmer's RAM buffer: `exec_state t(std::span<std::byte>)`. */
    consteval bool test_takes_buffer(std::meta::info m)
    {
        return std::meta::parameters_of(m).size() == 1;
    }

    /** A test's display name: test{.name} when given, otherwise the function name. */
    struct test_name_buf {
        char text[32]{};
        std::size_t len = 0;
    };

    consteval test_name_buf test_name(std::meta::info m)
    {
        const test t = test_of(m);
        const std::string_view src = t.name[0] != '\0' ? std::string_view{t.name} : std::meta::identifier_of(m);
        test_name_buf out{};
        out.len = src.size();
        for (std::size_t i = 0; i < src.size() && i < sizeof(out.text) - 1; i++) {
            out.text[i] = src[i];
        }
        return out;
    }

    template <class A>
    consteval auto members()
    {
        return std::define_static_array(std::meta::members_of(^^A, ctx()));
    }

    template <class A>
    consteval std::size_t test_count()
    {
        std::size_t n = 0;
        for (auto m : std::meta::members_of(^^A, ctx())) {
            if (is_test(m)) {
                n += 1;
            }
        }
        return n;
    }

    template <class A>
    consteval std::span<const sector> sectors_of()
    {
        if constexpr (has_sectors<A>) {
            return A::sectors;
        } else {
            // Uniform flash: one run of page-sized sectors
            static constexpr sector uniform[] = {{A::device.page_size, 0}};
            return uniform;
        }
    }

    /** Hook name -> whether the algorithm's member with that name has the expected signature. */
    template <class A>
    consteval bool hook_signature_ok(std::string_view name, bool &is_hook, std::string_view &expected)
    {
        struct hook {
            std::string_view name;
            bool ok;
            std::string_view expected;
        };

        const hook hooks[] = {
            {"init", has_init<A>, "exec_state init(std::uint32_t, std::uint32_t, flashalgo::function)"},
            {"deinit", has_deinit<A>, "exec_state deinit()"},
            {"erase_sector", has_erase_sector<A>, "exec_state erase_sector(std::uint32_t)"},
            {"program_page", has_program_page<A>, "exec_state program_page(std::uint32_t, std::span<const std::byte>)"},
            {"erase_chip", has_erase_chip<A>, "exec_state erase_chip()"},
            {"verify", has_verify<A>, "std::uint32_t verify(std::uint32_t, std::span<const std::byte>)"},
            {"blank_check", has_blank_check<A>, "exec_state blank_check(std::uint32_t, std::uint32_t, std::uint8_t)"},
            {"read_flash", has_read_flash<A>, "exec_state read_flash(std::uint32_t, std::span<std::byte>)"},
        };

        for (const hook &h : hooks) {
            if (h.name == name) {
                is_hook = true;
                expected = h.expected;
                return h.ok;
            }
        }

        is_hook = false;
        return false;
    }

    template <class A>
    consteval diag check_test(std::meta::info m, std::span<const std::uint32_t> earlier_ids)
    {
        using std::meta::is_same_type;
        using std::meta::remove_cvref;

        const std::string_view fn = std::meta::identifier_of(m);
        const test t = test_of(m);

        if (std::meta::annotations_of_with_type(m, ^^test).size() != 1) {
            return error("test '") << fn << "' has more than one [[=flashalgo::test]] annotation";
        }

        const device_info &dev = A::device;
        if (t.id >= dev.flash_address && t.id - dev.flash_address < dev.flash_size) {
            return (error("test '") << fn << "' id ").hex(t.id) << " is inside the flash range; Verify would treat it as a normal verify";
        }

        for (std::uint32_t other : earlier_ids) {
            if (other == t.id) {
                return (error("test '") << fn << "' reuses id ").hex(t.id);
            }
        }

        if (test_name(m).len > sizeof(t.name) - 1) {
            return error("test '") << fn << "' name is longer than 31 characters (the programmer's limit); set a shorter test{.name}";
        }

        if (!is_same_type(remove_cvref(std::meta::return_type_of(m)), ^^exec_state)) {
            return error("test '") << fn << "' must return flashalgo::exec_state";
        }

        const auto params = std::meta::parameters_of(m);
        const bool params_ok =
            params.empty() || (params.size() == 1 && is_same_type(remove_cvref(std::meta::type_of(params[0])), ^^std::span<std::byte>));
        if (!params_ok) {
            return error("test '") << fn << "' must take () or (std::span<std::byte> buffer)";
        }

        return {};
    }

    /** All the rules an algorithm must follow, so mistakes fail the build instead of the production line. */
    template <class A>
    consteval diag check()
    {
        if constexpr (!has_device<A>) {
            return error("the algorithm needs `static constexpr flashalgo::device_info device{...};`");
        } else {
            const device_info &dev = A::device;
            if (dev.flash_size == 0 || dev.page_size == 0) {
                return error("device.flash_size and device.page_size must be non-zero");
            }

            const auto sectors = sectors_of<A>();
            if (sectors.empty()) {
                return error("sectors[] must not be empty");
            }
            for (std::size_t i = 0; i < sectors.size(); i++) {
                if (sectors[i].size == 0 || sectors[i].address >= dev.flash_size) {
                    return error("every sector run needs a non-zero size and an address (offset) inside the flash");
                }
                if (i > 0 && sectors[i].address <= sectors[i - 1].address) {
                    return error("sectors[] must be sorted by address with no duplicates");
                }
            }

            std::uint32_t ids[64]{};
            std::size_t id_count = 0;

            for (auto m : std::meta::members_of(^^A, ctx())) {
                if (!is_checked_function(m)) {
                    continue;
                }

                const std::string_view fn = std::meta::identifier_of(m);
                bool is_hook = false;
                std::string_view expected;
                const bool hook_ok = hook_signature_ok<A>(fn, is_hook, expected);

                if ((is_test(m) || is_hook) && !std::meta::is_public(m)) {
                    // The generated calls are ordinary member accesses, so they obey access control
                    return error("'") << fn << "' must be public: hooks and tests are called from flashalgo::entry_points";
                }

                if (is_test(m)) {
                    if (is_hook) {
                        return error("'") << fn << "' is a hook name and cannot also be a test";
                    }
                    if (id_count == std::size(ids)) {
                        return error("too many tests (max 64)");
                    }
                    diag d = check_test<A>(m, std::span{ids, id_count});
                    if (!d.ok) {
                        return d;
                    }
                    ids[id_count++] = test_of(m).id;
                } else if (is_hook) {
                    if (!hook_ok) {
                        return error("hook '") << fn << "' has the wrong signature, expected: " << expected;
                    }
                } else if (std::meta::is_public(m) && !is_helper(m)) {
                    return error("public member function '")
                           << fn << "' is not a hook or a [[=flashalgo::test]] (typo?). Make it private or mark it [[=flashalgo::helper{}]]";
                }
            }

            if (!has_erase_sector<A>) {
                return error("missing required hook: exec_state erase_sector(std::uint32_t)");
            }
            if (!has_program_page<A>) {
                return error("missing required hook: exec_state program_page(std::uint32_t, std::span<const std::byte>)");
            }

            return {};
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Tables for the programmer / host tools. Both are placed outside PrgCode by the linker script,
    // so they cost no target RAM.
    // ---------------------------------------------------------------------------------------------

    /** CMSIS-Pack FlashDevice (FlashOS.h), natural alignment as armcc/armclang lay it out. */
    template <std::size_t NSectors>
    struct flash_device_desc {
        std::uint16_t vers;
        char dev_name[128];
        std::uint16_t dev_type;
        std::uint32_t dev_addr;
        std::uint32_t device_size;
        std::uint32_t page_size;
        std::uint32_t reserved;
        std::uint8_t empty;
        std::uint32_t program_timeout;
        std::uint32_t erase_timeout;
        sector sectors[NSectors + 1]; // terminated with {0xffffffff, 0xffffffff}
    };

    template <class A>
    consteval auto make_device_desc()
    {
        constexpr auto sectors = sectors_of<A>();
        const device_info &dev = A::device;

        flash_device_desc<sectors.size()> d{};
        d.vers = 0x0101;
        for (std::size_t i = 0; i < sizeof(d.dev_name) - 1 && dev.name[i] != '\0'; i++) {
            d.dev_name[i] = dev.name[i];
        }
        d.dev_type = static_cast<std::uint16_t>(dev.type);
        d.dev_addr = dev.flash_address;
        d.device_size = dev.flash_size;
        d.page_size = dev.page_size;
        d.empty = dev.empty_value;
        d.program_timeout = dev.program_timeout_ms;
        d.erase_timeout = dev.erase_timeout_ms;
        for (std::size_t i = 0; i < sectors.size(); i++) {
            d.sectors[i] = sectors[i];
        }
        d.sectors[sectors.size()] = {0xffffffff, 0xffffffff};
        return d;
    }

    /**
     * Self test table, read by tools/flashalgo_tests.py to generate the programmer's `self_tests:` list.
     * Little-endian, fixed-size records so a host tool can parse it without relocations.
     */
    struct test_table_header {
        char magic[4];             // "FATS"
        std::uint16_t version;     // 1
        std::uint16_t count;       // number of records
        std::uint16_t record_size; // sizeof(test_record)
        std::uint16_t reserved;
    };

    enum test_kind : std::uint8_t {
        test_kind_simple = 0, // exec_state t()
        test_kind_buffer = 1, // exec_state t(std::span<std::byte>): uses the r1/r2 buffer from the programmer
    };

    struct test_record {
        std::uint32_t id;
        std::uint8_t kind;
        std::uint8_t reserved[3];
        char name[32];
    };

    template <std::size_t N>
    struct test_table {
        test_table_header header;
        std::array<test_record, N> records;
    };

    template <class A>
    consteval auto make_test_table()
    {
        constexpr std::size_t count = test_count<A>();
        test_table<count> t{};
        t.header = {{'F', 'A', 'T', 'S'}, 1, static_cast<std::uint16_t>(count), sizeof(test_record), 0};

        std::size_t i = 0;
        for (auto m : std::meta::members_of(^^A, ctx())) {
            if (!is_test(m)) {
                continue;
            }
            test_record &r = t.records[i++];
            r.id = test_of(m).id;
            r.kind = test_takes_buffer(m) ? test_kind_buffer : test_kind_simple;
            const test_name_buf name = test_name(m);
            for (std::size_t c = 0; c < sizeof(r.name); c++) {
                r.name[c] = name.text[c];
            }
        }
        return t;
    }
}
