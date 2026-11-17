#include "support/vec.h"

void *nc_vec_grow_(void *data, size_t *cap, size_t elem_size, size_t min)
{
    size_t n = *cap ? *cap : 8;
    while (n < min) {
        if (n > SIZE_MAX / 2) nc_ice("vector", "size overflow");
        n *= 2;
    }
    if (n > SIZE_MAX / elem_size) nc_ice("vector", "size overflow");
    *cap = n;
    return nc_xrealloc(data, n * elem_size);
}
