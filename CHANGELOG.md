# Changelog

All notable changes to NovaC are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). NovaC has no
released versions yet.

## [Unreleased]

### Added — Phase 4a: aggregates, enums and `switch`

- Structs (C layout, `@packed`, `@repr(C)`, opaque `struct Name;`), unions,
  enums with an underlying integer type, fixed arrays `T[N]`, `typedef`,
  `constexpr`, `static const`, character literals, `sizeof`, `alignof`,
  `offsetof`. Module-level declarations may appear in any order; cycles
  are detected (E3005, E4080).
- Initialiser lists and struct literals with positional and designated
  members; omitted members are zero when zero is a valid value (D-043).
- `switch` with case lists, `default`, `fallthrough`, exhaustiveness for
  enums and `bool`, and the D-025 fallthrough rule (E2050, E2051).
- Run-time checks: array indices (`index out of bounds`) and integer to
  enum conversions (`value is not an enumerator of 'E'`).
- NIR places with field and element projections, aggregate values,
  `switch` terminators and statics; the LLVM layout of every aggregate is
  verified against the computed one.
- Attributes are checked: unknown ones are E3040, misapplied ones E3041.
- Diagnostics E2051, E3005, E3006, E3041, E4071, E4073 to E4075, E4080
  to E4090, E9005.
- NovaC's internal calling convention passes structs, unions and arrays
  by address and returns them through a hidden pointer; aggregate copies
  and zeroing use `memcpy`/`memset` (NOVAC_ABI.md §6).
- Implementation limits: objects of at most 2^47 bytes (E4089), and at
  most 2^22 array elements expanded by initialiser lists per module (E9005).

### Fixed during Phase 4a

- Accesses to fields inside `@packed` structs were emitted with the
  field's natural alignment. They now use alignment 1.
- A program passing a 200 KB struct by value crashed the compiler in LLVM
  instruction selection after a minute; aggregates are now passed
  indirectly (`tests/run/big_arrays.nc`).
- The fuzz build did not track header dependencies, so a stale harness
  object could be linked against changed structures.

### Added — Phase 3: native compilation

- NIR (`src/nir/`): lowering of the checked AST to a control-flow graph in
  which every run-time check is an explicit `assert`; verifier; `--emit=nir`.
- Definite-initialisation analysis on NIR (E5031), brought forward from
  Phase 5 because generated code must not read indeterminate values (D-041).
- LLVM back end (`src/codegen/`, LLVM 18 through the C API): checked
  arithmetic with the overflow intrinsics, a panic call per failing check,
  a weak hosted panic handler (D-039), a C `main` wrapper, symbol mangling
  per NOVAC_ABI.md §7, DWARF 5 line tables (`-g`), and optimisation levels
  `-O0` to `-Ofast`.
- Outputs: executables (default), `-c`, `-S`, `--emit=llvm-ir`,
  `--emit=llvm-bc`; targets `x86_64-linux-gnu`, `aarch64-linux-gnu` and
  `riscv64-linux-gnu` (`--target=`, `-mcpu=`, `-mattr=`); `--linker=`;
  `-fpanic=minimal`.
- Diagnostics E8001 to E8005.
- Tests: `tests/run/` executes programs natively at seven configurations
  and under QEMU for AArch64 and RISC-V; `tests/nir/` golden NIR;
  `tests/codegen/` LLVM IR checks.

### Verified

- Fuzzing of commit `d3a3935` through NIR lowering and analysis:
  8,029,243 inputs in 30 minutes, no crash, leak, timeout or sanitizer
  report.

### Fixed during Phase 3

- `-Ofast` used `nnan`/`ninf`, which made a NaN conversion check
  ineffective (undefined behaviour from safe code). It now uses only flags
  that cannot create poison (D-040).
- The initialisation analysis treated `while (true)` as able to exit at
  its condition; constant branches now have one successor.


### Added — Phase 2: compiler front end

- `ncc`, the compiler driver (C11, GNU Make; D-001, D-002) with `--check`,
  `--dump-tokens`, `--dump-ast`, `--emit=typed`, `-Werror` and
  `--error-limit`. Documented options of later phases are rejected with
  E9002, and the default action (code generation) with E9003.
- Lexer for the complete NovaC token set, including all literal forms,
  escapes, and C-ism diagnostics (`#include`, leading-zero octal, `.5`).
- Parser for the Phase 2 subset with error recovery; the full expression
  and type grammar is parsed, and later-phase declarations and statements
  are recognised and rejected (E9001).
- Name resolution and type checking of primitive types: untyped constants,
  exact constant folding with overflow detection, implicit widening at
  coercion sites only, checked conversions, return-path analysis.
- Diagnostics with stable codes, source excerpts, notes and help.
- Tests: C unit tests; golden and inline-expectation tests run by
  `tests/run_tests.py` with a self-test; libFuzzer harness. `make test`
  builds with ASan and UBSan; `make test-all` uses GCC and Clang.
- `tools/check_docs.py` now also checks that the keywords in
  `src/lex/token.h` and every diagnostic code used in `src/` and `tests/`
  are defined in the specification.

### Verified

- Fuzzing (`make fuzz-run FUZZ_SECONDS=1800`) of commit `1ab9c6c`:
  9,732,975 inputs in 30 minutes, no crash, leak, timeout or sanitizer
  report.

### Fixed during Phase 2 review

- A very long operator chain (`a + a + ...`, 200,000 terms) built a
  left-deep syntax tree that overflowed the stack in later stages, although
  every syntactic nesting limit was respected. The depth limit (E9004, now
  1000 levels) counts operator, postfix and type-suffix chains;
  `tests/regress/long_operator_chain.nc`, `long_postfix_chain.nc`.

### Changed — specification, while implementing Phase 2

- D-037: implicit conversions apply only at coercion sites, never between
  the operands of a binary operator (resolves a Phase 1 contradiction).
- D-038: a variable used in its own initialiser is an error (E3003).
- NOVAC_GRAMMAR.md: `>>`/`>>=` are lexed as joint `>` tokens; struct
  literal lookahead; new lexical codes E1002, E1003, E1024, E1025, E1031,
  E1032, E2060, E9004.
- NOVAC_TYPE_SYSTEM.md: `%` is integer-only; `&`/`|`/`^` apply to `bool`;
  width rules for untyped constants; new codes E4005–E4009, E4011, E4013,
  E4062, E4064, E4065, W4001, W4002.
- NOVAC_COMPILER_ARCHITECTURE.md: `session/` module, stage gating, test
  and fuzzing layout, codes E0005 and E9003.

### Added — Phases 0 and 1

- Phase 0 report on NovarisOS and LibNovaC integration
  (`docs/NOVARISOS_INTEGRATION.md`).
- Phase 1 specification: `NOVAC_SPEC.md`, `NOVAC_GRAMMAR.md`,
  `NOVAC_TYPE_SYSTEM.md`, `NOVAC_MEMORY_MODEL.md`, `NOVAC_ABI.md`,
  `NOVAC_COMPILER_ARCHITECTURE.md`, `NOVAC_ROADMAP.md`,
  `NOVAC_DECISIONS.md` (36 decision records, 11 open questions), and
  `FEATURE_STATUS.md`.
- `tools/check_docs.py` and `make check-docs`: documentation consistency
  checks.
- `README.md`, `CONTRIBUTING.md`, `.gitignore`, `Makefile`.

### Changed

- Renamed `Liscense.md` to `LICENSE` (content unchanged).
