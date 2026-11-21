#include "driver/options.h"

#include <stdlib.h>
#include <string.h>

/* Documented options that later phases implement. `takes_arg`: the value
 * is the next argument. */
static const struct {
    const char *name;
    bool prefix;    /* matches any argument starting with `name` */
    bool takes_arg; /* consumes the following argument */
    int phase;
} future_options[] = {
    {"--freestanding", false, false, 7},
    {"--module-path=", true, false, 6},
    {"--feature=", true, false, 6},
    {"-T", false, true, 7},
    {"-flto=", true, false, 8},
    {"-fprofile-generate", false, false, 8},
    {"-fprofile-use=", true, false, 8},
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
          "Output (default: link an executable, a.out):\n"
          "  -o <file>             output file\n"
          "  -c                    compile to an object file (also --emit=obj)\n"
          "  -S                    compile to assembly (also --emit=asm)\n"
          "  --emit=<kind>         tokens | ast | typed | nir | llvm-ir | llvm-bc | asm | obj | exe\n"
          "  --check               run every compile-time check; produce no output\n"
          "  --dump-tokens, --dump-ast, --dump-nir   aliases of --emit to stdout\n"
          "\n"
          "Code generation:\n"
          "  -O0 -O1 -O2 -O3 -Os -Oz -Ofast   optimisation (default -O0); -Ofast relaxes\n"
          "                        floating point only and keeps every safety check\n"
          "  -g, -g0               DWARF line tables on / off\n"
          "  --target=<name>       x86_64-linux-gnu (default), aarch64-linux-gnu, riscv64-linux-gnu,\n"
          "                        x86_64-novaris (static, against LibNovaC)\n"
          "  -mcpu=<cpu>, -mattr=<features>   CPU and features beyond the baseline\n"
          "  --linker=<command>    linker driver (default: cc, or <triple>-gcc when cross-compiling)\n"
          "  -l<lib>, -L<dir>, file.o, file.a   passed to the linker, in order\n"
          "  --libnovac=<dir>      LibNovaC build directory (crt0.o, libNovaC.a) for x86_64-novaris;\n"
          "                        default: $NOVAC_LIBNOVAC\n"
          "  -fpanic=verbose|minimal   panic messages with or without text and file name\n"
          "  --max-mode=safe|controlled|kernel   reject modules with a higher @mode\n"
          "\n"
          "Diagnostics:\n"
          "  -Werror               treat warnings as errors\n"
          "  --error-limit=<n>     stop after n errors (default 50, 0 = no limit)\n"
          "  --help, --version\n"
          "\n"
          "Documented options of later phases are rejected with E9002; see\n"
          "docs/FEATURE_STATUS.md.\n",
          out);
}

