# How flashalgo works

This document explains what the library generates and how, for readers who know C++ but have not
used C++26 reflection yet. It goes from the problem, to a reflection primer, to each mechanism in the
headers.

> **Note:** this library was initially proposed by a human (Jackson Ming Hu) and implemented by AI
> (Claude). It is tested (host tests, compile-fail tests, an ARM build of the example) but has not been
> verified on hardware yet, and it may contain AI slop: mistakes, odd choices or misleading comments.
> Review it before you rely on it, and please report anything that looks wrong.

- [1. What a flash algorithm is](#1-what-a-flash-algorithm-is)
- [2. What the library has to produce](#2-what-the-library-has-to-produce)
- [3. C++26 reflection in 10 minutes](#3-c26-reflection-in-10-minutes)
- [4. Checking the algorithm at compile time](#4-checking-the-algorithm-at-compile-time)
- [5. The self test dispatcher](#5-the-self-test-dispatcher)
- [6. Generated tables: FlashDevice and the self test table](#6-generated-tables-flashdevice-and-the-self-test-table)
- [7. The entry points and the object lifecycle](#7-the-entry-points-and-the-object-lifecycle)
- [8. Exporting the C names: FLASHALGO_EXPORT](#8-exporting-the-c-names-flashalgo_export)
- [9. Memory layout and the linker script](#9-memory-layout-and-the-linker-script)
- [10. Keeping it small](#10-keeping-it-small)
- [11. Limitations](#11-limitations)

## 1. What a flash algorithm is

A debug probe can write RAM and registers, but it can't write flash directly. Flash needs a
chip-specific sequence: unlock, erase, program in the right unit size, wait for busy flags. So the
programmer (Soul Injector, probe-rs, pyOCD, Keil) does this:

1. Halt the target and copy a small program, the **flash algorithm**, into the target's RAM.
2. Call one of its functions by setting registers and starting the core:
   - `pc` = the function,
   - `r0..r3` = the arguments,
   - `sp` = a stack it reserved,
   - `lr` = a breakpoint, so the core halts again when the function returns.
3. Read the result from `r0` once the core halts.
4. Repeat, for example `Init`, then `EraseSector` per sector, then `ProgramPage` per page, then `UnInit`.

The function names and arguments come from Arm's CMSIS-Pack specification:

```c
int Init(unsigned long adr, unsigned long clk, unsigned long fnc);
int UnInit(unsigned long fnc);
int EraseChip(void);
int EraseSector(unsigned long adr);
int ProgramPage(unsigned long adr, unsigned long sz, unsigned char *buf);
unsigned long Verify(unsigned long adr, unsigned long sz, unsigned char *buf);
int BlankCheck(unsigned long adr, unsigned long sz, unsigned char pat);
```

Each one returns 0 on success, except `Verify`, which returns `adr + sz` on success and the first
mismatching address otherwise. The ELF also carries a `FlashDevice` structure describing the flash
(address, size, page size, sector layout, timeouts).

Important properties of this environment:

- **No startup code runs.** Nothing copies `.data`, zeroes `.bss` or runs global constructors. The
  programmer loads the image exactly as linked.
- **No interrupts.** There is no SysTick and no tick counter.
- **A small stack**, and RAM that is shared with the page buffers the programmer uses.
- **Fixed load address.** The programmer is told where it loads the image (`load_address`), and the code
  is linked for that address.

### Self tests

Soul Injector reuses `Verify` to run **in-circuit self tests** after programming, for example "is the external
crystal running?" or "does the EEPROM answer on I2C?". It calls `Verify` with an address that can't be
flash, such as `0xfffff004`; the algorithm recognises the address and runs the test instead. A return
value of 0 means the test passed. The `self_tests:` list in `target.yaml` tells the programmer which
addresses to call and what to show on the screen.

## 2. What the library has to produce

From a class like this:

```cpp
class my_algo
{
public:
    static constexpr flashalgo::device_info device{ /* ... */ };
    exec_state erase_sector(std::uint32_t addr);
    exec_state program_page(std::uint32_t addr, std::span<const std::byte> data);
    [[=flashalgo::test{0xfffff004}]] exec_state clock_check();
};
FLASHALGO_EXPORT(my_algo);
```

the library has to produce:

1. **C functions named `Init`, `UnInit`, `EraseSector`, `ProgramPage`** (and `EraseChip`, `Verify`,
   `BlankCheck`, `ReadFlash` only when supported). They create the object, check that it exists, convert
   the raw arguments to proper types, call the member functions and convert the results.
2. **A `Verify` that dispatches self tests**: `0xfffff004` must call `clock_check()`.
3. **A `FlashDevice`** in the CMSIS layout, from `device`.
4. **A self test table** for host tools: `{0xfffff004, "clock_check"}`.
5. **Errors at compile time** for anything that would otherwise fail on the production line, such as a
   misspelled hook, two tests with the same id, or a test id that overlaps the flash.

Items 1 and 3 could be done with ordinary templates. Items 2, 4 and 5 need the compiler to answer
questions such as "which member functions does this class have, what are they called, and what
annotations do they carry?". Before C++26 that was impossible without a macro per function, or a
code generator. **Static reflection** answers exactly those questions.

## 3. C++26 reflection in 10 minutes

C++26 adds *static* reflection (proposal P2996, plus P3394 annotations and P1306 expansion
statements). "Static" means it all happens inside the compiler. There is no run-time type information,
nothing is stored in the binary, and nothing is slower at run time. Reflection is a way to write
programs that run *during compilation* and inspect your code.

GCC 16 implements it behind `-std=gnu++26 -freflection`. The library API is in `<meta>`, namespace
`std::meta`.

### 3.1 Reflections: `^^` and `std::meta::info`

The reflection operator `^^` (two carets) turns a name into a value of type `std::meta::info`, an opaque
handle the compiler understands:

```cpp
struct point { int x; int y; };

constexpr std::meta::info r_point = ^^point;   // a handle to the type
constexpr std::meta::info r_x = ^^point::x;    // a handle to the member
constexpr std::meta::info r_int = ^^int;       // works for any type, function, namespace...
```

An `info` value can only exist at compile time. You can pass it around, store it in compile-time
vectors and compare it, but it never reaches the binary. Functions that work with it are `consteval`,
meaning they always run at compile time.

### 3.2 Asking questions: `std::meta` functions

`<meta>` provides `consteval` functions that query a reflection:

```cpp
std::meta::identifier_of(^^point::x)   // "x" (a std::string_view)
std::meta::is_public(^^point::x)       // true
std::meta::type_of(^^point::x)         // ^^int

// All members of a class, as a std::vector<std::meta::info>:
std::meta::members_of(^^point, std::meta::access_context::unchecked())
```

The `access_context` argument says whose point of view to use when deciding what is visible.
`unchecked()` means "show me everything, including private members". The library uses it so it can
report private hooks as an error, instead of silently not seeing them.

Other queries the library uses:

| Function | Answers |
| -------- | ------- |
| `is_function(r)` | Is this a function (not a data member or nested type)? |
| `is_special_member_function(r)` | Constructor, destructor, copy/move assignment? |
| `has_identifier(r)` | Does it have a plain name? (Operators like `operator==` don't.) |
| `parameters_of(r)` | The function's parameters, as reflections |
| `return_type_of(r)` | The function's return type |
| `is_same_type(a, b)`, `remove_cvref(t)` | Type traits, as functions on reflections |

Because these are ordinary (`consteval`) functions, you can use loops, `if`, local variables and even
`std::vector` with them. It reads like normal code:

```cpp
consteval int count_functions(std::meta::info cls)
{
    int n = 0;
    for (std::meta::info m : std::meta::members_of(cls, std::meta::access_context::unchecked())) {
        if (std::meta::is_function(m)) {
            n += 1;
        }
    }
    return n;
}

static_assert(count_functions(^^my_algo) == 3);
```

The `std::vector` here only exists while the compiler evaluates the function. That's why the library
can use `<meta>` and `std::vector` and still have nothing from the standard library in the binary.

### 3.3 Annotations: `[[=value]]`

An annotation attaches a compile-time value to a declaration (P3394). It looks like an attribute that
starts with `=`:

```cpp
struct test { std::uint32_t id; char name[32]{}; };

struct my_algo {
    [[=test{0xfffff004}]] exec_state clock_check();
};
```

Unlike a normal attribute, the compiler doesn't interpret it. It just remembers the value, and
reflection can read it back:

```cpp
auto anns = std::meta::annotations_of_with_type(^^my_algo::clock_check, ^^test); // reflections of the values
test t = std::meta::extract<test>(anns[0]);                              // t.id == 0xfffff004
```

The value must be usable as a template argument (a "structural type"): plain structs of integers,
arrays, enums. That's why `flashalgo::test::name` is a `char[32]` and not a `const char *`.

### 3.4 Splicing: `[: r :]`

A splice turns a reflection back into code. Wherever you could write a name, you can write
`[: r :]` with a constant reflection:

```cpp
constexpr auto m = ^^my_algo::clock_check;
my_algo a;
a.[:m:]();          // the same as a.clock_check()
```

Splicing is how the library calls a test it found by reflection, without knowing its name when the
library was written. A splice is an ordinary member access, so access control still applies: a private
member can't be called this way from outside the class. That's why tests and hooks must be public.

### 3.5 `template for`: a loop the compiler unrolls

A normal `for` loop runs at run time and can't splice, because splicing needs a *constant* reflection.
An **expansion statement** (P1306) is a loop that the compiler unrolls at compile time. Each iteration
gets its own constant:

```cpp
template for (constexpr std::meta::info m : some_constant_range) {
    // m is a constant here, so [:m:] is allowed.
    // The body is stamped out once per element, like copy-pasting it N times.
}
```

The range has to be a constant, but `members_of` returns a `std::vector`, which can't outlive
compile-time evaluation. `std::define_static_array(vec)` copies the elements into a static array and
returns a span over it, which is a constant:

```cpp
template <class A>
consteval auto members()
{
    return std::define_static_array(std::meta::members_of(^^A, std::meta::access_context::unchecked()));
}
```

Since the array holds `std::meta::info` values, it too exists only inside the compiler.

### 3.6 Putting it together

The basic pattern of the whole library is:

1. Get `^^A`, the user's class.
2. Loop over `members_of(^^A)`. A plain loop is used to check things; `template for` is used to generate
   code.
3. Look at each member's name, signature and annotations.
4. Generate code by splicing, or `static_assert` with a message.

## 4. Checking the algorithm at compile time

`detail::check<A>()` in [`include/flashalgo/detail/reflect.hpp`](../include/flashalgo/detail/reflect.hpp)
is a `consteval` function that walks the class and returns a `diag`, a small struct with `ok` and a
message buffer. `entry_points<A>` asserts on it:

```cpp
static_assert(detail::check<A>().ok, detail::check<A>());
```

The second argument uses another C++26 feature, **user-generated `static_assert` messages** (P2741). The
message can be any constant object with `.data()` and `.size()`, not just a string literal. This is how
the error can name *your* function and *your* id:

```
error: static assertion failed: flashalgo: test 'b' reuses id 0xfffff001
```

What `check<A>()` does, in order:

1. Checks that `A::device` exists, with a non-zero flash size and page size, and that `A::sectors` (if
   present) is non-empty, sorted and inside the flash.
2. Loops over every named, non-special member function of `A` (`members_of` with `unchecked()` access):
   - **Hook names or tests that aren't public**: error, because the generated calls couldn't reach them.
   - **Tests** (`annotations_of_with_type(m, ^^test)` is non-empty):
     - the name must not be a hook name,
     - there must be exactly one `test` annotation,
     - the id must not lie in `[flash_address, flash_address + flash_size)`,
     - the id must not be used by an earlier test,
     - the name must be at most 31 characters,
     - the return type must be `exec_state`,
     - the parameters must be `()` or `(std::span<std::byte>)`.
   - **Hooks** (the name is in the hook table): the matching concept from
     [`hooks.hpp`](../include/flashalgo/hooks.hpp) must be satisfied, otherwise it reports the expected
     signature.
   - **Anything else that is public and not marked `[[=flashalgo::helper{}]]`**: error, probably a typo.
3. Checks that the required hooks `erase_sector` and `program_page` exist.

The hook signatures themselves are plain C++20 concepts, for example:

```cpp
template <class A>
concept has_program_page = requires(A &a, std::uint32_t u, std::span<const std::byte> d) {
    { a.program_page(u, d) } -> std::same_as<exec_state>;
};
```

Concepts can say "this call works", but not "a function called `program_page` exists with some other
signature". So without reflection, a wrong signature would look the same as a missing optional hook,
and be silently ignored. Reflection finds the function by name, and the concept then says whether it
fits.

## 5. The self test dispatcher

`entry_points<A>::run_test` is called by `Verify`:

```cpp
static bool run_test(std::uint32_t addr, std::uint32_t size, std::uint8_t *data, std::uint32_t &ret)
{
    template for (constexpr auto m : detail::members<A>()) {
        if constexpr (detail::is_test(m)) {
            if (addr == detail::test_of(m).id) {
                if (!is_init) {
                    ret = result(err_not_init);
                } else if constexpr (detail::test_takes_buffer(m)) {
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
```

The `template for` is unrolled once per member of `A`. `if constexpr` throws away the members that
aren't tests. `detail::test_of(m).id` is a compile-time constant, and `[:m:]` becomes a direct call. For
the README's `stm32wle5` class, what the compiler actually compiles is equivalent to:

```cpp
static bool run_test(std::uint32_t addr, std::uint32_t size, std::uint8_t *data, std::uint32_t &ret)
{
    if (addr == 0xfffff001) {
        if (!is_init) ret = 2;
        else ret = self().read_uid(std::span<std::byte>{(std::byte *)data, data ? size : 0});
        return true;
    }
    if (addr == 0xfffff008) {
        if (!is_init) ret = 2;
        else ret = self().rdp_is_level0();
        return true;
    }
    return false;
}
```

That's the same code you would write by hand as a `switch`. The difference is that it can't get out of
sync with the class, and the signature (buffer or not) is adapted per test.

`Verify` then does:

```cpp
static std::uint32_t Verify(std::uint32_t addr, std::uint32_t size, std::uint8_t *data)
    requires detail::exports_verify<A>
{
    std::uint32_t ret = 0;
    if (run_test(addr, size, data, ret)) {
        return ret;                      // a test: its exec_state, 0 = pass
    }
    // otherwise: a CMSIS verify (the user's verify() hook, or "addr + size" when there is none)
}
```

## 6. Generated tables: FlashDevice and the self test table

Both tables are built by `consteval` functions and stored in `static constexpr` data members of
`entry_points<A>`, each in its own linker section:

```cpp
[[gnu::section("DeviceData")]] static constexpr auto device_desc = detail::make_device_desc<A>();
[[gnu::section(".flashalgo_tests")]] static constexpr auto test_table = detail::make_test_table<A>();
```

The values are computed entirely by the compiler. The object file just contains the finished bytes,
with no code to build them.

### FlashDevice

`make_device_desc<A>()` fills the CMSIS `FlashDevice` structure (from `FlashOS.h`) from `A::device` and
`A::sectors`. Without `sectors`, it describes one run of `page_size` sectors. The sector list ends with
`{0xffffffff, 0xffffffff}`, as the specification requires. Natural alignment gives this layout, which the
host tests check byte by byte:

| Offset | Field |
| -----: | ----- |
| 0 | `u16 Vers` (0x0101) |
| 2 | `char DevName[128]` |
| 130 | `u16 DevType` |
| 132 | `u32 DevAdr` |
| 136 | `u32 szDev` |
| 140 | `u32 szPage` |
| 144 | `u32 Res` |
| 148 | `u8 valEmpty` |
| 152 | `u32 toProg` (ms) |
| 156 | `u32 toErase` (ms) |
| 160 | `{u32 size; u32 address}` sectors..., then `{~0, ~0}` |

### The self test table

`make_test_table<A>()` produces a header plus one fixed-size record per test, in declaration order.
Everything is little-endian on Cortex-M:

```
header (12 bytes):  char magic[4] = "FATS"; u16 version = 1; u16 count; u16 record_size = 40; u16 reserved
record (40 bytes):  u32 id; u8 kind (0 = no buffer, 1 = takes a buffer); u8 reserved[3]; char name[32]
```

The names are stored inline, not as pointers, so a host tool can parse the section without applying
relocations. [`tools/flashalgo_yaml.py`](../tools/flashalgo_yaml.py) reads it from the ELF and prints
Soul Injector's `self_tests:` list.

## 7. The entry points and the object lifecycle

[`include/flashalgo/entry.hpp`](../include/flashalgo/entry.hpp) defines `entry_points<A>`. Its static
member functions have exactly the CMSIS signatures, and all of them are placed in the `.entry` section.

There are no global constructors, so the algorithm object can't be a normal global. Instead:

```cpp
alignas(A) static inline std::byte storage[sizeof(A)];   // raw bytes in .bss
static inline bool is_init = false;
```

The programmer loads `.bss` as zero bytes, because it's part of the image (see section 9), so
`is_init` starts out `false` without any startup code.

- **`Init`** calls `UnInit` first if the object already exists (programmers do call `Init` twice). It
  then constructs the object with placement new (`::new (storage) A{}`) and calls `init()` if there is
  one. When `init()` fails, the object is destroyed again, so a failed `Init` leaves nothing half set up.
- **`UnInit`** calls `deinit()` if there is one, runs the destructor, and clears `is_init`.
- **Every other entry point** returns `err_not_init` when `is_init` is false, and otherwise calls the
  hook. Raw pointer and size pairs become `std::span`, and `exec_state` becomes the `r0` value.

Optional entry points use a C++20 *trailing requires clause*:

```cpp
static std::uint32_t EraseChip()
    requires detail::exports_erase_chip<A>
{ ... }
```

When `A` has no `erase_chip()`, this member is never instantiated, so no code is generated for it.

## 8. Exporting the C names: FLASHALGO_EXPORT

The programmer looks up entry points by symbol name: `Init`, `ProgramPage` and so on. The functions
above are template members, whose symbols are mangled
(`_ZN9flashalgo12entry_pointsI9stm32wle5E4InitEmmm`). Something has to give them their C names. This
is the one place where the library needs a macro, because C++ can't do it:

- A template can't define an `extern "C"` function.
- Reflection (in C++26) can inspect declarations, but can't create new functions.
- GCC accepts `asm("Init")` labels on members of class templates, but silently ignores them. The
  library's first prototype found that out.

So `FLASHALGO_EXPORT(T)` uses **toplevel extended asm**, which GCC supports from version 15. It is an
`asm(...)` statement at namespace scope that can take operands, much like `asm` inside a function. The
operands are the addresses of the instantiated members, and the assembly creates aliases:

```cpp
asm(".globl Init\n\t.thumb_set Init, %c0\n\t"
    ...
    ".if %c6\n\t .globl EraseChip\n\t .thumb_set EraseChip, %c7\n\t .endif\n\t"
    :
    : "s"(&flashalgo::entry_points<T>::Init), ...,
      "i"(flashalgo::detail::exports_erase_chip<T> ? 1 : 0), "s"(flashalgo::entry_points<T>::erase_chip_entry()), ...);
```

On Cortex-M this assembles to something like:

```asm
    .globl Init
    .thumb_set Init, _ZN9flashalgo12entry_pointsI9stm32wle5E4InitEmmm
    .if 1
    .globl EraseChip
    .thumb_set EraseChip, _ZN9flashalgo12entry_pointsI9stm32wle5E9EraseChipEv...
    .endif
```

- **`"s"` operands** are symbol references, and `%cN` prints the bare symbol name. Naming the members here
  also makes the compiler instantiate them, which in turn runs the `static_assert` checks of
  section 4. No explicit instantiation is needed.
- **`.thumb_set`** creates an alias that is also marked as a Thumb function. On Cortex-M, a function
  symbol's value has bit 0 set (the Thumb bit), and programmers rely on it. That's why `pc_init` is odd.
  Other targets get `.set` plus `.type ..., function`.
- **`.if 0` / `.if 1`** skips the alias of an optional entry point that isn't implemented, so the symbol
  doesn't exist at all. The operand still has to name some symbol, so `erase_chip_entry()` returns
  `&Init` as a stand-in. It's never used, because the `.if` is false.
- An alias costs nothing: `Init` *is* the member function, with no wrapper or extra jump.

`FlashDevice` and `FlashAlgoTests` are exported the same way, as aliases of the two tables.

## 9. Memory layout and the linker script

[`ld/flashalgo.ld`](../ld/flashalgo.ld) produces these sections:

| Section | Contents | Loaded to target RAM? |
| ------- | -------- | --------------------- |
| `PrgCode` | `.entry` (kept), `.text`, `.rodata`, `.data`, `.bss` (as zeros) | **yes**, this is the `instructions:` blob |
| `DeviceData` | `FlashDevice` | no, read from the ELF by host tools |
| `.flashalgo_tests` | the self test table | no, a non-allocated (`INFO`) section |
| `.flashalgo_ctors` | any global constructors | no; the link fails if it isn't empty |

Notes:

- **Everything goes in one blob.** `.data` and `.bss` live inside `PrgCode`, because nothing would copy
  or clear them, and the code doesn't use R9-relative data addressing (RWPI) that a separate `PrgData`
  would need. `.bss` becomes real zero bytes in the image.
- **Fixed address.** The code is linked at `FLASHALGO_LOAD_ADDRESS` and isn't position independent, so
  the programmer must load it there. The script refuses to link if the symbol is missing.
- **Section garbage collection** (`--gc-sections`) starts from the `KEEP(*(.entry))` roots and removes
  everything the entry points don't reach.
- **Global constructors** would silently never run. The script collects `.init_array` and `.ctors` into
  an `INFO` section and asserts that it's empty, so the mistake is a link error.

## 10. Keeping it small

On Cortex-M4 at `-Os`, the library's own code is the entry points and the dispatcher: about 160 bytes
for a class with empty hooks and two tests. Every byte above that is your hook code. These design
choices keep it there:

- **Reflection runs only in the compiler.** `<meta>`, `std::vector`, `std::array`, the checks and the
  table builders are all `consteval`. The binary contains only the dispatcher (a chain of compares, as in
  section 5) and the finished tables, and the tables aren't loaded.
- **No explicit instantiation.** `template class entry_points<T>;` would emit every member, including
  out-of-line copies of small helpers. Instead the `asm` operands instantiate exactly the exported
  functions, and the helpers (`self()`, `result()`, `run_test()`) are `[[gnu::always_inline]]`.
- **Variable templates instead of static members** for compile-time flags (`detail::exports_verify<A>`
  etc.). A `static constexpr` data member of an explicitly instantiated class would be emitted into
  `.rodata`; a variable template that's only read in constant expressions never is. The same goes for
  the `static_assert` on `check<A>()`, which needs no stored result.
- **No run-time library.** `nm -u` on an algorithm is empty, apart from `memcpy`/`memset` if your code
  makes GCC emit them. Those come from newlib-nano and are tiny.
- **Aliases, not wrappers**, for the C names (section 8).

To see where your bytes go, use the `.map` file next to the ELF, or
`arm-none-eabi-nm -S --size-sort -C algo.elf`.

## 11. Limitations

- **GCC 16+ only.** It needs P2996 reflection, P3394 annotations, P1306 `template for` (all in GCC 16
  with `-freflection`) and toplevel extended asm (GCC 15+). Upstream Clang has neither reflection nor
  toplevel asm with operands yet.
- **Hooks and tests must be public.** Splices obey access control (section 3.4).
- **Only the class's own members are scanned.** `members_of` doesn't include base classes, so tests
  declared in a base class aren't found. Hooks inherited from a base class do work, because the concepts
  use normal member lookup.
- **At most 64 tests**, with names of at most 31 characters (Soul Injector's limit).
- **One algorithm per ELF**, the same as with any CMSIS flash algorithm. `FLASHALGO_EXPORT` twice would
  define the C names twice.
- **No position independence.** The image must be loaded at the address it was linked for.
- **Host tests need `-fno-pie`.** The `"s"` asm operands need plain, non-PIC symbol references. That
  matters only for the host test build; Cortex-M builds aren't PIE.
