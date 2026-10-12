#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

#include "detail/reflect.hpp"
#include "hooks.hpp"
#include "types.hpp"

namespace flashalgo
{
    namespace detail
    {
        // Variable templates rather than static members: they are only used in constant expressions,
        // so nothing is emitted for them (static data members would be, by the explicit instantiation).
        template <class A>
        constexpr bool exports_erase_chip = has_erase_chip<A>;
        template <class A>
        constexpr bool exports_verify = has_verify<A> || test_count<A>() > 0;
        template <class A>
        constexpr bool exports_blank_check = has_blank_check<A>;
        template <class A>
        constexpr bool exports_read_flash = has_read_flash<A>;
    }

    /**
     * The CMSIS-Pack flash algorithm ABI for one algorithm type `A`.
     *
     * The programmer loads the image into RAM and calls these with r0..r3 as arguments, without
     * running any startup code. So there are no global constructors here: the algorithm object
     * lives in raw static storage and is constructed by Init and destroyed by UnInit.
     *
     * Don't use this class directly; FLASHALGO_EXPORT(A) instantiates it and exports the C names.
     */
    template <class A>
    class entry_points
    {
        static_assert(detail::check<A>().ok, detail::check<A>());

        alignas(A) static inline std::byte storage[sizeof(A)];
        static inline bool is_init = false;

        [[gnu::always_inline]] static A &self()
        {
            return *std::launder(reinterpret_cast<A *>(storage));
        }

        [[gnu::always_inline]] static std::uint32_t result(exec_state state)
        {
            return static_cast<std::uint32_t>(state);
        }

        /** Runs the test whose id is `addr`; returns false when `addr` is not a test id. */
        [[gnu::always_inline]] static bool run_test(std::uint32_t addr, std::uint32_t size, std::uint8_t *data, std::uint32_t &ret)
        {
            template for (constexpr auto m : detail::members<A>()) {
                if constexpr (detail::is_test(m)) {
                    if (addr == detail::test_of(m).id) {
                        if (!is_init) {
                            ret = result(err_not_init);
                        } else if constexpr (detail::test_takes_buffer(m)) {
                            // The programmer may pass no buffer (r2 == 0); the test sees an empty span then
                            auto *buf = reinterpret_cast<std::byte *>(data);
                            ret = result(self().[:m:](std::span<std::byte>{buf, buf == nullptr ? 0 : size}));
                        } else {
                            ret = result(self().[:m:]());
                        }
                        return true;
                    }
                }
            }
            return false;
        }

    public:
        // ---- int Init(unsigned long adr, unsigned long clk, unsigned long fnc) ----
        [[gnu::section(".entry")]] static std::uint32_t Init(std::uint32_t addr, std::uint32_t clock, std::uint32_t fn)
        {
            if (is_init) {
                UnInit(fn);
            }

            ::new (static_cast<void *>(storage)) A{};
            is_init = true;

            if constexpr (has_init<A>) {
                const exec_state state = self().init(addr, clock, static_cast<function>(fn));
                if (state != ok) {
                    self().~A();
                    is_init = false;
                }
                return result(state);
            } else {
                (void)addr;
                (void)clock;
                return result(ok);
            }
        }

        // ---- int UnInit(unsigned long fnc) ----
        [[gnu::section(".entry")]] static std::uint32_t UnInit(std::uint32_t)
        {
            if (!is_init) {
                return result(err_not_init);
            }

            exec_state state = ok;
            if constexpr (has_deinit<A>) {
                state = self().deinit();
            }
            self().~A();
            is_init = false;
            return result(state);
        }

        // ---- int EraseSector(unsigned long adr) ----
        [[gnu::section(".entry")]] static std::uint32_t EraseSector(std::uint32_t addr)
        {
            return is_init ? result(self().erase_sector(addr)) : result(err_not_init);
        }

        // ---- int ProgramPage(unsigned long adr, unsigned long sz, unsigned char *buf) ----
        [[gnu::section(".entry")]] static std::uint32_t ProgramPage(std::uint32_t addr, std::uint32_t size, const std::uint8_t *data)
        {
            if (!is_init) {
                return result(err_not_init);
            }
            return result(self().program_page(addr, std::span{reinterpret_cast<const std::byte *>(data), size}));
        }

        // ---- int EraseChip(void) ----
        [[gnu::section(".entry")]] static std::uint32_t EraseChip()
            requires detail::exports_erase_chip<A>
        {
            return is_init ? result(self().erase_chip()) : result(err_not_init);
        }

        /*
         * ---- unsigned long Verify(unsigned long adr, unsigned long sz, unsigned char *buf) ----
         * A self test id runs that test and returns its exec_state (0 = pass). Any other address is
         * a CMSIS verify: return adr + sz on success, otherwise the first failing address.
         */
        [[gnu::section(".entry")]] static std::uint32_t Verify(std::uint32_t addr, std::uint32_t size, std::uint8_t *data)
            requires detail::exports_verify<A>
        {
            std::uint32_t ret = 0;
            if (run_test(addr, size, data, ret)) {
                return ret;
            }

            if constexpr (has_verify<A>) {
                if (!is_init || data == nullptr) {
                    return addr;
                }
                const std::uint32_t matched = self().verify(addr, std::span{reinterpret_cast<const std::byte *>(data), size});
                return addr + (matched < size ? matched : size);
            } else {
                // Only tests were declared; there is nothing to verify against
                return addr + size;
            }
        }

        // ---- int BlankCheck(unsigned long adr, unsigned long sz, unsigned char pat) ----
        [[gnu::section(".entry")]] static std::uint32_t BlankCheck(std::uint32_t addr, std::uint32_t size, std::uint32_t pattern)
            requires detail::exports_blank_check<A>
        {
            return is_init ? result(self().blank_check(addr, size, static_cast<std::uint8_t>(pattern))) : result(err_not_init);
        }

        // ---- int ReadFlash(unsigned long adr, unsigned long sz, unsigned char *buf) (probe-rs extension) ----
        [[gnu::section(".entry")]] static std::uint32_t ReadFlash(std::uint32_t addr, std::uint32_t size, std::uint8_t *data)
            requires detail::exports_read_flash<A>
        {
            if (!is_init) {
                return result(err_not_init);
            }
            return result(self().read_flash(addr, std::span{reinterpret_cast<std::byte *>(data), size}));
        }

        // Symbols for the optional entry points. When one is absent its alias is skipped by `.if 0`
        // in FLASHALGO_EXPORT, which still needs some symbol to name, so Init stands in.
        static consteval auto erase_chip_entry()
        {
            if constexpr (detail::exports_erase_chip<A>) {
                return &EraseChip;
            } else {
                return &Init;
            }
        }

        static consteval auto verify_entry()
        {
            if constexpr (detail::exports_verify<A>) {
                return &Verify;
            } else {
                return &Init;
            }
        }

        static consteval auto blank_check_entry()
        {
            if constexpr (detail::exports_blank_check<A>) {
                return &BlankCheck;
            } else {
                return &Init;
            }
        }

        static consteval auto read_flash_entry()
        {
            if constexpr (detail::exports_read_flash<A>) {
                return &ReadFlash;
            } else {
                return &Init;
            }
        }

        /** CMSIS FlashDevice, in the DeviceData section that target-gen / the programmer reads. */
        [[gnu::section("DeviceData")]] static constexpr auto device_desc = detail::make_device_desc<A>();

        /** Self test table for host tools (tools/flashalgo_tests.py); never loaded to the target. */
        [[gnu::section(".flashalgo_tests")]] static constexpr auto test_table = detail::make_test_table<A>();
    };
}
