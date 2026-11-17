# NovaC

NovaC is a C-derived systems programming language being designed for the
NovarisOS ecosystem. It aims to combine C-like syntax and hardware control
with compile-time ownership and borrow checking, LLVM-based native code
generation, C ABI interoperability, and freestanding compilation for
kernels and drivers. It has no garbage collector and requires no runtime.

> **Project status: compiler front end (Phase 2 complete).**
> `ncc` can lex, parse and type-check programs written in a first subset
> of the language (functions, primitive types, variables, arithmetic,
> control flow) and report precise diagnostics. **It cannot generate code
> yet**: no NovaC program can be compiled or run. Most of the language in
> the documents below is designed but not implemented;
> [docs/FEATURE_STATUS.md](docs/FEATURE_STATUS.md) is the authoritative
> record of what works.

## What exists today

| Implemented | Where |
| --- | --- |
| Phase 0 investigation of NovarisOS and LibNovaC | [docs/NOVARISOS_INTEGRATION.md](docs/NOVARISOS_INTEGRATION.md) |
| Phase 1 language specification | [docs/](docs/) |
| Phase 2 front end: lexer, parser, name resolution, type checker, diagnostics | [src/](src/), tests in [tests/](tests/) |
| Documentation consistency checker | `tools/check_docs.py` (`make check-docs`) |

Not implemented yet: code generation (Phase 3), C interoperability,
structs, pointers and `unsafe` (Phase 4), ownership and borrowing
(Phase 5), modules, generics, slices and the libraries (Phase 6), kernel
features (Phase 7), concurrency and optimisation (Phase 8). The front end
recognises these constructs and rejects them with error E9001, naming the
phase that will implement them.

Next: **Phase 3**, native code generation through LLVM. See
[docs/NOVAC_ROADMAP.md](docs/NOVAC_ROADMAP.md).

## Trying the front end

```bash
make
```

```bash
build/release-cc/ncc --check examples/phase2.nc
```

```bash
build/release-cc/ncc --emit=typed examples/phase2.nc
```

`--dump-tokens` and `--dump-ast` show the earlier stages. A program with
mistakes gets diagnostics such as:

```text
demo.nc:3:12: error[E4010]: operands of '+' have different types 'i32' and 'u8'
   3 |     return a + b;
     |            ^~~~~
      = help: convert the other operand explicitly with `i32(...)`; operands are not converted implicitly (D-037)
```

## A taste of the designed language

*Not yet compilable: slices, `import` and formatting belong to later phases.*

```c
module demo;
import std.io;

u32 sum(slice<const u32> xs) {          // shared borrow; indexing is bounds-checked
    u32 total = 0;
    for (usize i = 0; i < xs.len; i++) {
        total += xs[i];                  // panics on overflow, never undefined behaviour
    }
    return total;
}

int main(void) {
    u32[4] values = { 1, 2, 3, 4 };
    io.println("sum = {}", sum(&values)); // format string checked at compile time
    return 0;
}
```

## Documentation

| Document | Contents |
| --- | --- |
| [NOVAC_SPEC.md](docs/NOVAC_SPEC.md) | language overview, declarations, statements, attributes, examples |
| [NOVAC_GRAMMAR.md](docs/NOVAC_GRAMMAR.md) | normative syntax (EBNF) |
| [NOVAC_TYPE_SYSTEM.md](docs/NOVAC_TYPE_SYSTEM.md) | types, integer semantics, conversions, generics |
| [NOVAC_MEMORY_MODEL.md](docs/NOVAC_MEMORY_MODEL.md) | safe / controlled / kernel modes, ownership, borrowing, unsafe, concurrency |
| [NOVAC_ABI.md](docs/NOVAC_ABI.md) | targets, data layout, calling conventions, C interop, NovarisOS specifics |
| [NOVAC_COMPILER_ARCHITECTURE.md](docs/NOVAC_COMPILER_ARCHITECTURE.md) | compiler pipeline and subsystems |
| [NOVAC_ROADMAP.md](docs/NOVAC_ROADMAP.md) | phases, exit criteria, Phase 2 plan |
| [NOVAC_DECISIONS.md](docs/NOVAC_DECISIONS.md) | architectural decision records, open questions |
| [FEATURE_STATUS.md](docs/FEATURE_STATUS.md) | implemented / planned / open / unsupported |
| [NOVARISOS_INTEGRATION.md](docs/NOVARISOS_INTEGRATION.md) | Phase 0 report, integration requirements, risks |

## The NovarisOS ecosystem

NovaC is one of several separate projects:

- **NovaC** (this repository) — the language, its compiler (`ncc`, planned)
  and its own libraries (`core`, `alloc`, `std`, planned).
- **[LibNovaC](https://github.com/NaBrO-pixel/LibNovaC)** — the C standard
  library for NovarisOS. NovaC is not LibNovaC; hosted NovaC programs on
  NovarisOS will call LibNovaC through the C ABI.
- **[NovarisOS](https://github.com/NaBrO-pixel/NovarisOS)** — the
  operating system (x86-64).
- **libNova-Sys**, **Nova Runtime**, **Nova Linker** — planned ecosystem
  components that do not exist yet. NovaC is designed not to depend on
  them.

## Building and testing

Requirements: a C11 compiler (GCC ≥ 13 or Clang ≥ 18), GNU Make, Python 3.
LLVM is not needed until Phase 3.

| Command | What it does |
| --- | --- |
| `make` | build `build/release-<cc>/ncc` |
| `make test` | build with AddressSanitizer and UndefinedBehaviorSanitizer, run unit tests, all compiler tests, the runner self-test and the documentation checks |
| `make test-all` | `make test` with both GCC and Clang |
| `make fuzz-run FUZZ_SECONDS=1800` | fuzz the front end (Clang with libFuzzer runtime, e.g. `libclang-rt-18-dev`) |
| `make check-docs` | documentation consistency checks only |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Changes are recorded in
[CHANGELOG.md](CHANGELOG.md).

## License

PolyForm Noncommercial License 1.0.0 — see [LICENSE](LICENSE). How this
applies to programs compiled with NovaC is an open question (Q-01 in
[NOVAC_DECISIONS.md](docs/NOVAC_DECISIONS.md#outstanding-questions)).
