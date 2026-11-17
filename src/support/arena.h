/* Bump allocator. Compiler data structures with a common lifetime (tokens'
 * strings, the AST, types) are allocated from one arena and released
 * together with nc_arena_free (NOVAC_COMPILER_ARCHITECTURE.md §5.1). */
#ifndef NC_SUPPORT_ARENA_H
#define NC_SUPPORT_ARENA_H

#include "support/common.h"

typedef struct nc_arena_chunk nc_arena_chunk;

typedef struct nc_arena {
    nc_arena_chunk *head;
    size_t bytes_allocated; /* total requested, for statistics and tests */
} nc_arena;

void nc_arena_init(nc_arena *arena);

/* Zero-initialised memory aligned for any object type. Never fails. */
void *nc_arena_alloc(nc_arena *arena, size_t size);

/* Copy `len` bytes and append a NUL terminator. */
char *nc_arena_strndup(nc_arena *arena, const char *s, size_t len);

void *nc_arena_memdup(nc_arena *arena, const void *p, size_t size);

/* Release every allocation made from the arena. The arena can be reused. */
void nc_arena_free(nc_arena *arena);

#endif
