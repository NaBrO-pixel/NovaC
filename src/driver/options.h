/* Command-line options (NOVAC_COMPILER_ARCHITECTURE.md §11). Options that
 * are documented but belong to later phases are recognised and rejected
 * with E9002 rather than ignored. */
#ifndef NC_DRIVER_OPTIONS_H
#define NC_DRIVER_OPTIONS_H

#include "diag/diag.h"
#include "target/target.h"

typedef enum nc_action {
    NC_ACT_EXE,     /* default: compile and link an executable */
    NC_ACT_TOKENS,  /* --dump-tokens, --emit=tokens */
    NC_ACT_AST,     /* --dump-ast, --emit=ast */
    NC_ACT_TYPED,   /* --emit=typed */
    NC_ACT_CHECK,   /* --check: every compile-time check, no output */
    NC_ACT_NIR,     /* --dump-nir, --emit=nir */
    NC_ACT_LLVM_IR, /* --emit=llvm-ir */
    NC_ACT_LLVM_BC, /* --emit=llvm-bc */
    NC_ACT_ASM,     /* -S, --emit=asm */
    NC_ACT_OBJ,     /* -c, --emit=obj */
} nc_action;

typedef struct nc_options {
    nc_action action;
    const char **inputs;
    int ninputs;
    const char *output;   /* -o, or NULL */
    char opt;             /* '0' '1' '2' '3' 's' 'z' */
    bool fast_math;       /* -Ofast */
    bool debug;           /* -g */
    bool panic_minimal;   /* -fpanic=minimal */
    const nc_target *target;
    const char *cpu;      /* -mcpu= */
    const char *features; /* -mattr= */
    const char *linker;   /* --linker= */
    bool help;
    bool version;
    bool werror;
    unsigned error_limit;
    unsigned max_mode;    /* --max-mode=: 0 safe, 1 controlled, 2 kernel (default: no limit) */
} nc_options;

/* Parses argv. Diagnostics go to `d`. Returns false on a command-line error
 * (exit status 2). `o->inputs` must be released with free(). */
bool nc_parse_options(int argc, char **argv, nc_options *o, nc_diag *d);

void nc_print_help(FILE *out);

/* Does this action need NIR (lowering and the NIR analyses)? */
bool nc_action_needs_nir(nc_action a);

#endif
