#include "lex/lexer.h"

#include "support/vec.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef NC_VEC(char) nc_charvec;

typedef struct lexer {
    nc_session *s;
    uint32_t file;
    const char *src;
    uint32_t len;
    uint32_t pos;
    bool line_start; /* only whitespace since the last newline */
    NC_VEC(nc_token) toks;
    nc_tok *kw_by_sym;
    uint32_t kw_max_sym;
} lexer;

static const struct {
    const char *text;
    nc_tok kind;
} keywords[] = {
#define NC_X(name, text) {text, NC_T_##name},
    NC_TOKEN_KEYWORDS(NC_X)
#undef NC_X
};

/* ---- helpers ------------------------------------------------------------ */

static nc_span span(lexer *lx, uint32_t lo, uint32_t hi)
{
    return (nc_span){lx->file, lo, hi};
}

static int peek(lexer *lx, uint32_t ahead)
{
    uint32_t p = lx->pos + ahead;
    return p < lx->len ? (unsigned char)lx->src[p] : -1;
}

static bool is_digit(int c) { return c >= '0' && c <= '9'; }
static bool is_hex(int c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static bool is_ident_start(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_ident_char(int c) { return is_ident_start(c) || is_digit(c); }

static int hex_value(int c)
{
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static nc_token *push(lexer *lx, nc_tok kind, uint32_t lo)
{
    nc_token t;
    memset(&t, 0, sizeof t);
    t.kind = kind;
    t.span = span(lx, lo, lx->pos);
    nc_vec_push(&lx->toks, t);
    return &lx->toks.data[lx->toks.len - 1];
}

static void error(lexer *lx, uint32_t lo, uint32_t hi, const char *code, const char *msg)
{
    nc_diag_error(&lx->s->diag, span(lx, lo, hi), code, "%s", msg);
}

/* Skip one UTF-8 encoded character (or one invalid byte). */
static void skip_utf8(lexer *lx)
{
    lx->pos++;
    while (lx->pos < lx->len && ((unsigned char)lx->src[lx->pos] & 0xC0) == 0x80) lx->pos++;
}

/* ---- comments and directives -------------------------------------------- */

static void directive(lexer *lx)
{
    uint32_t lo = lx->pos;
    lx->pos++; /* '#' */
    while (peek(lx, 0) == ' ' || peek(lx, 0) == '\t') lx->pos++;
    uint32_t nlo = lx->pos;
    while (is_ident_char(peek(lx, 0))) lx->pos++;
    size_t n = lx->pos - nlo;
    const char *name = lx->src + nlo;
    while (lx->pos < lx->len && lx->src[lx->pos] != '\n') lx->pos++;

    nc_diag *d = &lx->s->diag;
    nc_diag_error(d, span(lx, lo, nlo + (uint32_t)n), "E1010",
                  "NovaC has no preprocessor; directive '#%.*s' is not allowed", (int)n, name);
    if (n == 7 && memcmp(name, "include", 7) == 0)
        nc_diag_help(d, "use `import module.name;` to use another module (D-004)");
    else if (n == 6 && memcmp(name, "define", 6) == 0)
        nc_diag_help(d, "use `constexpr` for constants and functions for code (D-004)");
    else if ((n == 2 && memcmp(name, "if", 2) == 0) || (n == 5 && memcmp(name, "ifdef", 5) == 0) ||
             (n == 6 && memcmp(name, "ifndef", 6) == 0))
        nc_diag_help(d, "use the `@cfg(...)` attribute for conditional compilation (D-004)");
    else if (n == 6 && memcmp(name, "pragma", 6) == 0)
        nc_diag_help(d, "use attributes such as `@inline` or `@packed` (D-004)");
    else
        nc_diag_help(d, "see D-004 in docs/NOVAC_DECISIONS.md for the replacements");
}

static void block_comment(lexer *lx)
{
    uint32_t lo = lx->pos;
    lx->pos += 2;
    while (lx->pos + 1 < lx->len) {
        if (lx->src[lx->pos] == '*' && lx->src[lx->pos + 1] == '/') {
            lx->pos += 2;
            return;
        }
        lx->pos++;
    }
    lx->pos = lx->len;
    error(lx, lo, lo + 2, "E1001", "unterminated block comment");
}

/* ---- numbers ------------------------------------------------------------- */

static nc_suffix suffix_from(const char *s, size_t n)
{
    static const struct {
        const char *text;
        nc_suffix suf;
    } table[] = {
        {"i8", NC_SUF_I8}, {"i16", NC_SUF_I16}, {"i32", NC_SUF_I32}, {"i64", NC_SUF_I64},
        {"isize", NC_SUF_ISIZE}, {"u8", NC_SUF_U8}, {"u16", NC_SUF_U16}, {"u32", NC_SUF_U32},
        {"u64", NC_SUF_U64}, {"usize", NC_SUF_USIZE}, {"f32", NC_SUF_F32}, {"f64", NC_SUF_F64},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strlen(table[i].text) == n && memcmp(table[i].text, s, n) == 0) return table[i].suf;
    return NC_SUF_NONE;
}

/* Consume a run of digits accepted by `ok`, with single underscores between
 * digits. Decimal digits that are invalid for the base (8 in an octal
 * literal) are consumed and reported. Returns false if anything was
 * malformed; at most one error is reported per run. */
static bool digit_run(lexer *lx, bool (*ok)(int), bool *saw_digit)
{
    bool good = true;
    bool last_underscore = false;
    *saw_digit = false;
    for (;;) {
        int c = peek(lx, 0);
        if (c == '_') {
            if ((!*saw_digit || last_underscore) && good) {
                error(lx, lx->pos, lx->pos + 1, "E1024", "'_' in a numeric literal must be between digits");
                good = false;
            }
            last_underscore = true;
            lx->pos++;
        } else if (ok(c)) {
            *saw_digit = true;
            last_underscore = false;
            lx->pos++;
        } else if (is_digit(c)) {
            if (good) error(lx, lx->pos, lx->pos + 1, "E1024", "invalid digit for the literal's base");
            good = false;
            *saw_digit = true;
            last_underscore = false;
            lx->pos++;
        } else {
            break;
        }
    }
    if (last_underscore && good) {
        error(lx, lx->pos - 1, lx->pos, "E1024", "'_' in a numeric literal must be between digits");
        good = false;
    }
    return good;
}

static bool ok_dec(int c) { return is_digit(c); }
static bool ok_hex(int c) { return is_hex(c); }
static bool ok_bin(int c) { return c == '0' || c == '1'; }
static bool ok_oct(int c) { return c >= '0' && c <= '7'; }

static void number(lexer *lx)
{
    uint32_t lo = lx->pos;
    int base = 10;
    bool is_float = false;
    bool good = true;
    bool saw;

    if (peek(lx, 0) == '0' && (peek(lx, 1) == 'x' || peek(lx, 1) == 'X')) base = 16;
    else if (peek(lx, 0) == '0' && (peek(lx, 1) == 'o' || peek(lx, 1) == 'O')) base = 8;
    else if (peek(lx, 0) == '0' && (peek(lx, 1) == 'b' || peek(lx, 1) == 'B')) base = 2;

    uint32_t digits_lo;
    if (base != 10) {
        lx->pos += 2;
        digits_lo = lx->pos;
        if (base == 16) good &= digit_run(lx, ok_hex, &saw);
        else if (base == 8) good &= digit_run(lx, ok_oct, &saw);
        else good &= digit_run(lx, ok_bin, &saw);
        if (!saw && good) {
            error(lx, lo, lx->pos, "E1024", "numeric literal has no digits after its base prefix");
            good = false;
        }
        if (base == 16 && good && (peek(lx, 0) == '.' || peek(lx, 0) == 'p' || peek(lx, 0) == 'P')) {
            is_float = true;
            if (peek(lx, 0) == '.') {
                lx->pos++;
                good &= digit_run(lx, ok_hex, &saw);
            }
            if (peek(lx, 0) == 'p' || peek(lx, 0) == 'P') {
                lx->pos++;
                if (peek(lx, 0) == '+' || peek(lx, 0) == '-') lx->pos++;
                good &= digit_run(lx, ok_dec, &saw);
                if (!saw && good) {
                    error(lx, lo, lx->pos, "E1024", "hexadecimal float exponent has no digits");
                    good = false;
                }
            } else if (good) {
                error(lx, lo, lx->pos, "E1024",
                      "hexadecimal floating-point literal requires a 'p' exponent");
                good = false;
            }
        }
    } else {
        digits_lo = lx->pos;
        good &= digit_run(lx, ok_dec, &saw);
        if (good && lx->src[lo] == '0' && lx->pos - lo > 1) {
            nc_diag_error(&lx->s->diag, span(lx, lo, lx->pos), "E1020",
                          "decimal literal with a leading zero");
            nc_diag_help(&lx->s->diag, "write octal literals as `0o%.*s`; NovaC has no leading-zero octal (D-030)",
                         (int)(lx->pos - lo - 1), lx->src + lo + 1);
            good = false;
        }
        if (peek(lx, 0) == '.' && peek(lx, 1) != '.') {
            if (is_digit(peek(lx, 1))) {
                is_float = true;
                lx->pos++;
                good &= digit_run(lx, ok_dec, &saw);
            } else {
                lx->pos++;
                if (good) {
                    nc_diag_error(&lx->s->diag, span(lx, lo, lx->pos), "E1021",
                                  "floating-point literal needs digits after the '.'");
                    nc_diag_help(&lx->s->diag, "write `%.*s.0`", (int)(lx->pos - lo - 1), lx->src + lo);
                }
                good = false;
                is_float = true;
            }
        }
        int e = peek(lx, 0);
        if ((e == 'e' || e == 'E') &&
            (is_digit(peek(lx, 1)) || ((peek(lx, 1) == '+' || peek(lx, 1) == '-') && is_digit(peek(lx, 2))))) {
            is_float = true;
            lx->pos++;
            if (peek(lx, 0) == '+' || peek(lx, 0) == '-') lx->pos++;
            good &= digit_run(lx, ok_dec, &saw);
        }
    }
    uint32_t digits_hi = lx->pos;

    nc_suffix suf = NC_SUF_NONE;
    if (is_ident_start(peek(lx, 0))) {
        uint32_t slo = lx->pos;
        while (is_ident_char(peek(lx, 0))) lx->pos++;
        suf = suffix_from(lx->src + slo, lx->pos - slo);
        bool float_suf = suf == NC_SUF_F32 || suf == NC_SUF_F64;
        if (suf == NC_SUF_NONE) {
            if (good)
                nc_diag_error(&lx->s->diag, span(lx, slo, lx->pos), "E1022",
                              "unknown literal suffix '%.*s'", (int)(lx->pos - slo), lx->src + slo);
            good = false;
        } else if (float_suf && !is_float) {
            if (good) {
                nc_diag_error(&lx->s->diag, span(lx, slo, lx->pos), "E1022",
                              "floating-point suffix '%.*s' on an integer literal",
                              (int)(lx->pos - slo), lx->src + slo);
                nc_diag_help(&lx->s->diag, "write `%.*s.0%.*s`", (int)(slo - lo), lx->src + lo,
                             (int)(lx->pos - slo), lx->src + slo);
            }
            good = false;
        } else if (!float_suf && is_float) {
            if (good)
                nc_diag_error(&lx->s->diag, span(lx, slo, lx->pos), "E1022",
                              "integer suffix '%.*s' on a floating-point literal",
                              (int)(lx->pos - slo), lx->src + slo);
            good = false;
        }
    }

    nc_token *t = push(lx, is_float ? NC_T_FLOAT_LIT : NC_T_INT_LIT, lo);
    if (!good) return; /* value stays 0; an error was reported */

    if (is_float) {
        t->v.f.suffix = suf;
        char buf[512];
        size_t n = 0;
        for (uint32_t i = lo; i < digits_hi && n + 1 < sizeof buf; i++)
            if (lx->src[i] != '_') buf[n++] = lx->src[i];
        buf[n] = '\0';
        if (digits_hi - lo >= sizeof buf) {
            error(lx, lo, lx->pos, "E1025", "floating-point literal is too long");
            return;
        }
        errno = 0;
        double v = strtod(buf, NULL);
        if (isinf(v)) {
            error(lx, lo, lx->pos, "E1025", "floating-point literal is out of range for f64");
            return;
        }
        t->v.f.value = v;
        return;
    }

    t->v.i.suffix = suf;
    uint64_t value = 0;
    for (uint32_t i = digits_lo; i < digits_hi; i++) {
        int c = (unsigned char)lx->src[i];
        if (c == '_') continue;
        uint64_t dv = (uint64_t)hex_value(c);
        if (value > (UINT64_MAX - dv) / (uint64_t)base) {
            error(lx, lo, lx->pos, "E1023", "integer literal does not fit in 64 bits");
            return;
        }
        value = value * (uint64_t)base + dv;
    }
    t->v.i.value = value;
}

/* ---- characters and strings --------------------------------------------- */

/* Decode one escape sequence starting at the backslash. Appends the decoded
 * bytes to `out` (may be NULL). Returns false on an invalid escape
 * (reported). `allow_unicode` permits \u{...}. */
static bool escape(lexer *lx, nc_charvec *out, bool allow_unicode, int *byte_out)
{
    uint32_t lo = lx->pos;
    lx->pos++; /* backslash */
    int c = peek(lx, 0);
    int v = -1;
    switch (c) {
    case 'n': v = '\n'; break;
    case 'r': v = '\r'; break;
    case 't': v = '\t'; break;
    case '0': v = 0; break;
    case '\\': v = '\\'; break;
    case '\'': v = '\''; break;
    case '"': v = '"'; break;
    case 'x':
        if (is_hex(peek(lx, 1)) && is_hex(peek(lx, 2))) {
            v = hex_value(peek(lx, 1)) * 16 + hex_value(peek(lx, 2));
            lx->pos += 2;
        }
        break;
    case 'u':
        if (allow_unicode && peek(lx, 1) == '{') {
            uint32_t p = 2;
            uint32_t cp = 0;
            int nd = 0;
            while (is_hex(peek(lx, p)) && nd < 7) {
                cp = cp * 16 + (uint32_t)hex_value(peek(lx, p));
                p++;
                nd++;
            }
            if (nd >= 1 && nd <= 6 && peek(lx, p) == '}' && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                lx->pos += p + 1;
                if (out) {
                    if (cp < 0x80) {
                        nc_vec_push(out, (char)cp);
                    } else if (cp < 0x800) {
                        nc_vec_push(out, (char)(0xC0 | (cp >> 6)));
                        nc_vec_push(out, (char)(0x80 | (cp & 0x3F)));
                    } else if (cp < 0x10000) {
                        nc_vec_push(out, (char)(0xE0 | (cp >> 12)));
                        nc_vec_push(out, (char)(0x80 | ((cp >> 6) & 0x3F)));
                        nc_vec_push(out, (char)(0x80 | (cp & 0x3F)));
                    } else {
                        nc_vec_push(out, (char)(0xF0 | (cp >> 18)));
                        nc_vec_push(out, (char)(0x80 | ((cp >> 12) & 0x3F)));
                        nc_vec_push(out, (char)(0x80 | ((cp >> 6) & 0x3F)));
                        nc_vec_push(out, (char)(0x80 | (cp & 0x3F)));
                    }
                }
                return true;
            }
        }
        break;
    default:
        break;
    }
    if (v < 0) {
        if (c >= 0 && c != '\n') skip_utf8(lx);
        error(lx, lo, lx->pos, "E1031", "invalid escape sequence");
        return false;
    }
    lx->pos++;
    if (out) nc_vec_push(out, (char)v);
    if (byte_out) *byte_out = v;
    return true;
}

static void string_lit(lexer *lx, uint32_t lo, nc_tok kind)
{
    nc_charvec buf = {0};
    lx->pos++; /* opening quote */
    bool good = true;
    for (;;) {
        int c = peek(lx, 0);
        if (c < 0 || c == '\n') {
            error(lx, lo, lx->pos, "E1030", "unterminated string literal");
            good = false;
            break;
        }
        if (c == '"') {
            lx->pos++;
            break;
        }
        if (c == '\\') {
            good &= escape(lx, &buf, true, NULL);
            continue;
        }
        nc_vec_push(&buf, (char)c);
        lx->pos++;
    }
    nc_token *t = push(lx, kind, lo);
    if (good) t->v.sym = nc_intern(&lx->s->strtab, buf.data ? buf.data : "", buf.len);
    nc_vec_free(&buf);
}

static void char_lit(lexer *lx)
{
    uint32_t lo = lx->pos;
    lx->pos++; /* opening quote */
    int value = -1;
    bool good = true;
    int c = peek(lx, 0);
    if (c == '\'') {
        lx->pos++;
        error(lx, lo, lx->pos, "E1032", "empty character literal");
        push(lx, NC_T_CHAR_LIT, lo);
        return;
    }
    if (c < 0 || c == '\n') {
        error(lx, lo, lx->pos, "E1030", "unterminated character literal");
        push(lx, NC_T_CHAR_LIT, lo);
        return;
    }
    if (c == '\\') {
        good = escape(lx, NULL, false, &value);
    } else if (c >= 0x80) {
        skip_utf8(lx);
        good = false;
        if (peek(lx, 0) == '\'') {
            lx->pos++;
            error(lx, lo, lx->pos, "E1032", "character literal must be ASCII; use a string literal");
            push(lx, NC_T_CHAR_LIT, lo);
            return;
        }
    } else {
        value = c;
        lx->pos++;
    }
    if (peek(lx, 0) == '\'') {
        lx->pos++;
        if (good && value > 0x7F) {
            error(lx, lo, lx->pos, "E1032", "character literal must be ASCII (value at most 0x7F)");
            good = false;
        }
        nc_token *t = push(lx, NC_T_CHAR_LIT, lo);
        if (good) t->v.ch = (uint8_t)value;
        return;
    }
    /* More than one character, or no closing quote on this line. After an
     * invalid escape (already reported) only skip to the closing quote. */
    uint32_t p = lx->pos;
    while (p < lx->len && lx->src[p] != '\n' && lx->src[p] != '\'') p++;
    if (p < lx->len && lx->src[p] == '\'') {
        lx->pos = p + 1;
        if (good) error(lx, lo, lx->pos, "E1032", "character literal must contain exactly one character");
    } else {
        error(lx, lo, lx->pos, "E1030", "unterminated character literal");
    }
    push(lx, NC_T_CHAR_LIT, lo);
}

/* ---- identifiers and punctuation ------------------------------------------ */

static void identifier(lexer *lx)
{
    uint32_t lo = lx->pos;
    while (is_ident_char(peek(lx, 0))) lx->pos++;
    nc_sym sym = nc_intern(&lx->s->strtab, lx->src + lo, lx->pos - lo);
    nc_tok kind = NC_T_IDENT;
    if (sym <= lx->kw_max_sym && lx->kw_by_sym[sym] != NC_T_EOF) kind = lx->kw_by_sym[sym];
    nc_token *t = push(lx, kind, lo);
    if (kind == NC_T_IDENT) t->v.sym = sym;
}

static bool punct(lexer *lx)
{
    int c = peek(lx, 0), c1 = peek(lx, 1), c2 = peek(lx, 2);
    uint32_t lo = lx->pos;
    nc_tok k;
    int n = 1;
#define P3(a, b, d, kind) if (c == a && c1 == b && c2 == d) { k = kind; n = 3; goto done; }
#define P2(a, b, kind) if (c == a && c1 == b) { k = kind; n = 2; goto done; }
#define P1(a, kind) if (c == a) { k = kind; n = 1; goto done; }
    P3('.', '.', '.', NC_T_ELLIPSIS)
    P3('+', '%', '=', NC_T_PLUS_WRAP_ASSIGN)
    P3('-', '%', '=', NC_T_MINUS_WRAP_ASSIGN)
    P3('*', '%', '=', NC_T_STAR_WRAP_ASSIGN)
    P3('<', '<', '=', NC_T_SHL_ASSIGN)
    P2('-', '>', NC_T_ARROW) P2(':', ':', NC_T_COLONCOLON)
    P2('+', '%', NC_T_PLUS_WRAP) P2('-', '%', NC_T_MINUS_WRAP) P2('*', '%', NC_T_STAR_WRAP)
    P2('+', '+', NC_T_PLUSPLUS) P2('-', '-', NC_T_MINUSMINUS)
    P2('<', '<', NC_T_SHL) P2('&', '&', NC_T_ANDAND) P2('|', '|', NC_T_OROR)
    P2('+', '=', NC_T_PLUS_ASSIGN) P2('-', '=', NC_T_MINUS_ASSIGN) P2('*', '=', NC_T_STAR_ASSIGN)
    P2('/', '=', NC_T_SLASH_ASSIGN) P2('%', '=', NC_T_PERCENT_ASSIGN)
    P2('&', '=', NC_T_AMP_ASSIGN) P2('|', '=', NC_T_PIPE_ASSIGN) P2('^', '=', NC_T_CARET_ASSIGN)
    P2('=', '=', NC_T_EQ) P2('!', '=', NC_T_NE) P2('<', '=', NC_T_LE) P2('>', '=', NC_T_GE)
    P1('{', NC_T_LBRACE) P1('}', NC_T_RBRACE) P1('(', NC_T_LPAREN) P1(')', NC_T_RPAREN)
    P1('[', NC_T_LBRACKET) P1(']', NC_T_RBRACKET) P1(';', NC_T_SEMI) P1(',', NC_T_COMMA)
    P1('.', NC_T_DOT) P1(':', NC_T_COLON) P1('?', NC_T_QUESTION) P1('@', NC_T_AT)
    P1('+', NC_T_PLUS) P1('-', NC_T_MINUS) P1('*', NC_T_STAR) P1('/', NC_T_SLASH)
    P1('%', NC_T_PERCENT) P1('&', NC_T_AMP) P1('|', NC_T_PIPE) P1('^', NC_T_CARET)
    P1('~', NC_T_TILDE) P1('!', NC_T_BANG) P1('=', NC_T_ASSIGN) P1('<', NC_T_LT)
    P1('>', NC_T_GT)
#undef P1
#undef P2
#undef P3
    return false;
done:
    lx->pos += (uint32_t)n;
    push(lx, k, lo);
    return true;
}

/* ---- driver ---------------------------------------------------------------- */

nc_token_list nc_lex(nc_session *s, uint32_t file)
{
    const nc_source_file *f = nc_source_get(&s->sources, file);
    lexer lx;
    memset(&lx, 0, sizeof lx);
    lx.s = s;
    lx.file = file;
    lx.src = f->data;
    lx.len = f->len;
    lx.line_start = true;

    size_t nkw = sizeof keywords / sizeof keywords[0];
    nc_sym kw_syms[sizeof keywords / sizeof keywords[0]];
    for (size_t i = 0; i < nkw; i++) {
        kw_syms[i] = nc_intern_cstr(&s->strtab, keywords[i].text);
        if (kw_syms[i] > lx.kw_max_sym) lx.kw_max_sym = kw_syms[i];
    }
    lx.kw_by_sym = nc_xcalloc(lx.kw_max_sym + 1, sizeof *lx.kw_by_sym); /* NC_T_EOF == 0 */
    for (size_t i = 0; i < nkw; i++) lx.kw_by_sym[kw_syms[i]] = keywords[i].kind;

    if (lx.len >= 3 && (unsigned char)lx.src[0] == 0xEF && (unsigned char)lx.src[1] == 0xBB &&
        (unsigned char)lx.src[2] == 0xBF) {
        error(&lx, 0, 3, "E1003", "source files must not begin with a byte-order mark");
        lx.pos = 3;
    }

    while (lx.pos < lx.len) {
        int c = peek(&lx, 0);
        if (c == '\n') {
            lx.pos++;
            lx.line_start = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            lx.pos++;
            continue;
        }
        if (c == '/' && peek(&lx, 1) == '/') {
            while (lx.pos < lx.len && lx.src[lx.pos] != '\n') lx.pos++;
            continue;
        }
        if (c == '/' && peek(&lx, 1) == '*') {
            block_comment(&lx);
            continue;
        }
        bool at_line_start = lx.line_start;
        lx.line_start = false;

        if (c == '#' && at_line_start) {
            directive(&lx);
            continue;
        }
        if (is_digit(c)) {
            number(&lx);
        } else if (c == '.' && is_digit(peek(&lx, 1))) {
            uint32_t lo = lx.pos;
            lx.pos++;
            while (is_digit(peek(&lx, 0)) || peek(&lx, 0) == '_') lx.pos++;
            nc_diag_error(&s->diag, span(&lx, lo, lx.pos), "E1021",
                          "floating-point literal needs a digit before the '.'");
            nc_diag_help(&s->diag, "write `0%.*s`", (int)(lx.pos - lo), lx.src + lo);
            push(&lx, NC_T_FLOAT_LIT, lo);
        } else if (c == 'c' && peek(&lx, 1) == '"') {
            uint32_t lo = lx.pos;
            lx.pos++;
            string_lit(&lx, lo, NC_T_CSTRING_LIT);
        } else if (is_ident_start(c)) {
            identifier(&lx);
        } else if (c == '"') {
            string_lit(&lx, lx.pos, NC_T_STRING_LIT);
        } else if (c == '\'') {
            char_lit(&lx);
        } else if (!punct(&lx)) {
            uint32_t lo = lx.pos;
            if (c >= 0x80) {
                skip_utf8(&lx);
                error(&lx, lo, lx.pos, "E1002", "non-ASCII character outside a comment or string literal");
            } else {
                lx.pos++;
                if (c >= 0x21 && c <= 0x7E)
                    nc_diag_error(&s->diag, span(&lx, lo, lx.pos), "E1002", "unexpected character '%c'", c);
                else
                    nc_diag_error(&s->diag, span(&lx, lo, lx.pos), "E1002",
                                  "unexpected character (byte 0x%02X)", (unsigned)c);
            }
        }
    }
    push(&lx, NC_T_EOF, lx.len);
    for (size_t i = 0; i + 1 < lx.toks.len; i++)
        lx.toks.data[i].joint = lx.toks.data[i + 1].span.lo == lx.toks.data[i].span.hi;

    nc_token_list out;
    out.count = (uint32_t)lx.toks.len;
    out.tokens = nc_arena_memdup(&s->arena, lx.toks.data, lx.toks.len * sizeof(nc_token));
    nc_vec_free(&lx.toks);
    free(lx.kw_by_sym);
    return out;
}

/* ---- dump ------------------------------------------------------------------ */

void nc_print_escaped(FILE *out, const char *s, size_t n)
{
    fputc('"', out);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        case '\\': fputs("\\\\", out); break;
        case '"': fputs("\\\"", out); break;
        default:
            if (c < 0x20 || c >= 0x7F) fprintf(out, "\\x%02X", c);
            else fputc(c, out);
        }
    }
    fputc('"', out);
}

void nc_lex_dump(nc_session *s, nc_token_list toks, FILE *out)
{
    for (uint32_t i = 0; i < toks.count; i++) {
        const nc_token *t = &toks.tokens[i];
        uint32_t line, col;
        nc_source_line_col(&s->sources, t->span.file, t->span.lo, &line, &col);
        fprintf(out, "%u:%u ", line, col);
        switch (t->kind) {
        case NC_T_EOF: fputs("eof", out); break;
        case NC_T_IDENT: fprintf(out, "ident %s", nc_sym_str(&s->strtab, t->v.sym)); break;
        case NC_T_INT_LIT:
            fprintf(out, "int %llu", (unsigned long long)t->v.i.value);
            if (t->v.i.suffix) fprintf(out, " %s", nc_suffix_name(t->v.i.suffix));
            break;
        case NC_T_FLOAT_LIT:
            fprintf(out, "float %.17g", t->v.f.value);
            if (t->v.f.suffix) fprintf(out, " %s", nc_suffix_name(t->v.f.suffix));
            break;
        case NC_T_CHAR_LIT: fprintf(out, "char %u", (unsigned)t->v.ch); break;
        case NC_T_STRING_LIT:
        case NC_T_CSTRING_LIT:
            fputs(t->kind == NC_T_STRING_LIT ? "string " : "cstring ", out);
            nc_print_escaped(out, nc_sym_str(&s->strtab, t->v.sym), nc_sym_len(&s->strtab, t->v.sym));
            break;
        default:
            fprintf(out, "%s %s", nc_tok_is_keyword(t->kind) ? "keyword" : "punct",
                    nc_tok_spelling(t->kind));
        }
        if (t->joint && t->kind == NC_T_GT) fputs(" joint", out);
        fputc('\n', out);
    }
}
