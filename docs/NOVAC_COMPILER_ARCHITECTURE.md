# NovaC Compiler Architecture

Status: **Design, Phase 1.** No compiler code exists yet; see
[FEATURE_STATUS.md](FEATURE_STATUS.md).

## 1. Goals

- **Correctness first:** every stage has a verifier or a dump format that
  tests can inspect; no stage silently accepts input it does not
  understand (E9001).
- **Modularity:** one directory per subsystem, each with a single public
  header describing its interface (§13). No subsystem reaches into another
  subsystem's internals.
- **Testability:** every stage's output can be printed in a stable text
  form (`--dump-*`) for golden tests.
- **Robustness:** the compiler never crashes on any input; malformed
  programs produce diagnostics. Enforced by sanitizer builds and fuzzing.

## 2. Implementation language and build

- C11, compiled with `-std=c11 -Wall -Wextra -Werror -Wshadow
  -Wstrict-prototypes` by GCC ≥ 13 or Clang ≥ 18 (D-001).
- GNU Make, out-of-tree output in `build/` (D-002).
- LLVM ≥ 18 through the LLVM-C API, located with `llvm-config`
  (overridable: `make LLVM_CONFIG=...`) (D-003). Phase 2 does not link
  LLVM at all, so the front end builds without LLVM installed.
- Python 3 (standard library only) for the test runner and documentation
  checks.
- Debug configuration (`make DEBUG=1`, used by `make test`): `-O1 -g
  -fsanitize=address,undefined`. Release: `-O2`.
- Compiler-internal C symbols use the `nc_` prefix (D-020).

## 3. Pipeline

```text
 .nc source
    │  support/source     load file, line table
    ▼
 lex/        Lexer ─────────────► tokens                       --dump-tokens
    ▼
 parse/      Parser ────────────► AST (untyped)                --dump-ast
    ▼
 sema/       Module loader ─────► module graph (imports, .nci)
             Name resolution ───► AST + symbol bindings
             Type checking ─────► typed AST                    --dump-typed
             Const evaluation
             Mode/unsafe checks
    ▼
 nir/        Lowering ──────────► NIR (CFG over typed places)  --dump-nir
    ▼
 analysis/   Definite init, move, borrow & lifetime checking,
             drop elaboration (drop flags), check elimination
    ▼
 target/     ABI lowering (C calling convention classification)
    ▼
 codegen/    LLVM IR generation ─► LLVM module                 --emit=llvm-ir
             LLVM optimisation (new pass manager)
             Object emission ────► .o / .s / .bc
    ▼
 driver/     Link (ld.lld / GNU ld / system cc) ──► executable
```

This differs from the brief's proposed pipeline in one respect:
ownership and borrow analysis runs **after** lowering to NIR, because it
is a dataflow analysis over a control-flow graph (D-013). Semantic
validation that does not need control flow (types, modes, attribute
placement) runs in `sema/`, before NIR.

Each arrow is a function call within one process. Stage outputs live in
per-stage memory arenas (§5.1) and are freed when no longer needed.

## 4. Source tree

```text
src/
  driver/     main.c, options.c/.h, frontend.c/.h   command line, pipeline, linking
  session/    session.c/.h                the per-invocation state passed to every stage
  support/    arena, strtab (interning), vec, source manager, common
  diag/       diagnostics engine and rendering
  lex/        token.h, lexer.c/.h
  ast/        ast.h, ast.c, ast_dump.c
  parse/      parser.c/.h, parse_decl.c, parse_stmt.c, parse_expr.c, parse_type.c
  sema/       scope, resolve, types, check_*, consteval, modes
  nir/        nir.h, lower.c, verify.c, nir_dump.c
  analysis/   init.c, borrowck.c, drops.c, checks.c
  target/     target.c/.h, abi_x86_64.c, abi_aarch64.c, abi_riscv64.c
  codegen/    llvm.c, debuginfo.c, emit.c, shim/ (C++ only if justified, D-001)
lib/          NovaC source of core/alloc/std (Phase 6+)
tests/        see §12
tools/        check_docs.py and other maintenance scripts
```

