#include "support/source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void nc_source_init(nc_source_map *map)
{
    memset(map, 0, sizeof *map);
}

void nc_source_free(nc_source_map *map)
{
    for (uint32_t i = 0; i < map->count; i++) {
        free(map->files[i].name);
        free(map->files[i].data);
        free(map->files[i].line_starts);
    }
    free(map->files);
    memset(map, 0, sizeof *map);
}

static uint32_t add_file(nc_source_map *map, const char *name, char *data, uint32_t len)
{
    if (map->count == map->cap) {
        map->cap = map->cap ? map->cap * 2 : 4;
        map->files = nc_xrealloc(map->files, map->cap * sizeof *map->files);
    }
    nc_source_file *f = &map->files[map->count];
    size_t nlen = strlen(name);
    f->name = nc_xmalloc(nlen + 1);
    memcpy(f->name, name, nlen + 1);
    f->data = data;
    f->len = len;

    uint32_t cap = 64, n = 0;
    uint32_t *starts = nc_xmalloc(cap * sizeof *starts);
    starts[n++] = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (data[i] == '\n') {
            if (n == cap) {
                cap *= 2;
                starts = nc_xrealloc(starts, cap * sizeof *starts);
            }
            starts[n++] = i + 1;
        }
    }
    f->line_starts = starts;
    f->nlines = n;
    return map->count++;
}

uint32_t nc_source_add(nc_source_map *map, const char *name, const char *data, size_t len)
{
    if (len >= UINT32_MAX) nc_ice("source", "buffer too large");
    char *copy = nc_xmalloc(len + 1);
    if (len) memcpy(copy, data, len);
    copy[len] = '\0';
    return add_file(map, name, copy, (uint32_t)len);
}

nc_load_result nc_source_load(nc_source_map *map, const char *path, uint32_t *out_id)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NC_LOAD_UNREADABLE;
    size_t cap = 4096, len = 0;
    char *buf = nc_xmalloc(cap);
    for (;;) {
        if (len + 1 >= cap) {
            if (cap >= UINT32_MAX) {
                free(buf);
                fclose(fp);
                return NC_LOAD_TOO_LARGE;
            }
            cap *= 2;
            buf = nc_xrealloc(buf, cap);
        }
        size_t got = fread(buf + len, 1, cap - len - 1, fp);
        len += got;
        if (got == 0) break;
    }
    bool failed = ferror(fp) != 0;
    fclose(fp);
    if (failed) {
        free(buf);
        return NC_LOAD_UNREADABLE;
    }
    if (len >= UINT32_MAX) {
        free(buf);
        return NC_LOAD_TOO_LARGE;
    }
    buf[len] = '\0';
    *out_id = add_file(map, path, buf, (uint32_t)len);
    return NC_LOAD_OK;
}

const nc_source_file *nc_source_get(const nc_source_map *map, uint32_t id)
{
    if (id >= map->count) return NULL;
    return &map->files[id];
}

void nc_source_line_col(const nc_source_map *map, uint32_t file, uint32_t offset,
                        uint32_t *line, uint32_t *col)
{
    const nc_source_file *f = nc_source_get(map, file);
    if (!f) {
        *line = *col = 0;
        return;
    }
    if (offset > f->len) offset = f->len;
    /* Binary search for the last line start <= offset. */
    uint32_t lo = 0, hi = f->nlines;
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (f->line_starts[mid] <= offset) lo = mid;
        else hi = mid;
    }
    *line = lo + 1;
    *col = offset - f->line_starts[lo] + 1;
}

const char *nc_source_line_text(const nc_source_map *map, uint32_t file, uint32_t line,
                                uint32_t *len)
{
    const nc_source_file *f = nc_source_get(map, file);
    if (!f || line == 0 || line > f->nlines) {
        *len = 0;
        return "";
    }
    uint32_t start = f->line_starts[line - 1];
    uint32_t end = line < f->nlines ? f->line_starts[line] - 1 : f->len;
    if (end > start && f->data[end - 1] == '\r') end--;
    *len = end - start;
    return f->data + start;
}
