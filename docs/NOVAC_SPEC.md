# NovaC Language Specification

Version: **0.1-draft (Phase 1)**. Nothing in this specification is
implemented yet. What is implemented at any time is recorded only in
[FEATURE_STATUS.md](FEATURE_STATUS.md).

## 1. Introduction

NovaC is a C-derived systems programming language for the NovarisOS
ecosystem and for general systems work on x86-64, AArch64 and RISC-V 64.
It aims to combine:

- C-like syntax and C-level control over layout, memory and hardware;
- compile-time ownership and borrow checking (Rust-inspired) for safe
  code, with explicitly marked unsafe code;
- ahead-of-time native compilation through LLVM;
- C ABI interoperability;
- freestanding compilation for kernels and drivers, with no runtime and
  no garbage collector.

### 1.1 Design principles

1. **No undefined behaviour in safe code.** Operations C leaves undefined
   are compile errors, defined results, or panics (D-009, D-011).
2. **C-looking code means what it means in C, or is rejected** (D-011).
3. **Unsafe is explicit and narrow.** An `unsafe` block enables the listed
   unsafe operations and nothing else; it never disables unrelated checks
   (D-012).
4. **No hidden costs.** No garbage collector, no implicit heap
   allocation, no exceptions, no mandatory runtime (D-015, D-021).
   Performance claims require reproducible benchmarks (D-036).
5. **Explicit over implicit** for conversions, atomics and memory
   ordering (D-008, D-010).
6. **Architecture differences are exposed, not papered over**
   (NOVAC_ABI.md, NOVAC_MEMORY_MODEL.md §8.5).

### 1.2 Relationship to other NovarisOS components

| Component | Responsibility | Relationship to NovaC |
| --- | --- | --- |
| **NovaC** (this repository) | the language: specification, compiler (`ncc`), NovaC libraries `core`/`alloc`/`std` | — |
| **LibNovaC** | the C standard library for NovarisOS | separate project; NovaC `std` may *call* it over the C ABI on NovarisOS (D-021); NovaC is never implemented as LibNovaC |
| **libNova-Sys** | OS abstraction layer (system calls, platform interfaces) | does not exist yet (Q-02); NovaC `std.sys` will target it when it exists |
| **Nova Runtime** | optional runtime support | does not exist; NovaC's only runtime requirement is the panic entry point and `mem*` functions (NOVAC_ABI.md §9) |
| **Nova Linker** | linking infrastructure | does not exist; `ncc` uses `ld.lld` / GNU `ld` (NOVAC_ABI.md §8) |

## 2. Document map

| Document | Contents |
| --- | --- |
| [NOVAC_SPEC.md](NOVAC_SPEC.md) | this overview: program structure, declarations, statements, expressions, attributes |
| [NOVAC_GRAMMAR.md](NOVAC_GRAMMAR.md) | normative syntax |
| [NOVAC_TYPE_SYSTEM.md](NOVAC_TYPE_SYSTEM.md) | types, conversions, generics |
| [NOVAC_MEMORY_MODEL.md](NOVAC_MEMORY_MODEL.md) | modes, ownership, borrowing, unsafe, concurrency |
| [NOVAC_ABI.md](NOVAC_ABI.md) | targets, layout, calling conventions, symbols, C interop, NovarisOS |
| [NOVAC_COMPILER_ARCHITECTURE.md](NOVAC_COMPILER_ARCHITECTURE.md) | compiler design |
| [NOVAC_ROADMAP.md](NOVAC_ROADMAP.md) | phases, exit criteria, Phase 2 plan |
| [NOVAC_DECISIONS.md](NOVAC_DECISIONS.md) | decision records and open questions |
| [FEATURE_STATUS.md](FEATURE_STATUS.md) | implemented / planned / unsupported |
| [NOVARISOS_INTEGRATION.md](NOVARISOS_INTEGRATION.md) | Phase 0 findings and integration requirements |

