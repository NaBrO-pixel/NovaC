# NovaC ABI and C Interoperability

Status: **Specification, Phase 1.** Nothing in this document is
implemented yet; see [FEATURE_STATUS.md](FEATURE_STATUS.md).

This document defines targets, data layout, calling conventions, symbol
naming, object files, the runtime contract, and C interoperability.

**Only the C ABI is stable** (D-018). Everything specified here about
NovaC-native layouts of built-in generic types and NovaC-native calling
is fixed *for a given compiler version* and may change before 1.0.

## 1. Scope: ABI compatibility is not source compatibility

NovaC is **ABI-compatible** with C on its targets: a NovaC function
declared `extern "C"` can call and be called by C code compiled by GCC or
Clang for the same target, and NovaC structs have C layout (D-017).

NovaC is **not source-compatible** with C. It does not compile C, does
not read C headers (D-004), and many C programs are not valid NovaC.
Claims of "C compatibility" in this project always mean ABI and layout
compatibility unless stated otherwise.

## 2. Targets

| Target name (`--target`) | LLVM triple used | Environment | Status |
| --- | --- | --- | --- |
| `x86_64-linux-gnu` | `x86_64-unknown-linux-gnu` | hosted, Linux, glibc | Implemented (Phase 3), default on x86-64 hosts |
| `x86_64-novaris` | `x86_64-unknown-linux-gnu` (see §10.1) | hosted, NovarisOS, LibNovaC | Planned (Phase 3/4) |
| `x86_64-unknown-none` | `x86_64-unknown-none-elf` | freestanding | Planned (Phase 7) |
| `x86_64-novaris-kernel` | `x86_64-unknown-none-elf` + kernel options (§10.2) | freestanding kernel | Planned (Phase 7) |
| `aarch64-linux-gnu` | `aarch64-unknown-linux-gnu` | hosted, Linux | Implemented (Phase 3), cross; tested under QEMU |
| `aarch64-unknown-none` | `aarch64-unknown-none-elf` | freestanding | Planned (Phase 7) |
| `riscv64-linux-gnu` | `riscv64-unknown-linux-gnu` | hosted, Linux | Implemented (Phase 3), cross; tested under QEMU |
| `riscv64-unknown-none` | `riscv64-unknown-none-elf` | freestanding | Planned (Phase 7) |

Baselines (D-023): x86-64-v1 (SSE2), ARMv8.0-A, RV64GC with the `lp64d`
ABI. `-mcpu=` / `-mattr=` select newer CPUs and features.

There is no `aarch64-novaris` or `riscv64-novaris`: NovarisOS runs only on
x86-64.

## 3. Data layout

Identical on all three version-1 targets unless shown:

| Type | Size | Align |
| --- | --- | --- |
| `i8`, `u8`, `bool` | 1 | 1 |
| `i16`, `u16` | 2 | 2 |
| `i32`, `u32`, `f32` | 4 | 4 |
| `i64`, `u64`, `f64` | 8 | 8 |
| `isize`, `usize`, `T*`, `T&`, function pointers | 8 | 8 |
| `slice<T>`, `str` | 16 | 8 |
| maximum fundamental alignment | 16 | |

Byte order: little-endian on all targets.

Target differences that NovaC exposes only through `core.ffi` (D-031):

| C type | x86-64 Linux | AArch64 Linux | RISC-V 64 Linux |
| --- | --- | --- | --- |
| `char` signedness (`c_char`) | signed | **unsigned** | **unsigned** |
| `long` (`c_long`) | 64-bit | 64-bit | 64-bit |
| `long double` | 80-bit x87, size 16 | IEEE binary128 | IEEE binary128 |
| `__int128` alignment | 16 | 16 | 16 |
| `va_list` | struct (1-element array) | struct | `void*` |

Stack and frame conventions:

