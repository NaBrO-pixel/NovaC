/* Diagnostics engine (NOVAC_COMPILER_ARCHITECTURE.md §6).
 *
 * Every error and warning carries a stable code ("E4010"). Diagnostics are
 * rendered as they are emitted:
 *
 *   file.nc:3:17: error[E4010]: operands of '+' have different types
 *       3 |     i32 c = a + b;
 *         |                 ^
 *         = help: ...
 *
 * Notes, help lines and fix-its attach to the most recent error or
 * warning and are dropped with it if it was suppressed. */
#ifndef NC_DIAG_DIAG_H
#define NC_DIAG_DIAG_H

#include "support/source.h"

#include <stdarg.h>
#include <stdio.h>

typedef struct nc_diag {
    const nc_source_map *sources;
    FILE *out;             /* NULL: count only, render nothing (fuzzing) */
    bool werror;           /* -Werror: warnings count and render as errors */
    unsigned error_limit;  /* 0 = unlimited */
    unsigned errors;
    unsigned warnings;
    bool limit_reached;
    bool last_suppressed;  /* the last error/warning was not rendered */
} nc_diag;

void nc_diag_init(nc_diag *d, const nc_source_map *sources, FILE *out);

#if defined(__GNUC__) || defined(__clang__)
# define NC_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
# define NC_PRINTF(a, b)
#endif

/* `code` is "Ennnn" or "Wnnnn". Spans with file == NC_NO_FILE render without
 * a location ("ncc: error[E0002]: ..."). */
void nc_diag_error(nc_diag *d, nc_span span, const char *code, const char *fmt, ...) NC_PRINTF(4, 5);
void nc_diag_warning(nc_diag *d, nc_span span, const char *code, const char *fmt, ...) NC_PRINTF(4, 5);

/* Attach to the most recent error or warning. A note with a span shows a
 * source excerpt; without (NC_NO_SPAN) it is a single "= note:" line. */
void nc_diag_note(nc_diag *d, nc_span span, const char *fmt, ...) NC_PRINTF(3, 4);
void nc_diag_help(nc_diag *d, const char *fmt, ...) NC_PRINTF(2, 3);

/* Suggested replacement of `span` by `replacement`. */
void nc_diag_fixit(nc_diag *d, nc_span span, const char *replacement);

bool nc_diag_has_errors(const nc_diag *d);

#endif