Phase 2 created `driver`, `session`, `support`, `diag`, `lex`, `ast`,
`parse` and the Phase 2 subset of `sema`. Directories for later phases are
not created until they contain working code.

## 5. Core infrastructure (`support/`)

### 5.1 Memory

- **Arenas** (`nc_arena`): bump allocators with chunk lists. The AST,
  types and NIR are arena-allocated and freed wholesale. This removes most
  individual `free` calls and their bugs.
- **Interned strings** (`nc_sym`): identifiers and string literal contents
  are interned once; comparisons are integer comparisons.
- **Vectors** (`nc_vec`): typed growable arrays via a small macro set.
- No global mutable state except the process-wide options struct, which
  is read-only after argument parsing. All compilation state hangs off one
  `nc_session` passed explicitly, so the compiler could later be used as a
  library or run multithreaded per module.

### 5.2 Source manager

Files are loaded whole. A location is a `nc_span { u32 file; u32 lo;
u32 hi; }` of byte offsets. Line/column (1-based, columns in bytes;
display width is computed for rendering) are derived lazily from a per-
file line-start table.

## 6. Diagnostics

### 6.1 Format

```text
src/demo.nc:3:17: error[E4010]: operands of '+' have different types 'i32' and 'u8'
    3 |     i32 c = a + b;
      |             - ^ -
      = help: convert explicitly: `a + i32(b)`
```

- Severity: `error`, `warning`, `note`, `help`. `-Werror` promotes
  warnings.
- Every error and warning has a stable code. Codes are never reused for a
  different condition.
- Fix-its carry a span and replacement text (used by tests and, later,
  editors).
- `--diag-format=json` (Phase 9) emits one JSON object per diagnostic.
- After `--error-limit` errors (default 50) compilation stops.
- An internal compiler error prints `internal compiler error:` with the
  failing stage and exits with status 3; it is always a bug.

### 6.2 Code ranges

| Range | Area | Defined in |
| --- | --- | --- |
| E0xxx | driver, files, options | this document |
| E1xxx | lexical | NOVAC_GRAMMAR.md §7 |
| E2xxx | syntax | NOVAC_GRAMMAR.md §7 |
| E3xxx | names, modules, attributes | NOVAC_SPEC.md §15 |
| E4xxx | types and expressions | NOVAC_TYPE_SYSTEM.md §11, NOVAC_SPEC.md §15 |
| E5xxx | ownership, borrowing, initialisation, bounds | NOVAC_MEMORY_MODEL.md §9 |
| E6xxx | modes and unsafe | NOVAC_MEMORY_MODEL.md §9 |
| E7xxx | target, ABI, FFI | NOVAC_ABI.md §13 |
| E8xxx | code generation and linking | this document (Phase 3) |
| E9xxx | implementation limits, not-yet-implemented | NOVAC_GRAMMAR.md §7 |

Warnings use `W` with the same ranges. Codes defined in this document:

| Code | Condition |
| --- | --- |
| E0001 | cannot read input file |
| E0002 | unknown command-line option |
| E0003 | invalid option value |
| E0004 | no input files |
| E0005 | input file too large (4 GiB or more) |
| E8001 | code generation failed (target set-up, optimisation or writing the output; the LLVM message is included) |
| E8002 | the linker ran and failed |
| E8003 | the linker could not be started (not installed, or a wrong `--linker=`) |
| E8004 | an executable was requested but no input defines `main` |
| E8005 | `main` does not have the signature `i32 main()` |
| E8010 | object compiled against a different interface hash of an imported module |
| E9002 | command-line option recognised but not implemented in this version |
| E9003 | code generation requested but not implemented (reported by Phase 2 only; retired, never reused) |

### 6.3 Error recovery

