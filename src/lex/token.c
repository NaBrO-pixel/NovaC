#include "lex/token.h"

static const char *const spellings[NC_T_COUNT] = {
#define NC_X(name, text) [NC_T_##name] = text,
    NC_TOKEN_SPECIAL(NC_X)
    NC_TOKEN_KEYWORDS(NC_X)
    NC_TOKEN_PUNCT(NC_X)
#undef NC_X
};

const char *nc_tok_spelling(nc_tok kind)
{
    if ((unsigned)kind >= NC_T_COUNT) return "?";
    return spellings[kind];
}

bool nc_tok_is_keyword(nc_tok kind)
{
    return kind >= NC_T_KW_ALIGNOF && kind <= NC_T_KW_WHILE;
}

const char *nc_suffix_name(nc_suffix s)
{
    static const char *const names[] = {
        "", "i8", "i16", "i32", "i64", "isize",
        "u8", "u16", "u32", "u64", "usize", "f32", "f64",
    };
    if ((unsigned)s >= sizeof names / sizeof names[0]) return "?";
    return names[s];
}
