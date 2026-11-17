/* String interning. Identifiers and literal contents are stored once; an
 * nc_sym is a small integer, so comparing two names is an integer compare.
 * Strings may contain any bytes, including NUL (string literals). */
#ifndef NC_SUPPORT_STRTAB_H
#define NC_SUPPORT_STRTAB_H

#include "support/arena.h"

typedef uint32_t nc_sym; /* 0 is "no symbol" */

typedef struct nc_strtab_entry {
    const char *str; /* NUL-terminated copy (may also contain NULs) */
    uint32_t len;
    uint32_t hash;
} nc_strtab_entry;

typedef struct nc_strtab {
    nc_arena arena;
    nc_strtab_entry *entries; /* index = symbol; entry 0 unused */
    uint32_t count;           /* including entry 0 */
    uint32_t cap;
    uint32_t *slots;          /* open addressing; 0 = empty, else symbol */
    uint32_t nslots;          /* power of two */
} nc_strtab;

void nc_strtab_init(nc_strtab *tab);
void nc_strtab_free(nc_strtab *tab);

nc_sym nc_intern(nc_strtab *tab, const char *s, size_t len);
nc_sym nc_intern_cstr(nc_strtab *tab, const char *s);

const char *nc_sym_str(const nc_strtab *tab, nc_sym sym);
uint32_t nc_sym_len(const nc_strtab *tab, nc_sym sym);

#endif