The parser recovers at synchronisation points (`;`, `}`, and the start of
a top-level declaration keyword) so one mistake yields one diagnostic.
Name resolution and type checking assign an *error type* to erroneous
expressions; the error type is compatible with everything, which
suppresses cascades.

**Stage gating.** Each stage runs only if the earlier stages reported no
errors for the module: the parser does not run after lexical errors (the
lexer drops invalid characters, so parsing the rest would mostly report
consequences of the same mistake), semantic analysis does not run after
syntax errors, and later stages never run on a module with errors.
`--dump-tokens` prints the token stream even after lexical errors.

## 7. Front end

### 7.1 Lexer (`lex/`)

Hand-written, single pass over the byte buffer. Produces `nc_token { kind;
span; value }` where `value` is an interned symbol (identifiers, string
contents) or a parsed literal (`u64` magnitude + suffix for integers —
range checking against the final type happens in sema; `f64`/hex-float
text for floats). An integer literal that does not fit in 64 bits is
E1023 at lex time; fitting the value to its final type is checked in sema
(E4041).
The full keyword set is recognised from Phase 2 even where the parser does
not yet use it, so adding features never changes tokenisation.

### 7.2 Parser (`parse/`)

Recursive descent for declarations and statements; Pratt (precedence
climbing) for expressions, driven by the table in NOVAC_GRAMMAR.md §6.
Declaration-vs-expression ambiguity is resolved by a speculative
`try_parse_type` followed by an identifier check (D-005); the token
stream is an array, so backtracking is resetting an index. Non-associative
operators produce E2010 when chained.

### 7.3 AST (`ast/`)

Tagged structs allocated in the AST arena; every node has a span. Node
families: `Decl`, `Stmt`, `Expr`, `TypeExpr`, `Pattern`. `--dump-ast`
prints an S-expression form with stable field order, used for golden
tests:

```text
(fn add (params (param a (type i32)) (param b (type i32))) (ret (type i32))
  (block (return (binary + (name a) (name b)))))
```

### 7.4 Semantic analysis (`sema/`)

1. **Collect:** walk the module, enter every top-level declaration into
   the module scope (order-independent declarations).
2. **Resolve:** bind every name to a declaration; lexical scopes for
   blocks; imports via the module graph.
3. **Types:** canonical, interned `nc_type` objects (pointer equality is
   type equality). Untyped literals carry a provisional "untyped int /
   untyped float" type until context fixes them.
4. **Check:** expressions, statements, return paths, conversions,
   constant evaluation, attribute placement, mode/unsafe rules (Phase 4+).
5. Results are stored on AST nodes (`expr->type`, `name->decl`).

## 8. Modules and separate compilation

- The driver builds the import graph from `import` declarations,
  rejects cycles (E3031), and processes modules in dependency order.
- Each compiled module produces an object file and a **`.nci` interface
  file** containing: the module path, mode, public declarations
  (signatures, struct layouts, enum values, constants), bodies of public
  generic and `@inline` functions (for instantiation in importers), and a
  hash of that content.
- An importer records the interface hash it compiled against;
  `ncc` refuses to link objects compiled against a different hash of the
  same interface (E8010), which is the "interface checking" requirement.
- `.nci` is a versioned text format (Phase 6) so it can be diffed and
  golden-tested. It is not an ABI promise across compiler versions.

## 9. Middle and back end

### 9.1 NIR (NovaC Intermediate Representation)

A control-flow graph per function, in the style of Rust's MIR:

- **Locals** with types (parameters, user variables, temporaries).
- **Places:** a local plus projections (field, index, deref).
- **Statements:** `assign(place, rvalue)`, `storage_live/dead(local)`,
  `drop(place)`, `nop`.
- **Rvalues:** `use(operand)`, `binary(op, a, b)`,
  `checked_binary(op, a, b)` (value + overflow flag), `unary`,
  `ref(shared|exclusive, place)`, `cast(kind, operand)`, `aggregate`,
  `len(place)`.
