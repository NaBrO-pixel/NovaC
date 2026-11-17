/* Command-line options (NOVAC_COMPILER_ARCHITECTURE.md §11). Options that
 * are documented but belong to later phases are recognised and rejected
 * with E9002 rather than ignored. */
#ifndef NC_DRIVER_OPTIONS_H
#define NC_DRIVER_OPTIONS_H

#include "diag/diag.h"

typedef enum nc_action {
    NC_ACT_COMPILE, /* default: code generation (not implemented yet) */
    NC_ACT_TOKENS,  /* --dump-tokens, --emit=tokens */
    NC_ACT_AST,     /* --dump-ast, --emit=ast */
    NC_ACT_TYPED,   /* --emit=typed */
    NC_ACT_CHECK,   /* --check */
} nc_action;

typedef struct nc_options {
    nc_action action;
    const char **inputs;
    int ninputs;
    bool help;
    bool version;
    bool werror;
    unsigned error_limit;
} nc_options;

/* Parses argv. Diagnostics go to `d`. Returns false on a command-line error
 * (exit status 2). `o->inputs` must be released with free(). */
bool nc_parse_options(int argc, char **argv, nc_options *o, nc_diag *d);

void nc_print_help(FILE *out);

#endif
