/* The Phase 2 pipeline for one source file: lex, parse, semantic analysis,
 * and the requested dump. Shared by `ncc` and the fuzzing harness. */
#ifndef NC_DRIVER_FRONTEND_H
#define NC_DRIVER_FRONTEND_H

#include "driver/options.h"
#include "session/session.h"

/* Runs `action` on the already-loaded file. Dumps go to `out` (may be
 * NULL to discard). Returns true if no errors were reported. */
bool nc_frontend_run(nc_session *s, uint32_t file, nc_action action, FILE *out);

#endif
