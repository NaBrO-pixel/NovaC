#include "driver/options.h"

#include <stdlib.h>
#include <string.h>

/* Documented options that later phases implement. `takes_arg`: the value
 * is the next argument when not joined. */
static const struct {
    const char *name;
    bool prefix;    /* matches any argument starting with `name` */
    bool takes_arg; /* consumes the following argument */
    int phase;
} future_options[] = {
    {"-o", false, true, 3},
    {"-c", false, false, 3},
    {"-S", false, false, 3},
    {"--dump-nir", false, false, 3},
    {"-O", true, false, 3},
    {"-g", false, false, 3},
    {"-g0", false, false, 3},
    {"--target=", true, false, 3},
    {"-mcpu=", true, false, 3},
    {"-mattr=", true, false, 3},
    {"--freestanding", false, false, 7},
    {"--max-mode=", true, false, 4},
    {"--module-path=", true, false, 6},
    {"--feature=", true, false, 6},
    {"--linker=", true, false, 3},
    {"-T", false, true, 7},
    {"-L", true, false, 4},
    {"-l", true, false, 4},
    {"-flto=", true, false, 8},
    {"-fprofile-generate", false, false, 8},
    {"-fprofile-use=", true, false, 8},
    {"-fpanic=", true, false, 3},
    {"-fstack-probes", false, false, 7},
    {"-fstack-protector", false, false, 7},
    {"-ffunction-sections", false, false, 7},
    {"--diag-format=", true, false, 9},
    {"--color=", true, false, 9},
};

void nc_print_help(FILE *out)
{
    fputs("usage: ncc [options] <file.nc>...\n"
          "\n"
          "Implemented in this version (Phase 2 front end):\n"
          "  --check               lex, parse, resolve and type-check; report diagnostics\n"
          "  --dump-tokens         print the token stream (also --emit=tokens)\n"
          "  --dump-ast            print the syntax tree (also --emit=ast)\n"
          "  --emit=typed          print the type-checked syntax tree\n"
          "  -Werror               treat warnings as errors\n"
          "  --error-limit=<n>     stop after n errors (default 50, 0 = no limit)\n"
          "  --help, --version\n"
          "\n"
          "Code generation, linking and the other documented options are not\n"
          "implemented yet; they are rejected with E9002. See docs/FEATURE_STATUS.md.\n",
          out);
}

static bool parse_uint(const char *s, unsigned *out)
{
    if (!*s) return false;
    unsigned long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (unsigned long)(*s - '0');
        if (v > 1000000) return false;
    }
    *out = (unsigned)v;
    return true;
}

static void set_action(nc_options *o, nc_action a, bool *conflict)
{
    if (o->action != NC_ACT_COMPILE && o->action != a) *conflict = true;
    o->action = a;
}

bool nc_parse_options(int argc, char **argv, nc_options *o, nc_diag *d)
{
    memset(o, 0, sizeof *o);
    o->error_limit = 50;
    o->inputs = calloc((size_t)(argc > 0 ? argc : 1), sizeof *o->inputs);
    if (!o->inputs) return false;
    bool ok = true;
    bool conflict = false;
    bool only_inputs = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (only_inputs || a[0] != '-' || a[1] == '\0') {
            o->inputs[o->ninputs++] = a;
            continue;
        }
        if (strcmp(a, "--") == 0) {
            only_inputs = true;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            o->help = true;
        } else if (strcmp(a, "--version") == 0) {
            o->version = true;
        } else if (strcmp(a, "--check") == 0) {
            set_action(o, NC_ACT_CHECK, &conflict);
        } else if (strcmp(a, "--dump-tokens") == 0) {
            set_action(o, NC_ACT_TOKENS, &conflict);
        } else if (strcmp(a, "--dump-ast") == 0) {
            set_action(o, NC_ACT_AST, &conflict);
        } else if (strncmp(a, "--emit=", 7) == 0) {
            const char *k = a + 7;
            if (strcmp(k, "tokens") == 0) set_action(o, NC_ACT_TOKENS, &conflict);
            else if (strcmp(k, "ast") == 0) set_action(o, NC_ACT_AST, &conflict);
            else if (strcmp(k, "typed") == 0) set_action(o, NC_ACT_TYPED, &conflict);
            else if (strcmp(k, "nir") == 0 || strcmp(k, "llvm-ir") == 0 || strcmp(k, "llvm-bc") == 0 ||
                     strcmp(k, "asm") == 0 || strcmp(k, "obj") == 0 || strcmp(k, "exe") == 0) {
                nc_diag_error(d, NC_NO_SPAN, "E9002",
                              "option '%s' is not implemented in this version (Phase 3)", a);
                ok = false;
            } else {
                nc_diag_error(d, NC_NO_SPAN, "E0003", "invalid value '%s' for --emit", k);
                ok = false;
            }
        } else if (strcmp(a, "-Werror") == 0) {
            o->werror = true;
        } else if (strncmp(a, "--error-limit=", 14) == 0) {
            if (!parse_uint(a + 14, &o->error_limit)) {
                nc_diag_error(d, NC_NO_SPAN, "E0003", "invalid value '%s' for --error-limit", a + 14);
                ok = false;
            }
        } else {
            bool known = false;
            for (size_t k = 0; k < sizeof future_options / sizeof future_options[0]; k++) {
                const char *n = future_options[k].name;
                bool match = future_options[k].prefix ? strncmp(a, n, strlen(n)) == 0 : strcmp(a, n) == 0;
                if (!match) continue;
                known = true;
                nc_diag_error(d, NC_NO_SPAN, "E9002",
                              "option '%s' is not implemented in this version (Phase %d)", a,
                              future_options[k].phase);
                if (future_options[k].takes_arg && i + 1 < argc) i++;
                break;
            }
            if (!known) nc_diag_error(d, NC_NO_SPAN, "E0002", "unknown option '%s'", a);
            ok = false;
        }
    }
    if (conflict) {
        nc_diag_error(d, NC_NO_SPAN, "E0003", "conflicting output options; give only one of "
                                             "--check, --dump-tokens, --dump-ast, --emit=");
        ok = false;
    }
    if (ok && !o->help && !o->version && o->ninputs == 0) {
        nc_diag_error(d, NC_NO_SPAN, "E0004", "no input files");
        ok = false;
    }
    return ok;
}