- **Operands:** `copy(place)`, `move(place)`, `const(value)`.
- **Terminators:** `goto`, `branch(cond)`, `switch_int`, `call(fn, args,
  dest, next)`, `return`, `assert(cond, panic_kind, next)`,
  `unreachable`.

NIR is not SSA; LLVM's `mem2reg`/SROA produce SSA. A verifier checks
well-formedness after lowering, in every build (a failure is an internal
compiler error). `--dump-nir` (`--emit=nir`) prints a stable text form.

**Implemented in Phases 3 and 4a** (`src/nir/`): a place is a local or a
static followed by field and element projections (`_3.v[_5]`); an
element index is a constant (checked by sema, E5040) or a `usize`
temporary that a preceding `assert` bounds-checked. Statements are
`assign(place, rvalue)` and `checked(op, a, b)`, which writes the wrapped
result and an overflow flag. Rvalues are `use`, `binary`, `unary`, `cast`
and `aggregate`; operands are `copy`, `const` and `zeroed`. Terminators
are `goto`, `branch`, `switch`, `call`, `return`, `unreachable` and
`assert(cond, expected, message)`. Every run-time check of
NOVAC_TYPE_SYSTEM.md (overflow, division by zero, `MIN / -1`, shift
amount, conversion range, enum membership, array bounds) is an explicit
`assert`, so analyses and code generation see the same program. Code
generation computes each aggregate's LLVM type and stops with an internal
error if LLVM's layout differs from the one sema computed. `move`, `drop`,
`storage_live/dead` and the other planned forms arrive with the phases
that need them.

### 9.2 Analyses (`analysis/`)

1. Definite initialisation (implemented in Phase 3, `analysis/init.c`:
   a forward "must" dataflow over the CFG for declared variables, E5031;
   branches on constant conditions have one successor, as in the type
   checker's return-path analysis) and move checking (Phase 5).
2. Borrow checking: compute borrow liveness (backward liveness of
   reference-typed locals), then check each access against live
   conflicting borrows (NOVAC_MEMORY_MODEL.md §4). Two-phase borrows for
   auto-referenced method receivers.
3. Drop elaboration: insert `drop` statements and drop flags.
4. Check elimination: remove `assert` terminators proven redundant
   (constant folding, dominating identical checks, simple induction-
   variable ranges). LLVM removes more later; both are measured.

### 9.3 Target and ABI (`target/`)

`nc_target` describes a target: triple, data layout string, pointer size,
features, code model, and its C ABI classifier
(`classify_arg`, `classify_return`) per NOVAC_ABI.md §5.

### 9.4 LLVM code generation (`codegen/`)

- One LLVM module per NovaC module. Functions declared first, bodies
  second.
- NIR locals become `alloca`s in the entry block; LLVM promotes them.
- `checked` statements use `llvm.{s,u}{add,sub,mul}.with.overflow`; each
  failing `assert` branches to its own block that calls `__nova_panic`
  (`noreturn`, `cold`) with a static description of that site
  (NOVAC_ABI.md §9.1), then `unreachable`.
- Hosted modules carry a weak default `__nova_panic` (D-039). A NovaC
  `main` is wrapped by a C `main` (NOVAC_ABI.md §7).
- Bounds checks compare against the length and branch likewise.
- Attributes: `nounwind` everywhere (D-015); `noredzone` and target
  features for kernel targets (NOVAC_ABI.md §10.2).
- Debug info through LLVM-C's DIBuilder (D-035).
- `LLVMTargetMachineEmitToFile` writes objects and assembly.

## 10. Optimisation

| Flag | LLVM pipeline | Other effects |
| --- | --- | --- |
| `-O0` | `O0` (always-inline only) | debugging; checks kept literally |
| `-O1` | `default<O1>` | |
| `-O2` | `default<O2>` | default for release builds |
| `-O3` | `default<O3>` | more inlining, unrolling, vectorisation |
| `-Os` | `default<Os>` | |
| `-Oz` | `default<Oz>` | |
| `-Ofast` | `default<O3>` | adds the fast-math flags `reassoc nsz arcp contract afn` to floating-point operations. Not `nnan`/`ninf`: under them a NaN or infinity becomes poison, and a check branching on poison is undefined behaviour (D-040). **Does not** remove overflow, bounds or conversion checks (D-016) |

- **Inlining, constant propagation, DCE, loop optimisation,
  vectorisation, register allocation, instruction selection:** provided by
  the LLVM pipelines above; NovaC contributes good input (NIR check
  elimination, precise aliasing information: `noalias` on exclusive
  borrows, `readonly` on shared ones — only where the borrow rules make
  that sound).
- **CPU-specific selection:** `-mcpu=native|<name>`, `-mattr=`.
- **LTO:** `-flto=thin|full` emits bitcode objects; requires `ld.lld`
  (Phase 8).
- **PGO:** `-fprofile-generate` instruments via LLVM's `pgo-instr-gen`
  pass and requires LLVM's profile runtime at link time (an opt-in
  runtime dependency for instrumented builds only); `-fprofile-use=FILE`
  applies the merged profile (Phase 8).

