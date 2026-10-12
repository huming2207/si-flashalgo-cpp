/*
 * Linker script for flashalgo algorithms.
 *
 * Link with (flashalgo_add_algorithm() in CMake does this for you):
 *   -nostartfiles -Wl,--gc-sections -T flashalgo.ld -Wl,--defsym=FLASHALGO_LOAD_ADDRESS=0x20000020
 *
 * The code is not position independent: FLASHALGO_LOAD_ADDRESS must be the address the programmer
 * loads the image to (Soul Injector's `load_address`).
 */

SECTIONS
{
    ASSERT(DEFINED(FLASHALGO_LOAD_ADDRESS), "flashalgo: define FLASHALGO_LOAD_ADDRESS (-Wl,--defsym=FLASHALGO_LOAD_ADDRESS=0x...)")
    . = DEFINED(FLASHALGO_LOAD_ADDRESS) ? FLASHALGO_LOAD_ADDRESS : 0;

    /*
     * Everything the programmer loads, in one blob. The PrgCode name comes from the CMSIS-Pack / armlink
     * templates and is what target-gen and tools/flashalgo_yaml.py look for. Data and bss go in here too
     * (bss as zero bytes), because the programmer does not run startup code to copy or clear anything,
     * and the code does not use R9-relative (RWPI) data that a separate PrgData would need.
     */
    PrgCode : {
        KEEP(*(.entry))
        KEEP(*(.entry.*))

        *(.text .text.*)
        *(.text:*) /* TI naming convention */

        *(.rodata .rodata.*)
        *(.srodata .srodata.*)

        *(.data .data.*)
        *(.sdata .sdata.*)

        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)

        *(.uninit .uninit.*)

        . = ALIGN(4);
    }

    /* CMSIS FlashDevice description; read from the ELF by host tools, never loaded */
    DeviceData . : {
        KEEP(*(DeviceData))
    }

    /* Self test table (see include/flashalgo/detail/reflect.hpp); non-allocated, ELF only */
    .flashalgo_tests 0 (INFO) : {
        KEEP(*(.flashalgo_tests))
    }

    /*
     * Nothing runs global constructors in a flash algorithm. Collect them only to fail the link,
     * instead of shipping an algorithm whose globals are silently uninitialised.
     */
    .flashalgo_ctors 0 (INFO) : {
        KEEP(*(.init_array .init_array.* .ctors .ctors.* .preinit_array .preinit_array.*))
    }
    ASSERT(SIZEOF(.flashalgo_ctors) == 0, "flashalgo: a global needs a run-time constructor, but nothing runs them in a flash algorithm. Make it constinit/constexpr or construct it in init().")

    /DISCARD/ : {
        /* Unwind info: flash algorithms are built without exceptions */
        *(.ARM.exidx .ARM.exidx.*)
        *(.ARM.extab .ARM.extab.*)
        *(.eh_frame .eh_frame_hdr)
        *(.fini_array .fini_array.* .dtors .dtors.*)
    }
}