| | x86-64 SysV | AArch64 AAPCS64 | RISC-V LP64D |
| --- | --- | --- | --- |
| Stack alignment at call | 16 | 16 | 16 |
| Red zone | 128 bytes (disabled for kernels) | none (Linux) | none |
| Frame pointer register | `rbp` (optional) | `x29` (frame records; whether they are mandatory is platform-defined, and NovaC keeps them by default on AArch64) | `s0`/`fp` (optional) |
| Platform register | — | `x18` (reserved on some platforms; NovaC never allocates it by default) | `tp` = thread pointer, `gp` = global pointer |

`long double` and `va_list` are unsupported in NovaC version 1 FFI
signatures (E7031).

## 4. Aggregate and built-in type layout

- **Structs:** C layout (D-017): fields in declaration order, each at the
  next offset aligned to its alignment; struct alignment is the maximum
  field alignment; size rounded up to the alignment. `@packed` sets every
  field alignment to 1 (as GCC's `packed`). `@align(N)` raises the
  alignment. An empty struct has size 0 in NovaC but is not FFI-safe
  (C forbids empty structs; GNU C gives them size 0, C++ size 1).
- **Unions:** size of the largest field rounded to the largest alignment.
- **Enums:** the underlying integer type.
- **Arrays `T[N]`:** `N * sizeof(T)`, alignment of `T`.
- **`bool`:** one byte, 0 or 1 (C `_Bool`).
- **`slice<T>` / `str`:** `struct { T* ptr; usize len; }`.
- **`optional<T>`:** niche or `struct { T value; bool present; }`
  (NOVAC_TYPE_SYSTEM.md §4.1).
- **`result<T,E>`:** `struct { union { T ok; E err; } u; bool is_err; }`.
- **`atomic<T>`:** size of `T`, alignment = size.
- **`mmio<T>`:** layout of `T`.

Bitfields and flexible array members are not supported in version 1.

In Phase 3 no value crosses a C ABI boundary except the generated `main`
wrapper, and `bool` is LLVM `i1` in registers and locals. Phase 4 must
give `bool` its one-byte memory form at FFI boundaries and in aggregates
(as Clang does for `_Bool`) before `extern "C"` is enabled.

## 5. C calling-convention lowering

LLVM does not implement the C ABI for aggregates: the frontend must
decide how each argument and return value is passed (D-003). NovaC
implements a classification module per target (`src/target/abi_*.c`,
NOVAC_COMPILER_ARCHITECTURE.md §9) covering:

- **x86-64 System V:** classification of each eightbyte into
  INTEGER / SSE / MEMORY; aggregates up to 16 bytes in up to two
  registers; larger or unaligned aggregates in memory; `sret` hidden
  pointer for MEMORY returns (returned in `rax`); `al` holds the number of
  vector registers used when calling variadic functions.
- **AArch64 AAPCS64:** homogeneous floating-point aggregates (HFA) of up
  to four members in `v0`–`v7`; other aggregates ≤ 16 bytes in up to two
  general registers; larger aggregates passed by reference to a
  caller-made copy; indirect results via `x8`.
- **RISC-V LP64D:** scalars in `a0`–`a7` / `fa0`–`fa7`; aggregates ≤ 16
  bytes in up to two registers, with the hardware floating-point
  convention for structs of one or two floating-point fields (or one
  float and one integer); larger aggregates by reference.

Correctness is established only by tests: for every target, generated
test cases call between NovaC and Clang-compiled C in both directions with
structs of every classification and compare results (§12). Until those
tests pass for a target, passing aggregates by value across `extern "C"`
on that target is rejected (E7032), not guessed.

## 6. Calling conventions

| Attribute | Meaning | Targets | Mode |
| --- | --- | --- | --- |
| *(none)* | NovaC-native; unspecified, currently C | all | any |
| `extern "C"` / `@callconv(c)` | target C ABI | all | any |
| `@callconv(sysv64)` / `@callconv(win64)` | explicit x86-64 conventions | x86-64 | kernel |
| `@callconv(preserve_all)` | callee saves all registers (LLVM `preserve_allcc`) | x86-64; other targets only where the pinned LLVM version supports it (checked at Phase 7) | kernel |
| `@interrupt` | hardware interrupt handler (§6.1) | x86-64, RISC-V | kernel |
| `@naked` | no prologue/epilogue; body must be a single `asm` statement | all | kernel |

### 6.1 Interrupt handlers differ per architecture

- **x86-64:** `@interrupt void handler(const InterruptFrame& f)` and
  `@interrupt void handler(const InterruptFrame& f, u64 error_code)` map to
  LLVM's `x86_intrcc`; the compiler saves all registers it uses and
  returns with `iretq`. Handlers must not use SSE unless the kernel saves
  that state (the NovarisOS kernel does not, §10.2).
- **RISC-V:** `@interrupt(supervisor)` / `@interrupt(machine)` map to
  LLVM's `"interrupt"` function attribute (return with `sret`/`mret`).
  There is a single trap vector unless vectored mode is configured.
- **AArch64:** there is no hardware-defined interrupt calling convention;
  exception vectors are a table of 128-byte assembly stubs that save state
  and call an ordinary function. `@interrupt` is **rejected** on AArch64
  (E7040); kernels write the vector table in `asm` / `@naked` functions.

## 7. Symbols and mangling

- `extern "C"` definitions and declarations use the unmangled name, or
  the name given by `@export("name")` / `@link_name("name")`.
- `main` in a hosted program is emitted as the C symbol `main` through a
  generated wrapper with the C signature
  `int main(int argc, char** argv, char** envp)`, which LibNovaC's `_start`
  calls (confirmed in LibNovaC `src/arch/x86_64/start.c`); glibc's start
  files call the same symbol.
- All other NovaC symbols are mangled (implemented for functions in
  Phase 3; generic type codes remain Provisional):

  ```text
  mangled   = "_NC" module-path "M" item-path [ "I" { type-code } "E" ]
  module-path, item-path = { <decimal length> <identifier> }
  ```

  Examples: `math.add` → `_NC4mathM3add`; method `len` of `geom.Point`
  → `_NC4geomM5Point3len`; `core.mem.swap::<u64>` →
  `_NC4core3memM4swapI3u64E`. The `M` separator prevents a collision
  between module `a.b` item `c` and module `a` item `b.c`. Type codes for
  generic arguments are specified with Phase 6.

- Private (non-`public`) functions get LLVM `internal` linkage, unless
  referenced from a `public` generic or inline body exported through an
  `.nci` file, in which case they get external linkage with hidden
  visibility.
- Generic instantiations and inline functions emitted in more than one
  object use `linkonce_odr` linkage in a COMDAT group.
- Prefixes reserved by D-020: `_NC` and `__nova_` for NovaC; `novac_` and
  `NOVAC_` belong to LibNovaC and are never emitted by NovaC.

## 8. Object files, sections and linking

- Output is ELF64 relocatable objects (`.o`), static archives (via
  `ar`), and executables linked by an external linker. **Nova Linker
  does not exist**; the driver invokes `ld.lld` or GNU `ld` (selectable
  with `--linker=`), or the system C compiler driver for hosted Linux
  targets so that the correct start files are used.
- Linker scripts are passed through (`-T script.ld`); NovarisOS's kernel
  depends on this (§10.2).
- By default functions and data are **not** placed in per-symbol sections
  (`.text.name`), because NovarisOS's `linker64.ld` collects only
  `*(.text)`, `*(.rodata)`, `*(.data)`, `*(.bss)`. `-ffunction-sections`
  is available for builds whose linker scripts handle it.
- `@section(".name")` places a function or static in a named section.
  `@used` keeps an otherwise-unreferenced symbol.
- LTO: objects may contain LLVM bitcode (`-flto=thin|full`), linked with
  `ld.lld` (Phase 8).

## 9. Runtime and freestanding contract

NovaC needs **no runtime library** to execute (D-015, D-022). Compiled
code may reference only the following external symbols, which the
environment must provide:

| Symbol | When emitted | Hosted provider | Freestanding provider |
| --- | --- | --- | --- |
| `memcpy`, `memmove`, `memset`, `memcmp` (C semantics) | LLVM may emit calls for copies, zeroing and comparisons of aggregates | the C library (LibNovaC, glibc) | the kernel/program (NovarisOS already has its own in `kernel/kstring.c`); `core` will offer weak fallbacks (planned) |
| `__nova_panic` | any panic path | `std` (prints, then `abort`) | the program, via a function marked `@panic_handler` |
| Compiler-rt builtins (`__udivti3`, …) | only for 128-bit integers (planned) and soft-float, which NovaC rejects in no-FPU modules (E7010) | libgcc / compiler-rt | must be linked by the program |
| `__stack_chk_guard`, `__stack_chk_fail` | only with `-fstack-protector` (off by default) | the C library | the program |

### 9.1 Panic interface

```c
// C view of the ABI-stable panic entry
struct __nova_panic_info {
    const unsigned char *msg;  size_t msg_len;
    const unsigned char *file; size_t file_len;
    uint32_t line; uint32_t column;
};
_Noreturn void __nova_panic(const struct __nova_panic_info *info);
```

Message and location strings are static data: the panic path never
allocates, which makes it usable in kernels and interrupt handlers
("kernel-safe diagnostics"). `-fpanic=minimal` omits the message and file
strings (null pointers, length 0) to save space; line and column remain.

The hosted default handler (D-039) is a weak definition in every hosted
object. It writes the following to standard error and calls `abort()`:

```text
<file>:<line>:<column>: panic: <message>
```

The messages produced in Phase 3 are `integer overflow in '+'` (also
`'-'`, `'*'`, `'/'`, `'%'`, `'++'`, `'--'`), `integer overflow in
negation`, `division by zero`, `shift amount too large`, and `value out of
range in conversion to '<type>'`.

### 9.2 Entry points

Hosted: `main` (§7). Freestanding: none implied. The program names its
entry symbol with `@export("name")` and the linker script or `-e` selects
it. NovaC never emits `_start`.

### 9.3 Stack

The compiler emits no stack probes and no stack-overflow checks by
default. Hosted targets rely on the OS guard page; on Linux and NovarisOS
large frames (> 4 KiB) can skip it, so `-fstack-probes` (inline probing,
LLVM `"probe-stack"="inline-asm"`) is planned and recommended for hosted
builds. Kernels manage their own stacks; the compiler reports each
function's frame size with `--emit-stack-sizes` (planned) so a kernel can
audit worst-case stack use.

### 9.4 Thread-local storage

`@thread_local` statics use ELF TLS (local-exec model for static
executables). Unavailable on freestanding targets (E7020).

## 10. NovarisOS specifics

### 10.1 Userspace (`x86_64-novaris`)

Derived from Phase 0 inspection of NovarisOS (commit `7656729`) and
LibNovaC (commit `244ed54`):

- NovarisOS implements the **Linux x86-64 system-call ABI** and loads
  ELF64 executables, both static and dynamic (through the real
  `ld-linux`). NovaC version 1 produces **static, non-PIE** executables
  for this target, the form LibNovaC programs use.
- Because the ABI is Linux's, code generation uses the
  `x86_64-unknown-linux-gnu` triple with the static relocation model; the
  only differences from `x86_64-linux-gnu` are the C library and link
  line.
- Link line (from LibNovaC's documentation):
  `ld -m elf_x86_64 -static -nostdlib -e _start crt0.o <objects> libNovaC.a`.
- LibNovaC has **no threads** (`errno` and stdio are process-wide), so
  `std.thread` is unavailable on this target until a threaded C library or
  libNova-Sys exists, even though the kernel implements `clone` and
  `futex`.
- Testing follows the established NovarisOS pattern: the same static
  binary runs on the Linux host and inside NovarisOS, and the transcripts
  must match.

### 10.2 Kernel (`x86_64-novaris-kernel`)

Requirements taken from NovarisOS `Makefile.amd64` and `linker64.ld`:

| NovarisOS requirement | NovaC setting |
| --- | --- |
| Kernel linked at `0xFFFFFFFF80000000` (top 2 GiB), `-mcmodel=kernel` | LLVM code model `kernel` |
| `-mno-red-zone` (interrupts push onto the same stack) | `noredzone` on every function |
| `-mno-sse -mno-mmx` (kernel never touches xmm; the syscall ABI preserves user xmm) | target features `-mmx,-sse,-sse2,-avx,…,+soft-float`; floating-point types rejected (E7010) so no soft-float calls appear |
| `-fno-pic -fno-pie` | static relocation model |
| `-fno-stack-protector`, `-ffreestanding` | defaults for freestanding targets |
| `.eh_frame` discarded | no unwind tables (`nounwind`, D-015) |
| Linker script collects `*(.text)` etc. only | no per-function sections (§8) |
| Boot code in `.boot.text`, VMA = LMA, entered in 32-bit mode | stays in NASM: NovaC has no 32-bit target |

Adoption path (Q-11): a NovaC kernel module is compiled to an object with
`--target x86_64-novaris-kernel`, exports `extern "C"` functions, and is
added to `Makefile.amd64`'s object list beside the C kernel objects. No
NovarisOS change is made from this repository.

## 11. C interoperability

### 11.1 Importing C functions

```c
@mode(controlled) module sys.libc;

extern "C" {
    i64 write(c_int fd, const u8* buf, usize count);
    c_int printf(const c_char* fmt, ...);
}
```

Every function in an `extern` block is `unsafe` to call (§6 of
NOVAC_MEMORY_MODEL.md). `@link_name("sym")` binds to a different symbol
name. Libraries are linked with `-l`/`-L` driver options.

### 11.2 Exporting NovaC functions

```c
extern "C" i32 nova_checksum(const u8* data, usize len) { ... }
```

The symbol is unmangled. `ncc --emit-c-header` (planned) writes a C
header for a module's exported functions and FFI-safe types into a
`nova/` directory (D-020).

### 11.3 FFI-safe types

Allowed in `extern "C"` signatures and in structs passed to C:
integers, `f32`, `f64`, `bool`, raw pointers, references (as non-null
pointers), `@callconv(c)` function pointers, `optional` of a reference or
function pointer (as a nullable pointer), enums (with an explicit
underlying type matching the C declaration — C enums are usually
`c_int`), and structs/unions/arrays-in-structs composed of FFI-safe types.

Not FFI-safe (E7030): `slice`, `str`, `optional` of other types,
`result`, `atomic` (C11 `_Atomic` layout is not guaranteed equal),
generic types, empty structs, types with destructors passed by value.

### 11.4 What is not compatible

C preprocessor macros, inline functions in headers, bitfields, flexible
array members, `long double`, `va_list`, defining variadic functions,
`setjmp`/`longjmp` across NovaC frames that hold values with destructors
(undefined behaviour), and C's `errno` (accessed through the C library's
function such as `__errno_location`, wrapped by `std.sys`).

## 12. ABI conformance testing (plan)

`tests/abi/` (Phase 4): for each target, a generator produces pairs of
NovaC and C files exercising every scalar type and struct classification
in arguments and return values, both directions, plus `sizeof`/`alignof`/
`offsetof` agreement for C-layout structs. Cross targets run under
`qemu-aarch64`/`qemu-riscv64` user-mode emulation (not installed in the
reference environment; a Phase 3 prerequisite for cross testing).

## 13. Diagnostics (ABI)

| Code | Condition |
| --- | --- |
| E7010 | floating point in a module targeting no FPU |
| E7020 | `@thread_local` on a freestanding target |
| E7030 | non-FFI-safe type in an `extern "C"` signature |
| E7031 | `long double` / `va_list` in FFI (unsupported) |
| E7032 | aggregate by value across `extern "C"` on a target whose ABI lowering is not yet verified |
| E7040 | `@interrupt` on a target without an interrupt calling convention |
