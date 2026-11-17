# tests/

Run everything with `make test` (sanitizer build) or `make test-all` (GCC
and Clang). The runner is `tests/run_tests.py`; its format is documented
at the top of that file and in
[NOVAC_COMPILER_ARCHITECTURE.md §12](../docs/NOVAC_COMPILER_ARCHITECTURE.md#12-testing-infrastructure).

| Directory | What | How it is checked |
| --- | --- | --- |
| `unit/` | C unit tests of support code, diagnostics, constant arithmetic, lexer | `build/<config>/unit_tests` |
| `lex/` | token streams | `--dump-tokens` vs `<name>.expected` |
| `parse/` | syntax trees | `--dump-ast` vs `<name>.expected` |
| `sema/` | typed syntax trees with constant values | `--emit=typed` vs `<name>.expected` |
| `diag/` | invalid programs, one or more per diagnostic code | inline `// ERROR Ennnn` expectations |
| `driver/` | input files for the command-line cases in `run_tests.py` | exit status and codes |
| `selftest/` | fixtures the runner must reject | `run_tests.py --selftest` |
| `fuzz/` | libFuzzer harness and dictionary | `make fuzz-run` |
| `regress/` | one test per fixed bug (none yet) | `// MODE:` line, default `--check` |

The programs in `../examples/` are also checked (`--check`, no errors).
