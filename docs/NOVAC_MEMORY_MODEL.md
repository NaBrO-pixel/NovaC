# NovaC Memory Model

Status: **Specification, Phase 1.** Nothing in this document is
implemented yet; see [FEATURE_STATUS.md](FEATURE_STATUS.md).

This document defines programming modes, ownership, borrowing, lifetimes,
initialisation, bounds safety, unsafe operations, and the concurrent
memory model. Types referenced here are defined in
[NOVAC_TYPE_SYSTEM.md](NOVAC_TYPE_SYSTEM.md).

## 1. Goals and non-goals

**Goal:** a program whose modules are all `safe`, or whose `unsafe` code
upholds the obligations stated in §6, has no undefined behaviour. In
particular it cannot exhibit use-after-free, double free, dangling
references, out-of-bounds access, reads of uninitialised memory, null
dereference, data races, invalid `bool`/enum values, or integer-overflow
UB.

**Non-goals (explicitly not guaranteed, in any mode):**
- absence of memory leaks (leaking is safe, as in Rust);
- absence of deadlocks or logical race conditions;
- absence of panics;
- stack-overflow detection in freestanding/kernel code (hosted builds
  rely on the OS guard page; see NOVAC_ABI.md §9.3).

**No garbage collector and no mandatory runtime.** All checks are
compile-time, except bounds, arithmetic and conversion checks, which
compile to an inline compare-and-branch to the panic handler and are
removed when the compiler proves them redundant.

## 2. The three modes

A module's mode is declared on its `module` declaration (D-012):

```c
module app.main;                              // safe (default)
@mode(controlled) module alloc.arena;
@mode(kernel) module arch.x86_64.interrupts;
```

A mode is a **ceiling on which unsafe operations may be written**, not a
switch that turns checks off. In every mode, each unsafe operation must
be inside an `unsafe { }` block or an `unsafe` function, and type
checking, borrow checking, initialisation checking, overflow checks and
bounds checks on arrays and slices stay in force inside `unsafe` blocks.

### 2.1 Capability matrix

| Capability | safe | controlled | kernel |
| --- | :---: | :---: | :---: |
| Ownership, borrow, lifetime, initialisation checking | always | always | always |
| Bounds checks on arrays/slices | always | always (unsafe `get_unchecked` available) | same as controlled |
| Overflow checks | always | default; `@overflow(wrap)` permitted | same as controlled |
| Raw pointer types (declare, copy, compare, convert) | ✓ | ✓ | ✓ |
| `unsafe` blocks and `unsafe` functions | ✗ (E6001) | ✓ | ✓ |
| Dereference / arithmetic on raw pointers | ✗ | in `unsafe` | in `unsafe` |
| Raw pointer → reference | ✗ | in `unsafe` | in `unsafe` |
| Call `unsafe` functions, incl. all `extern "C"` | ✗ | in `unsafe` | in `unsafe` |
| Read union fields | ✗ | in `unsafe` | in `unsafe` |
| Access mutable `static` (non-`Sync`) | ✗ | in `unsafe` | in `unsafe` |
| `uninit` initialiser | ✗ | in `unsafe` | in `unsafe` |
| Manual allocation (`alloc` raw APIs) | via safe wrappers only | ✓ | ✓ |
| Define `extern "C"` functions (exported) | ✓ (if signature is FFI-safe) | ✓ | ✓ |
| `ptr_from_addr`, `mmio<T>` from address | ✗ | in `unsafe` | in `unsafe` |
| `asm` statements, module-level `asm`, `@naked` | ✗ | ✗ (E6002) | in `unsafe` |
| `@interrupt`, custom `@callconv` other than `c` | ✗ | ✗ | ✓ |
| Privileged intrinsics (`core.arch.*.priv`: `cli`, `wrmsr`, CR access, `invlpg`, …) | ✗ | ✗ | in `unsafe` |
| `@section`, `@used`, `@export` with custom symbol names | `@export` only | ✓ | ✓ |
| Import `std` | ✓ (hosted targets) | ✓ (hosted targets) | ✓ (hosted targets) |

