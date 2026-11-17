/* Parser infrastructure: token access, diagnostics, recovery, allocation. */
#include "parse/parser_internal.h"

#include <stdarg.h>
#include <string.h>

const nc_token *nc_p_cur(nc_parser *p)
{
    return &p->toks[p->pos];
}

const nc_token *nc_p_peek(nc_parser *p, uint32_t ahead)
{
    uint32_t i = p->pos + ahead;
    if (i >= p->count) i = p->count - 1; /* EOF */
    return &p->toks[i];
}

const nc_token *nc_p_prev(nc_parser *p)
{
    return &p->toks[p->pos ? p->pos - 1 : 0];
}

bool nc_p_at(nc_parser *p, nc_tok kind)
{
    return p->toks[p->pos].kind == kind;
}

const nc_token *nc_p_advance(nc_parser *p)
{
    const nc_token *t = &p->toks[p->pos];
    if (t->kind != NC_T_EOF) p->pos++;
    return t;
}

bool nc_p_accept(nc_parser *p, nc_tok kind)
{
    if (!nc_p_at(p, kind)) return false;
    nc_p_advance(p);
    return true;
}

nc_span nc_p_cur_span(nc_parser *p)
{
    return nc_p_cur(p)->span;
}

nc_span nc_p_prev_end(nc_parser *p)
{
    if (p->pos == 0) return nc_p_cur_span(p);
    nc_span s = nc_p_prev(p)->span;
    s.lo = s.hi;
    return s;
}

void nc_p_describe(nc_parser *p, const nc_token *t, char *buf, size_t n)
{
    switch (t->kind) {
    case NC_T_EOF: snprintf(buf, n, "end of file"); break;
    case NC_T_IDENT: snprintf(buf, n, "identifier '%s'", nc_sym_str(&p->s->strtab, t->v.sym)); break;
    case NC_T_INT_LIT:
    case NC_T_FLOAT_LIT:
    case NC_T_CHAR_LIT:
    case NC_T_STRING_LIT:
    case NC_T_CSTRING_LIT: snprintf(buf, n, "%s", nc_tok_spelling(t->kind)); break;
    default:
        snprintf(buf, n, "%s '%s'", nc_tok_is_keyword(t->kind) ? "keyword" : "token",
                 nc_tok_spelling(t->kind));
    }
}

static bool report(nc_parser *p, nc_span span, const char *code, const char *fmt, va_list ap)
{
    if (p->panic) return false;
    char msg[512];
    vsnprintf(msg, sizeof msg, fmt, ap);
    unsigned before = p->s->diag.errors;
    nc_diag_error(&p->s->diag, span, code, "%s", msg);
    return p->s->diag.errors > before && !p->s->diag.last_suppressed;
}

bool nc_p_error(nc_parser *p, nc_span span, const char *code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    bool shown = report(p, span, code, fmt, ap);
    va_end(ap);
    p->panic = true;
    return shown;
}

bool nc_p_recoverable(nc_parser *p, nc_span span, const char *code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    bool shown = report(p, span, code, fmt, ap);
    va_end(ap);
    return shown;
}

void nc_p_expected(nc_parser *p, const char *what)
{
    char found[160];
    nc_p_describe(p, nc_p_cur(p), found, sizeof found);
    /* A missing terminator is reported where it belongs: after the previous
     * token, not at the start of the next line. */
    bool at_prev = nc_p_cur(p)->kind != NC_T_EOF &&
                   (strcmp(what, "';'") == 0 || strcmp(what, "')'") == 0 || strcmp(what, "']'") == 0);
    nc_p_error(p, at_prev ? nc_p_prev_end(p) : nc_p_cur_span(p), "E2001", "expected %s, found %s", what, found);
}

/* Is there a line break between the previous token and the current one? */
static bool newline_before_cur(nc_parser *p)
{
    if (p->pos == 0) return false;
    const char *src = nc_source_get(&p->s->sources, p->file)->data;
    for (uint32_t i = nc_p_prev(p)->span.hi; i < nc_p_cur(p)->span.lo; i++)
        if (src[i] == '\n') return true;
    return false;
}

