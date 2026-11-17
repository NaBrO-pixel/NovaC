# NovarisOS Integration and Phase 0 Report

Status: **Phase 0 findings**, recorded 2026-11-17. Facts here describe
the inspected commits. Re-check them when those repositories move.

## 1. Scope

Phase 0 inspected the NovaC repository and the two existing ecosystem
repositories (read-only; nothing outside NovaC was modified), to establish
what NovaC must integrate with and which assumptions in the project brief
hold.

## 2. Repositories inspected

| Repository | Commit | Contents | Languages / build | License |
| --- | --- | --- | --- | --- |
| NaBrO-pixel/NovaC (branch `dev`) | `280be06` | one file, `Liscense.md` | — | PolyForm Noncommercial 1.0.0 |
| NaBrO-pixel/NovarisOS (`main`) | `7656729` | 372 files: kernel, boot code, userland tests, Wine integration, tools | GNU C99 (`-std=gnu99`), NASM, GNU Make, Python 3; GCC, GNU ld, GRUB, QEMU | PolyForm Noncommercial 1.0.0 |
| NaBrO-pixel/LibNovaC (`main`) | `244ed54` | 134 files: C library v0.3 | C11, GNU Make, Python 3 generators; GCC, `ar`, `ld` | LGPL-2.1 |

### 2.1 NovaC

The repository held only the license, committed under the misspelled name
`Liscense.md`. There was no code, build system, or documentation, so
there was nothing to preserve beyond that file and its history. The file
is renamed to `LICENSE` (content unchanged, history preserved by
`git mv`) because the brief requires `LICENSE` and the misspelling
prevents GitHub from detecting the license.

### 2.2 NovarisOS

A from-scratch x86 operating system, recorded in a 11,000-line
`ROADMAP.md` (Milestone 101 at the inspected commit).

- **Two kernels:** the original 32-bit i386 kernel (`Makefile`,
  `linker.ld`) and the 64-bit kernel (`Makefile.amd64`, `linker64.ld`),
  where current work (Wine, Chromium) happens.
- **No other architectures.** There is no AArch64 or RISC-V code.
- **Userspace ABI:** the 64-bit kernel implements the **Linux x86-64
  system-call ABI** (`include/syscall64.h` uses Linux's numbers
  deliberately), loads ELF64 static and dynamic executables (real
  `ld-linux`), supports `clone`, `futex`, signals, `fork`/`exec`, sockets.
- **Kernel compilation constraints** (`Makefile.amd64`):
  `-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
  -m64 -mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx`, with the
  rationale for each recorded in the Makefile.
- **Kernel layout** (`linker64.ld`): entry `_start`; `.boot.text` /
  `.boot.data` linked at physical addresses (VMA = LMA) from 1 MiB;
  everything else at `KERNEL_VMA = 0xFFFFFFFF80000000`; output sections
  collect only `*(.text)`, `*(.rodata)`, `*(.data)`, `*(COMMON) *(.bss)`;
  `.eh_frame`, `.comment`, `.note*` discarded.
- **Boot:** GRUB Multiboot 1, entered in 32-bit protected mode; the kernel
  switches to long mode itself (`boot/boot64.s`).
- **Kernel already provides** `memcpy`-family functions (`kernel/kstring.c`)
  and its own heap (`kheap64.c`).
- **Testing culture:** QEMU boot tests driven over the serial port; host-
  vs-guest transcript comparison of the same static binary.
- **LibNovaC integration:** `Makefile.amd64` builds LibNovaC from
  `userland/libnovac`, checks its syscall numbers against
  `include/syscall64.h`, embeds its target test in the kernel and requires
  host and guest transcripts to match. It names this with `NOVAC_DIR`,
  `NOVAC_BUILD`, `NOVAC_TEST`, `NOVAC_CHILD`, `novac-force`,
  `novac64_blob`.

### 2.3 LibNovaC

A freestanding C11 library for static x86-64 programs on NovarisOS
(version 0.3): stdio with floating-point formatting, exact `strtod`,
math, setjmp, signals, processes, sockets, time.

- **x86-64 only**, static linking only, its own `crt0.o` (`_start` calls
  `int main(int argc, char **argv, char **envp)` and passes the result to
  `exit`).