Freestanding is orthogonal to mode (D-022): on a freestanding target no
module may import `std`, whatever its mode.

### 2.2 Guarantees per mode

- **safe:** all goals in §1 hold for code in the module, unconditionally,
  *provided the modules it calls uphold theirs*.
- **controlled:** all goals hold for code outside `unsafe` blocks.
  Code inside `unsafe` blocks must uphold the obligations in §6; if it
  does not, behaviour is undefined (and may be observed anywhere in the
  program).
- **kernel:** as controlled, plus the additional obligations of §6.3 for
  privileged operations, interrupt handlers and MMIO.

### 2.3 Boundaries

A public non-`unsafe` function of a controlled or kernel module is a
promise that **no call from safe code can cause undefined behaviour**,
whatever arguments are passed. Breaking that promise is a bug in the
controlled/kernel module. An `unsafe` function documents its caller's
obligations in a comment starting `// SAFETY:` (enforced as warning
W6010 when missing).

The driver option `--max-mode=` (D-012) and the `.nci` interface record
each module's mode, so a build can audit which modules use unsafe
capabilities.

## 3. Ownership

### 3.1 Places, values, owners

A **place** is a storage location: a local variable, a parameter, a
static, a field or element of a place, or `*r` for a reference/pointer
`r`. Every value of a non-Copy type has exactly one owning place at a
time.

### 3.2 Copy and move

Copy types are defined in D-014 / NOVAC_TYPE_SYSTEM.md. For a non-Copy
type, these **move** the value out of its place:
- initialising or assigning another place from it;
- passing it by value as an argument;
- returning it.

After a move the source place is **uninitialised**. Reading it, borrowing
it, or moving from it again is E5001 ("use of moved value"). It can be
re-initialised by assignment.

Moving out of a place that is not a whole local (e.g. a field `s.f`, an
element `a[i]`, or `*r`) is E5002 in version 1, except that a struct
local may be destructured field by field. (Use `core.mem.replace` /
`swap`, which are safe.)

### 3.3 Destruction

A value of a type with a destructor (NOVAC_TYPE_SYSTEM.md §6) is
destroyed exactly once:
- when its owning local goes out of scope, in reverse order of
  declaration;
- when a place holding it is assigned a new value (the old value is
  destroyed first);
- when it is passed to `core.mem.drop`.

Struct fields are destroyed after the struct's own destructor runs, in
declaration order; array elements in index order.

Where a move happens on only some paths, the compiler inserts a hidden
**drop flag** (one `bool` local) to decide at scope exit; this is the
only runtime cost of ownership and is removed when the paths are static.
Destructors are **not** run on panic (D-015).

Because each value has one owner and is destroyed once, double free and
use-after-free through owned values are impossible in safe code.

## 4. Borrowing

### 4.1 Rules

For every place `P`, at every program point, the set of **live** borrows
of `P` (or of places overlapping `P`) satisfies:

1. **Shared XOR exclusive:** either any number of shared borrows
   (`const T&`, `slice<const T>`) or exactly one exclusive borrow
   (`T&`, `slice<T>`), not both.
2. While any borrow of `P` is live, `P` cannot be moved from (E5010).
3. While a shared borrow of `P` is live, `P` cannot be assigned or
   exclusively borrowed (E5011), except through interior mutability
   (`atomic<T>`).
4. While an exclusive borrow of `P` is live, `P` cannot be read, written,
   or borrowed except through that borrow (E5012).

Overlap: `s.a` and `s.b` are disjoint; `a[i]` and `a[j]` are treated as
overlapping (indices are not compared statically; use
`slice.split_at_mut`).

### 4.2 Liveness (non-lexical)

A borrow is live from its creation to the **last use** of the reference
(or of any reference derived from it), not to the end of its lexical
scope. Liveness is computed on the control-flow graph of NIR (D-013).

