# NovaC Architectural Decision Records

Status: **Phase 1 — accepted unless marked otherwise.**

This file is append-only. A decision is never edited to mean something
different: it is *superseded* by a new record that names the old one.
Every other specification document cites decisions by ID (`D-NNN`), and
`tools/check_docs.py` verifies that every cited ID exists here.

Each record states the context, the options considered, the decision,
and its consequences. Where the original project brief proposed
something different, the record says so explicitly.

Status values: **Accepted**, **Provisional** (accepted for planning, to be
revisited before the phase that implements it), **Open** (not decided;
listed in [Outstanding questions](#outstanding-questions)).

---

## D-001 — Compiler implementation language: C11

**Status:** Accepted

**Context.** The brief requires the implementation language to be chosen
from the existing infrastructure. Phase 0 found: NovarisOS is written in
GNU C99 + NASM; LibNovaC is C11; both build with GNU Make and use Python 3
for tooling. NovaC itself had no code.

**Options.**

| Option | For | Against |
| --- | --- | --- |
| C11 + LLVM-C API | Matches both sister projects; the toolchain NovarisOS already needs; a later self-hosting path (NovaC can import C ABI); no new language in the ecosystem | Manual memory management in the compiler; LLVM-C API covers less than the C++ API; no generics in the implementation language |
| C++17 + LLVM C++ API | Full LLVM API; RAII; containers | Introduces a language used nowhere else in the ecosystem; LLVM C++ API is unstable across major versions |
| Rust + inkwell | Memory safety in the compiler itself | New language and toolchain for the ecosystem; NovaC would be bootstrapped from a different safe language, duplicating its purpose |

**Decision.** The compiler is written in **C11** (ISO C11 plus no
extensions beyond what both GCC 13 and Clang 18 accept with
`-std=c11 -Wall -Wextra -Werror`). The backend uses the **LLVM-C API**.

**Consequences.**
- The compiler uses arena allocation per compilation phase (see
  [NOVAC_COMPILER_ARCHITECTURE.md](NOVAC_COMPILER_ARCHITECTURE.md)) to keep
  manual memory management tractable.
- If a required LLVM facility has no C binding, a small C++ shim is
  permitted **only** under `src/codegen/shim/`, exporting a C interface,
  with each function justified in a comment. No C++ elsewhere.
- Self-hosting is a post-1.0 goal, not a commitment (see D-036).

## D-002 — Build system: GNU Make; test tooling in Python 3

**Status:** Accepted

**Context.** Both sister projects use GNU Make and Python 3 (standard
library only). CMake 3.28 is available but used by neither.

**Decision.** GNU Make, out-of-tree outputs under `build/`. Test runners
and documentation checks are Python 3 scripts using only the standard
library. No other build or test dependency is introduced without a new
decision record.

**Consequences.** Contributors on NovarisOS-oriented machines need nothing
new. LLVM is located with `llvm-config` (overridable via `LLVM_CONFIG`).

## D-003 — Backend: LLVM ≥ 18 via LLVM-C

**Status:** Accepted

**Decision.** Minimum supported LLVM is **18** (the version available in
the reference environment). The compiler links `libLLVM` through
`llvm-config --libs`. Optimisation uses the new pass manager through
`LLVMRunPasses` with pipeline strings (`default<O2>` etc.).

**Consequences.**
- Phase 3 requires LLVM development headers (`llvm-c/*.h`). They are not
  installed in the reference environment; installing `llvm-18-dev` (or
  equivalent) is a Phase 3 prerequisite.
- **LLVM does not perform C ABI lowering.** Like Clang, NovaC must
  classify aggregate arguments per target itself (see
  [NOVAC_ABI.md §5](NOVAC_ABI.md#5-c-calling-convention-lowering)). This is
  the largest single technical risk in C interoperability.
- LLVM major-version upgrades are deliberate events with their own
  decision record.

## D-004 — No textual preprocessor; `import` and `@cfg` replace it

**Status:** Accepted. **Deviates from the brief's example**, which used
`#include <nova/stdio.h>`.

**Context.** A C preprocessor operates on tokens before parsing. It
defeats separate compilation of modules (every importer re-parses
headers), makes diagnostics point into macro expansions, lets macros
silently change ownership-relevant code, and makes "what does this module
export" undecidable without expansion.

**Options.** (a) Full C preprocessor for compatibility. (b) A restricted
preprocessor (`#include`, `#if` only). (c) No preprocessor: modules for
sharing, attributes for conditional compilation, `constexpr` for
constants, generics and inline functions for macros.

**Decision.** (c). A line beginning with `#` is a lexical error (E1010)
whose diagnostic suggests the replacement (`import`, `@cfg`, `constexpr`).

**Consequences.**
- C headers are not textually includable. C declarations are imported by
  writing `extern "C"` declarations (Phase 4). An optional binding
  generator that reads C headers via libclang is *planned* as a separate
  tool, never a compiler dependency (see [FEATURE_STATUS.md](FEATURE_STATUS.md)).
- The brief's `#include <nova/stdio.h>` + `nova_print(...)` example
  becomes `import std.io;` + a type-checked print function (Phase 6; see
  D-026 and [NOVAC_SPEC.md §12](NOVAC_SPEC.md#12-standard-library-layering)).

## D-005 — Declarations: type-first C style, parseable without a symbol table

**Status:** Accepted

**Context.** C's declarator syntax (`int (*fp)(int)`, `int a[4]`) needs
the "lexer hack": the parser must know whether an identifier names a type.
NovaC declarations are order-independent across a module and imported
from other modules, so no symbol table exists at parse time.

**Decision.**
- Every type is written entirely **before** the declared name.
  Type constructors are postfix: `T*`, `T[N]`, `T&`, `R fn(P1, P2)`.
  C's `int a[4]` is written `i32[4] a`; C's `int (*f)(int)` is written
  `i32 fn(i32) f`.
- The parser decides between declaration and expression statements with
  the rule **"if a statement parses as a declaration, it is one"**
  (the rule D uses). `a * b;` declares `b` of type `a*`. Such
  statements have no useful expression meaning, so nothing is lost.
- Relational operators are **non-associative** (`a < b < c` is a syntax
  error), which removes the generic-argument ambiguity `a < b > c`.

**Consequences.** The grammar in [NOVAC_GRAMMAR.md](NOVAC_GRAMMAR.md) is
parseable with bounded backtracking and no semantic feedback. C-style
array declarators are rejected with a fix-it diagnostic (E2020).

## D-006 — Pointer and reference syntax

**Status:** Accepted. **Replaces the brief's proposed `borrow int*`.**

**Context.** The brief proposed `borrow int* reference = &value;`. That
spells a borrow as a qualified raw pointer, which conflates two things
with different rules (a raw pointer has no lifetime, a borrow does) and
has no way to say shared vs. exclusive.

**Decision.**

| Type | Meaning | Null? | Checked by borrow checker? |
| --- | --- | --- | --- |
| `T*` | raw pointer to mutable `T` | yes | no |
| `const T*` | raw pointer to immutable `T` | yes | no |
| `T&` | **exclusive** (mutable) borrow | never | yes |
| `const T&` | **shared** (immutable) borrow | never | yes |

Expressions: `&x` creates a shared borrow (`const T&`); `&mut x` creates
an exclusive borrow (`T&`). A borrow converts implicitly to the raw
pointer of matching constness; creating a raw pointer is safe,
dereferencing one is an unsafe operation.

The `const` placement rule is C's: `const` before the base type qualifies
the base; `const` after a `*` qualifies that pointer level; a top-level
`const` makes the binding immutable (`u8* const p`).

**Consequences.** `i32* p = &x;` is an error (shared borrow cannot become
a mutable pointer); the diagnostic suggests `&mut x`. Member access
through any pointer or reference uses `->`; `.` is for values only (C's
rule, kept).

## D-007 — Generic argument syntax

**Status:** Accepted

**Decision.** In type position: `Name<Args>` (`slice<u8>`,
`optional<i32>`). In expression position, explicit generic arguments use
`name::<Args>(...)`; they are usually inferred and omitted. Relational
operators are non-associative (D-005).

## D-008 — `optional`, `slice`, `atomic` are built-in generic types

**Status:** Accepted. **Refines the brief's `optional int`, `slice<int>`,
`atomic int`.**

**Context.** The brief mixed a qualifier style (`optional int`,
`atomic int`) with a generic style (`slice<int>`). One style is needed.
C11 `_Atomic int` also allows `counter++` to silently be a sequentially
consistent read-modify-write, hiding cost and ordering.

**Decision.** `optional<T>`, `result<T, E>`, `slice<T>` / `slice<const T>`,
`atomic<T>` and `mmio<T>` are built-in generic types with fixed layouts
(see [NOVAC_TYPE_SYSTEM.md](NOVAC_TYPE_SYSTEM.md)). `atomic<T>` has **no**
operators; every operation is a method with an explicit memory ordering.

## D-009 — Integer semantics: no undefined behaviour

**Status:** Accepted

**Context.** C: signed overflow is UB, operands are promoted to `int`,
shifts by ≥ width are UB, `INT_MIN / -1` is UB. Rust: overflow panics in
debug, wraps in release (two semantics). Zig: UB in ReleaseFast.

**Decision.**
- Arithmetic happens in the operand type. There are **no integer
  promotions** and no implicit signed/unsigned mixing.
- Overflow of `+ - * ++ --` and unary `-`, division by zero,
  `MIN / -1`, `MIN % -1`, and shift amounts `>=` the bit width are
  **checked errors that panic**, at every optimisation level including
  `-Ofast`.
- Wrapping arithmetic is spelled explicitly: `+%`, `-%`, `*%`, and the
  compound forms `+%=`, `-%=`, `*%=`.
- Saturating and overflow-reporting forms are library functions in
  `core.num` (Phase 6).

**Consequences.** NovaC is not assumed to match C on integer-heavy code;
check elimination by range analysis is an optimisation to be *measured*
(Phase 8), not promised. See D-016 for the only configurable aspect.

## D-010 — Conversions

**Status:** Accepted

**Decision.**
- **Implicit**: only lossless widening between integer types of the same
  signedness, unsigned→wider signed, `f32→f64`, borrow→raw pointer, `T`→
  `optional<T>`, array borrow→slice, and integer literals to any type that
  can represent the value.
- **Checked explicit**: `T(x)` converts and **panics** if the value is not
  representable (integer narrowing, sign change, float→int out of range
  or NaN).
- **Unchecked explicit** (always defined behaviour, never UB):
  `truncate::<T>(x)` (keep low bits), `bitcast::<T>(x)` (same size),
  `ptr_cast::<T*>(p)`, `addr(p)` / `ptr_from_addr::<T>(a)`.
- There is **no C-style cast** `(T)x`: it is ambiguous with
  parenthesised expressions without a symbol table (D-005). It is
  rejected with a fix-it (E2031).

## D-011 — The C-familiarity principle

**Status:** Accepted

**Decision.** *Code that looks like C either means what it means in C, or
is rejected with a diagnostic.* Documented exceptions, each of which turns
C undefined or implementation-defined behaviour into a **defined panic**,
never into a silently different result:

1. Integer overflow panics (C: UB for signed, wraps for unsigned — the
   unsigned case is a real divergence; `+%` restores C's meaning).
2. Arithmetic is performed at operand width, not promoted to `int`
   (where C would have produced a wider intermediate, NovaC panics on
   overflow instead of giving C's result).
3. Evaluation order of operands and arguments is left to right
   (C: unspecified).

Examples of rejection rather than reinterpretation: `0755` (leading-zero
octal), `(int)x` casts, implicit `switch` fallthrough, `if (x = 1)`,
`int a[4]`, `.5` float literals.

## D-012 — Three programming modes as module-level capability sets

**Status:** Accepted

**Decision.** Every module has exactly one mode: `safe` (default),
`controlled`, or `kernel`, declared by an attribute on the module
declaration: `@mode(kernel) module arch.x86_64.idt;`.

- The mode states which *unsafe operations* may appear in the module.
  It does **not** make them implicit: every unsafe operation still has
  to be inside an `unsafe { }` block or an `unsafe` function, in every
  mode.
- An `unsafe` block permits only the operations listed as unsafe in
  [NOVAC_MEMORY_MODEL.md §6](NOVAC_MEMORY_MODEL.md#6-unsafe-operations).
  Borrow checking, initialisation checking, type checking, bounds checks
  on slices and arrays, and overflow checks **continue to apply inside
  unsafe blocks**. ("Unsafe never silently disables unrelated checks.")
- The driver option `--max-mode=safe|controlled|kernel` caps the mode a
  compilation accepts, so an application build can refuse kernel modules.

**Consequences.** Guarantees per mode are tabulated in
[NOVAC_MEMORY_MODEL.md §2](NOVAC_MEMORY_MODEL.md#2-the-three-modes). A safe
module may call public, non-`unsafe` functions of controlled or kernel
modules: those modules are responsible for upholding safety at their
boundary (the "safe abstraction" model).

## D-013 — Ownership and borrow analysis runs on NovaC IR, not the AST

**Status:** Accepted. **Reorders the brief's pipeline**, which placed
ownership analysis before IR generation.

**Context.** Borrow lifetimes that end at last use (non-lexical
lifetimes), conditional moves, loops and early returns are dataflow
problems over a control-flow graph. Rust replaced its AST borrow checker
with a CFG-based one for exactly this reason.

**Decision.** Pipeline: parse → AST → name resolution → type checking →
typed AST → **lowering to NIR** (a CFG of basic blocks over typed places)
→ **initialisation, move, borrow and lifetime analysis on NIR** → NIR
cleanup → LLVM IR. Full pipeline in
[NOVAC_COMPILER_ARCHITECTURE.md](NOVAC_COMPILER_ARCHITECTURE.md).

## D-014 — Copy and move

**Status:** Provisional (revisit before Phase 5)

**Options.** (a) Rust: move by default; `Copy` only by explicit opt-in.
(b) Structural: plain-data structs are implicitly `Copy`.

**Decision.** (b). A type is **Copy** if it is a scalar, raw pointer,
shared borrow, function pointer, `slice<const T>`, array of Copy, or a
struct/union whose fields are all Copy *and* which has no destructor and
is not marked `@nocopy`. All other types **move**.

**Rationale.** C structs are passed by value constantly; requiring
annotation on every plain struct fights the C-familiarity goal. The cost
is that adding a destructor or a non-Copy field changes a type from copy
to move — which produces compile errors at every affected use, never
silent behaviour change.

## D-015 — Panics abort; no unwinding

**Status:** Accepted

**Decision.** A panic (failed check, `unwrap` of `none`, explicit
`panic(...)`) calls the panic handler, which must not return. There is no
stack unwinding and destructors are not run. Hosted builds get a default
handler (print to stderr, abort); freestanding builds must define one
(see [NOVAC_ABI.md §9](NOVAC_ABI.md#9-runtime-and-freestanding-contract)).

**Consequences.** No unwind tables are required; no personality routine;
kernel-friendly. Recoverable errors use `optional`/`result` (D-026).

## D-016 — No global switch disables safety checks

**Status:** Accepted. **Interprets the brief's "configurable safety
checks" for controlled mode.**

**Decision.** No compiler flag removes bounds, overflow, or null checks
from code that the type system treats as safe. What *is* configurable:

1. **Overflow policy, scoped:** in `controlled` and `kernel` modules,
   `@overflow(wrap)` on a function or module makes `+ - *` wrap (defined
   behaviour) instead of panicking.
2. **Unchecked operations, per call site:** unsafe intrinsics such as
   `get_unchecked`, `add_unchecked` (overflow is UB) exist and require an
   `unsafe` block.
3. **Check-failure reporting:** `-fpanic=verbose|minimal` controls how much
   location information panics carry (affects binary size, not
   semantics).

## D-017 — Default struct layout is the C layout

**Status:** Accepted

**Decision.** Structs are laid out in declaration order with the target C
ABI's alignment and padding rules, identical to `@repr(C)`. The compiler
never reorders fields. `@packed` and `@align(N)` modify layout explicitly.
Because default layout *is* C layout, `@repr(C)` is accepted but
redundant; it exists to document intent at FFI boundaries.

## D-018 — Calling conventions

**Status:** Accepted

**Decision.** Functions declared `extern "C"` follow the target's C ABI,
which is the only **stable** NovaC ABI. The ABI of ordinary NovaC
functions is unspecified and may change between compiler versions
(initially it is the C calling convention, which keeps debugging simple).
Explicit conventions are selected with `@callconv(...)`; see
[NOVAC_ABI.md §6](NOVAC_ABI.md#6-calling-conventions).

## D-019 — Module system shape

**Status:** Accepted

**Decision.**
- One source file is one module; its first declaration is
  `module a.b.c;` and must match its path relative to a source root.
- `import a.b;` makes the module available **qualified** (`b.f()`;
  `import a.b as x;` renames). `import a.b.{f, g};` imports names
  unqualified. The brief's example (`import math;` then unqualified
  `add(...)`) is written `import math.{add};` or `math.add(...)`.
- Symbols are private to their module unless declared `public`.
- **Import cycles are an error** in version 1.
- Separate compilation: each module compiles to an object file plus a
  **`.nci` interface file** (see
  [NOVAC_COMPILER_ARCHITECTURE.md §8](NOVAC_COMPILER_ARCHITECTURE.md#8-modules-and-separate-compilation)).

## D-020 — Names reserved to avoid collisions with LibNovaC

**Status:** Accepted

**Context.** Phase 0 found that LibNovaC already owns the `NOVAC_*` macro
prefix, the `novac_*` C symbol prefix, the `<novac/...>` header directory,
and the Make variable `NOVAC_CC` (which names the *C compiler used to
build LibNovaC*). NovarisOS's `Makefile.amd64` uses `NOVAC_DIR`,
`NOVAC_TEST` and `novac-force` to mean LibNovaC.

**Decision.** The NovaC project never defines or emits any of these.

| Use | NovaC name |
| --- | --- |
| Compiler driver executable | `ncc` |
| Make/environment variable naming the NovaC compiler | `NCC` |
| Compiler-internal C symbols | `nc_` prefix |
| Runtime / compiler-emitted support symbols | `__nova_` prefix |
| Mangled NovaC symbols | `_NC` prefix (see NOVAC_ABI.md §7) |
| C headers generated for C consumers of NovaC exports | `<nova/...>` |

## D-021 — Library layering: `core`, `alloc`, `std`

**Status:** Accepted

**Decision.**
- `core`: freestanding, no heap, no OS. Built-in type support, slices,
  optionals, atomics, `mmio`, intrinsics, `core.ffi` C types, formatting
  into caller-provided buffers. Always available.
- `alloc`: heap data structures, parameterised by an allocator interface.
  Requires an allocator to be provided; never assumes one exists.
- `std`: hosted facilities (files, threads, processes, time). Talks to the
  OS **only** through a `std.sys` interface. Its first implementation
  binds to a C library over the C ABI (LibNovaC on NovarisOS, glibc on
  Linux hosts). When libNova-Sys exists, `std.sys` is re-targeted to it.

**Consequences.** NovaC is not implemented as LibNovaC and does not
depend on it for anything but hosted OS access; freestanding programs
use only `core`. The language standard library is a NovaC library, not a C
library.

## D-022 — Freestanding is a target property, orthogonal to mode

**Status:** Accepted. **Refines the brief's `@kernel @no_runtime`.**

**Decision.** *Freestanding* (no OS, no `std`) is selected by the target
(`--target x86_64-unknown-none`) or `--freestanding`. *Kernel mode* is a
permission set for a module. They are independent: a kernel-mode module
can be unit-tested hosted; a freestanding embedded program can be
entirely safe-mode. `@no_runtime` is unnecessary because NovaC never
requires a runtime (D-015, NOVAC_ABI.md §9). The brief's example becomes:

```c
@mode(kernel) module kernel.entry;

@export("kernel_entry") @section(".text.entry")
extern "C" void kernel_entry() { ... }
```

## D-023 — Initial targets: 64-bit, little-endian, ELF

**Status:** Accepted

**Decision.** Supported architectures: x86-64 (baseline x86-64-v1),
AArch64 (ARMv8.0-A, little-endian), RISC-V 64 (RV64GC, `lp64d`).
Object format: ELF64 only. 32-bit targets, big-endian targets, PE/COFF and
Mach-O are unsupported.

**Consequences.** NovarisOS currently runs only on x86-64 (its legacy
i386 kernel is not a NovaC target). AArch64 and RISC-V are supported as
Linux-hosted and freestanding targets; **NovaC cannot target NovarisOS on
those architectures until NovarisOS is ported to them**, which is outside
this project.

## D-024 — Assignment is not a value; evaluation order is defined

**Status:** Accepted

**Decision.** Assignment and compound assignment are expressions of type
`void`; using one where a value is needed (`if (x = 1)`, `a = b = 0`) is a
type error (E4030). `++`/`--` exist in prefix and postfix form and yield
values. Operands, arguments and initialisers are evaluated left to right.
The comma operator does not exist; `for` headers accept comma-separated
expression lists.

## D-025 — `switch` without implicit fallthrough

**Status:** Accepted

**Decision.** Each non-empty `case` body must end in `break`, `continue`,
`return`, a call to a no-return function, or the statement `fallthrough;`.
Otherwise E2050. Consecutive labels (`case 1: case 2:`) share one body as
in C. A `switch` over an enum must be exhaustive or have `default`.

## D-026 — Errors are values; `try` propagates

**Status:** Accepted

**Decision.** No exceptions. `optional<T>` and `result<T, E>` carry
failure. `try expr` evaluates `expr`; on `none`/error it returns early
from the enclosing function with that value (converted if needed). The
postfix `?` operator used by Rust is **not** adopted: it conflicts with
C's conditional operator `a ? b : c`, which NovaC keeps.

## D-027 — Memory model; MMIO without a `volatile` qualifier

**Status:** Accepted

**Decision.** NovaC adopts the C++11/C11 memory model as implemented by
LLVM, with orderings `relaxed`, `acquire`, `release`, `acq_rel`, `seq_cst`
(no `consume`). Volatile access is expressed with `volatile_load` /
`volatile_store` intrinsics and the `mmio<T>` type, **not** a `volatile`
type qualifier, whose C semantics (which accesses are volatile through
which lvalues) are a long-standing source of bugs. `volatile` is a
reserved word whose use produces a fix-it diagnostic.

## D-028 — Inline assembly uses GCC extended syntax

**Status:** Accepted

**Decision.** `asm volatile ("template" : outputs : inputs : clobbers);`
with GCC constraint strings, mapped to LLVM inline asm. x86 templates use
AT&T syntax by default (as NovarisOS's existing GCC inline asm does);
`@asm_syntax(intel)` selects Intel. Module-level assembly and `@naked`
functions are supported in kernel mode.

## D-029 — No language-level async in version 1

**Status:** Provisional

**Decision.** `async` and `await` are reserved. Asynchronous programming
in version 1 is library-level (callbacks, readiness polling). A future
design will lower `async` functions to stackless state machines with no
mandatory executor or runtime; executors would be ordinary libraries.

## D-030 — Literals

**Status:** Accepted

**Decision.** Integer: decimal, `0x`, `0o`, `0b`, with `_` separators and
optional type suffix (`255u8`, `1_000i64`); a leading `0` followed by
digits is an error (E1020). Float: requires digits on both sides of `.`
or an exponent (`1.0`, `1e9`, `0x1.8p3`), suffixes `f32`/`f64`. Untyped
literals take their type from context, default `i32` / `f64`, and it is a
compile-time error if the value does not fit. Character literal `'a'` is
a `u8` and must be ASCII. String literal `"..."` has type `str`
(`slice<const u8>`, UTF-8, not NUL-terminated); `c"..."` has type
`const c_char*` and is NUL-terminated.

## D-031 — C's fundamental type names

**Status:** Accepted

**Decision.** `int` is a keyword alias for `i32` (fixed: 32-bit on every
target). `char`, `short`, `long`, `signed`, `unsigned` are not NovaC
types: their C sizes and signedness vary by target (e.g. `char` is signed
on x86-64 Linux but unsigned on AArch64 and RISC-V Linux; `long` is 64-bit
on LP64 but 32-bit on Windows). C interop uses `core.ffi` aliases
(`c_char`, `c_int`, `c_long`, …) whose definitions are per-target. Using
`char`/`long`/etc. produces a fix-it diagnostic (E3011).

## D-032 — Adjustments to the phase plan

**Status:** Accepted

**Context.** Reviewing the brief's roadmap found gaps: name resolution and
type checking are not assigned to any phase, but Phase 3 code generation
needs typed programs; structs, arrays and pointers are not assigned
before Phase 4 needs them for C struct layout; raw pointers would exist in
Phase 4 before the `unsafe` gate in Phase 5, creating a window where
pointer dereference compiles in safe code.

**Decision.**
- Phase 2 includes name resolution and type checking for its subset.
- Phase 4 introduces structs, enums, unions, fixed arrays, raw pointers,
  **and** `unsafe` blocks and mode checking, together. There is never a
  build in which a raw pointer can be dereferenced outside `unsafe`.
- Phase 5 adds references, ownership, borrowing, lifetimes and bounds
  checks.

## D-033 — Generics are bounded and checked at definition

**Status:** Provisional (syntax of `interface` to be revisited before Phase 6)

**Decision.** Generic functions and types are type-checked once against
their declared bounds (`T: Ord`), not re-checked per instantiation like C++
templates. Code generation is by monomorphisation. Bounds are
`interface`s. Dynamic dispatch (interface objects / vtables) is **Open**.

## D-034 — No `goto`; `defer` instead

**Status:** Provisional

**Decision.** `goto` is reserved and unsupported in version 1: jumps
across initialisations and borrows complicate every analysis, and the
dominant C use (`goto cleanup;`) is served by destructors and `defer`.
`defer stmt;` runs `stmt` when the enclosing block exits normally (not on
panic, D-015). Labelled `break`/`continue` are planned.

## D-035 — Debug information

**Status:** Provisional

**Decision.** DWARF 5 (DWARF 4 selectable) emitted through the LLVM-C
DIBuilder. Until a DWARF language code for NovaC exists, compile units
use `DW_LANG_C11` so existing debuggers display values sensibly.

## D-036 — Documentation and process

**Status:** Accepted

**Decision.** Specifications live in `docs/`. The single source of truth
for what works is [FEATURE_STATUS.md](FEATURE_STATUS.md). Decisions are
append-only. Self-hosting the compiler is a post-1.0 consideration, not a
roadmap commitment. Performance claims require reproducible benchmarks
committed with their harness (no benchmark results exist yet).

## D-037 — Implicit conversions only at coercion sites; untyped constants

**Status:** Accepted (Phase 2)

**Context.** Phase 1 contained a contradiction found while implementing
the type checker: NOVAC_TYPE_SYSTEM.md §2.2 said binary operands "must
have the same type after implicit conversion", which would make
`i32 + u8` valid (u8 widens losslessly to i32), while
NOVAC_COMPILER_ARCHITECTURE.md used `i32 + u8` as its example of error
E4010.

**Options.** (a) Widen operands implicitly to a common type (C's usual
arithmetic conversions, restricted to lossless cases). (b) Require
identical operand types; widen only where a value flows into a place of
known type.

**Decision.** (b). Implicit widening (NOVAC_TYPE_SYSTEM.md §5.1) applies
only at *coercion sites*: initialisation, assignment, function arguments
and `return`. Between the operands of a binary operator, typed operands
must have identical types (E4010). Untyped constants (unsuffixed literals
and constant expressions of them) adopt the other operand's type, or the
expected type, and are evaluated exactly; operators that depend on a width
give two untyped operands the expected integer type, otherwise `i32`.

**Consequences.** Mixed-width arithmetic is written with an explicit
conversion (`i64(a) + b`), so the width of every operation is visible in
the source. The typed AST records each implicit widening as an explicit
`implicit` node for later phases.

## D-038 — A variable may not be used in its own initialiser

**Status:** Accepted (Phase 2)

**Context.** In C a variable's scope begins at its declarator, so
`int x = x;` reads an indeterminate value (undefined behaviour). If NovaC
instead began the scope after the initialiser, `i32 x = 1; { i32 x = x; }`
would silently read the *outer* `x` — a C-looking program with a different
meaning, which D-011 forbids.

**Decision.** The scope begins at the declarator, as in C, and any use of
the variable inside its own initialiser is an error (E3003). Earlier
declarators in the same declaration are already initialised and may be
used (`i32 a = 1, b = a;`).

## D-039 — Hosted panics: a weak default handler printing with `dprintf`

**Status:** Accepted (Phase 3)

**Context.** NOVAC_ABI.md §9 requires a `__nova_panic` symbol and says
hosted programs get a default. Some code must print the message and stop
the process, but D-015 and the brief forbid a mandatory runtime library.

**Options.** (a) A runtime library linked into every program. (b) The
default handler is generated into every hosted object as a *weak*
definition: `dprintf(2, "<file>:<line>:<col>: panic: <message>\n")`, then
`abort()`.

**Decision.** (b). Both functions are standard C (glibc and LibNovaC
provide them). The format matches compiler diagnostics. A program can
replace the handler with a strong definition (Phase 4, `extern "C"` /
`@panic_handler`). Freestanding targets get no default (Phase 7).

**Consequences.** A panic ends the process with `SIGABRT`, exit status 134
in a shell. No runtime file is linked, so Q-01 (licensing of linked code)
still applies only to code generated into the program.

## D-040 — `-Ofast` does not use `nnan` or `ninf`

**Status:** Accepted (Phase 3). **Corrects NOVAC_COMPILER_ARCHITECTURE.md
§10 of Phase 1**, which listed `nnan ninf` among the `-Ofast` flags.

**Context.** Testing found that with LLVM's `nnan`, `i32(x / x)` with
`x = 0.0` no longer panicked. Under `nnan`/`ninf`, an operation producing
NaN or infinity yields *poison*, and branching on poison (as every check
does) is undefined behaviour. That would make undefined behaviour
reachable from safe code, contradicting D-009, D-016 and
NOVAC_MEMORY_MODEL.md §1.

**Decision.** `-Ofast` sets only `reassoc`, `nsz`, `arcp`, `contract` and
`afn`. These may change floating-point *results* (by rounding, the sign of
zero or approximate functions), but they never create poison.
`tests/run/panic_conv_float_nan.nc` runs at `-Ofast` as a regression test.

## D-041 — Definite initialisation is checked from Phase 3

**Status:** Accepted (Phase 3). **Moves part of F-402 from Phase 5.**

**Context.** Phase 2 accepted `i32 x; return x;` because no code was
generated. Once code is generated, that program reads an indeterminate
value: undefined behaviour from safe code.

**Decision.** The definite-initialisation analysis (E5031) runs on NIR in
Phase 3, before code generation and as part of `--check`. Move checking,
the other half of NOVAC_MEMORY_MODEL.md §5, stays in Phase 5 with
ownership.

## D-042 — Hosted linking and cross testing use the GNU toolchain and QEMU

**Status:** Accepted (Phase 3)

**Decision.** Executables are linked by the C compiler driver for the
target: `cc` on the host, and `aarch64-linux-gnu-gcc` or
`riscv64-linux-gnu-gcc` when cross-compiling. The driver supplies the C
library's start files and linker defaults (PIE, so code is generated
position-independent). `--linker=` overrides it. Cross-compiled tests run
under QEMU user-mode emulation with `QEMU_LD_PREFIX` pointing at the
cross C library. Nova Linker remains future work (NOVAC_ABI.md §8).

**Consequences.** Building cross executables needs the cross GCC packages.
Running cross tests also needs `qemu-user`. When they are missing, the
test runner reports those configurations as skipped, never as passed.

## D-043 — Omitted initialiser members are zero, when zero is a valid value

**Status:** Accepted (Phase 4)

**Context.** NOVAC_TYPE_SYSTEM.md §3.2 first required every field of an
initialiser list to be given. Definite initialisation tracks variables
whole (D-041), so a field cannot be assigned before its struct is
initialised, and an array of 4096 elements would need 4096 values or
`uninit` (unsafe). The alternatives:

1. Require every member (Rust): safe, but large arrays need `unsafe` or a
   repeat syntax NovaC does not have.
2. Zero-fill omitted members, as C does: familiar, and `{}` is a cheap way
   to initialise anything. But zero is not a valid value of every type: an
   enum may have no enumerator 0, and Phase 5 references are never null.
3. Add a repeat initialiser (`{0; 4096}`): new syntax for one case.

**Decision.** Option 2, restricted to types for which the all-zero bit
pattern is a valid value ("zeroable"): integers, floats, `bool`, raw
pointers, unions, an enum with an enumerator whose value is 0, and arrays
and structs whose members are all zeroable. Omitting a member that is not
zeroable is E4081. `{}` zero-initialises a whole value of a zeroable type.
A union initialiser sets at most one field; the rest of the union's bytes
are zero.

**Consequences.** `i32[4096] buf = {};` is safe and needs no new syntax;
C code that relies on `= {0}` translates directly. Field-by-field
initialisation after a bare declaration remains an error (E5031): write
`= {}` first.

## D-044 — Until Phase 5, `&x` is a raw pointer, and only where one is expected

**Status:** Accepted (Phase 4)

**Context.** C interoperability (Phase 4) needs addresses of locals,
fields and statics. In the final language `&x` is a reference, which
coerces to a raw pointer (NOVAC_TYPE_SYSTEM.md §5.1); references and the
borrow checker arrive in Phase 5. The alternatives:

1. Give `&x` the type `const T*` now: `auto p = &x;` would then change type
   when references arrive.
2. A separate address-of operator or builtin (`addr_of(x)`): new syntax
   that Phase 5 would make redundant.
3. Accept `&x` only where the expected type is a raw pointer
   (initialisers, assignments, arguments, returns, comparison with a
   pointer), with the result `const T*` (`&mut x`: `T*`).

**Decision.** Option 3. Every program accepted now keeps its meaning when
`&x` becomes a reference that coerces to a pointer. Elsewhere `&x` is
E9001 (Phase 5). Taking an address is safe, including of a mutable static;
dereferencing is unsafe.

**Consequences.** Pointers made this way carry no lifetime: a pointer to a
local outlives it without a diagnostic, and using it then is undefined
behaviour of the unsafe code that dereferences it, as in Rust's raw
pointers.

## D-045 — The `core.ffi` names are predeclared until modules exist

**Status:** Accepted (Phase 4)

**Context.** C interoperability needs `c_int`, `c_char`, `c_void` and the
other C types (NOVAC_SPEC.md §10: `core.ffi`), but `import` arrives in
Phase 6. The alternatives: make them keywords; predeclare them; or wait
for modules.

**Decision.** They are predeclared in every module, as aliases of the
matching NovaC integer types (`c_char` depends on the target) and an opaque
struct `c_void`. A module's own declaration of the same name takes
precedence. When `core.ffi` exists, the predeclared names become imports
of it in the prelude, so programs keep compiling.

## D-046 — Function pointer types move to Phase 6, with `optional`

**Status:** Accepted (Phase 4)

**Context.** NOVAC_TYPE_SYSTEM.md §3.5 makes function pointers non-null,
with `optional<R fn(P...)>` for nullable ones, so that C callbacks that may
be NULL have a sound type. `optional` is Phase 6. Introducing non-null
function pointers now would leave nullable C callbacks unrepresentable.
Alternatively they could be nullable raw pointers now, but that would
change their meaning later.

**Decision.** Function pointer types and calls through them are
implemented in Phase 6 together with `optional`. Phase 4 C interoperability
covers direct calls in both directions. APIs that take callbacks (`qsort`,
`signal`) wait for Phase 6; `@callconv(c)` on function types moves with
them.

---

## Outstanding questions

These are **Open** and need an owner decision. They do not block Phase 2.

| ID | Question | Blocks |
| --- | --- | --- |
| Q-01 | **Licensing of compiled output.** NovaC is under PolyForm Noncommercial 1.0.0. If any NovaC code (the `core` library, panic handler, compiler-emitted helpers) is linked into user programs, those programs inherit noncommercial restrictions. Should the libraries linked into user programs carry a runtime exception or a different license? Separately, LibNovaC is LGPL-2.1: statically linking NovaC programs against it carries LGPL relinking obligations. | Phase 3 (first linked output) |
| Q-02 | **libNova-Sys.** It does not exist. Where will it live (own repository?), who owns its API, and is it C-ABI (recommended, so both LibNovaC and NovaC `std` can use it)? | `std` on NovarisOS beyond the C-library binding |
| Q-03 | Accept `ncc` as the driver name (D-020)? | Phase 2 binary name |
| Q-04 | Should LibNovaC/NovarisOS rename their `NOVAC_*` build variables (e.g. to `LIBNOVAC_*`) to remove ambiguity? Outside this repository; recommended. | Nothing in NovaC |
| Q-05 | Closures / lambdas: capture semantics and whether they allocate. | Phase 6 (`std.thread` ergonomics) |
| Q-06 | General tagged unions (sum types) beyond `optional`/`result`: syntax and pattern matching. | Phase 6 |
| Q-07 | Explicit lifetime annotation syntax (provisionally `T&'a`). | Phase 5b |
| Q-08 | Package manifest and dependency fetching (a `nova.toml`?) vs. plain search paths. | Phase 6 |
| Q-09 | Interface objects / dynamic dispatch (D-033). | Phase 6 |
| Q-10 | Async design (D-029). | Phase 8 |
| Q-11 | NovarisOS adoption path: recommended first step is a NovaC module compiled to an object and linked into the existing C kernel by `Makefile.amd64`. Confirm. | Phase 7 |
