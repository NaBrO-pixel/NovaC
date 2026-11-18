/* Code generation from NIR through LLVM (NOVAC_COMPILER_ARCHITECTURE.md §9.4).
 *
 * Contract with the environment (NOVAC_ABI.md §9): generated code calls
 * only `__nova_panic` (and `memcpy`-family functions LLVM may introduce).
 * For hosted targets each module also carries a weak default
 * `__nova_panic` that prints the panic to standard error with `dprintf`
 * and calls `abort` (D-039); a program can override it with a strong
 * definition. */
#ifndef NC_CODEGEN_CODEGEN_H
#define NC_CODEGEN_CODEGEN_H

#include "nir/nir.h"
#include "target/target.h"

typedef enum nc_output_kind {
    NC_OUT_LLVM_IR,
    NC_OUT_LLVM_BC,
    NC_OUT_ASM,
    NC_OUT_OBJ,
} nc_output_kind;

typedef struct nc_codegen_options {
    const nc_target *target;
    const char *cpu;      /* NULL: the target's baseline */
    const char *features; /* NULL: the target's baseline */
    char opt;             /* '0' '1' '2' '3' 's' 'z' */
    bool fast_math;       /* -Ofast (floating point only; checks are kept, D-016) */
    bool debug;           /* -g: DWARF line tables */
    bool panic_minimal;   /* -fpanic=minimal: no message or file strings */
} nc_codegen_options;

/* Generates `m` and writes it to `path` (NULL writes LLVM IR to stdout).
 * Reports E8001 on LLVM failure and E8005 for a malformed `main`. */
bool nc_codegen(nc_session *s, const nc_nir_module *m, const nc_codegen_options *o, nc_output_kind kind,
                const char *path);

/* One-time LLVM target initialisation (idempotent). */
void nc_codegen_init(void);

/* "18.1.3": the LLVM version ncc is linked against. */
void nc_codegen_llvm_version(char *buf, size_t n);

#endif
