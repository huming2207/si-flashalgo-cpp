# flashalgo

A header-only C++26 library for writing [CMSIS-Pack flash algorithms](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/flashAlgorithm.html)
and in-circuit self tests for [Soul Injector](https://github.com/huming2207/soul-injector) and other
CMSIS-style programmers (probe-rs, pyOCD, Keil).

It is the C++ counterpart of the Rust [`flash-algorithm`](https://github.com/probe-rs/flash-algorithm)
crate. You write ordinary member functions and can use the vendor's C/C++ SDK (ST, NXP, ...). The
library generates the C entry points the programmer calls, the `FlashDevice` description, and the
self test table, using **C++26 static reflection**.

> **Note:** this library was initially proposed by a human (Jackson Ming Hu) and implemented by AI
> (Claude). It is tested (host tests, compile-fail tests, an ARM build of the example) but has not been
> verified on hardware yet, and it may contain AI slop: mistakes, odd choices or misleading comments.
> Review it before you rely on it, and please report anything that looks wrong.

```cpp
#include <flashalgo/flashalgo.hpp>
using flashalgo::exec_state;

class stm32wle5
{
public:
    static constexpr flashalgo::device_info device{
        .name = "STM32WLE5xC 256KB Flash",
        .flash_address = 0x0800'0000,
        .flash_size = 0x4'0000,
        .page_size = 0x800,
    };

    exec_state erase_sector(std::uint32_t addr);
    exec_state program_page(std::uint32_t addr, std::span<const std::byte> data);
    exec_state erase_chip();                                   // optional

    // In-circuit self tests: annotate them, and the programmer can run them by id.
    [[=flashalgo::test{0xfffff001, "Read device UID"}]] exec_state read_uid(std::span<std::byte> out);
    [[=flashalgo::test{0xfffff004}]] exec_state clock_check();
};

FLASHALGO_EXPORT(stm32wle5);
```

You don't need to know the CMSIS calling convention, linker sections, or how self tests reach the
algorithm. If you're new to C++ reflection, [docs/how-it-works.md](docs/how-it-works.md) explains it
from the ground up, along with everything the library does.

## Contents

- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Writing an algorithm](#writing-an-algorithm)
  - [The device description](#the-device-description)
  - [Hooks](#hooks)
  - [Self tests](#self-tests)
  - [Result codes: exec_state](#result-codes-exec_state)
  - [Helpers and the "typo" check](#helpers-and-the-typo-check)
- [Compile-time checks](#compile-time-checks)
- [Building](#building)
- [Using it with Soul Injector](#using-it-with-soul-injector)
- [Rules of the flash algorithm environment](#rules-of-the-flash-algorithm-environment)
- [Size](#size)
- [Repository layout](#repository-layout)
- [Testing the library](#testing-the-library)

## Requirements

- **GCC 16 or newer**, with `-std=gnu++26 -freflection`, for both the target (`arm-none-eabi-gcc`) and,
  if you run the library tests, the host.
  - Clang can't build it yet: upstream Clang has no P2996 reflection, and `FLASHALGO_EXPORT` uses GCC's
    toplevel extended `asm` (GCC 15+).
  - Arch Linux ships `arm-none-eabi-gcc` 16; [`docker/Dockerfile`](docker/Dockerfile) builds an image with
    everything needed.
- CMake 3.30+ (optional; the library is just headers plus a linker script).
- Python 3.8+ for [`tools/flashalgo_yaml.py`](tools/flashalgo_yaml.py) (standard library only).

## Quick start

```sh
docker build -t flashalgo-dev docker
docker run --rm -v "$PWD":/src flashalgo-dev /src/scripts/check.sh   # tests + ARM example
```

To start your own algorithm, copy [`examples/stm32wl`](examples/stm32wl), then:

1. Change `device`, the hooks and the tests in `algorithm.cpp`.
2. Set `LOAD_ADDRESS` in `CMakeLists.txt` to where the programmer loads the algorithm in RAM.
3. Set the CPU flags (`-mcpu=...`).

```sh
cmake -S examples/stm32wl -B build/stm32wl -G Ninja
cmake --build build/stm32wl
# -> build/stm32wl/stm32wle5_algo.elf   the algorithm
# -> build/stm32wl/stm32wle5_algo.yaml  flash_algorithms: + self_tests: for Soul Injector's target.yaml
```

## Writing an algorithm

An algorithm is a class (or struct) with:

- a `static constexpr flashalgo::device_info device`,
- an optional `static constexpr flashalgo::sector sectors[]`,
- some **hooks** (member functions with fixed names),
- any number of **self tests** (member functions annotated with `[[=flashalgo::test{...}]]`).

Then `FLASHALGO_EXPORT(your_class);` once, at namespace scope in one `.cpp` file. That is the only
macro in the library.

The class is default-constructed by `Init` and destroyed by `UnInit`, so it can hold state (a HAL
handle, a buffer, a flag) like any object. A constructor is fine; the `init` hook is where errors can
be reported.

### The device description

```cpp
static constexpr flashalgo::device_info device{
    .name = "STM32WLE5xC 256KB Flash",           // max 127 chars
    .type = flashalgo::device_type::onchip,      // default
    .flash_address = 0x0800'0000,
    .flash_size = 0x4'0000,
    .page_size = 0x800,                          // the programming unit
    .empty_value = 0xff,                         // default
    .program_timeout_ms = 100,                   // default
    .erase_timeout_ms = 3000,                    // default
};

// Optional. Each entry starts a run of equally sized sectors, at an offset from flash_address.
// Without it, the flash is one run of page_size sectors.
static constexpr flashalgo::sector sectors[] = {
    {.size = 0x4000, .address = 0x00000},   // 4 x 16K
    {.size = 0x10000, .address = 0x10000},  // 1 x 64K
    {.size = 0x20000, .address = 0x20000},  // 128K sectors from here on
};
```

This becomes the CMSIS `FlashDevice` structure in the ELF (`DeviceData` section), which programmers and
`tools/flashalgo_yaml.py` read. It does not use target RAM.

### Hooks

Only `erase_sector` and `program_page` are required. Each optional hook adds the matching entry point
to the ELF; when a hook is absent, its entry point doesn't exist at all, so the programmer can see
what the algorithm supports.

| Hook | Entry point | When it's called |
| ---- | ----------- | ---------------- |
| `exec_state init(std::uint32_t flash_addr, std::uint32_t clock_hz, flashalgo::function fn)` *(optional)* | `Init` | After construction. Set up clocks, unlock flash. A non-`ok` result destroys the object again. `fn` says what comes next (`erase`, `program`, `verify`, `blank_check`). |
| `exec_state deinit()` *(optional)* | `UnInit` | Before destruction. Lock the flash, restore clocks. |
| `exec_state erase_sector(std::uint32_t addr)` | `EraseSector` | Erase the sector containing `addr`. |
| `exec_state program_page(std::uint32_t addr, std::span<const std::byte> data)` | `ProgramPage` | Program `data` at `addr` (usually one page, but the last chunk can be shorter). |
| `exec_state erase_chip()` *(optional)* | `EraseChip` | Mass erase. |
| `std::uint32_t verify(std::uint32_t addr, std::span<const std::byte> expected)` *(optional)* | `Verify` | Compare flash with `expected`; return **how many bytes matched** (`expected.size()` on success). The library converts this to the CMSIS return value. |
| `exec_state blank_check(std::uint32_t addr, std::uint32_t size, std::uint8_t pattern)` *(optional)* | `BlankCheck` | Check that a range is erased. |
| `exec_state read_flash(std::uint32_t addr, std::span<std::byte> out)` *(optional)* | `ReadFlash` | Read flash that isn't memory-mapped (probe-rs extension). |

Hooks must be public. Calls made before `Init` (or after `UnInit`) return `err_not_init` without
reaching your code.

### Self tests

A self test is a public member function annotated with `flashalgo::test`:

```cpp
[[=flashalgo::test{0xfffff004}]] exec_state clock_check();                          // name: "clock_check"
[[=flashalgo::test{0xfffff006, "Button pressed"}]] exec_state button();             // name: "Button pressed"
[[=flashalgo::test{0xfffff001}]] exec_state read_uid(std::span<std::byte> out);     // gets a RAM buffer
```

- **The id** is the address the programmer passes to `Verify` to run the test. It has to be outside the
  flash range. Soul Injector's convention is `0xfffff000` and up.
- **The name** is shown on the programmer's screen. It defaults to the function name, and is at most 31
  characters.
- **Signature**: `exec_state f()` or `exec_state f(std::span<std::byte> buffer)`. The buffer form gets
  the RAM buffer the programmer passes in r2 (size in r1), to return data such as a UID or a
  calibration value. When the programmer passes no buffer (Soul Injector currently passes none), the span is empty.
- **Result**: `ok` (0) passes; anything else fails, and the value is shown to the operator.

On the target, the programmer calls `Verify(id, size, buffer)`. The library generates a dispatcher that
compares the address against each test id and calls the matching function, so there's no switch
statement to keep in sync. The same information goes into an ELF-only table that `flashalgo_yaml.py`
turns into Soul Injector's `self_tests:` list.

### Result codes: exec_state

```cpp
enum exec_state : std::int32_t {
    ok = 0,
    err_fail = 1,
    err_not_init = 2,
    err_invalid_arg = 3,
    err_invalid_addr = 4,
    err_timeout = 5,
    err_verify = 6,
    err_unsupported = 7,
    err_user_base = 0x100, // custom codes should start here
};
```

The value goes back to the programmer in r0 unchanged, so any 32-bit code works:
`return exec_state(flashalgo::err_user_base + 3);` or `return exec_state(0xffff0001);`.

### Helpers and the "typo" check

Every **public** member function must be either a hook or a test. A misspelled hook (`erase_sectr`)
would otherwise be silently ignored, and you'd only find out on the production line. Helpers can be
private (private members aren't checked), or public and marked:

```cpp
[[=flashalgo::helper{}]] std::uint32_t page_of(std::uint32_t addr) const;
```

## Compile-time checks

Mistakes fail the build with a readable message (C++26 `static_assert` with a generated message):

| Mistake | Message |
| ------- | ------- |
| No `device` member | `the algorithm needs static constexpr flashalgo::device_info device{...};` |
| `flash_size` / `page_size` is 0 | `device.flash_size and device.page_size must be non-zero` |
| Bad `sectors[]` | `sectors[] must be sorted by address with no duplicates`, ... |
| Missing `erase_sector` / `program_page` | `missing required hook: exec_state program_page(std::uint32_t, std::span<const std::byte>)` |
| Hook with the wrong parameters | `hook 'program_page' has the wrong signature, expected: ...` |
| Private hook or test | `'erase_sector' must be public: hooks and tests are called from flashalgo::entry_points` |
| Unknown public function | `public member function 'erase_chp' is not a hook or a [[=flashalgo::test]] (typo?)` |
| Two tests with one id | `test 'b' reuses id 0xfffff001` |
| Test id inside flash | `test 'a' id 0x08000400 is inside the flash range; Verify would treat it as a normal verify` |
| Test with other parameters / return type | `test 'a' must take () or (std::span<std::byte> buffer)`, `test 'a' must return flashalgo::exec_state` |
| Test name too long | `test '...' name is longer than 31 characters (the programmer's limit)` |
| A hook name used as a test | `'erase_chip' is a hook name and cannot also be a test` |
| Built with exceptions | `flash algorithms have no unwinder: build with -fno-exceptions` |
| A global with a run-time constructor | (link error) `flashalgo: a global needs a run-time constructor, but nothing runs them...` |

Each of these is covered by a test in [`tests/compile_fail`](tests/compile_fail).

## Building

### With CMake

```cmake
add_subdirectory(path/to/si_flashalgo_cpp flashalgo)

flashalgo_add_algorithm(my_algo LOAD_ADDRESS 0x20000020 SOURCES algorithm.cpp)
target_compile_options(my_algo PRIVATE -mcpu=cortex-m4 -mthumb)
target_link_options(my_algo PRIVATE -mcpu=cortex-m4 -mthumb)
```

Configure with the toolchain file [`cmake/arm-none-eabi.cmake`](cmake/arm-none-eabi.cmake) (the example
sets it for you). `flashalgo::flashalgo` adds the include path and the required compile flags.
`flashalgo_add_algorithm` adds the link setup:

- no startup files,
- section garbage collection,
- the linker script,
- the load address,
- a `.map` file.

### Without CMake

```sh
arm-none-eabi-g++ -std=gnu++26 -freflection -fno-exceptions -fno-rtti -fno-threadsafe-statics \
    -fno-use-cxa-atexit -D_GLIBCXX_NO_ASSERTIONS -Os -ffunction-sections -fdata-sections -mcpu=cortex-m4 -mthumb \
    -I path/to/include algorithm.cpp -o algo.elf \
    -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
    -Wl,--defsym=FLASHALGO_LOAD_ADDRESS=0x20000020 -T path/to/ld/flashalgo.ld
```

The code is **not position independent**. It must be loaded at `FLASHALGO_LOAD_ADDRESS`, which must
match the programmer's `load_address`.

## Using it with Soul Injector

`tools/flashalgo_yaml.py algo.elf --name my_algo` prints the `flash_algorithms:` entry and the
`self_tests:` list for `target.yaml`:

```yaml
flash_algorithms:
  - name: "stm32wle5"
    description: "STM32WLE5xC 256KB Flash"
    default: true
    instructions: BkoTeDux...      # the PrgCode section, base64
    load_address: 0x20000020
    pc_init: 0x25                  # entry point offsets (Thumb bit included)
    pc_uninit: 0x1
    pc_program_page: 0x105
    pc_erase_sector: 0xa9
    pc_erase_all: 0x1f1
    pc_verify: 0x22d
    data_section_offset: 0x20000368
    flash_properties:
      address_range:
        start: 0x8000000
        end: 0x8040000
      page_size: 0x800
      erased_byte_value: 0xff
      program_page_timeout: 1000
      erase_sector_timeout: 2000
      sectors:
        - size: 0x800
          address: 0x0
    cores:
      - main

self_tests:
  - name: "Read device UID"
    type: simple
    addr: 0xfffff001  # takes a RAM buffer (r1 = size, r2 = pointer)
  - name: "RDP level 0"
    type: simple
    addr: 0xfffff008
```

Paste both into `target.yaml` (add `variants:` as usual) and compile the job with `sidp-agent`. With
`--tests-only` it prints just the `self_tests:` list. The ELF is also laid out the way probe-rs
`target-gen` expects (`PrgCode`, `DeviceData`, entry point symbols).

How Soul Injector runs a test:

1. It calls `Verify` with r0 = the test id, r1 = buffer size, r2 = buffer pointer (currently 0).
2. It treats a return value of 0 as a pass.

That's exactly what the generated dispatcher implements.

## Rules of the flash algorithm environment

The programmer copies the image into RAM and jumps to an entry point with a small stack, no startup
code and no interrupts. In practice:

- **No global constructors.** Nothing runs them. Keep globals `constexpr`/`constinit`, or put state in
  your algorithm class, which `Init` constructs. The linker script fails the link if a global needs a
  constructor, instead of letting it stay uninitialised.
- **No exceptions, no RTTI, no heap.** The flags are set by `flashalgo::flashalgo`.
- **No interrupts or SysTick.** ST HAL functions that wait on `HAL_GetTick()` (`HAL_Delay`, the
  timeouts inside `HAL_FLASH_*`) never time out unless you provide `HAL_GetTick()` from a counter you
  advance yourself, or the DWT cycle counter. LL drivers and register access work as is.
- **Small stack.** The programmer reserves a fixed, small stack (a few KiB at most). Keep big buffers
  in the algorithm object, not on the stack.
- **Watchdogs.** If the option bytes start the IWDG in hardware, reload it in long operations.
- **`memcpy`/`memset`.** GCC may emit calls to these. `--specs=nano.specs` links newlib-nano's small
  versions (28 bytes for `memcpy`); nothing else from the C library is pulled in unless you call it.
- **Debug builds (`-O0`).** GCC 15+ turns on libstdc++'s run-time assertions when not optimizing, and a
  failing one calls `fprintf()` and `abort()`, which can't link here (`undefined reference to _exit`).
  `flashalgo::flashalgo` defines `_GLIBCXX_NO_ASSERTIONS` to turn them off; add it yourself when you
  build without CMake.
- **Standard library.** Freestanding-friendly headers like `<span>`, `<cstdint>` and `<array>` are fine.
  Anything that allocates or does I/O is not. The library itself uses `<meta>` and friends only at compile
  time: `nm -u` on an algorithm is empty.

## Size

All numbers are for Cortex-M4, `-Os`, GCC 16.2. They count only what the programmer loads (`PrgCode`):

| Build | Code + data loaded to RAM |
| ----- | ------------------------- |
| Library overhead: `Init`, `UnInit`, `EraseSector`, `ProgramPage`, `EraseChip`, `Verify` with a two-test dispatcher, all hooks empty | 160 bytes |
| [`examples/stm32wl`](examples/stm32wl): page/mass erase, fast row (FSTPG) + double-word programming, two self tests | 840 bytes |

`FlashDevice` and the self test table are not loaded: they live only in the ELF.

## Repository layout

```
include/flashalgo/
  flashalgo.hpp        umbrella header (include this one)
  types.hpp            exec_state, function, device_info, sector, test, helper
  hooks.hpp            the hook signatures, as concepts
  entry.hpp            entry_points<A>: the CMSIS ABI and the generated test dispatcher
  export.hpp           FLASHALGO_EXPORT
  detail/reflect.hpp   compile-time checks and table generation (reflection)
ld/flashalgo.ld        linker script
cmake/                 flashalgo_add_algorithm(), arm-none-eabi toolchain file
tools/flashalgo_yaml.py  ELF -> Soul Injector target.yaml pieces
examples/stm32wl/      STM32WLE5 algorithm (register level, no SDK needed)
tests/                 host tests, compile-fail tests
LICENSE                MIT
docs/how-it-works.md   reflection primer and design notes
docker/Dockerfile      GCC 16 + arm-none-eabi-gcc 16 build image
scripts/check.sh       build and test everything
```

## Testing the library

`scripts/check.sh` runs everything.

The **host tests** build an algorithm for the build machine. The test program then plays the
programmer: it calls `Init`, `EraseSector`, `Verify(0xfffff001, ...)` and so on through their C names,
against a fake flash in RAM. They check:

- the return conventions,
- the `FlashDevice` byte layout,
- the self test table,
- that unimplemented entry points are absent.

The **compile-fail tests** compile each mistake in `tests/compile_fail/` and pass only if the compiler
prints the expected message.

The **ARM build** of the example checks that the image links with no undefined symbols.

## License

MIT, see [LICENSE](LICENSE).
