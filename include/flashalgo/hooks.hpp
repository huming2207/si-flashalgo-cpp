#pragma once

#include <concepts>
#include <cstdint>
#include <span>

#include "types.hpp"

/*
 * The member functions an algorithm may provide. Only erase_sector and program_page are required;
 * an entry point for an optional hook is exported only when the hook exists, so the programmer
 * can tell from the ELF what the algorithm supports.
 *
 *   exec_state init(std::uint32_t flash_addr, std::uint32_t clock_hz, flashalgo::function fn);  // optional
 *   exec_state deinit();                                                                      // optional
 *   exec_state erase_sector(std::uint32_t addr);
 *   exec_state program_page(std::uint32_t addr, std::span<const std::byte> data);
 *   exec_state erase_chip();                                                                  // optional -> EraseChip
 *   std::uint32_t verify(std::uint32_t addr, std::span<const std::byte> expected);           // optional, returns bytes that matched
 *   exec_state blank_check(std::uint32_t addr, std::uint32_t size, std::uint8_t pattern);     // optional -> BlankCheck
 *   exec_state read_flash(std::uint32_t addr, std::span<std::byte> out);                      // optional -> ReadFlash
 */
namespace flashalgo
{
    template <class A>
    concept has_init = requires(A &a, std::uint32_t u, function fn) {
        { a.init(u, u, fn) } -> std::same_as<exec_state>;
    };

    template <class A>
    concept has_deinit = requires(A &a) {
        { a.deinit() } -> std::same_as<exec_state>;
    };

    template <class A>
    concept has_erase_sector = requires(A &a, std::uint32_t u) {
        { a.erase_sector(u) } -> std::same_as<exec_state>;
    };

    template <class A>
    concept has_program_page = requires(A &a, std::uint32_t u, std::span<const std::byte> d) {
        { a.program_page(u, d) } -> std::same_as<exec_state>;
    };

    template <class A>
    concept has_erase_chip = requires(A &a) {
        { a.erase_chip() } -> std::same_as<exec_state>;
    };

    template <class A>
    concept has_verify = requires(A &a, std::uint32_t u, std::span<const std::byte> d) {
        { a.verify(u, d) } -> std::same_as<std::uint32_t>;
    };

    template <class A>
    concept has_blank_check = requires(A &a, std::uint32_t u, std::uint8_t p) {
        { a.blank_check(u, u, p) } -> std::same_as<exec_state>;
    };

    template <class A>
    concept has_read_flash = requires(A &a, std::uint32_t u, std::span<std::byte> d) {
        { a.read_flash(u, d) } -> std::same_as<exec_state>;
    };

    template <class A>
    concept has_device = requires {
        { A::device } -> std::convertible_to<const device_info &>;
    };

    template <class A>
    concept has_sectors = requires {
        { A::sectors[0] } -> std::convertible_to<const sector &>;
        sizeof(A::sectors) / sizeof(A::sectors[0]);
    };
}
