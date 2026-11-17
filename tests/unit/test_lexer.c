#include "unit.h"

#include "lex/lexer.h"

/* Every keyword in the token table lexes as that keyword, and the table
 * is free of duplicates. */
static void keywords(void)
{
    static const struct {
        const char *text;
        nc_tok kind;
    } kws[] = {
#define NC_X(name, text) {text, NC_T_##name},
        NC_TOKEN_KEYWORDS(NC_X)
#undef NC_X
    };
    size_t n = sizeof kws / sizeof kws[0];
    for (size_t i = 0; i < n; i++) {
        nc_session s;
        nc_session_init(&s, NULL);
        uint32_t f = nc_source_add(&s.sources, "kw.nc", kws[i].text, strlen(kws[i].text));
        nc_token_list t = nc_lex(&s, f);
        CHECK(t.count == 2 && t.tokens[0].kind == kws[i].kind && t.tokens[1].kind == NC_T_EOF);
        for (size_t k = i + 1; k < n; k++) CHECK(strcmp(kws[i].text, kws[k].text) != 0);
        nc_session_free(&s);
    }
}

static void joint_gt(void)
{
    nc_session s;
    nc_session_init(&s, NULL);
    const char *src = "a >> b > > c >>= d";
    uint32_t f = nc_source_add(&s.sources, "j.nc", src, strlen(src));
    nc_token_list t = nc_lex(&s, f);
    /* a > > b > > c > >= d EOF */
    CHECK(t.count == 11);
    CHECK(t.tokens[1].kind == NC_T_GT && t.tokens[1].joint);
    CHECK(t.tokens[2].kind == NC_T_GT && !t.tokens[2].joint);
    CHECK(t.tokens[4].kind == NC_T_GT && !t.tokens[4].joint);
    CHECK(t.tokens[7].kind == NC_T_GT && t.tokens[7].joint && t.tokens[8].kind == NC_T_GE);
    CHECK(s.diag.errors == 0);
    nc_session_free(&s);
}

static void literal_values(void)
{
    nc_session s;
    nc_session_init(&s, NULL);
    const char *src = "18446744073709551615u64 0x1.8p1 1_000 0b1010 0o17 'A' \"a\\x00b\"";
    uint32_t f = nc_source_add(&s.sources, "l.nc", src, strlen(src));
    nc_token_list t = nc_lex(&s, f);
    CHECK(s.diag.errors == 0);
    CHECK(t.tokens[0].v.i.value == UINT64_MAX && t.tokens[0].v.i.suffix == NC_SUF_U64);
    CHECK(t.tokens[1].kind == NC_T_FLOAT_LIT && t.tokens[1].v.f.value == 3.0);
    CHECK(t.tokens[2].v.i.value == 1000);
    CHECK(t.tokens[3].v.i.value == 10);
    CHECK(t.tokens[4].v.i.value == 15);
    CHECK(t.tokens[5].kind == NC_T_CHAR_LIT && t.tokens[5].v.ch == 'A');
    CHECK(t.tokens[6].kind == NC_T_STRING_LIT && nc_sym_len(&s.strtab, t.tokens[6].v.sym) == 3);
    nc_session_free(&s);

    nc_session_init(&s, NULL);
    src = "18446744073709551616";
    f = nc_source_add(&s.sources, "o.nc", src, strlen(src));
    nc_lex(&s, f);
    CHECK(s.diag.errors == 1); /* E1023 */
    nc_session_free(&s);
}

void test_lexer(void)
{
    keywords();
    joint_gt();
    literal_values();
}
