#include "support/strtab.h"

#include <stdlib.h>
#include <string.h>

static uint32_t hash_bytes(const char *s, size_t len)
{
    uint32_t h = 2166136261u; /* FNV-1a */
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

void nc_strtab_init(nc_strtab *tab)
{
    nc_arena_init(&tab->arena);
    tab->cap = 256;
    tab->count = 1;
    tab->entries = nc_xcalloc(tab->cap, sizeof *tab->entries);
    tab->nslots = 512;
    tab->slots = nc_xcalloc(tab->nslots, sizeof *tab->slots);
}

void nc_strtab_free(nc_strtab *tab)
{
    nc_arena_free(&tab->arena);
    free(tab->entries);
    free(tab->slots);
    memset(tab, 0, sizeof *tab);
}

static void rehash(nc_strtab *tab)
{
    uint32_t n = tab->nslots * 2;
    uint32_t *slots = nc_xcalloc(n, sizeof *slots);
    for (uint32_t sym = 1; sym < tab->count; sym++) {
        uint32_t i = tab->entries[sym].hash & (n - 1);
        while (slots[i]) i = (i + 1) & (n - 1);
        slots[i] = sym;
    }
    free(tab->slots);
    tab->slots = slots;
    tab->nslots = n;
}

nc_sym nc_intern(nc_strtab *tab, const char *s, size_t len)
{
    if (len > UINT32_MAX) nc_ice("strtab", "string too long");
    uint32_t h = hash_bytes(s, len);
    uint32_t i = h & (tab->nslots - 1);
    while (tab->slots[i]) {
        const nc_strtab_entry *e = &tab->entries[tab->slots[i]];
        if (e->hash == h && e->len == len && memcmp(e->str, s, len) == 0)
            return tab->slots[i];
        i = (i + 1) & (tab->nslots - 1);
    }
    if (tab->count == tab->cap) {
        tab->cap *= 2;
        tab->entries = nc_xrealloc(tab->entries, tab->cap * sizeof *tab->entries);
    }
    nc_sym sym = tab->count++;
    tab->entries[sym].str = nc_arena_strndup(&tab->arena, s, len);
    tab->entries[sym].len = (uint32_t)len;
    tab->entries[sym].hash = h;
    tab->slots[i] = sym;
    if ((uint64_t)tab->count * 2 > tab->nslots) rehash(tab);
    return sym;
}

nc_sym nc_intern_cstr(nc_strtab *tab, const char *s)
{
    return nc_intern(tab, s, strlen(s));
}

const char *nc_sym_str(const nc_strtab *tab, nc_sym sym)
{
    if (sym == 0 || sym >= tab->count) return "";
    return tab->entries[sym].str;
}

uint32_t nc_sym_len(const nc_strtab *tab, nc_sym sym)
{
    if (sym == 0 || sym >= tab->count) return 0;
    return tab->entries[sym].len;
}
