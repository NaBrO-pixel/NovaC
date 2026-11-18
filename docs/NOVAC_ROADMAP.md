# NovaC Roadmap

This file records project state by phase. A phase is marked complete only
when its exit criteria are met by working, tested code (or, for Phases 0
and 1, reviewed and checked documents) that has been pushed to the
repository. Feature-level status is in [FEATURE_STATUS.md](FEATURE_STATUS.md).

| Phase | Title | Status |
| --- | --- | --- |
| 0 | Repository and architecture investigation | ✅ Complete |
| 1 | Formal specification | ✅ Complete (this commit series) |
| 2 | Compiler foundation | ✅ Complete |
| 3 | Native compilation | ✅ Complete |
| 4 | C interoperability (plus aggregates, raw pointers, unsafe) | Next |
| 5 | Memory safety | Planned |
| 6 | Advanced language features | Planned |
| 7 | Systems programming | Planned |
| 8 | Concurrency and optimisation | Planned |
| 9 | Stable release | Planned |

The phase order is the brief's, with content adjustments recorded in
D-032: type checking moves into Phase 2; structs, arrays, raw pointers
**and** `unsafe` gating land together in Phase 4.

---

## Phase 0 — Repository and architecture investigation ✅

Deliverable: [NOVARISOS_INTEGRATION.md](NOVARISOS_INTEGRATION.md).

- [x] Inspect the NovaC repository (one commit: the license file).
- [x] Inspect NovarisOS (`7656729`) and LibNovaC (`244ed54`): languages,
      build systems, ABIs, toolchains.
- [x] Establish that libNova-Sys, Nova Runtime and Nova Linker do not yet
      exist.
- [x] Identify integration constraints and naming collisions.

## Phase 1 — Formal specification ✅

- [x] [NOVAC_SPEC.md](NOVAC_SPEC.md)
- [x] [NOVAC_GRAMMAR.md](NOVAC_GRAMMAR.md)
- [x] [NOVAC_MEMORY_MODEL.md](NOVAC_MEMORY_MODEL.md)
- [x] [NOVAC_TYPE_SYSTEM.md](NOVAC_TYPE_SYSTEM.md)
- [x] [NOVAC_ABI.md](NOVAC_ABI.md)
- [x] [NOVAC_COMPILER_ARCHITECTURE.md](NOVAC_COMPILER_ARCHITECTURE.md)
- [x] [NOVAC_ROADMAP.md](NOVAC_ROADMAP.md) (this file)
- [x] [NOVAC_DECISIONS.md](NOVAC_DECISIONS.md)
- [x] [FEATURE_STATUS.md](FEATURE_STATUS.md)
- [x] `tools/check_docs.py` passes (`make check-docs`)

Exit criteria met: all documents exist, cross-references resolve, the
keyword lists agree, every cited decision exists, every feature status is
from the allowed vocabulary.

---

## Phase 2 — Compiler foundation

**Result.** All steps below are done and every exit criterion is met:
`make test-all` passes with GCC 13 and Clang 18 under ASan, UBSan and
LeakSanitizer (4,296 unit-test checks; 80 runner tests: 7 lexer, 8
parser and 6 type-checker golden tests, 38 diagnostic tests, 2
regression tests, 1 example program, 18 command-line cases; plus 6
runner self-test fixtures); every diagnostic
code Phase 2 can emit has a negative test except E0005 (it needs an input
file of 4 GiB or more); and a 30-minute libFuzzer run on the final code
(commit `1ab9c6c`, Clang 18, ASan + UBSan, inputs up to 4 KiB) executed
9,732,975 inputs with no crash, leak, timeout or sanitizer report. Two
earlier runs on preceding versions (433,000 and 2.1 million inputs) also
found nothing. Specification changes made while
implementing are recorded as D-037 and D-038.

**Goal:** `ncc --check file.nc` lexes, parses, resolves and type-checks
single-file programs in the Phase 2 subset ([P2] productions in
NOVAC_GRAMMAR.md), with precise diagnostics. No code generation.

### Scope

In: the [P2] grammar — optional `module` declaration; functions with
primitive-typed parameters and returns; local variables (`T x = e;`,
`auto`, `const`, multiple declarators, deferred initialisation);
integer, float and `bool` literals with context typing; all arithmetic
(including wrapping `+% -% *%`), bitwise, shift, comparison and logical
operators; assignment and compound assignment (type `void`); `++`/`--`;
conversions `T(x)`; `?:`; calls; `if`/`else`, `while`, `do`/`while`,
`for`, `break`, `continue`, `return`; blocks and scoping.