No claim is made that any of this is "zero-cost" or competitive with C
until the Phase 8 benchmark suite shows it.

## 11. Driver (`ncc`)

```text
ncc [options] <file.nc>...

  -o <file>               output file
  -c | -S                 stop after object | assembly
  --emit=<k>              tokens|ast|typed|nir|llvm-ir|llvm-bc|asm|obj|exe
  --dump-tokens | --dump-ast | --dump-nir     (aliases of --emit to stdout)
  --check                 front end only: diagnostics, no output
  -O0 -O1 -O2 -O3 -Os -Oz -Ofast
  -g | -g0
  --target=<name>  -mcpu=<cpu>  -mattr=<features>
  --freestanding  --max-mode=<safe|controlled|kernel>
  --module-path=<dir>     (repeatable)
  --feature=<name>        (repeatable; for @cfg)
  --linker=<lld|ld|cc>  -T <script>  -L <dir>  -l <lib>
  -flto=<thin|full>  -fprofile-generate  -fprofile-use=<file>
  -fpanic=<verbose|minimal>  -fstack-probes  -fstack-protector  -ffunction-sections
  --diag-format=<text|json>  --color=<auto|always|never>  -Werror  --error-limit=<n>
  --version  --help
```

Exit status: 0 success; 1 compilation errors; 2 command-line errors;
3 internal compiler error.

The default action compiles and links an executable (`a.out`). The
default optimisation level is `-O0`. Without `-o`, `-c`, `-S` and
`--emit=llvm-bc` write `<stem>.o`, `.s` and `.bc` in the current
directory, and `--emit=llvm-ir` writes to standard output. Executables are
linked by the target's C compiler driver: `cc` for the host, and
`<triple>-gcc` when cross-compiling (`--linker=` overrides it). Options are introduced in the phase that
implements them; an option that exists in this list but is not yet
implemented is rejected with E9002, not ignored.

## 12. Testing infrastructure

| Directory | Kind | Runner | Phase |
| --- | --- | --- | --- |
| `tests/unit/` | C unit tests of `support/` and `lex/` | `build/tests/unit` binary | 2 |
| `tests/lex/` | `.nc` + expected `--dump-tokens` (`.tokens`) | `tests/run_tests.py` | 2 |
| `tests/parse/` | `.nc` + expected `--dump-ast` (`.ast`) | same | 2 |
| `tests/sema/` | valid programs, expected typed dump | same | 2 |
| `tests/diag/` | invalid programs with inline expectations | same | 2 |
| `tests/nir/` | `--emit=nir` golden files | `tests/run_tests.py` | 3 |
| `tests/run/` | programs compiled and executed natively at -O0…-Ofast and -g, and under QEMU for AArch64 and RISC-V; exit status or panic message and location checked | same | 3 |
| `tests/codegen/` | `--emit=llvm-ir` with ordered `// CHECK:` / `// CHECK-NOT:` patterns | same | 3 |
| `tests/abi/` | NovaC ↔ C cross-calls (NOVAC_ABI.md §12) | same | 4 |
| `tests/borrow/` | ownership/borrow positive and negative tests | same | 5 |
| `tests/regress/` | one test per fixed bug, named by issue | same | from 2 |
| `tests/driver/` | command-line behaviour (exit status, option errors) | `tests/run_tests.py` | 2 |
| `tests/selftest/` | fixtures the runner must reject (`--selftest`) | `tests/run_tests.py` | 2 |
| `tests/fuzz/` | libFuzzer harness and dictionary for the front end | `make fuzz-run` | 2 |
| `bench/` | benchmark workloads in NovaC and C, harness | `bench/run.py` | 8 |

