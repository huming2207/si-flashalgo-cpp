#pragma once

/*
 * Host test harness: the test plays the programmer and calls the exported C entry points the
 * same way a debug probe does on the target. Only one algorithm can be exported per program,
 * so every test_*.cpp is its own executable.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
    std::uint32_t Init(std::uint32_t addr, std::uint32_t clock, std::uint32_t fn);
    std::uint32_t UnInit(std::uint32_t fn);
    std::uint32_t EraseSector(std::uint32_t addr);
    std::uint32_t ProgramPage(std::uint32_t addr, std::uint32_t size, const std::uint8_t *data);

    // Optional entry points: weak, so a test can check they were not exported (address == nullptr)
    [[gnu::weak]] std::uint32_t EraseChip();
    [[gnu::weak]] std::uint32_t Verify(std::uint32_t addr, std::uint32_t size, std::uint8_t *data);
    [[gnu::weak]] std::uint32_t BlankCheck(std::uint32_t addr, std::uint32_t size, std::uint32_t pattern);
    [[gnu::weak]] std::uint32_t ReadFlash(std::uint32_t addr, std::uint32_t size, std::uint8_t *data);

    extern const std::uint8_t FlashDevice[];
    extern const std::uint8_t FlashAlgoTests[];
}

namespace harness
{
    inline int failures = 0;

    inline void check(bool ok, const char *expr, const char *file, int line)
    {
        if (!ok) {
            std::printf("%s:%d: CHECK failed: %s\n", file, line, expr);
            failures += 1;
        }
    }

    inline void check_eq(std::uint32_t got, std::uint32_t want, const char *expr, const char *file, int line)
    {
        if (got != want) {
            std::printf("%s:%d: CHECK_EQ failed: %s -> 0x%08x, want 0x%08x\n", file, line, expr, got, want);
            failures += 1;
        }
    }

    /** Little-endian field readers for the FlashDevice / FlashAlgoTests byte layouts. */
    inline std::uint32_t u32(const std::uint8_t *p)
    {
        return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
    }

    inline std::uint32_t u16(const std::uint8_t *p)
    {
        return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8;
    }

    inline int result(const char *name)
    {
        if (failures == 0) {
            std::printf("%s: all checks passed\n", name);
            return 0;
        }
        std::printf("%s: %d check(s) failed\n", name, failures);
        return 1;
    }
}

#define CHECK(expr) harness::check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(expr, want) harness::check_eq((expr), (want), #expr, __FILE__, __LINE__)

// FlashDevice field offsets (CMSIS FlashOS.h with natural alignment)
namespace flash_device_offset
{
    constexpr int vers = 0;
    constexpr int dev_name = 2;
    constexpr int dev_type = 130;
    constexpr int dev_addr = 132;
    constexpr int device_size = 136;
    constexpr int page_size = 140;
    constexpr int empty = 148;
    constexpr int program_timeout = 152;
    constexpr int erase_timeout = 156;
    constexpr int sectors = 160;
}
