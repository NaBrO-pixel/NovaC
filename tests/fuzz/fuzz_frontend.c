/* libFuzzer harness for everything before code generation: lexer, parser,
 * semantic analysis, NIR lowering, the NIR verifier and the definite
 * initialisation analysis, on arbitrary bytes (NOVAC_ROADMAP.md step 2.8).
 *
 *   make fuzz-run FUZZ_SECONDS=1800
 *
 * Any crash, sanitizer report, leak or timeout is a bug; reduce the input
 * and add it to tests/regress/. */
#include "driver/frontend.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    nc_session s;
    nc_session_init(&s, NULL); /* count diagnostics, print nothing */
    s.diag.error_limit = 0;    /* exercise recovery all the way to EOF */
    uint32_t file = nc_source_add(&s.sources, "fuzz.nc", (const char *)data, size);
    nc_frontend_run(&s, file, NC_ACT_CHECK, NULL, NULL); /* through NIR lowering and analysis */
    nc_session_free(&s);
    return 0;
}