**Inline diagnostic expectations** in `tests/diag/*.nc`:

```c
i32 f(u8 b) {
    i32 a = 1;
    return a + b;     // ERROR E4010
}
// ERROR@+1 E2001
i32 g( {
```

`// ERROR <code>` expects that code on the same line, `// ERROR@+N` /
`@-N` on a relative line; `WARNING` likewise. The test fails on any
missing or unexpected diagnostic.

Golden files are updated only with `make test UPDATE=1`, and the
resulting diff must be reviewed in the commit.

**Fuzzing** (Phase 2 exit criterion): `tests/fuzz/fuzz_frontend.c` runs
lexing, parsing and semantic analysis on arbitrary bytes, built with Clang,
libFuzzer, ASan and UBSan (`make fuzz-run FUZZ_SECONDS=1800`; requires
Clang's runtime libraries, e.g. Ubuntu's `libclang-rt-18-dev`). The corpus
is seeded from `tests/`; the dictionary holds the keywords and
punctuators.

**Build directories.** `build/<config>-<cc>/` (e.g. `build/debug-gcc/`), so
GCC, Clang, release and sanitizer builds coexist. `make test-all` runs the
sanitizer test suite with both compilers.

**Benchmarks** (Phase 8): each workload has equivalent NovaC and C
sources; C is built with GCC and Clang at matching optimisation levels.
Measurements (execution time with repetitions and variance, binary size,
peak RSS, compile time) are produced by the harness from a clean build,
with the machine, compiler versions and commit recorded. **No result is
published that the harness did not produce.**

## 13. Subsystem interfaces

| Subsystem | Public header | Provides | Depends on |
| --- | --- | --- | --- |
| support | `support/*.h` | arenas, interning, vectors, source files, spans | libc only |
| session | `session/session.h` | `nc_session`: source map, string table, diagnostics, arena | support, diag |
| diag | `diag/diag.h` | `nc_diag_error(session, span, code, fmt, ...)`, rendering, counts | support |
| lex | `lex/lexer.h` | `nc_lex_file(session, file) -> token array` | support, diag |
| ast | `ast/ast.h` | node types, constructors, `nc_ast_dump` | support |
| parse | `parse/parser.h` | `nc_parse(session, tokens) -> nc_module_ast*` | lex, ast, diag |
| sema | `sema/sema.h` | `nc_sema_check(session, module) -> bool` | ast, diag |
| nir | `nir/nir.h` | NIR types, `nc_lower_module`, `nc_nir_verify`, dump | sema |
| analysis | `analysis/analysis.h` | `nc_analyze(session, nir_module) -> bool` | nir, diag |
| target | `target/target.h` | target lookup, data layout, ABI classification | support |
| codegen | `codegen/codegen.h` | `nc_codegen(session, nir_module, target) -> LLVM module`, emission | nir, target, LLVM-C |
| driver | — | `main`, option parsing, orchestration, linker invocation | everything |

Dependencies point only downward in this table (plus `diag` and
`support`); a cycle is a design error.

## 14. Compiler performance

Compile time is measured from Phase 3 by the benchmark harness (lines per
second for the front end; total time per optimisation level). Targets are
set once measurements exist, not before.
