#include "support/arena.h"

#include <stdlib.h>
#include <string.h>

#define NC_ARENA_CHUNK_SIZE (64u * 1024u)

struct nc_arena_chunk {
    nc_arena_chunk *next;
    size_t size;
    size_t used;
    max_align_t data[];
};

void nc_arena_init(nc_arena *arena)
{
    arena->head = NULL;
    arena->bytes_allocated = 0;
}

static size_t align_up(size_t n)
{
    size_t a = _Alignof(max_align_t);
    return (n + a - 1) & ~(a - 1);
}

void *nc_arena_alloc(nc_arena *arena, size_t size)
{
    size = align_up(size ? size : 1);
    nc_arena_chunk *c = arena->head;
    if (!c || c->size - c->used < size) {
        size_t cap = size > NC_ARENA_CHUNK_SIZE ? size : NC_ARENA_CHUNK_SIZE;
        c = nc_xmalloc(sizeof(nc_arena_chunk) + cap);
        c->size = cap;
        c->used = 0;
        c->next = arena->head;
        arena->head = c;
    }
    void *p = (char *)c->data + c->used;
    c->used += size;
    arena->bytes_allocated += size;
    memset(p, 0, size);
    return p;
}

char *nc_arena_strndup(nc_arena *arena, const char *s, size_t len)
{
    char *p = nc_arena_alloc(arena, len + 1);
    if (len) memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

void *nc_arena_memdup(nc_arena *arena, const void *p, size_t size)
{
    void *q = nc_arena_alloc(arena, size);
    if (size) memcpy(q, p, size);
    return q;
}

void nc_arena_free(nc_arena *arena)
{
    nc_arena_chunk *c = arena->head;
    while (c) {
        nc_arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    arena->head = NULL;
    arena->bytes_allocated = 0;
}
