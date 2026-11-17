# Changelog

All notable changes to NovaC are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). NovaC has no
released versions yet.

## [Unreleased]

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
