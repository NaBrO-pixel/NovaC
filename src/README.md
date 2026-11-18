# src/

The NovaC compiler, `ncc`, in C11 (D-001). One directory per subsystem,
each with one public header; dependencies point downward only
([NOVAC_COMPILER_ARCHITECTURE.md §4, §13](../docs/NOVAC_COMPILER_ARCHITECTURE.md#4-source-tree)).

| Directory | Contents | Phase |
| --- | --- | --- |
| `support/` | arenas, string interning, vectors, source files and spans | 2 |
| `diag/` | diagnostics engine and rendering | 2 |
| `session/` | per-invocation state passed to every stage | 2 |
| `lex/` | tokens and the lexer | 2 |
| `ast/` | syntax tree and its S-expression dump | 2 |
| `parse/` | parser: declarations, statements, expressions, types | 2 |
| `sema/` | name resolution, types, constant evaluation, type checking | 2 |
| `nir/` | NovaC IR: lowering with explicit checks, verifier, dump | 3 |
| `analysis/` | analyses on NIR: definite initialisation | 3 |
| `target/` | target descriptions (triples, baselines, cross tools) | 3 |
| `codegen/` | LLVM IR generation, optimisation, object and assembly output | 3 |
| `driver/` | command line, pipeline, linking, `main` | 2–3 |

Directories for later phases are created when they contain working,
tested code.
