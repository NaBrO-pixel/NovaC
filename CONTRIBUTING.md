# Contributing to NovaC

NovaC prioritises correctness over speed of progress. These rules apply
to every change.

## Ground rules

1. **Do not claim what does not work.** A feature is Implemented only
   when it has tests in this repository and
   [docs/FEATURE_STATUS.md](docs/FEATURE_STATUS.md) says so. Features
   that cannot be implemented yet are marked Planned; never add
   placeholder code that appears to work.
2. **Tests accompany every compiler change**, including negative tests
   that check invalid programs are rejected with the right diagnostic
   code.
3. **Never fabricate benchmark results.** Performance numbers come only
   from the benchmark harness, with the machine, compiler versions and
   commit recorded.
4. **Specification first.** A change to language behaviour updates the
   relevant document in `docs/`. A significant or contested choice gets a
   new record in [docs/NOVAC_DECISIONS.md](docs/NOVAC_DECISIONS.md)
   listing the alternatives. Decision records are append-only: supersede,
   do not rewrite.
5. **Keep subsystems separate.** Follow the interfaces in
   [docs/NOVAC_COMPILER_ARCHITECTURE.md](docs/NOVAC_COMPILER_ARCHITECTURE.md);
   no subsystem includes another's private headers.
6. **No new dependencies** (languages, libraries, build tools) without a
   decision record.
7. **Respect project boundaries.** NovaC is not LibNovaC. Do not use the
   `NOVAC_`/`novac_` prefixes or the `<novac/...>` header directory, which
   belong to LibNovaC (D-020). Changes to NovarisOS or LibNovaC are made in
   those repositories.

## Workflow

- Development happens on the `dev` branch or on topic branches merged
  into it.
- Before committing: `make test-all` (sanitizer builds with GCC and Clang,
  all tests, documentation checks) must pass; inspect `git diff` and
  `git status`.
- A new diagnostic needs a code defined in `docs/` and a negative test in
  `tests/diag/` with an inline `// ERROR Ennnn` expectation;
  `tools/check_docs.py` rejects codes that are used but undefined.
- Golden files (`.expected`) are regenerated with `make test UPDATE=1`;
  read the resulting diff before committing it.
- A bug fix comes with a test in `tests/regress/` that fails without it.
- Commit messages: an imperative summary line under 72 characters
  ("Add lexer for integer literals"), a blank line, then what changed and
  why. One logical change per commit.
- Do not commit build output, binaries, caches, editor files, or secrets.
- Never force-push shared branches or rewrite published history.
- Update [CHANGELOG.md](CHANGELOG.md) under "Unreleased" for user-visible
  changes.

## C coding conventions (compiler source)

- C11; must build warning-free with `-Wall -Wextra -Werror -Wshadow
  -Wstrict-prototypes` under both GCC and Clang.
- Public compiler symbols use the `nc_` prefix; file-local functions are
  `static`.
- Memory for compiler data structures comes from arenas; ownership of any
  heap allocation outside an arena is documented at its declaration.
- No global mutable state outside the session object.
- Every diagnostic has a code documented in `docs/`.

## Licensing of contributions

The project is licensed under the PolyForm Noncommercial License 1.0.0
([LICENSE](LICENSE)). By contributing, you agree that your contribution is
licensed under the same terms.
