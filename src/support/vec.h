/* Typed growable arrays.
 *
 *   NC_VEC(int) v = {0};
 *   nc_vec_push(&v, 42);
 *   ... v.data[i], v.len ...
 *   nc_vec_free(&v);
 *
 * Storage comes from the C heap; the owner frees it with nc_vec_free. */
#ifndef NC_SUPPORT_VEC_H
#define NC_SUPPORT_VEC_H

#include "support/common.h"

#include <stdlib.h>

#define NC_VEC(T) struct { T *data; size_t len; size_t cap; }

/* Returns `data` reallocated to hold at least `min` elements. */
void *nc_vec_grow_(void *data, size_t *cap, size_t elem_size, size_t min);

#define nc_vec_reserve(v, n) \
    do { \
        if ((n) > (v)->cap) \
            (v)->data = nc_vec_grow_((v)->data, &(v)->cap, sizeof *(v)->data, (n)); \
    } while (0)

#define nc_vec_push(v, x) \
    do { \
        if ((v)->len == (v)->cap) \
            (v)->data = nc_vec_grow_((v)->data, &(v)->cap, sizeof *(v)->data, (v)->len + 1); \
        (v)->data[(v)->len++] = (x); \
    } while (0)

#define nc_vec_free(v) \
    do { \
        free((v)->data); \
        (v)->data = NULL; \
        (v)->len = (v)->cap = 0; \
    } while (0)

#endif