bool nc_p_expect(nc_parser *p, nc_tok kind, const char *what)
{
    if (nc_p_accept(p, kind)) return true;
    if (kind == NC_T_SEMI && !p->panic && nc_p_cur(p)->kind != NC_T_EOF && newline_before_cur(p)) {
        /* A `;` missing at the end of a line: report it and continue as if it
         * were there, so the next line is parsed normally. */
        char found[160];
        nc_p_describe(p, nc_p_cur(p), found, sizeof found);
        nc_p_recoverable(p, nc_p_prev_end(p), "E2001", "expected %s, found %s", what, found);
        return true;
    }
    nc_p_expected(p, what);
    return false;
}

bool nc_p_enter(nc_parser *p)
{
    if (p->depth >= NC_PARSE_MAX_DEPTH) {
        if (!p->depth_reported) {
            p->depth_reported = true;
            nc_diag_error(&p->s->diag, nc_p_cur_span(p), "E9004",
                          "nesting is too deep (the limit is %d levels of syntax tree)", NC_PARSE_MAX_DEPTH);
        }
        p->panic = true;
        return false;
    }
    p->depth++;
    return true;
}

void nc_p_leave(nc_parser *p)
{
    p->depth--;
}

/* ---- recovery ---- */

void nc_p_skip_balanced(nc_parser *p)
{
    unsigned depth = 0;
    do {
        nc_tok k = nc_p_cur(p)->kind;
        if (k == NC_T_EOF) return;
        if (k == NC_T_LPAREN || k == NC_T_LBRACKET || k == NC_T_LBRACE) depth++;
        else if (k == NC_T_RPAREN || k == NC_T_RBRACKET || k == NC_T_RBRACE) depth--;
        nc_p_advance(p);
    } while (depth > 0);
}

/* Skip the rest of a broken statement: through the next `;`, or through a
 * `{...}` block, stopping before a `}` that closes an enclosing block.
 * Parentheses and brackets are not balanced across a `;`, so a missing `)`
 * does not swallow the following statements. */
void nc_p_sync_stmt(nc_parser *p)
{
    for (;;) {
        nc_tok k = nc_p_cur(p)->kind;
        if (k == NC_T_EOF || k == NC_T_RBRACE) return;
        if (k == NC_T_SEMI) {
            nc_p_advance(p);
            return;
        }
        if (k == NC_T_LBRACE) {
            nc_p_skip_balanced(p);
            return; /* a skipped block ends the broken statement */
        }
        nc_p_advance(p);
    }
}

void nc_p_skip_decl(nc_parser *p)
{
    for (;;) {
        nc_tok k = nc_p_cur(p)->kind;
        if (k == NC_T_EOF) return;
        if (k == NC_T_SEMI) {
            nc_p_advance(p);
            return;
        }
        if (k == NC_T_LBRACE) {
            nc_p_skip_balanced(p);
            nc_p_accept(p, NC_T_SEMI);
            return;
        }
        if (k == NC_T_LPAREN || k == NC_T_LBRACKET) {
            nc_p_skip_balanced(p);
            continue;
        }
        if (k == NC_T_RBRACE) {
            nc_p_advance(p); /* stray closer at module level */
            return;
        }
        nc_p_advance(p);
    }
}

/* ---- allocation ---- */

void *nc_p_alloc(nc_parser *p, size_t size)
{
    return nc_arena_alloc(&p->s->arena, size);
}

nc_expr *nc_p_expr(nc_parser *p, nc_expr_kind kind, nc_span span)
{
    nc_expr *e = nc_p_alloc(p, sizeof *e);
    e->kind = kind;
    e->span = span;
    return e;
}

nc_stmt *nc_p_stmt(nc_parser *p, nc_stmt_kind kind, nc_span span)
{
    nc_stmt *s = nc_p_alloc(p, sizeof *s);
    s->kind = kind;
    s->span = span;
    return s;
}

nc_type_expr *nc_p_type_node(nc_parser *p, nc_type_expr_kind kind, nc_span span)
{
    nc_type_expr *t = nc_p_alloc(p, sizeof *t);
    t->kind = kind;
    t->span = span;
    return t;
}

nc_expr **nc_p_expr_list(nc_parser *p, nc_expr_vec *v)
{
    nc_expr **out = v->len ? nc_arena_memdup(&p->s->arena, v->data, v->len * sizeof *v->data) : NULL;
    nc_vec_free(v);
    return out;
}