```c
i32 x = 1;
const i32& r = &x;
print_i32(*r);   // last use of r
x = 2;           // OK: r is no longer live
```

Two-phase borrows: in `v.push(v.len())`, the exclusive auto-borrow of `v`
for `push` is reserved first and activated after the arguments are
evaluated, so the shared borrow in `v.len()` is accepted.

### 4.3 Reborrowing

Passing a `T&` to a function expecting `T&` reborrows it for the duration
of the call rather than moving it, so the original reference is usable
afterwards. `T&` is otherwise not Copy.

## 5. Lifetimes and initialisation

### 5.1 Lifetimes

A reference must not outlive the place it borrows. Within a function this
is checked directly (E5020 "borrowed value does not live long enough";
returning a reference to a local is E5021).

Across functions, version 1 (Phase 5a) uses **elision only**:
- each reference parameter gets a distinct lifetime;
- if there is exactly one reference parameter, or a `self` receiver that
  is a reference, references in the return type borrow from it;
- otherwise returning a reference is an error (E5022) until explicit
  lifetime syntax lands (Phase 5b, Q-07, provisionally `const T&'a`).

Structs containing references require lifetime parameters and are
therefore Phase 5b. `static` items have the `'static` lifetime; string
literals are `str` with `'static` lifetime.

### 5.2 Definite initialisation

Every local must be definitely initialised on every path before it is
read, borrowed or moved (E5030 is reserved for destructors; this is
E5031). Declaring without an initialiser is allowed:

```c
i32 x;
if (c) { x = 1; } else { x = 2; }
use(x);              // OK: initialised on both paths
```

`T x = uninit;` (unsafe) produces storage the analysis treats as
initialised; reading it before writing is undefined behaviour. Statics
must have a constant initialiser.

## 6. Unsafe operations

### 6.1 The complete list

These, and only these, require an `unsafe` context (§2.1 lists which
modes may contain one):

1. Dereferencing a raw pointer (`*p`, `p->f`, `p[i]`).
2. Pointer arithmetic (`p + n`, `p - n`, `p - q`).
3. Converting a raw pointer to a reference or slice
   (`ref_from_ptr`, `slice_from_raw_parts`).
4. Calling an `unsafe` function, including every function declared in an
   `extern` block and calling C variadic functions.
5. Reading a union field.
6. Reading or writing a mutable `static` whose type is not `Sync`.
7. Using `uninit`.
8. Unchecked intrinsics (`get_unchecked`, `add_unchecked`, …).
9. `ptr_from_addr`, constructing `mmio<T>` from an address,
   `ptr_cast_mut` (removing `const`).
10. (kernel only) `asm`, privileged intrinsics.

### 6.2 Obligations

Code in an `unsafe` context must ensure that:
- a dereferenced raw pointer is non-null, aligned, and points to a live
  allocation containing an initialised value of its type;
- a reference created from a raw pointer satisfies §4's rules for its
  entire lifetime (no other exclusive access, no mutation behind a shared
  reference);
- pointer arithmetic stays within one allocation (or one past its end);
- no data race (§8) is introduced.