Recognised but rejected with E9001: everything else in the grammar
(imports, structs, pointers, strings, `switch`, attributes, …). The
lexer handles the **complete** token set from the start.

Semantics checked in Phase 2: name resolution (E3001, E3002), types
(E4001–E4004, E4010, E4012, E4020, E4030, E4041, E4060), compile-time
evaluation of constant integer expressions sufficient to report constant
overflow (E4040), non-associative comparisons (E2010), C-isms (E1010,
E1020, E1021, E2020, E2031, E3011). Definite initialisation (E5031) is
**not** in Phase 2: it needs the CFG (Phase 3/5).

### Work breakdown (each step is one or more commits, tested before the next)

| Step | Deliverable | Tests |
| --- | --- | --- |
| 2.1 | `Makefile` (`all`, `test`, `check-docs`, `clean`, `DEBUG=1`, `CC=`); `src/driver/main.c` with `--version`, `--help`, option parsing skeleton (unknown option E0002, unimplemented option E9002); `support/`: arena, string interning, vector, source manager with line tables | `tests/unit/` C harness: arena, interning, line/column mapping; builds with GCC and Clang, warning-free, ASan/UBSan-clean |
| 2.2 | `diag/`: diagnostic records, codes, spans, rendering with source excerpt and caret, notes/help, fix-its, error limit | unit tests of rendering (golden text) |
| 2.3 | `lex/`: complete lexer per NOVAC_GRAMMAR.md §2; `--dump-tokens` | `tests/lex/` golden files for every token class and literal form; `tests/diag/` for E1001, E1010, E1020–E1023, E1030 |
| 2.4 | `tests/run_tests.py`: golden and inline-expectation runner (NOVAC_COMPILER_ARCHITECTURE.md §12), `UPDATE=1` mode | self-test of the runner on a deliberately failing fixture |
| 2.5 | `ast/` + `parse/`: [P2] declarations and statements, full expression precedence table, declaration/expression disambiguation, error recovery; `--dump-ast` | `tests/parse/` golden ASTs for every [P2] production and every precedence level; `tests/diag/` for E2001, E2010, E2020, E2031, E9001 |
| 2.6 | `sema/` resolution: module scope, block scopes, parameters, shadowing rules | `tests/diag/` E3001, E3002, E3011 |
| 2.7 | `sema/` types: primitive types, untyped literals, operator typing, conversions, `void` assignments, `bool` conditions, return-path analysis, constant evaluation for literal range and constant overflow; `--emit=typed` | `tests/sema/` typed dumps; `tests/diag/` for every E4xxx above |
| 2.8 | libFuzzer harness for lexer+parser+sema (`make fuzz`, Clang only) | ≥ 30 minutes per harness with no crash, leak or sanitizer report; any finding becomes a `tests/regress/` case |
| 2.9 | Documentation: FEATURE_STATUS.md, README "implemented" list, CHANGELOG, `ncc --help` text | `make check-docs` |

### Exit criteria (all met)

1. `make test` passes with both `CC=gcc` and `CC=clang`, in the
   sanitizer configuration.
2. Every [P2] production has at least one positive test; every diagnostic
   code Phase 2 can emit has at least one negative test.
3. Fuzzing ran as in step 2.8 with no open findings.
4. FEATURE_STATUS.md lists each Phase 2 feature as Implemented with a
   pointer to its tests, and nothing else changed status.
5. The work is committed and pushed.

### Explicit non-goals for Phase 2

Code generation, LLVM, multiple files/imports, structs, pointers,
strings, ownership. These are not stubbed: they are rejected (E9001).

---

## Phase 3 — Native compilation

**Result.** Everything listed below is implemented and tested.
- **Tests:** `make test-all` (GCC 13 and Clang 18, ASan + UBSan) passes:
  4,296 unit-test checks and 136 runner tests (3 NIR golden, 9 LLVM IR,
  26 execution, 35 command-line, plus the Phase 2 suites).
- **Execution coverage:** each execution test runs natively at `-O0`,
  `-O1`, `-O2`, `-O3`, `-Os`, `-Ofast` and `-O2 -g`. It also runs as
  AArch64 and RISC-V executables under QEMU at `-O0` and `-O2`, so the
  cross targets are verified by execution, not only by disassembly.
- **Panic coverage:** 20 of the execution tests check one panic each,
  covering every kind of run-time check.
- **Departures from the plan:** the hosted panic handler uses `dprintf`
  rather than `write`, so it can format the location (D-039). Definite
  initialisation was brought forward from Phase 5 (D-041). `-Ofast`
  excludes `nnan`/`ninf` (D-040).
