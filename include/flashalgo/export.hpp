#pragma once

#include "entry.hpp"

/*
 * FLASHALGO_EXPORT(T) - the one macro of the library.
 *
 * The programmer finds the entry points by their C names (Init, UnInit, ProgramPage, ...). C++ has
 * no way for a template to define an extern "C" function, and GCC ignores asm("name") labels on
 * members of class templates, so the names are attached with toplevel extended asm (GCC >= 15):
 * each one is an alias of the instantiated entry_points<T> member, with no wrapper code. Optional
 * entry points are wrapped in `.if <present>` so they only exist when T implements the hook.
 */

#if defined(__thumb__)
// .thumb_set marks the alias as a Thumb function, so its value carries bit 0 like the original
#define FLASHALGO_DETAIL_FN(sym, op) ".globl " sym "\n\t.thumb_set " sym ", %c" #op "\n\t"
#define FLASHALGO_DETAIL_TYPE_PREFIX "%%"
#elif defined(__arm__)
#define FLASHALGO_DETAIL_FN(sym, op) ".globl " sym "\n\t.set " sym ", %c" #op "\n\t.type " sym ", %%function\n\t"
#define FLASHALGO_DETAIL_TYPE_PREFIX "%%"
#else
#define FLASHALGO_DETAIL_FN(sym, op) ".globl " sym "\n\t.set " sym ", %c" #op "\n\t.type " sym ", @function\n\t"
#define FLASHALGO_DETAIL_TYPE_PREFIX "@"
#endif

#define FLASHALGO_DETAIL_OBJ(sym, op) ".globl " sym "\n\t.set " sym ", %c" #op "\n\t.type " sym ", " FLASHALGO_DETAIL_TYPE_PREFIX "object\n\t"
#define FLASHALGO_DETAIL_OPT_FN(sym, present_op, op) ".if %c" #present_op "\n\t" FLASHALGO_DETAIL_FN(sym, op) ".endif\n\t"

// No explicit instantiation: naming the members below instantiates exactly what is exported (and runs
// the static_assert checks), while helpers stay inline instead of getting out-of-line copies.
#define FLASHALGO_EXPORT(T)                                                                                                                          \
    asm(FLASHALGO_DETAIL_FN("Init", 0) FLASHALGO_DETAIL_FN("UnInit", 1) FLASHALGO_DETAIL_FN("EraseSector", 2)                                       \
            FLASHALGO_DETAIL_FN("ProgramPage", 3) FLASHALGO_DETAIL_OBJ("FlashDevice", 4) FLASHALGO_DETAIL_OBJ("FlashAlgoTests", 5)                   \
                FLASHALGO_DETAIL_OPT_FN("EraseChip", 6, 7) FLASHALGO_DETAIL_OPT_FN("Verify", 8, 9) FLASHALGO_DETAIL_OPT_FN("BlankCheck", 10, 11)     \
                    FLASHALGO_DETAIL_OPT_FN("ReadFlash", 12, 13)                                                                                     \
        :                                                                                                                                            \
        : "s"(&flashalgo::entry_points<T>::Init), "s"(&flashalgo::entry_points<T>::UnInit), "s"(&flashalgo::entry_points<T>::EraseSector),          \
          "s"(&flashalgo::entry_points<T>::ProgramPage), "s"(&flashalgo::entry_points<T>::device_desc),                                              \
          "s"(&flashalgo::entry_points<T>::test_table), "i"(flashalgo::detail::exports_erase_chip<T> ? 1 : 0),                                 \
          "s"(flashalgo::entry_points<T>::erase_chip_entry()), "i"(flashalgo::detail::exports_verify<T> ? 1 : 0),                              \
          "s"(flashalgo::entry_points<T>::verify_entry()), "i"(flashalgo::detail::exports_blank_check<T> ? 1 : 0),                             \
          "s"(flashalgo::entry_points<T>::blank_check_entry()), "i"(flashalgo::detail::exports_read_flash<T> ? 1 : 0),                         \
          "s"(flashalgo::entry_points<T>::read_flash_entry()))
