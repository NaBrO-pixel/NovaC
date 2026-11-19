# NovaC Feature Status

This is the **single source of truth** for what NovaC can do (D-036). If a
feature is not listed as **Implemented** here, it does not work, whatever
other documents describe.

Status vocabulary (checked by `tools/check_docs.py`):

| Status | Meaning |
| --- | --- |
| Implemented | works, and has tests in this repository (the Reference column names them) |
| Planned | specified; scheduled for the named phase; no code exists |
| Open | design not decided (see the question in NOVAC_DECISIONS.md) |
| Unsupported | deliberately not part of NovaC, or out of scope for version 1 |

Last updated: Phase 4 (step 4b).

## Implemented

| ID | Feature | Status | Phase | Reference |
| --- | --- | --- | --- | --- |
| F-001 | Phase 1 specification documents | Implemented | 1 | `docs/` |
| F-002 | Documentation consistency checker (also checks the implementation's keywords and diagnostic codes against the docs) | Implemented | 1 | `tools/check_docs.py`, `make check-docs` |
| F-100 | `ncc` driver: `--check`, `--dump-tokens`, `--dump-ast`, `--emit=typed`, `-Werror`, `--error-limit`, `--max-mode=` (Phase 4b); later-phase options rejected (E9002) | Implemented | 2 | `tests/driver/`, `DRIVER_CASES` in `tests/run_tests.py` |
| F-101 | Diagnostics engine with codes, source excerpts, notes, help, error limit | Implemented | 2 | `tests/unit/test_diag.c`, every test in `tests/diag/` |
| F-102 | Lexer for the complete token set, `--dump-tokens` | Implemented | 2 | `tests/lex/`, `tests/diag/lex_*.nc`, `tests/unit/test_lexer.c` |
| F-103 | Parser and AST for the [P2] subset with error recovery, `--dump-ast`; the full expression and type grammar is parsed; later-phase declarations and statements are recognised and rejected (E9001) | Implemented | 2 | `tests/parse/`, `tests/diag/parse_*.nc` |
| F-104 | Name resolution within one module (functions in any order, block scopes, shadowing, self-initialiser check) | Implemented | 2 | `tests/sema/scopes_calls.nc`, `tests/diag/sema_undeclared.nc`, `sema_redefinition.nc`, `sema_self_init.nc` |
| F-105 | Type checking of primitive types: untyped constants, operators, implicit widening at coercion sites, checked conversions `T(x)`, constant folding with overflow detection, return-path analysis | Implemented | 2 | `tests/sema/`, `tests/diag/sema_*.nc`, `tests/unit/test_consteval.c` |
| F-106 | Test runner: golden files, inline diagnostic expectations, driver cases, runner self-test | Implemented | 2 | `tests/run_tests.py`, `tests/selftest/` |
| F-107 | libFuzzer harness for the front end (through NIR lowering and analysis since Phase 3) | Implemented | 2 | `tests/fuzz/`, `make fuzz-run` |
| F-200 | NIR: lowering with explicit check asserts, verifier, `--emit=nir` | Implemented | 3 | `src/nir/`, `tests/nir/` |
| F-201 | LLVM IR generation; object, assembly, bitcode and LLVM IR output | Implemented | 3 | `src/codegen/`, `tests/codegen/`, driver cases |
| F-202 | Checked integer arithmetic, division, shifts and conversions (panics) | Implemented | 3 | `tests/run/panic_*.nc` (one per check) |
| F-203 | Optimisation levels `-O0`…`-Ofast` (`-Ofast` without `nnan`/`ninf`, D-040) | Implemented | 3 | every `tests/run/` test runs at each level |
| F-204 | Hosted executables on x86_64-linux-gnu (linked by `cc`, D-042) | Implemented | 3 | `tests/run/` |
| F-205 | Cross-compilation for AArch64 and RISC-V 64 (objects and executables) | Implemented | 3 | `tests/run/` under QEMU, `tests/codegen/target_*.nc` |
| F-206 | DWARF 5 line tables (`-g`) | Implemented | 3 | `tests/codegen/debug_info.nc`, `-O2 -g` run configuration |
| F-207 | Hosted default panic handler (weak, `dprintf` + `abort`), `-fpanic=minimal` | Implemented | 3 | `tests/run/panic_*.nc`, `tests/codegen/linkage_mangling.nc` |
| F-208 | Definite initialisation of local variables (E5031; moved from F-402, D-041) | Implemented | 3 | `tests/diag/init_uninitialised.nc`, `tests/diag/init_partial.nc` |
| F-300 | Structs (C layout, `@packed`, `@repr(C)`, opaque declarations), unions (initialise and write; reads need `unsafe`, F-302), enums with underlying types and checked conversions, arrays with bounds-checked indexing, `typedef`, `constexpr`, immutable `static const`, initialiser lists and struct literals with zero fill (D-043), `sizeof`/`alignof`/`offsetof`, character literals, `switch` with case lists, `fallthrough` and exhaustiveness (D-025) | Implemented | 4 | `tests/run/{structs,arrays,enums_switch,statics,layout,big_arrays,aggregate_arg_loop}.nc`, `tests/run/panic_{index,index_write,enum_convert,enum_range}.nc`, `tests/diag/sema_{struct,enum,array,init,switch,static,attrs,limits}.nc`, `tests/parse/aggregates.nc`, `tests/sema/aggregates.nc`, `tests/nir/places.nc`, `tests/codegen/aggregates.nc` |

| F-301 | Raw pointers `T*` / `const T*`, `null`, `&x` / `&mut x` where a pointer is expected (D-044), dereference, `->`, indexing and arithmetic through pointers, comparisons, `addr`, `ptr_cast`, `ptr_cast_mut`, `ptr_from_addr` | Implemented | 4 | `tests/run/pointers.nc`, `tests/diag/pointers.nc`, `tests/diag/init_pointer.nc`, `tests/parse/pointers.nc`, `tests/nir/pointers.nc`, `tests/codegen/pointers.nc` |
| F-302 | `@mode(...)` (safe, controlled, kernel), `unsafe` blocks and functions, E6001/E6003 for every unsafe operation of NOVAC_MEMORY_MODEL.md §6.1 implemented so far (pointer access and arithmetic, unsafe calls, union reads, mutable statics, `uninit`, `ptr_cast_mut`, `ptr_from_addr`), `--max-mode=` (E6004). Planned: W6010, E6002 (no kernel-only operation exists yet) | Implemented | 4 | `tests/run/unsafe_features.nc`, `tests/diag/unsafe_ops.nc`, `tests/diag/unsafe_safe_module.nc`, `--max-mode` cases in `tests/run_tests.py` |
Programs in the Phase 2 language subset, plus the aggregate types, enums
and `switch` of F-300 and the pointers and unsafe code of F-301 and F-302, compile to native executables for x86-64, AArch64
and RISC-V 64 Linux.

Known limitations (each is a later phase's feature, not a defect):

- Single-file programs only; `import` is rejected (F-500).
- Pointers made with `&x` carry no lifetime; references and borrow
  checking are Phase 5 (D-044).
- Generic structs, `static` locals, constexprs of aggregate type, and
  every construct of a later phase are rejected with E9001 naming the
  phase.
- No C interoperability yet (Phase 4): programs cannot call C functions
  or print; they communicate only through their exit status and panics.

## Planned

| ID | Feature | Status | Phase | Reference |
| --- | --- | --- | --- | --- |
| F-303 | `extern "C"` import/export, FFI-safety checks | Planned | 4 | NOVAC_ABI.md §11 |
| F-304 | C ABI lowering: x86-64 SysV, AArch64, RISC-V LP64D | Planned | 4 | NOVAC_ABI.md §5 |
| F-305 | ABI conformance tests against Clang | Planned | 4 | NOVAC_ABI.md §12 |
| F-306 | `x86_64-novaris` target linking LibNovaC | Planned | 4 | NOVAC_ABI.md §10.1 |
| F-400 | References, borrow checking (non-lexical), two-phase borrows | Planned | 5 | NOVAC_MEMORY_MODEL.md §4 |
| F-401 | Ownership, moves, destructors, drop flags | Planned | 5 | NOVAC_MEMORY_MODEL.md §3 |
| F-402 | Initialisation of moved-from places (locals: F-208) | Planned | 5 | NOVAC_MEMORY_MODEL.md §5.2 |
| F-403 | Lifetime elision | Planned | 5 | NOVAC_MEMORY_MODEL.md §5.1 |
| F-404 | Explicit lifetimes, references in structs | Planned | 5 | Q-07 |
| F-405 | Bounds checks and check elimination | Planned | 5 | NOVAC_MEMORY_MODEL.md §7 |
| F-406 | `impl` blocks and methods | Planned | 5 | NOVAC_TYPE_SYSTEM.md §6 |
| F-500 | Modules, imports, `.nci` interfaces, separate compilation | Planned | 6 | D-019 |
| F-501 | Generics and interfaces (monomorphised) | Planned | 6 | D-033 |
| F-502 | `optional`, `result`, `try`, `is` | Planned | 6 | NOVAC_TYPE_SYSTEM.md §4 |
| F-503 | Slices and `str` | Planned | 6 | NOVAC_TYPE_SYSTEM.md §3.8 |
| F-504 | Compile-time evaluation, `constexpr` functions | Planned | 6 | NOVAC_TYPE_SYSTEM.md §9 |
| F-505 | `@cfg` conditional compilation | Planned | 6 | NOVAC_SPEC.md §10 |
| F-506 | `defer` | Planned | 6 | D-034 |
| F-507 | `core`, `alloc`, `std` libraries (initial) | Planned | 6 | D-021 |
| F-508 | Compile-time-checked formatting | Planned | 6 | NOVAC_SPEC.md §13.1 |
| F-600 | Freestanding targets (x86-64, AArch64, RISC-V) | Planned | 7 | NOVAC_ABI.md §2 |
| F-601 | `x86_64-novaris-kernel` target | Planned | 7 | NOVAC_ABI.md §10.2 |
| F-602 | Inline assembly, `@naked` | Planned | 7 | D-028 |
| F-603 | `@interrupt` (x86-64, RISC-V) | Planned | 7 | NOVAC_ABI.md §6.1 |
| F-604 | `mmio<T>`, volatile intrinsics, `core.arch` | Planned | 7 | NOVAC_MEMORY_MODEL.md §8.5 |
| F-605 | `@panic_handler`, `@section`, `@used` | Planned | 7 | NOVAC_ABI.md §9 |
| F-700 | `atomic<T>`, fences, memory orderings | Planned | 8 | NOVAC_MEMORY_MODEL.md §8 |
| F-701 | `Send`/`Sync` | Planned | 8 | NOVAC_MEMORY_MODEL.md §8.2 |
| F-702 | `std.thread`, `std.sync` (Linux first) | Planned | 8 | NOVAC_MEMORY_MODEL.md §8.3 |
| F-703 | LTO, PGO | Planned | 8 | NOVAC_COMPILER_ARCHITECTURE.md §10 |
| F-704 | Benchmark suite vs GCC and Clang | Planned | 8 | NOVAC_COMPILER_ARCHITECTURE.md §12 |
| F-800 | JSON diagnostics | Planned | 9 | NOVAC_COMPILER_ARCHITECTURE.md §6 |
| F-801 | Reproducible builds | Planned | 9 | NOVAC_ROADMAP.md Phase 9 |
| F-802 | C header generation for exports (`--emit-c-header`) | Planned | 6 | NOVAC_ABI.md §11.2 |
| F-803 | C header binding generator (separate tool, libclang) | Planned | 9 | D-004 |
| F-804 | Stack probes, stack-size reports | Planned | 7 | NOVAC_ABI.md §9.3 |
| F-805 | 128-bit integers, `f16`, `f128` | Planned | 8 | NOVAC_TYPE_SYSTEM.md §2 |

## Open

| ID | Feature | Status | Phase | Reference |
| --- | --- | --- | --- | --- |
| F-900 | Closures / lambdas | Open | 6 | Q-05 |
| F-901 | General tagged unions and pattern matching | Open | 6 | Q-06 |
| F-902 | Interface objects / dynamic dispatch | Open | 6 | Q-09 |
| F-903 | Package manifest and dependency management | Open | 6 | Q-08 |
| F-904 | Language-level async | Open | 8 | D-029, Q-10 |
| F-905 | Runtime/library licensing for compiled programs | Open | 3 | Q-01 |

## Unsupported

| ID | Feature | Status | Phase | Reference |
| --- | --- | --- | --- | --- |
| F-950 | C source compatibility (compiling C code) | Unsupported | — | NOVAC_ABI.md §1 |
| F-951 | C preprocessor, `#include`, macros | Unsupported | — | D-004 |
| F-952 | Garbage collection | Unsupported | — | NOVAC_MEMORY_MODEL.md §1 |
| F-953 | Exceptions and stack unwinding | Unsupported | — | D-015 |
| F-954 | 32-bit targets, big-endian targets | Unsupported | — | D-023 |
| F-955 | PE/COFF and Mach-O output | Unsupported | — | D-023 |
| F-956 | NovarisOS on AArch64 / RISC-V (no such OS port exists) | Unsupported | — | D-023 |
| F-957 | Implicit integer promotions, undefined overflow | Unsupported | — | D-009 |
| F-958 | C-style casts, declarator syntax, leading-zero octal | Unsupported | — | D-005, D-010, D-030 |
| F-959 | `char`/`short`/`long`/`unsigned` type names | Unsupported | — | D-031 |
| F-960 | `goto` | Unsupported | — | D-034 |
| F-961 | Function overloading | Unsupported | — | NOVAC_SPEC.md §6.1 |
| F-962 | Bitfields, flexible array members, `long double`, `va_list`, defining variadic functions | Unsupported | — | NOVAC_ABI.md §11.4 |
| F-963 | `volatile` type qualifier | Unsupported | — | D-027 |
| F-964 | C++ interoperability | Unsupported | — | NOVAC_ABI.md §1 |
