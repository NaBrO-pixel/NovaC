#include "unit.h"

#include "support/arena.h"
#include "support/source.h"
#include "support/strtab.h"
#include "support/vec.h"

static void test_arena(void)
{
    nc_arena a;
    nc_arena_init(&a);
    for (int i = 0; i < 1000; i++) {
        unsigned char *p = nc_arena_alloc(&a, (size_t)(i % 37) + 1);
        CHECK(((uintptr_t)p % _Alignof(max_align_t)) == 0);
        int zero = 1;
        for (int k = 0; k < (i % 37) + 1; k++) zero &= p[k] == 0;
        CHECK(zero);
        p[0] = 0xAA; /* writable */
    }
    /* Larger than one chunk. */
    char *big = nc_arena_alloc(&a, 200000);
    big[199999] = 1;
    CHECK(big[0] == 0);
    char *s = nc_arena_strndup(&a, "hello world", 5);
    CHECK_STR(s, "hello");
    nc_arena_free(&a);
    CHECK(a.head == NULL);
}

static void test_strtab(void)
{
    nc_strtab t;
    nc_strtab_init(&t);
    nc_sym a = nc_intern_cstr(&t, "alpha");
    nc_sym b = nc_intern_cstr(&t, "beta");
    CHECK(a != 0 && b != 0 && a != b);
    CHECK(nc_intern(&t, "alphabet", 5) == a);
    CHECK_STR(nc_sym_str(&t, b), "beta");
    CHECK(nc_sym_len(&t, a) == 5);
    /* Embedded NUL bytes are part of the string. */
    nc_sym n1 = nc_intern(&t, "a\0b", 3);
    nc_sym n2 = nc_intern(&t, "a\0c", 3);
    CHECK(n1 != n2 && nc_sym_len(&t, n1) == 3);
    /* Many symbols force rehashing; lookups must stay stable. */
    char buf[32];
    nc_sym first = 0;
    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof buf, "sym%d", i);
        nc_sym s = nc_intern_cstr(&t, buf);
        if (i == 0) first = s;
    }
    CHECK(nc_intern_cstr(&t, "sym0") == first);
    CHECK(nc_intern_cstr(&t, "alpha") == a);
    CHECK_STR(nc_sym_str(&t, 0), "");
    nc_strtab_free(&t);
}

static void test_source(void)
{
    nc_source_map m;
    nc_source_init(&m);
    const char *text = "ab\r\ncd\n\nlast";
    uint32_t f = nc_source_add(&m, "t.nc", text, strlen(text));
    uint32_t line, col, len;
    nc_source_line_col(&m, f, 0, &line, &col);
    CHECK(line == 1 && col == 1);
    nc_source_line_col(&m, f, 4, &line, &col); /* 'c' */
    CHECK(line == 2 && col == 1);
    nc_source_line_col(&m, f, 8, &line, &col); /* 'l' of "last" */
    CHECK(line == 4 && col == 1);
    nc_source_line_col(&m, f, 12, &line, &col); /* end of file */
    CHECK(line == 4 && col == 5);
    const char *l1 = nc_source_line_text(&m, f, 1, &len);
    CHECK(len == 2 && strncmp(l1, "ab", 2) == 0); /* CR stripped */
    nc_source_line_text(&m, f, 3, &len);
    CHECK(len == 0);
    const char *l4 = nc_source_line_text(&m, f, 4, &len);
    CHECK(len == 4 && strncmp(l4, "last", 4) == 0);
    nc_source_line_text(&m, f, 5, &len);
    CHECK(len == 0);
    uint32_t id;
    CHECK(nc_source_load(&m, "/nonexistent/novac/file.nc", &id) == NC_LOAD_UNREADABLE);
    nc_source_free(&m);
}

static void test_vec(void)
{
    NC_VEC(int) v = {0};
    for (int i = 0; i < 1000; i++) nc_vec_push(&v, i * 2);
    CHECK(v.len == 1000 && v.data[999] == 1998 && v.cap >= 1000);
    nc_vec_free(&v);
    CHECK(v.data == NULL && v.len == 0);
}

void test_support(void)
{
    test_arena();
    test_strtab();
    test_source();
    test_vec();
}
