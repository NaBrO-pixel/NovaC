/* ncc: the NovaC compiler driver (NOVAC_COMPILER_ARCHITECTURE.md §11).
 *
 * Exit status: 0 success; 1 compilation errors; 2 command-line errors;
 * 3 internal compiler error. */
#include "driver/frontend.h"
#include "driver/options.h"
#include "driver/version.h"

#include <stdlib.h>

int main(int argc, char **argv)
{
    nc_session s;
    nc_session_init(&s, stderr);

    nc_options opts;
    bool ok = nc_parse_options(argc, argv, &opts, &s.diag);
    int status = 0;
    if (!ok) {
        status = 2;
        goto done;
    }
    if (opts.help) {
        nc_print_help(stdout);
        goto done;
    }
    if (opts.version) {
        printf("ncc %s (NovaC compiler, Phase 2 front end)\n", NC_VERSION);
        goto done;
    }
    s.diag.werror = opts.werror;
    s.diag.error_limit = opts.error_limit;

    if (opts.action == NC_ACT_COMPILE) {
        nc_diag_error(&s.diag, NC_NO_SPAN, "E9003",
                      "code generation is not implemented in this version (Phase 3)");
        nc_diag_help(&s.diag, "use --check, --dump-tokens, --dump-ast or --emit=typed");
        status = 1;
        goto done;
    }

    for (int i = 0; i < opts.ninputs; i++) {
        uint32_t file;
        nc_load_result r = nc_source_load(&s.sources, opts.inputs[i], &file);
        if (r != NC_LOAD_OK) {
            if (r == NC_LOAD_TOO_LARGE)
                nc_diag_error(&s.diag, NC_NO_SPAN, "E0005", "input file '%s' is too large (4 GiB or more)",
                              opts.inputs[i]);
            else
                nc_diag_error(&s.diag, NC_NO_SPAN, "E0001", "cannot read input file '%s'", opts.inputs[i]);
            continue;
        }
        nc_frontend_run(&s, file, opts.action, stdout);
        if (s.diag.limit_reached) break;
    }
    if (s.diag.errors) status = 1;

done:
    free(opts.inputs);
    nc_session_free(&s);
    return status;
}