- **Fuzzing:** a 30-minute libFuzzer run (ASan + UBSan) of the code
  committed as `d3a3935` went through lexing, parsing, type checking, NIR
  lowering, the NIR verifier and the initialisation analysis. It executed
  8,029,243 inputs with no crash, leak, timeout or sanitizer report.
- **Still open:** Q-01 (licensing of compiled programs), which was to be
  answered before linked programs exist. Programs are now linked; the
  question is reported to the owner.

Prerequisites: LLVM 18 development headers (`llvm-18-dev`); for cross
execution tests, `qemu-aarch64` and `qemu-riscv64` user-mode.

- NIR definition, lowering of the Phase 2 subset, verifier, `--dump-nir`.
- LLVM IR generation, overflow/division/shift/conversion checks branching
  to `__nova_panic`; default hosted panic handler (emitted by the
  compiler, calling the C library's `dprintf` and `abort`; no runtime
  library file; D-039).
- Optimisation levels `-O0`…`-Ofast` (NOVAC_COMPILER_ARCHITECTURE.md §10).
- Object, assembly and bitcode output; linking hosted executables on
  `x86_64-linux-gnu` through the system C compiler driver.
- Cross-compilation of objects for `aarch64-linux-gnu` and
  `riscv64-linux-gnu`.
- Debug line tables (D-035).
- Tests: `tests/run/` (exit status, panic behaviour), `tests/codegen/`.

Exit: run tests pass on x86-64; cross objects verified (by execution under
QEMU where available, otherwise by disassembly checks, and the report
says which).

## Phase 4 — C interoperability, aggregates and unsafe

- Structs, unions, enums, fixed arrays, raw pointers, `typedef`, statics,
  `constexpr` items, `switch`.
- `@mode`, `unsafe` blocks and functions, the full unsafe-operation check
  (E6001–E6003) — delivered together with raw pointers (D-032).
- `extern "C"` imports and exports, `@export`, `@link_name`, `core.ffi`
  types, FFI-safety checks (E7030, E7031).
- C ABI lowering for x86-64 SysV, then AArch64 and RISC-V
  (NOVAC_ABI.md §5); `tests/abi/` conformance against Clang. E7032 until a
  target's tests pass.
- `x86_64-novaris` target: link against LibNovaC's `crt0.o` and
  `libNovaC.a`; run the same binary on the Linux host and (when a
  NovarisOS image is available) inside NovarisOS, comparing transcripts.

## Phase 5 — Memory safety

- 5a: references (`T&`, `const T&`), `&`/`&mut`, `impl` blocks and methods,
  destructors, moves, definite initialisation, NLL borrow checking,
  two-phase borrows, elided lifetimes, drop elaboration, bounds checks on
  arrays, `@nocopy`, `@overflow`.
- 5b: explicit lifetime syntax (Q-07), references in structs.
- Tests: `tests/borrow/` with a negative test for every E5xxx code and a
  corpus of programs that must be accepted.

## Phase 6 — Advanced language features

Modules and imports with `.nci` interfaces (D-019); generics and
interfaces (D-033); `optional`, `result`, `try`, `is` patterns; slices and
`str`; compile-time evaluation including `constexpr` functions; `@cfg`;
`defer`; the first `core`, `alloc` and `std` modules (D-021), including
compile-time-checked formatting.

## Phase 7 — Systems programming

Freestanding targets for all three architectures; `x86_64-novaris-kernel`
(NOVAC_ABI.md §10.2); `asm`, `@naked`, `@interrupt` (per-architecture),
`@section`, `@used`, `@panic_handler`, `mmio<T>`, `core.arch` intrinsics;
stack-size reporting. Demonstration: a NovaC object (e.g. the serial
driver from NOVAC_SPEC.md §13.4) linked into the NovarisOS kernel with its
existing linker script and booted in QEMU — performed in a NovarisOS
checkout, not by modifying NovarisOS from this repository.

## Phase 8 — Concurrency and optimisation

`atomic<T>`, fences, `Send`/`Sync`, `core.sync.spin_mutex`; `std.thread`
and `std.sync` on Linux (on NovarisOS only once a threaded C library or
libNova-Sys exists); LTO, PGO, `noalias` emission; the benchmark suite
comparing NovaC with GCC- and Clang-compiled C (execution time, binary
size, memory, compile time, check overhead).

## Phase 9 — Stable release

Complete documentation; JSON diagnostics; regression suite; final ABI
document; reproducible builds (deterministic output, path remapping);
release engineering (versioning, tagged releases, changelog discipline).

---

## Risks to the schedule

See [NOVARISOS_INTEGRATION.md §5](NOVARISOS_INTEGRATION.md#5-technical-risks).
The brief describes several years of compiler work; phases are
deliberately small so each ends in something tested.
