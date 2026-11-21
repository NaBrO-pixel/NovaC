/* ncc: the NovaC compiler driver (NOVAC_COMPILER_ARCHITECTURE.md §11).
 *
 * Exit status: 0 success; 1 compilation errors; 2 command-line errors;
 * 3 internal compiler error. */
#define _POSIX_C_SOURCE 200809L /* mkstemp, close, unlink */
#include "codegen/codegen.h"
#include "driver/frontend.h"
#include "driver/link.h"
#include "driver/options.h"
#include "driver/version.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* "dir/name.nc" -> "name.<ext>" in the current directory. */
static char *derived_name(const char *input, const char *ext)
{
    const char *base = strrchr(input, '/');
    base = base ? base + 1 : input;
    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    char *out = nc_xmalloc(len + strlen(ext) + 2);
    memcpy(out, base, len);
    out[len] = '.';
    strcpy(out + len + 1, ext);
    return out;
}

static bool has_main(nc_session *s, const nc_nir_module *m)
{
    for (uint32_t i = 0; i < m->nfns; i++)
        if (strcmp(nc_sym_str(&s->strtab, m->fns[i]->name), "main") == 0) return true;
    return false;
}

/* Compile every input; link when the action is NC_ACT_EXE. */
static bool compile(nc_session *s, const nc_options *o)
{
    nc_codegen_options cgo = {o->target, o->cpu, o->features, o->opt, o->fast_math, o->debug, o->panic_minimal};
    char **temps = nc_xcalloc((size_t)o->ninputs + 1, sizeof *temps);
    int ntemps = 0;
    bool ok = true;
    bool any_main = false;

    for (int i = 0; i < o->ninputs; i++) {
        uint32_t file;
        nc_load_result r = nc_source_load(&s->sources, o->inputs[i], &file);
        if (r != NC_LOAD_OK) {
            if (r == NC_LOAD_TOO_LARGE)
                nc_diag_error(&s->diag, NC_NO_SPAN, "E0005", "input file '%s' is too large (4 GiB or more)", o->inputs[i]);
            else
                nc_diag_error(&s->diag, NC_NO_SPAN, "E0001", "cannot read input file '%s'", o->inputs[i]);
            ok = false;
            continue;
        }
        bool codegen = o->action == NC_ACT_EXE || o->action == NC_ACT_OBJ || o->action == NC_ACT_ASM ||
                       o->action == NC_ACT_LLVM_IR || o->action == NC_ACT_LLVM_BC;
        nc_nir_module *nm = NULL;
        bool fe_ok = nc_frontend_run(s, file, o->action, stdout, codegen ? &nm : NULL);
        if (!fe_ok || !codegen) {
            ok &= fe_ok;
            nc_nir_free(nm);
            if (s->diag.limit_reached) break;
            continue;
        }
        any_main |= has_main(s, nm);

        char *path = NULL;
        nc_output_kind kind = NC_OUT_OBJ;
        switch (o->action) {
        case NC_ACT_LLVM_IR:
            kind = NC_OUT_LLVM_IR;
            path = o->output ? strdup(o->output) : NULL; /* default: stdout */
            break;
        case NC_ACT_LLVM_BC:
            kind = NC_OUT_LLVM_BC;
            path = o->output ? strdup(o->output) : derived_name(o->inputs[i], "bc");
            break;
        case NC_ACT_ASM:
            kind = NC_OUT_ASM;
            path = o->output ? strdup(o->output) : derived_name(o->inputs[i], "s");
            break;
        case NC_ACT_OBJ:
            path = o->output ? strdup(o->output) : derived_name(o->inputs[i], "o");
            break;
        default: { /* NC_ACT_EXE: a temporary object */
            const char *dir = getenv("TMPDIR");
            if (!dir || !*dir) dir = "/tmp";
            path = nc_xmalloc(strlen(dir) + 32);
            sprintf(path, "%s/ncc-XXXXXX", dir);
            int fd = mkstemp(path);
            if (fd < 0) {
                nc_diag_error(&s->diag, NC_NO_SPAN, "E8001", "cannot create a temporary file in '%s'", dir);
                free(path);
                path = NULL;
                ok = false;
            } else {
                close(fd);
                temps[ntemps++] = path;
            }
            break;
        }
        }
        if (o->action != NC_ACT_EXE || path) ok &= nc_codegen(s, nm, &cgo, kind, path);
        if (o->action != NC_ACT_EXE) free(path);
        nc_nir_free(nm);
    }

    if (ok && o->action == NC_ACT_EXE) {
        if (!any_main) {
            nc_diag_error(&s->diag, NC_NO_SPAN, "E8004", "no `main` function: an executable needs `i32 main()`");
            nc_diag_help(&s->diag, "to compile a module without `main`, use -c");
            ok = false;
        } else {
            ok = nc_link(s, o->target, o->linker, o->libnovac, (const char *const *)temps, ntemps, o->link_args,
                         o->nlink_args, o->output ? o->output : "a.out");
        }
    }
    for (int i = 0; i < ntemps; i++) {
        unlink(temps[i]);
        free(temps[i]);
    }
    free(temps);
    return ok;
}

int main(int argc, char **argv)
{
    nc_session s;
    nc_session_init(&s, stderr);

    nc_options opts;
    bool ok = nc_parse_options(argc, argv, &opts, &s.diag);
    int status = 0;
    if (!ok) {
        status = 2;
    } else if (opts.help) {
        nc_print_help(stdout);
    } else if (opts.version) {
        char llvm[32];
        nc_codegen_llvm_version(llvm, sizeof llvm);
        printf("ncc %s (NovaC compiler; LLVM %s)\n", NC_VERSION, llvm);
    } else {
        s.diag.werror = opts.werror;
        s.diag.error_limit = opts.error_limit;
        s.max_mode = opts.max_mode;
        s.target = opts.target;
        ok = compile(&s, &opts);
        status = ok && !s.diag.errors ? 0 : 1;
    }
    free(opts.inputs);
    free(opts.link_args);
    nc_session_free(&s);
    return status;
}
