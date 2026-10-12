#pragma once

/*
 * flashalgo - header-only CMSIS-Pack flash algorithms with in-circuit self tests, in C++26.
 *
 * Requires GCC >= 16 with -std=gnu++26 -freflection. See README.md.
 */

#if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202506L
#error "flashalgo needs C++26 reflection: use GCC >= 16 with -std=gnu++26 -freflection"
#endif

#if defined(__cpp_exceptions) && !defined(FLASHALGO_ALLOW_EXCEPTIONS)
#error "flash algorithms have no unwinder: build with -fno-exceptions"
#endif

#include "types.hpp"
#include "hooks.hpp"
#include "entry.hpp"
#include "export.hpp"
