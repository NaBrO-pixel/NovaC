/* Source manager: owns the text of every input file and maps byte offsets
 * to 1-based line and column numbers (NOVAC_COMPILER_ARCHITECTURE.md §5.2).
 * Columns count bytes. */
#ifndef NC_SUPPORT_SOURCE_H
#define NC_SUPPORT_SOURCE_H

#include "support/common.h"

typedef struct nc_source_file {
    char *name;
    char *data;            /* `len` bytes followed by a NUL sentinel */
    uint32_t len;
    uint32_t *line_starts; /* offset of the first byte of each line */
    uint32_t nlines;
} nc_source_file;

typedef struct nc_source_map {
    nc_source_file *files;
    uint32_t count;
    uint32_t cap;
} nc_source_map;

typedef enum nc_load_result {
    NC_LOAD_OK,
    NC_LOAD_UNREADABLE, /* cannot open or read */
    NC_LOAD_TOO_LARGE,  /* does not fit 32-bit offsets */
} nc_load_result;

void nc_source_init(nc_source_map *map);
void nc_source_free(nc_source_map *map);

nc_load_result nc_source_load(nc_source_map *map, const char *path, uint32_t *out_id);

/* Add an in-memory buffer (tests, fuzzing). The data is copied. */
uint32_t nc_source_add(nc_source_map *map, const char *name, const char *data, size_t len);

const nc_source_file *nc_source_get(const nc_source_map *map, uint32_t id);

void nc_source_line_col(const nc_source_map *map, uint32_t file, uint32_t offset,
                        uint32_t *line, uint32_t *col);

/* Text of 1-based `line`, without its line terminator. */
const char *nc_source_line_text(const nc_source_map *map, uint32_t file, uint32_t line,
                                uint32_t *len);

#endif