- **No threads:** `errno` and stdio state are process-wide.
- **OS boundary:** the portable core reaches the kernel through four hooks
  in `src/internal/sys.h` (`novac_sys_map`, `novac_sys_unmap`,
  `novac_sys_write_stderr`, `novac_sys_exit`); everything else goes
  through `src/internal/syscall.h`. This is internal to LibNovaC — it is
  not a public OS abstraction layer.
- **Namespace:** `include/novac/*.h`, `NOVAC_*` macros, `novac_*`
  symbols, test files `novac_target_test.c`, `novac_child.c`, and the Make
  variable `NOVAC_CC` meaning *the C compiler used to build LibNovaC*.
- Builds are compiled with `-ffp-contract=off`; NovaC's floating-point
  semantics match (NOVAC_TYPE_SYSTEM.md §2.3).

### 2.4 libNova-Sys, Nova Runtime, Nova Linker

**None of these exist** in any repository accessible to this project.
Consequences are recorded in D-021 (std binds to the C library first),
NOVAC_ABI.md §8 (system linkers) and §9 (no runtime needed), and
Q-02 (libNova-Sys ownership).

## 3. Integration requirements

| ID | Requirement | Where addressed |
| --- | --- | --- |
| IR-1 | NovaC userspace programs for NovarisOS are static, non-PIE ELF64 executables using the Linux x86-64 syscall ABI | NOVAC_ABI.md §10.1 |
| IR-2 | Hosted NovaC programs on NovarisOS link against LibNovaC's `crt0.o` and `libNovaC.a` and export a C `main(argc, argv, envp)` | NOVAC_ABI.md §7, §10.1 |
| IR-3 | NovaC kernel objects must link into the existing kernel: code model `kernel`, no red zone, no SSE/MMX, no PIC, no unwind tables, no per-function sections, C ABI at the boundary | NOVAC_ABI.md §10.2 |
| IR-4 | The kernel's boot path (32-bit, Multiboot) stays in assembly; NovaC has no 32-bit target | D-023, NOVAC_ABI.md §10.2 |
| IR-5 | Kernel NovaC code must not use floating point | E7010, NOVAC_TYPE_SYSTEM.md §2.3 |
| IR-6 | NovaC must not need any runtime beyond `mem*` and a panic entry point, both of which the kernel can provide | NOVAC_ABI.md §9 |
| IR-7 | NovaC must not depend on LibNovaC for freestanding/kernel code | D-021, D-022 |
| IR-8 | NovaC must not use LibNovaC's `NOVAC_*` / `novac_*` / `<novac/>` names | D-020 |
| IR-9 | Testing NovaC on NovarisOS follows the established host-vs-guest transcript comparison | NOVAC_ABI.md §10.1 |
| IR-10 | NovaC's build tooling should match the ecosystem: C, GNU Make, Python 3 | D-001, D-002 |
| IR-11 | `std.thread` cannot be offered on NovarisOS until a threaded C library or libNova-Sys exists | NOVAC_ABI.md §10.1 |

## 4. Contradictions and unrealistic assumptions in the brief

Each item is resolved by a decision record or recorded as an open
question.

1. **Cross-platform targets vs. NovarisOS.** The brief targets x86-64,
   AArch64 and RISC-V as "the primary language for NovarisOS", but
   NovarisOS exists only for x86-64. AArch64/RISC-V support is limited to
   Linux-hosted and freestanding targets (D-023).
2. **`#include` vs. the module system.** The first example uses
   `#include`; the module section requires imports, separate compilation
   and interface checking, which a textual preprocessor undermines.
   Resolved for modules (D-004).
3. **Proposed syntax mixes styles:** `optional int` / `atomic int`
   (qualifier style) vs. `slice<int>` (generic style); `borrow int*`
   conflates raw pointers and borrows. Resolved (D-006, D-008).
4. **Unqualified use after `import math;`** conflicts with collision-free
   separate compilation. Resolved with explicit unqualified imports
   (D-019).
5. **`@kernel @no_runtime` on a function** conflates a permission set,
   an environment and an entry point. Resolved (D-012, D-022).
6. **Pipeline order:** ownership analysis before IR generation does not
   support non-lexical borrows. Reordered (D-013).
