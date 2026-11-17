#include "support/common.h"

#include <stdio.h>
#include <stdlib.h>

nc_span nc_span_join(nc_span a, nc_span b)
{
    nc_span s = a;
    if (b.lo < s.lo) s.lo = b.lo;
    if (b.hi > s.hi) s.hi = b.hi;
    return s;
}

_Noreturn void nc_ice(const char *stage, const char *message)
{
    fprintf(stderr, "ncc: internal compiler error in %s: %s\n", stage, message);
    fprintf(stderr, "ncc: this is a bug in the compiler; please report it\n");
    exit(3);
}

void *nc_xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p) nc_ice("allocator", "out of memory");
    return p;
}

void *nc_xcalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p) nc_ice("allocator", "out of memory");
    return p;
}

void *nc_xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size ? size : 1);
    if (!p) nc_ice("allocator", "out of memory");
    return p;
}
