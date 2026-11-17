# tests/

Compiler tests. **Empty until Phase 2**, because there is no compiler to
test yet. The documentation itself is checked by `make check-docs`
(`tools/check_docs.py`).

The planned test suites (unit, lexer, parser, semantic, diagnostic,
run, code generation, ABI, ownership/borrowing, regression) and the
inline diagnostic expectation format are specified in
[docs/NOVAC_COMPILER_ARCHITECTURE.md §12](../docs/NOVAC_COMPILER_ARCHITECTURE.md#12-testing-infrastructure).