bool nc_action_needs_nir(nc_action a)
{
    return a != NC_ACT_TOKENS && a != NC_ACT_AST && a != NC_ACT_TYPED;
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

static void set_action(nc_options *o, nc_action a, bool *explicit_action, bool *conflict)
{
    if (*explicit_action && o->action != a) *conflict = true;
    o->action = a;
    *explicit_action = true;
}

static bool emit_kind(const char *k, nc_action *a)
{
    static const struct {
        const char *name;
        nc_action action;
    } kinds[] = {
        {"tokens", NC_ACT_TOKENS}, {"ast", NC_ACT_AST}, {"typed", NC_ACT_TYPED}, {"nir", NC_ACT_NIR},
        {"llvm-ir", NC_ACT_LLVM_IR}, {"llvm-bc", NC_ACT_LLVM_BC}, {"asm", NC_ACT_ASM}, {"obj", NC_ACT_OBJ},
        {"exe", NC_ACT_EXE},
    };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (strcmp(kinds[i].name, k) == 0) {
            *a = kinds[i].action;
            return true;
        }
    return false;
}

static bool is_link_input(const char *a)
{
    const char *dot = strrchr(a, '.');
    return dot && (strcmp(dot, ".o") == 0 || strcmp(dot, ".a") == 0 || strcmp(dot, ".so") == 0);
}

bool nc_parse_options(int argc, char **argv, nc_options *o, nc_diag *d)
{
    memset(o, 0, sizeof *o);
    o->error_limit = 50;
    o->max_mode = 2;
    o->opt = '0';
    o->target = nc_target_host();
    o->inputs = calloc((size_t)(argc > 0 ? argc : 1), sizeof *o->inputs);
    o->link_args = calloc((size_t)(argc > 0 ? argc : 1), sizeof *o->link_args);
    if (!o->inputs || !o->link_args) return false;
    bool ok = true;
    bool conflict = false;
    bool explicit_action = false;
    bool only_inputs = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (only_inputs || a[0] != '-' || a[1] == '\0') {
            if (is_link_input(a)) o->link_args[o->nlink_args++] = a; /* objects and libraries go to the linker */
            else o->inputs[o->ninputs++] = a;
            continue;
        }
        if ((a[1] == 'l' || a[1] == 'L') && a[2] != '\0') {
            o->link_args[o->nlink_args++] = a;
            continue;
        }
        nc_action act;
        if (strcmp(a, "--") == 0) {
            only_inputs = true;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            o->help = true;
        } else if (strcmp(a, "--version") == 0) {
            o->version = true;
        } else if (strcmp(a, "--check") == 0) {
            set_action(o, NC_ACT_CHECK, &explicit_action, &conflict);
        } else if (strcmp(a, "--dump-tokens") == 0) {
            set_action(o, NC_ACT_TOKENS, &explicit_action, &conflict);
        } else if (strcmp(a, "--dump-ast") == 0) {
            set_action(o, NC_ACT_AST, &explicit_action, &conflict);
        } else if (strcmp(a, "--dump-nir") == 0) {
            set_action(o, NC_ACT_NIR, &explicit_action, &conflict);
        } else if (strcmp(a, "-c") == 0) {
            set_action(o, NC_ACT_OBJ, &explicit_action, &conflict);
        } else if (strcmp(a, "-S") == 0) {
            set_action(o, NC_ACT_ASM, &explicit_action, &conflict);
        } else if (strncmp(a, "--emit=", 7) == 0) {
            if (emit_kind(a + 7, &act)) {
                set_action(o, act, &explicit_action, &conflict);
            } else {
                nc_diag_error(d, NC_NO_SPAN, "E0003", "invalid value '%s' for --emit", a + 7);
                ok = false;
            }
        } else if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) {
                nc_diag_error(d, NC_NO_SPAN, "E0003", "option '-o' needs a file name");
                ok = false;
            } else {
                o->output = argv[++i];
            }
        } else if (a[1] == 'O') {
            const char *lvl = a + 2;
            if (strcmp(lvl, "") == 0) o->opt = '1'; /* as GCC and Clang */
            else if (strcmp(lvl, "fast") == 0) {
                o->opt = '3';
                o->fast_math = true;
            } else if (strlen(lvl) == 1 && strchr("0123sz", lvl[0])) {
                o->opt = lvl[0];
                o->fast_math = false;
            } else {
                nc_diag_error(d, NC_NO_SPAN, "E0003", "invalid optimisation level '%s'", a);
                ok = false;
            }
        } else if (strcmp(a, "-g") == 0) {
            o->debug = true;
        } else if (strcmp(a, "-g0") == 0) {
            o->debug = false;
        } else if (strncmp(a, "--target=", 9) == 0) {
            const nc_target *t = nc_target_find(a + 9);
            if (!t) {
                nc_diag_error(d, NC_NO_SPAN, "E0003", "unknown target '%s'", a + 9);
                nc_diag_help(d, "implemented targets: %s", nc_target_list());
                ok = false;
            } else if (!t->implemented) {
                nc_diag_error(d, NC_NO_SPAN, "E9002", "target '%s' is not implemented in this version (Phase %d)",
                              t->name, t->phase);
                ok = false;
            } else {
                o->target = t;
            }
        } else if (strncmp(a, "-mcpu=", 6) == 0) {
            o->cpu = a + 6;
        } else if (strncmp(a, "-mattr=", 7) == 0) {
            o->features = a + 7;
        } else if (strncmp(a, "--linker=", 9) == 0) {
            o->linker = a + 9;
        } else if (strncmp(a, "-fpanic=", 8) == 0) {
            if (strcmp(a + 8, "verbose") == 0) o->panic_minimal = false;
            else if (strcmp(a + 8, "minimal") == 0) o->panic_minimal = true;
            else {
                nc_diag_error(d, NC_NO_SPAN, "E0003", "invalid value '%s' for -fpanic", a + 8);
                ok = false;
            }
        } else if (strncmp(a, "--libnovac=", 11) == 0) {
            o->libnovac = a + 11;
        } else if (strncmp(a, "--max-mode=", 11) == 0) {
            const char *v = a + 11;
            if (strcmp(v, "safe") == 0) o->max_mode = 0;
            else if (strcmp(v, "controlled") == 0) o->max_mode = 1;
            else if (strcmp(v, "kernel") == 0) o->max_mode = 2;
            else {
                nc_diag_error(d, NC_NO_SPAN, "E0003", "invalid value '%s' for --max-mode", v);
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
                nc_diag_error(d, NC_NO_SPAN, "E9002", "option '%s' is not implemented in this version (Phase %d)", a,
                              future_options[k].phase);
                if (future_options[k].takes_arg && i + 1 < argc) i++;
                break;
            }
            if (!known) nc_diag_error(d, NC_NO_SPAN, "E0002", "unknown option '%s'", a);
            ok = false;
        }
    }
    if (conflict) {
        nc_diag_error(d, NC_NO_SPAN, "E0003",
                      "conflicting output options; give only one of --check, --dump-*, --emit=, -c, -S");
        ok = false;
    }
    if (ok && !o->help && !o->version && o->ninputs == 0) {
        nc_diag_error(d, NC_NO_SPAN, "E0004", "no input files");
        ok = false;
    }
    bool single_output = o->action == NC_ACT_OBJ || o->action == NC_ACT_ASM || o->action == NC_ACT_LLVM_BC ||
                         o->action == NC_ACT_LLVM_IR;
    if (ok && o->output && single_output && o->ninputs > 1) {
        nc_diag_error(d, NC_NO_SPAN, "E0003", "'-o' with several input files is only valid when linking");
        ok = false;
    }
    return ok;
}
