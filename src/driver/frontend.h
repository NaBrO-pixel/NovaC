/* The pipeline for one source file up to NIR: lex, parse, semantic
 * analysis, lowering, NIR verification and the NIR analyses. Shared by
 * `ncc` and the fuzzing harness. */
#ifndef NC_DRIVER_FRONTEND_H
#define NC_DRIVER_FRONTEND_H

#include "driver/options.h"
#include "nir/nir.h"
#include "session/session.h"

/* Runs `action` on the already-loaded file. Dumps (tokens, AST, typed AST,
 * NIR) go to `out` (NULL discards them). For actions that need NIR and
 * succeed, the module is stored in *nir (when non-NULL; the caller frees it
 * with nc_nir_free) for code generation. Returns true if no errors were
 * reported. */
bool nc_frontend_run(nc_session *s, uint32_t file, nc_action action, FILE *out, nc_nir_module **nir);

#endif