7. **Phase gaps:** type checking is in no phase; structs/pointers are
   needed by Phase 4 but not introduced; raw pointers before `unsafe`
   would make unchecked dereference compile in safe code. Adjusted
   (D-032).
8. **"Configurable safety checks"** vs. "unsafe must never silently
   disable unrelated checks". Reconciled: only defined-behaviour
   alternatives are configurable (D-016).
9. **`printf`-style `nova_print("%d")`** is type-unsafe in a language
   promising memory safety. Replaced by compile-time-checked formatting
   (Phase 6).
10. **Performance assumptions.** NovaC adds checks C lacks (overflow,
    bounds). Equal-to-C performance is a hypothesis to measure, not a
    property to claim (D-009, D-036).
11. **Ecosystem components assumed to exist** (libNova-Sys, Nova Runtime,
    Nova Linker) do not. NovaC is designed not to need them (D-021,
    NOVAC_ABI.md §8, §9).
12. **"Nova Runtime" for features that genuinely require runtime
    support:** with abort-on-panic and no async, NovaC needs none in
    version 1. Thread-local storage and PGO profiles use existing
    (ELF / LLVM) mechanisms.
13. **Licensing:** a noncommercial license on the compiler is coherent,
    but any NovaC library code linked into user programs would make every
    compiled program noncommercial (Q-01).
14. **Scope:** the nine phases describe multiple years of work by a
    compiler team; schedule is managed by small phases with exit
    criteria rather than dates.

## 5. Technical risks

| ID | Risk | Likelihood / impact | Mitigation |
| --- | --- | --- | --- |
| R-01 | C ABI lowering for aggregates must be implemented by NovaC (LLVM does not do it); subtle mismatches corrupt data silently | High / High | Per-target classifiers ported from the psABI documents; generated cross-call tests against Clang; E7032 until verified (NOVAC_ABI.md §5) |
| R-02 | Borrow checker unsoundness or excessive false rejections | High / High | Conservative first version (Phase 5a: elision only, no references in structs); NIR-based NLL; negative test for every rule; acceptance corpus |
| R-03 | A borrow checker and monomorphising generics written in C are large and memory-unsafe themselves | Medium / High | Arenas, sanitizer builds in `make test`, fuzzing from Phase 2 |
| R-04 | LLVM-C API gaps or LLVM version churn | Medium / Medium | Minimum LLVM 18; C++ shim confined to `codegen/shim/` (D-001); upgrades by decision record |
| R-05 | Overflow and bounds checks cost performance relative to C | Medium / Medium | NIR and LLVM check elimination; measured in Phase 8; no claims until measured |
| R-06 | Implicit Copy (D-014) surprises users when a type gains a destructor | Low / Low | Produces compile errors, never silent change; provisional decision revisited before Phase 5 |
| R-07 | No preprocessor raises the cost of adopting C libraries | Medium / Medium | `extern "C"` declarations; planned header binding generator (F-803) |
| R-08 | Cross-target execution testing needs QEMU user-mode, not installed in the reference environment | High / Medium | Phase 3 prerequisite; until then cross output is checked by disassembly and stated as such |
| R-09 | LLVM development headers are not installed in the reference environment | Certain / Low | Phase 2 does not need LLVM; install `llvm-18-dev` before Phase 3 |
| R-10 | LibNovaC has no threads; libNova-Sys does not exist | Certain / Medium | `std` binds to the C library behind `std.sys`; threads on NovarisOS wait (IR-11) |
| R-11 | Kernel linker-script incompatibilities (orphan sections, unwind sections) | Medium / Medium | No per-function sections, no unwind tables by default; Phase 7 demonstration links into the real kernel |
| R-12 | Name confusion between NovaC and LibNovaC in builds and documentation | High / Low | D-020; Q-04 recommends renaming LibNovaC's build variables |
| R-13 | License restricts users of compiled output | Unknown / High | Q-01, to be answered before Phase 3 produces linked programs |

## 6. Reference environment (Phase 0/1)

Recorded so later phases know what was verified where:

- Linux x86-64; GCC 13.3; Clang/LLVM 18.1.3 (targets include X86,
  AArch64, RISCV); `ld.lld` available; CMake 3.28; Python 3.
- `llvm-c` headers **not** installed; QEMU **not** installed.