**Provenance.** Raw pointers carry the provenance of the allocation they
were derived from (LLVM's model). `addr(p)` exposes the provenance;
`ptr_from_addr` produces a pointer that may use any exposed provenance.
This is the model that makes device and page-table addresses expressible
in kernel code.

### 6.3 Kernel-mode obligations

In addition to §6.2:
- an `@interrupt` handler may only access statics that are `Sync`
  (atomics, lock types that disable interrupts), because it runs
  concurrently with the code it interrupts (§8.4);
- `asm` must declare every register and memory effect in its operand and
  clobber lists; an `asm` without a `"memory"` clobber is assumed not to
  read or write memory not given as an operand;
- MMIO accesses are made only through `mmio<T>` / volatile intrinsics.

## 7. Bounds safety

- `a[i]` on an array or slice checks `i < len` and panics otherwise.
  The compiler removes the check when it proves it redundant (loop bounds
  derived from `len`, constant indices into arrays, dominating checks).
  A constant out-of-range index into an array is a compile error (E5040).
- Slicing checks `start <= end <= len`.
- Raw pointer indexing is unchecked and unsafe.
- `get_unchecked(i)` is unsafe and unchecked.

## 8. Concurrency memory model

### 8.1 Model

NovaC uses the C++11/C11 memory model as implemented by LLVM (D-027): a
program's executions are defined by happens-before over atomic operations
with orderings `relaxed`, `acquire`, `release`, `acq_rel`, `seq_cst`, and
fences `fence(order)` / `compiler_fence(order)`.

A **data race** is two accesses to the same location, from different
threads, at least one a write, at least one non-atomic, not ordered by
happens-before. Data races are undefined behaviour. Safe code cannot
create one (§8.2).

### 8.2 `Send` and `Sync`

Built-in capability interfaces, inferred structurally:
- `Send`: ownership of the value may move to another thread.
- `Sync`: `const T&` may be shared between threads.

Scalars, `atomic<T>` and structs of `Send`/`Sync` fields are
`Send`/`Sync`. Raw pointers are neither. `const T&` is `Send` iff `T` is
`Sync`. Types that are thread-safe by construction over raw pointers opt
in with an unsafe attribute `@unsafe_impl(Send)` (controlled/kernel
modules only).

`std.thread.spawn` requires its argument to be `Send`; a non-`atomic`
mutable `static` is not `Sync`, so safe code cannot share one.

### 8.3 Userspace synchronisation (library, Phase 8)

`std.sync` provides `mutex<T>` (owns the protected data; access via a
guard whose destructor unlocks), `rwlock<T>`, `condvar`, `once`, built
on futex system calls through `std.sys`. `std.thread` provides spawn and
join. None of this exists in `core`, and none of it is used by kernel
code.

### 8.4 Kernel concurrency (separate from userspace)

Kernel code must not use `std.sync`. `core.sync` provides only
primitives with no OS dependency: `atomic<T>`, fences, and `spin_mutex<T>`.
Interrupt-safe locking (disabling interrupts while holding a lock) and
per-CPU data are **architecture-specific and belong to the kernel**
(NovarisOS), built from `core.arch` intrinsics. An interrupt handler is
treated as another thread for data-race purposes.

### 8.5 MMIO ordering

Volatile accesses are not reordered with respect to each other by the
compiler, and are not merged, split or elided. They are **not** ordered
with respect to ordinary memory or across devices by the hardware on
every architecture: x86-64 orders uncached MMIO strongly; AArch64
requires `dmb`/`dsb` and RISC-V requires `fence io,io` / `fence rw,io`
for ordering with normal memory. `core.arch.io_barrier()` emits the
correct instruction per target. Code must not assume x86 ordering.

### 8.6 Async

No language-level async in version 1 (D-029).

## 9. Diagnostics (memory model)

| Code | Condition |
| --- | --- |
| E5001 | use of moved value |
| E5002 | move out of a non-local place |
| E5010 | move while borrowed |
| E5011 | mutation or exclusive borrow while shared-borrowed |
| E5012 | access while exclusively borrowed |
| E5020 | borrowed value does not live long enough |
| E5021 | returning a reference to a local |
| E5022 | lifetime of returned reference cannot be elided |
| E5030 | explicit call to a destructor |
| E5031 | use of possibly-uninitialised variable |
| E5040 | constant index out of bounds |
| E5050 | borrow of a misaligned field of a `@packed` struct |
| E6001 | `unsafe` in a safe module |
| E6002 | kernel-only operation in a non-kernel module |
| E6003 | unsafe operation outside an `unsafe` context |
| W6010 | `unsafe` function without a `// SAFETY:` comment |
