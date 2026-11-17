/* A compilation session: the state shared by every stage of one ncc
 * invocation (source files, interned strings, diagnostics, the long-lived
 * arena). Passed explicitly; there is no global compiler state
 * (NOVAC_COMPILER_ARCHITECTURE.md §5.1). */
#ifndef NC_SESSION_SESSION_H
#define NC_SESSION_SESSION_H

#include "diag/diag.h"
#include "support/arena.h"
#include "support/source.h"
#include "support/strtab.h"

typedef struct nc_session {
    nc_source_map sources;
    nc_strtab strtab;
    nc_diag diag;
    nc_arena arena; /* tokens, AST, types: freed with the session */
} nc_session;

/* `diag_out` may be NULL to count diagnostics without printing them. */
void nc_session_init(nc_session *s, FILE *diag_out);
void nc_session_free(nc_session *s);

#endif