When documents disagree, the order of precedence is: NOVAC_DECISIONS.md
(newest record), then NOVAC_GRAMMAR.md for syntax, NOVAC_TYPE_SYSTEM.md
and NOVAC_MEMORY_MODEL.md for semantics, NOVAC_ABI.md for layout and
linkage, then this document. A disagreement is a documentation bug.

## 3. Terminology

- **error** — the program is rejected at compile time with a diagnostic.
- **panic** — a defined run-time failure: the panic handler is called and
  does not return (D-015).
- **undefined behaviour (UB)** — no requirements. Possible only through
  violated obligations in `unsafe` code (NOVAC_MEMORY_MODEL.md §6.2).
- **target-defined** — fixed and documented per target in NOVAC_ABI.md.
- **unspecified** — one of a set of behaviours, may change between
  compiler versions (e.g. the NovaC-native calling convention).

## 4. Lexical structure (summary)

Normative rules: [NOVAC_GRAMMAR.md §2](NOVAC_GRAMMAR.md#2-lexical-grammar).
Source files are UTF-8 with extension `.nc`. There is no preprocessor
(D-004).

Keywords (must equal the list in NOVAC_GRAMMAR.md §2.3; checked by
`tools/check_docs.py`):

<!-- keywords:begin -->
```text
alignof as asm async auto await bool break case const constexpr continue
default defer do else enum extern f32 f64 false fallthrough fn for goto
i16 i32 i64 i8 if impl import in int interface is isize module mut none
null offsetof public return self Self sizeof some static struct switch
true try typedef u16 u32 u64 u8 uninit union unsafe usize void volatile
where while
```
<!-- keywords:end -->

## 5. Program structure

### 5.1 Modules

Each `.nc` file is one module (D-019):

```c
module net.tcp;            // file: <root>/net/tcp.nc

import core.mem;
import net.ip as ip;
import net.checksum.{fold16, sum16};

public i32 open_connection(u32 addr, u16 port) { ... }
i32 helper() { ... }       // private to net.tcp
```

- `import a.b;` binds the module name `b`; members are accessed as
  `b.member`. `as` renames. `.{x, y}` imports members unqualified.
- Importing a name that collides with a local declaration is E3030.
- Import cycles are E3031 (version 1).
- A module's **mode** is declared on its `module` declaration
  (NOVAC_MEMORY_MODEL.md §2).
- Module lookup: `--module-path DIR` (repeatable); `a.b.c` is found at
  `DIR/a/b/c.nc` or as a precompiled interface `DIR/a/b/c.nci`.

### 5.2 Visibility

Declarations are private to their module unless marked `public`. Struct
fields are private unless marked `public`, so a module can maintain
invariants that unsafe code relies on.

### 5.3 Program entry

Hosted programs define `i32 main()` (also spelled `int main(void)`).
Command-line arguments are obtained from `std.env` (Phase 6). The return
value is the process exit status. Freestanding programs have no implied
entry point (NOVAC_ABI.md §9.2).

## 6. Declarations

### 6.1 Functions

```c
i32 add(i32 a, i32 b) { return a + b; }
public void reset(Counter& c) { c->value = 0; }
unsafe u8 peek(const u8* p) { return *p; }        // controlled/kernel modules
extern "C" i32 nova_add(i32 a, i32 b) { return a + b; }
```

Functions are declared at module scope in any order. There is no
overloading: one name, one function. A parameter is a local variable:
mutable unless its type has a top-level `const`, as in C. A function
whose return type is not `void` must
return a value on every path (E4060). `@noreturn` functions must not
return (E4061).

### 6.2 Local variables

```c
i32 x = 5;              // mutable
const i32 y = 6;        // immutable binding
auto z = x + y;         // inferred: i32
const auto w = 1.5;     // inferred: f64, immutable
i32 a = 1, b = 2;       // several declarators share the type
i32 later;              // must be definitely assigned before use (E5031)
```

A local variable's scope begins at its declarator, as in C, but using it
inside its own initialiser is an error (E3003): in C `int x = x;` reads an
indeterminate value, and NovaC neither repeats that nor silently reads an
outer `x` (D-011). Earlier declarators of the same declaration are in
scope: `i32 a = 1, b = a;` is valid.

### 6.3 Module-level items

```c
constexpr usize PAGE_SIZE = 4096;          // compile-time constant, no address
static const str BANNER = "NovaC";         // immutable global, readable from safe code
static atomic<u64> ticks = 0;              // Sync interior mutability: safe to use
static i32 legacy_counter = 0;             // mutable, non-Sync: access is unsafe
typedef u64 PhysAddr;
```

### 6.4 Types

Structs, unions, enums, `impl` blocks and interfaces: see
[NOVAC_TYPE_SYSTEM.md](NOVAC_TYPE_SYSTEM.md).

## 7. Statements

| Statement | Semantics |
| --- | --- |
| `{ ... }` | block; a new scope; locals are destroyed at exit in reverse order |
| `if (c) s1 else s2` | `c` must be `bool`; `if (opt is some(v))` binds `v` in `s1` |
| `while (c) s`, `do s while (c);` | as C |
| `for (init; cond; step) s` | as C; variables declared in `init` are scoped to the loop |
| `switch (e) { case ...: }` | integer, enum or `bool` operand; no implicit fallthrough (D-025); duplicate case values are E4070 |
| `break;` `continue;` | as C |
| `return e;` | returns; runs pending `defer`s and destructors of the exited scopes |
| `defer s;` | `s` runs at exit of the enclosing block, in reverse order of `defer`s (D-034) |
| `unsafe { ... }` | permits unsafe operations in the block (NOVAC_MEMORY_MODEL.md §6) |
| `asm volatile (...);` | inline assembly (kernel mode; D-028) |
| `fallthrough;` | last statement of a `case` body, continues into the next |

## 8. Expressions

- **Evaluation order** is left to right for operands, arguments and
  initialiser lists; side effects of an operand complete before the next
  operand is evaluated (D-024).
- `&&`, `||`, `?:` short-circuit; their operands must be `bool`
  (the condition of `?:`).
- Assignments have type `void` (D-024).
- Integer arithmetic semantics: NOVAC_TYPE_SYSTEM.md §2.2.
- Conversions: NOVAC_TYPE_SYSTEM.md §5.
- `sizeof(T)` / `alignof(T)` / `offsetof(T, f)` have type `usize` and are
  constant.
- `try e` propagates `none`/`err` (D-026).
- `a is pattern` tests an `optional`/`result`/enum value.

## 9. Attributes

Attributes are written `@name` or `@name(args)` before the declaration
they apply to. Unknown attributes are E3040.

| Attribute | Applies to | Min. mode | Meaning | Phase |
| --- | --- | --- | --- | --- |
| `@mode(safe\|controlled\|kernel)` | module | — | module mode | 4 |
| `@cfg(cond)` | any declaration, statement | safe | conditional compilation (§10) | 6 |
| `@export("sym")` | function, static | safe | external symbol name | 4 |
| `@link_name("sym")` | extern item | controlled | bind to a differently named symbol | 4 |
| `@callconv(cc)` | function, fn type | kernel (except `c`) | calling convention (NOVAC_ABI.md §6) | 4/7 |
| `@interrupt`, `@interrupt(level)` | function | kernel | interrupt handler | 7 |
| `@naked` | function | kernel | no prologue/epilogue | 7 |
| `@section(".s")` | function, static | controlled | place in section | 7 |
| `@used` | function, static | controlled | keep even if unreferenced | 7 |
| `@align(N)` | struct, field, static | safe | raise alignment | 4 |
| `@packed` | struct | controlled | remove padding (fields may be misaligned; borrowing a misaligned field is E5050) | 4 |
| `@repr(C)` | struct, union, enum | safe | documents C layout (already the default, D-017) | 4 |
| `@nocopy` | struct | safe | opt out of implicit Copy (D-014) | 5 |
| `@inline`, `@inline(always\|never)` | function | safe | inlining hint | 3 |
| `@cold` | function | safe | unlikely to be called | 3 |
| `@noreturn` | function | safe | never returns | 3 |
| `@must_use` | function, type | safe | warn when a result is ignored | 6 |
| `@deprecated("msg")` | any | safe | warn on use | 6 |
| `@thread_local` | static | safe | per-thread storage (hosted) | 8 |
| `@overflow(wrap\|trap)` | module, function | controlled | overflow policy (D-016) | 5 |
| `@panic_handler` | function | controlled | defines `__nova_panic` (NOVAC_ABI.md §9.1) | 7 |
| `@asm_syntax(att\|intel)` | function, module | kernel | x86 assembly dialect | 7 |
| `@unsafe_impl(Send\|Sync)` | struct | controlled | assert thread safety | 8 |

## 10. Conditional compilation

`@cfg(cond)` keeps or removes the declaration or statement it precedes.
Conditions are constant expressions over:

| Name | Values |
| --- | --- |
| `target.arch` | `"x86_64"`, `"aarch64"`, `"riscv64"` |
| `target.os` | `"linux"`, `"novaris"`, `"none"` |
| `target.freestanding` | `bool` |
| `target.pointer_width` | `64` |
| `debug` | `bool` (true at `-O0` unless overridden) |
| `feature("name")` | `bool`, set by `--feature name` |

Combined with `&&`, `||`, `!`, `==`, `!=`. Removed code must still parse;
it is not name-resolved or type-checked.

## 11. Errors and panics

Recoverable errors are values (`optional<T>`, `result<T, E>`, D-026).
Unrecoverable errors panic (D-015). Built-in panic sources: arithmetic
checks, conversion checks, bounds checks, `unwrap` of `none`/`err`, and
`panic("message")` (a prelude function). A panic never unwinds.

## 12. Standard library layering

| Library | Requires | Contents (planned) |
| --- | --- | --- |
| `core` | nothing | prelude types, `core.mem`, `core.num`, `core.sync` (atomics, fences, `spin_mutex`), `core.ffi`, `core.arch.*`, `core.fmt` into buffers |
| `alloc` | an allocator supplied by the program | `box<T>`, `vec<T>`, `string`, maps; allocator interface |
| `std` | a hosted target | `std.io`, `std.fs`, `std.env`, `std.process`, `std.thread`, `std.sync`, `std.time`, `std.sys` |

`std.sys` is the only part of `std` that touches the OS. On NovarisOS and
Linux it is initially implemented over the C library (LibNovaC or glibc)
through `extern "C"`; it will be re-targeted to libNova-Sys when that
exists (D-021, Q-02).

The library modules themselves are Planned (Phase 6 onwards); see
FEATURE_STATUS.md.

## 13. Examples

These examples show intended syntax. They cannot be compiled yet.

### 13.1 The brief's first example, in NovaC

Brief (C-style, with `#include` and a printf-style function):

```c
#include <nova/stdio.h>
int main(void) { int a = 10; int b = 20; int result = a + b;
                 nova_print("Result: %d\n", result); return 0; }
```

NovaC (D-004: no preprocessor; formatting is type-checked at compile time
instead of trusting a `%d` string):

```c
module hello;
import std.io;

int main(void) {
    int a = 10;
    int b = 20;
    int result = a + b;            // panics on overflow instead of UB
    io.println("Result: {}", result);   // format checked at compile time (Phase 6)
    return 0;
}
```

### 13.2 The brief's module example

```c
// math.nc
module math;
public int add(int a, int b) { return a + b; }

// main.nc
module main;
import math.{add};          // the brief's unqualified `add` requires this form (D-019)
int main(void) { return add(10, 20); }
```

### 13.3 Ownership and borrowing

```c
module demo;
import alloc.vec.{vec};

void fill(vec<u32>& v, u32 n) {                 // exclusive borrow
    for (u32 i = 0; i < n; i++) { v->push(i); }   // `->` through a reference (D-006)
}

u32 sum(slice<const u32> xs) {                  // shared borrow
    u32 total = 0;
    for (usize i = 0; i < xs.len; i++) { total += xs[i]; }   // checks elided when provable
    return total;
}

int main(void) {
    vec<u32> v = vec.new();
    fill(&mut v, 10);
    u32 s = sum(v.as_slice());
    vec<u32> w = v;          // move: v is now uninitialised
    // v.push(1);            // E5001: use of moved value
    return int(s);           // checked conversion
}                            // w is destroyed here; v is not (it was moved)
```

### 13.4 Kernel code (the brief's `@kernel @no_runtime` example)

```c
@mode(kernel) module novaris.serial;

constexpr u16 COM1 = 0x3F8;

// SAFETY: port I/O can reprogram any device; callers must pass a port
// they own. That is why these are `unsafe` rather than safe wrappers.
unsafe u8 inb(u16 port) {
    u8 v;
    asm volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

// SAFETY: as inb.
unsafe void outb(u16 port, u8 v) {
    asm volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

@export("serial_putc")
extern "C" void serial_putc(u8 c) {
    unsafe {
        while ((inb(COM1 + 5) & 0x20) == 0) { }   // wait for the transmitter
        outb(COM1, c);
    }
}
```

The body of an `unsafe` function is an unsafe context, so the `asm`
statements need no inner block.

Compiled with `ncc --target x86_64-novaris-kernel -c serial.nc`, the
object links into the existing C kernel (NOVAC_ABI.md §10.2).

## 14. Evaluation of the brief's proposed syntax

| Proposed | Decision | Record |
| --- | --- | --- |
| `#include <nova/stdio.h>` | replaced by `import` | D-004 |
| `nova_print("...%d", x)` | compile-time-checked formatting, Phase 6 | D-004 |
| `optional int result;` | `optional<i32> result;` | D-008 |
| `slice<int> numbers;` | kept: `slice<i32>` (exclusive) / `slice<const i32>` (shared) | D-008 |
| `atomic int counter = 0;` | `atomic<i32> counter = 0;` with explicit orderings | D-008 |
| `borrow int* reference = &value;` | `const i32& reference = &value;` (or `i32& r = &mut value;`) | D-006 |
| `module math; public int add(...)` | kept | D-019 |
| `import math; ... add(10, 20)` | `import math.{add};` or `math.add(10, 20)` | D-019 |
| `@kernel @no_runtime void kernel_entry(void)` | `@mode(kernel)` on the module; freestanding is a target property; no runtime is ever required | D-012, D-022 |
| `int` | kept as alias of `i32` | D-031 |
| explicit `i8 … u64`, `f32`, `f64`, `bool`, `usize`, `isize` | kept | NOVAC_TYPE_SYSTEM.md §2 |

## 15. Diagnostics

Codes are allocated by range in
[NOVAC_COMPILER_ARCHITECTURE.md §6](NOVAC_COMPILER_ARCHITECTURE.md#6-diagnostics);
each specification document lists the codes it defines. Codes defined
here:

| Code | Condition |
| --- | --- |
| E3001 | use of an undeclared name |
| E3002 | redefinition of a name in the same scope |
| E3003 | a variable used in its own initialiser |
| E3011 | C type name (`char`, `long`, `short`, `unsigned`, …) used (D-031) |
| E3020 | user declaration of a reserved `__` identifier |
| E3030 | imported name collides with a declaration |
| E3031 | import cycle |
| E3040 | unknown attribute |
| E4060 | missing return value on some path |
| E4061 | `@noreturn` function can return |
| E4070 | duplicate `case` value |
| W3001 | prelude name shadowed |
