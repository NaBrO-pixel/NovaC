/* Token kinds (NOVAC_GRAMMAR.md §2).
 *
 * The keyword list below is the implementation of the authoritative list in
 * NOVAC_GRAMMAR.md §2.3; tools/check_docs.py checks that they agree.
 *
 * `>>` and `>>=` are not tokens: the lexer emits `>` `>` and `>` `>=` with
 * the first token marked `joint`, and the parser recombines them in
 * expression context. This lets `optional<slice<u8>>` close two generic
 * argument lists (NOVAC_GRAMMAR.md §2.5). */
#ifndef NC_LEX_TOKEN_H
#define NC_LEX_TOKEN_H

#include "support/strtab.h"

#define NC_TOKEN_SPECIAL(X) \
    X(EOF, "end of file") \
    X(IDENT, "identifier") \
    X(INT_LIT, "integer literal") \
    X(FLOAT_LIT, "floating-point literal") \
    X(CHAR_LIT, "character literal") \
    X(STRING_LIT, "string literal") \
    X(CSTRING_LIT, "C string literal")

#define NC_TOKEN_KEYWORDS(X) \
    X(KW_ALIGNOF, "alignof") X(KW_AS, "as") X(KW_ASM, "asm") X(KW_ASYNC, "async") \
    X(KW_AUTO, "auto") X(KW_AWAIT, "await") X(KW_BOOL, "bool") X(KW_BREAK, "break") \
    X(KW_CASE, "case") X(KW_CONST, "const") X(KW_CONSTEXPR, "constexpr") \
    X(KW_CONTINUE, "continue") X(KW_DEFAULT, "default") X(KW_DEFER, "defer") \
    X(KW_DO, "do") X(KW_ELSE, "else") X(KW_ENUM, "enum") X(KW_EXTERN, "extern") \
    X(KW_F32, "f32") X(KW_F64, "f64") X(KW_FALSE, "false") \
    X(KW_FALLTHROUGH, "fallthrough") X(KW_FN, "fn") X(KW_FOR, "for") \
    X(KW_GOTO, "goto") X(KW_I16, "i16") X(KW_I32, "i32") X(KW_I64, "i64") \
    X(KW_I8, "i8") X(KW_IF, "if") X(KW_IMPL, "impl") X(KW_IMPORT, "import") \
    X(KW_IN, "in") X(KW_INT, "int") X(KW_INTERFACE, "interface") X(KW_IS, "is") \
    X(KW_ISIZE, "isize") X(KW_MODULE, "module") X(KW_MUT, "mut") \
    X(KW_NONE, "none") X(KW_NULL, "null") X(KW_OFFSETOF, "offsetof") \
    X(KW_PUBLIC, "public") X(KW_RETURN, "return") X(KW_SELF, "self") \
    X(KW_SELF_TYPE, "Self") X(KW_SIZEOF, "sizeof") X(KW_SOME, "some") \
    X(KW_STATIC, "static") X(KW_STRUCT, "struct") X(KW_SWITCH, "switch") \
    X(KW_TRUE, "true") X(KW_TRY, "try") X(KW_TYPEDEF, "typedef") \
    X(KW_U16, "u16") X(KW_U32, "u32") X(KW_U64, "u64") X(KW_U8, "u8") \
    X(KW_UNINIT, "uninit") X(KW_UNION, "union") X(KW_UNSAFE, "unsafe") \
    X(KW_USIZE, "usize") X(KW_VOID, "void") X(KW_VOLATILE, "volatile") \
    X(KW_WHERE, "where") X(KW_WHILE, "while")

/* Longest spellings first is not required: the lexer matches explicitly. */
#define NC_TOKEN_PUNCT(X) \
    X(LBRACE, "{") X(RBRACE, "}") X(LPAREN, "(") X(RPAREN, ")") \
    X(LBRACKET, "[") X(RBRACKET, "]") X(SEMI, ";") X(COMMA, ",") \
    X(DOT, ".") X(ARROW, "->") X(ELLIPSIS, "...") X(COLONCOLON, "::") \
    X(COLON, ":") X(QUESTION, "?") X(AT, "@") \
    X(PLUS, "+") X(MINUS, "-") X(STAR, "*") X(SLASH, "/") X(PERCENT, "%") \
    X(PLUS_WRAP, "+%") X(MINUS_WRAP, "-%") X(STAR_WRAP, "*%") \
    X(PLUSPLUS, "++") X(MINUSMINUS, "--") \
    X(AMP, "&") X(PIPE, "|") X(CARET, "^") X(TILDE, "~") X(BANG, "!") \
    X(SHL, "<<") X(ANDAND, "&&") X(OROR, "||") \
    X(ASSIGN, "=") X(PLUS_ASSIGN, "+=") X(MINUS_ASSIGN, "-=") \
    X(STAR_ASSIGN, "*=") X(SLASH_ASSIGN, "/=") X(PERCENT_ASSIGN, "%=") \
    X(PLUS_WRAP_ASSIGN, "+%=") X(MINUS_WRAP_ASSIGN, "-%=") \
    X(STAR_WRAP_ASSIGN, "*%=") X(SHL_ASSIGN, "<<=") X(AMP_ASSIGN, "&=") \
    X(PIPE_ASSIGN, "|=") X(CARET_ASSIGN, "^=") \
    X(EQ, "==") X(NE, "!=") X(LT, "<") X(LE, "<=") X(GT, ">") X(GE, ">=")

typedef enum nc_tok {
#define NC_X(name, text) NC_T_##name,
    NC_TOKEN_SPECIAL(NC_X)
    NC_TOKEN_KEYWORDS(NC_X)
    NC_TOKEN_PUNCT(NC_X)
#undef NC_X
    NC_T_COUNT
} nc_tok;

/* Literal suffixes (NOVAC_GRAMMAR.md §2.4). */
typedef enum nc_suffix {
    NC_SUF_NONE,
    NC_SUF_I8, NC_SUF_I16, NC_SUF_I32, NC_SUF_I64, NC_SUF_ISIZE,
    NC_SUF_U8, NC_SUF_U16, NC_SUF_U32, NC_SUF_U64, NC_SUF_USIZE,
    NC_SUF_F32, NC_SUF_F64,
} nc_suffix;

typedef struct nc_token {
    nc_tok kind;
    nc_span span;
    bool joint; /* the next token starts immediately after this one */
    union {
        nc_sym sym; /* IDENT; STRING_LIT/CSTRING_LIT decoded bytes */
        struct {
            uint64_t value;
            nc_suffix suffix;
        } i;
        struct {
            double value;
            nc_suffix suffix;
        } f;
        uint8_t ch; /* CHAR_LIT */
    } v;
} nc_token;

/* "integer literal", "+=", "while", ... */
const char *nc_tok_spelling(nc_tok kind);
/* "identifier", "punct", "keyword", ... for --dump-tokens */
bool nc_tok_is_keyword(nc_tok kind);
const char *nc_suffix_name(nc_suffix s);

#endif
