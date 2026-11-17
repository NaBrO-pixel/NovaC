/* Common definitions shared by every compiler subsystem.
 *
 * Nothing in here knows about NovaC itself: integer types, the source span
 * type, and the out-of-memory policy. */
#ifndef NC_SUPPORT_COMMON_H
#define NC_SUPPORT_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A half-open byte range [lo, hi) in one source file. `file` indexes the
 * session's source map; NC_NO_FILE marks diagnostics that have no location
 * (command-line errors). */
typedef struct nc_span {
    uint32_t file;
    uint32_t lo;
    uint32_t hi;
} nc_span;

#define NC_NO_FILE UINT32_MAX
#define NC_NO_SPAN ((nc_span){NC_NO_FILE, 0, 0})

/* Span covering both a and b (which must be in the same file). */
nc_span nc_span_join(nc_span a, nc_span b);

/* Allocation that cannot fail: on exhaustion the compiler reports an
 * internal error and exits with status 3 (NOVAC_COMPILER_ARCHITECTURE.md
 * §11). Memory obtained here is released with free(). */
void *nc_xmalloc(size_t size);
void *nc_xcalloc(size_t count, size_t size);
void *nc_xrealloc(void *ptr, size_t size);

/* Report an internal compiler error and exit with status 3. */
_Noreturn void nc_ice(const char *stage, const char *message);

#endif
