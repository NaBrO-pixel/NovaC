# NovaC Type System

Status: **Specification, Phase 1.** Nothing in this document is
implemented yet; see [FEATURE_STATUS.md](FEATURE_STATUS.md).

Syntax is in [NOVAC_GRAMMAR.md §4](NOVAC_GRAMMAR.md#4-types). Ownership and
borrowing rules for these types are in
[NOVAC_MEMORY_MODEL.md](NOVAC_MEMORY_MODEL.md). Target-specific sizes and
alignments are in [NOVAC_ABI.md §3](NOVAC_ABI.md#3-data-layout).

## 1. Overview

NovaC is statically and nominally typed. Every expression has a type
known at compile time. There are no implicit conversions except the
lossless ones listed in §5. There is no undefined behaviour reachable
from safe code; every operation that C leaves undefined is either a
compile-time error, a defined result, or a panic (D-009, D-011).

## 2. Scalar types

### 2.1 Integers

| Type | Bits | Signed | Range |
| --- | --- | --- | --- |
| `i8` | 8 | yes | −2⁷ … 2⁷−1 |
| `i16` | 16 | yes | −2¹⁵ … 2¹⁵−1 |
| `i32` (alias `int`) | 32 | yes | −2³¹ … 2³¹−1 |
| `i64` | 64 | yes | −2⁶³ … 2⁶³−1 |
| `isize` | pointer width | yes | |
| `u8` | 8 | no | 0 … 2⁸−1 |
| `u16` | 16 | no | 0 … 2¹⁶−1 |
| `u32` | 32 | no | 0 … 2³²−1 |
| `u64` | 64 | no | 0 … 2⁶⁴−1 |
| `usize` | pointer width | no | |

Signed integers are two's complement. `isize`/`usize` are 64-bit on every
version-1 target (D-023) but are distinct types from `i64`/`u64`: code
must not assume they are interchangeable. 128-bit integers are planned
(their alignment differs between targets; see NOVAC_ABI.md §3).

`usize` is the type of sizes, lengths and indices.

### 2.2 Integer operations

Arithmetic is performed **in the operand type**. Both operands of a binary
arithmetic, bitwise or comparison operator must have the same type after
implicit conversion (§5); there are no integer promotions and no "usual
arithmetic conversions".

| Operation | Result |
| --- | --- |
| `a + b`, `a - b`, `a * b` | mathematically exact result if representable; otherwise **panic** |
| `a +% b`, `a -% b`, `a *% b` | result modulo 2ᴺ (two's complement wrap) |
| `a / b`, `a % b` | truncated toward zero (as C99); **panic** if `b == 0`, or if signed and `a == MIN && b == -1` |
| `-a` (signed) | **panic** if `a == MIN` |
| `-a` (unsigned) | compile error E4012 (use `0 -% a`) |
| `a << n`, `a >> n` | `n` must be an unsigned integer; **panic** if `n >= bits(a)`. `<<` discards high bits (no overflow check); signed `>>` is arithmetic, unsigned `>>` is logical |
| `~a`, `&`, `\|`, `^` | bitwise; never fail |
| `++x`, `x++`, `--x`, `x--` | as `x += 1` / `x -= 1`, including the overflow panic |

Compile-time constant expressions that would panic are compile errors
(E4040).

In `controlled` and `kernel` modules, `@overflow(wrap)` on a function or
the module declaration changes `+ - * ++ -- unary-` to wrap (D-016).
Division-by-zero and shift-amount checks are never disabled by it.

### 2.3 Floating point

| Type | Format |
| --- | --- |
| `f32` | IEEE 754 binary32 |
| `f64` | IEEE 754 binary64 |

Operations follow IEEE 754 with round-to-nearest-even; NaN and infinities
propagate; floating point never panics. The compiler does not contract
`a*b+c` into a fused multiply-add (`-ffp-contract=off` semantics,
matching LibNovaC's build) and does not reassociate, except under
`-Ofast` (see [NOVAC_COMPILER_ARCHITECTURE.md §10](NOVAC_COMPILER_ARCHITECTURE.md#10-optimisation)).
Comparison with NaN is false except `!=`.

`f16` and `f128` are planned. C's `long double` is unsupported (it is
80-bit on x86-64, 128-bit IEEE on AArch64/RISC-V Linux).

A module compiled for a target without floating-point registers
(e.g. the NovarisOS kernel, built with SSE disabled) may not use `f32`/
`f64` at all: E7010. This prevents silently emitting soft-float library
calls in a kernel.

### 2.4 `bool`

`true` or `false`. Size 1 byte; the only valid bit patterns are `0` and
`1`. No implicit conversion to or from integers: conditions must be
`bool` (`if (n)` is E4020; write `if (n != 0)`). `bool(x)` is not a
conversion; `u8(b)` converts `bool` to 0/1.

### 2.5 `void`

The type with one value and no storage. Used as a function return type and
as the type of assignment expressions (D-024). `void*` is **not** a
generic pointer: the untyped pointer type is `u8*` / `const u8*`; C's
`void*` in FFI signatures is `core.ffi.c_void*` (an opaque struct).

## 3. Compound types

### 3.1 Arrays: `T[N]`

A fixed-length sequence of `N` elements, `N` a constant expression of type
`usize`. Arrays are values: assignment and parameter passing copy (if `T`
is Copy) or move the whole array. An array does **not** decay to a
pointer. Indexing `a[i]` requires `i: usize` (integer literals adapt) and
is bounds-checked (NOVAC_MEMORY_MODEL.md §7).

`&a` or `&mut a` coerces to `slice<const T>` / `slice<T>`.

### 3.2 Structs

```c
struct Point { public f64 x; public f64 y; }
```

Nominal; layout as C (D-017). Fields are private to the declaring module
unless `public`. Struct values are created with initialiser lists:

```c
Point p = { .x = 1.0, .y = 2.0 };      // designated
Point q = Point{ .x = 0.0, .y = 0.0 }; // expression form
```

Every field must be initialised (no implicit zero-fill) unless the
initialiser is `uninit` (unsafe; NOVAC_MEMORY_MODEL.md §5).

### 3.3 Unions

Untagged, C-compatible. All fields must be Copy and the union has no
destructor. Writing a field is safe; **reading** a field is an unsafe
operation (NOVAC_MEMORY_MODEL.md §6), because nothing records which field
was last written.

### 3.4 Enums

```c
enum Color : u8 { Red, Green = 4, Blue }
```

An enum has an underlying integer type (default `i32`, which matches the C
ABI on all version-1 targets). Values are the enumerators only: converting
an integer to an enum with `Color(x)` panics if `x` is not an enumerator
value. `u8(c)` converts to the underlying value. Enumerators are scoped:
`Color.Red`. `switch` over an enum must be exhaustive or have `default`.
C-style "flag" enums that combine bits are not enums in NovaC: use
integer constants or a `bitset` type (planned).

### 3.5 Function types: `R fn(P...)`

Function pointers. Never null (use `optional<R fn(P...)>`, which has
pointer size). The calling convention is part of the type:
`@callconv(c) i32 fn(i32)` is distinct from `i32 fn(i32)` (D-018).
Functions convert implicitly to their function-pointer type.

### 3.6 Raw pointers: `T*`, `const T*`

Nullable, unchecked addresses with no lifetime. Creating, copying,
comparing and converting raw pointers is safe. **Dereferencing**, pointer
arithmetic (`p + n`, `p[n]`), and converting a raw pointer to a reference
are unsafe operations (NOVAC_MEMORY_MODEL.md §6). `null` has every raw
pointer type. Comparison `<`/`>` between pointers is permitted and defined
as address comparison.

### 3.7 References: `T&`, `const T&`

Non-null, aligned, always pointing to a valid, initialised `T` for as long
as the reference is usable. `const T&` is shared (many may coexist; the
referent cannot be mutated through it except via interior mutability, §4.4);
`T&` is exclusive. Creation: `&x` → `const T&`, `&mut x` → `T&`. Rules
for their validity are the borrow rules (NOVAC_MEMORY_MODEL.md §4).

References are *not* automatically dereferenced except:
- member access `r->field` and method calls `r->m()` (through a
  reference or pointer, `->` is always used, as in C; `.` is only for
  values — D-006);
- comparison operators are **not** applied through references: compare
  `*a == *b`.

### 3.8 Slices: `slice<T>`, `slice<const T>`; `str`

A borrowed view of a contiguous sequence: a pointer and a `usize` length.
`slice<const T>` is a shared borrow; `slice<T>` an exclusive one, with the
same rules as `const T&` and `T&`. Indexing is bounds-checked;
sub-slicing `s.sub(a, b)` checks `a <= b <= len`. `s.len` is the length.
A slice is itself a value (a pointer and a length), so its members are
accessed with `.`, consistent with D-006.

`str` is a predeclared alias for `slice<const u8>`. String literals have
type `str` and point to static, immutable storage. A `str` is expected to
hold UTF-8 but this is not enforced by the type (validation is a library
function).

## 4. Built-in generic types

These types are provided by the compiler with fixed layouts (D-008).

### 4.1 `optional<T>`

Either `none` or `some(v)`. `T` converts implicitly to `optional<T>`.
Access:

```c
optional<i32> r = find(key);
if (r is some(v)) { use(v); }      // v: i32, scoped to the then-branch
i32 x = r.unwrap();                 // panics on none
i32 y = r.or(0);
```

Layout: for `T` = reference, slice, or function pointer, `optional<T>`
has the size of `T` and represents `none` as the null pointer (the
"niche"). Otherwise it is laid out as
`struct { T value; bool present; }` with C layout. Raw pointers are already
nullable, so `optional<T*>` uses the general layout.

### 4.2 `result<T, E>`

Either `ok(v)` or `err(e)`. Layout: `struct { union { T ok; E err; } u;
bool is_err; }` with C layout. `try` (D-026) propagates `err`.

### 4.3 `atomic<T>`

`T` must be an integer type, `bool`, or a raw pointer type whose size is
supported by lock-free hardware atomics on the target (1, 2, 4, 8 bytes on
all version-1 targets; RISC-V requires the `A` extension, which RV64GC
includes). Layout: same size as `T`, alignment equal to its size.

No operators: methods take an explicit ordering (D-027):

```c
atomic<u64> counter = 0;
counter.fetch_add(1, relaxed);
u64 v = counter.load(acquire);
bool won = counter.compare_exchange(v, v + 1, acq_rel, acquire).is_ok();
```

The ordering arguments are constants of the enum `core.sync.Ordering`
(`relaxed`, `acquire`, `release`, `acq_rel`, `seq_cst`), imported in the
prelude. Invalid combinations (a `release` load) are compile errors
(E4050).

### 4.4 Interior mutability

`atomic<T>` is the only interior-mutable type in version 1: it can be
modified through a `const atomic<T>&`. Others (`cell<T>`, `mutex<T>`) are
library types built on unsafe code (planned, Phase 8).

### 4.5 `mmio<T>`

A memory-mapped register of integer type `T`. Same layout as `T`. Only
`read()` and `write(v)`, which are volatile accesses (D-027). Obtaining an
`mmio<T>&` from an address is unsafe (`kernel` and `controlled` modes).

## 5. Conversions

### 5.1 Implicit (always lossless)

| From | To |
| --- | --- |
| `iN` | `iM`, M > N |
| `uN` | `uM`, M > N; `iM`, M > N |
| `u8`, `u16`, `u32` | `usize` (and `i8`, `i16`, `i32` → `isize`). Only types of at most 32 bits convert implicitly, so the rule would remain lossless if a 32-bit target were ever added; `u64`→`usize` needs `usize(x)` |
| `f32` | `f64` |
| untyped integer literal | any integer type that can represent its value; any float type if exactly representable |
| untyped float literal | `f32` or `f64` (rounded to nearest) |
| `T&` | `const T&`, `T*`, `const T*` |
| `const T&` | `const T*` |
| `T*` | `const T*` |
| `T[N]&` / `const T[N]&` | `slice<T>` / `slice<const T>` |
| `slice<T>` | `slice<const T>` |
| `T` | `optional<T>` |
| function | function pointer of the same signature |

Integer ↔ float, `bool` ↔ integer, signed ↔ unsigned of the same width,
and every narrowing conversion are never implicit.

### 5.2 Checked explicit: `T(x)`

For numeric `T`: converts `x` to `T` and **panics** if the value is not
representable exactly (integers), or is NaN/out of range (float → integer,
which truncates toward zero when in range). Integer → float rounds to
nearest. Enum `E(x)`: panics if not an enumerator. Constant arguments are
checked at compile time.

### 5.3 Unchecked explicit (defined, never UB)

| Function | Meaning |
| --- | --- |
| `truncate::<T>(x)` | integer → narrower integer, keep low bits |
| `bitcast::<T>(x)` | reinterpret bits; `sizeof(T) == sizeof(x)`; both types must be "plain bits" (integers, floats, arrays/structs thereof without padding); not for pointers |
| `ptr_cast::<U*>(p)` | raw pointer to raw pointer of another pointee (constness cannot be removed: use `ptr_cast_mut`, which is unsafe) |
| `addr(p)` | raw pointer → `usize` |
| `ptr_from_addr::<T>(a)` | `usize` → `T*` (exposed provenance; see NOVAC_MEMORY_MODEL.md §6.2) |

## 6. Methods and `impl`

```c
impl Point {
    f64 len(const Self& self) { return sqrt(self->x * self->x + self->y * self->y); }
    void scale(Self& self, f64 k) { self->x = self->x * k; self->y = self->y * k; }
    Self origin() { return Self{ .x = 0.0, .y = 0.0 }; }
}
```

The receiver is the first parameter named `self`, of type `Self`,
`const Self&` or `Self&`. Method call `p.len()` auto-references `p` as
needed (`&p` for `const Self&`, `&mut p` for `Self&` — the latter requires
`p` to be mutable and is a visible exclusive borrow in borrow checking).
Associated functions without `self` are called as `Point.origin()`.

**Destructors.** A method `void drop(Self& self)` in an inherent `impl` is
the type's destructor. It cannot be called directly (E5030); use
`core.mem.drop(x)`, which consumes `x`. A type with a destructor is not
Copy (D-014).

## 7. Generics

```c
struct Pair<A, B> { public A first; public B second; }

T max<T: Ord>(T a, T b) { return a > b ? a : b; }

struct Ring<T, constexpr usize N> { T[N] items; usize head; }
```

- Type parameters and constant parameters (`constexpr usize N`).
- Bounds are interfaces (D-033). A generic body may use only operations
  its bounds provide; it is checked once at definition.
- Instantiation is by monomorphisation; identical instantiations across
  modules are merged by the linker (`linkonce_odr`, NOVAC_ABI.md §7).
- Generic arguments are inferred at calls; explicitly `max::<u64>(a, b)`.

### 7.1 Interfaces (Provisional, D-033)

```c
interface Ord { bool less(const Self& self, const Self& other); }
impl Ord for Point { bool less(const Self& self, const Self& other) { ... } }
```

Built-in capability interfaces, implemented automatically by the compiler
where applicable and not implementable by hand: `Copy` (D-014), `Send`,
`Sync` (NOVAC_MEMORY_MODEL.md §8). Operator interfaces (`Eq`, `Ord`,
`Add`, …) enable the corresponding operators on user types. Interface
objects (dynamic dispatch) are Open (Q-09).

## 8. Type inference

- `auto x = expr;` takes the type of `expr`. An untyped integer literal
  defaults to `i32`, an untyped float literal to `f64`; out of range is
  E4041 (no silent widening to `i64`).
- Generic arguments are inferred from call arguments and the expected
  type. Inference is local to one function body; function signatures are
  always explicit.
- `const auto` declares an immutable binding.

## 9. Constant evaluation

`constexpr` declarations, array lengths, enum values, generic constant
arguments and `@cfg` conditions are evaluated at compile time. Version 1
compile-time evaluation supports literals, arithmetic, comparisons,
logical operators, conditional expressions, `sizeof`/`alignof`/`offsetof`,
references to other `constexpr` values, and calls to functions marked
`constexpr` whose bodies use only those (no loops until Phase 6b). A panic
during constant evaluation is a compile error.

## 10. Type equivalence and aliases

Types are nominal for structs, unions, enums and interfaces; structural for
everything else. `typedef` introduces an alias, not a new type.

## 11. Diagnostics (type system)

| Code | Condition |
| --- | --- |
| E4001 | type mismatch |
| E4002 | wrong number of arguments in a call |
| E4003 | called expression is not a function |
| E4004 | assignment to an immutable binding or a non-place expression |
| E4010 | binary operator on operands of different types |
| E4012 | unary `-` on unsigned operand |
| E4020 | non-`bool` condition |
| E4030 | assignment used as a value (D-024) |
| E4040 | constant expression panics |
| E4041 | literal out of range for its type |
| E4050 | invalid memory ordering for the operation |

E7010 (floating point on a target without an FPU, §2.3) is defined with
the other target codes in [NOVAC_ABI.md §13](NOVAC_ABI.md#13-diagnostics-abi).
