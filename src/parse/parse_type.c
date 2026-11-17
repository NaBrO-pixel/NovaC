/* Types (NOVAC_GRAMMAR.md §4). Every type is written before the declared
 * name, with postfix constructors (D-005). */
#include "parse/parser_internal.h"

#include <string.h>

static const char *const c_words[7] = {"char", "short", "long", "unsigned", "signed", "float", "double"};

bool nc_p_is_c_type_word(nc_parser *p, const nc_token *t)
{
    if (t->kind != NC_T_IDENT) return false;
    if (!p->sym_c_words[0])
        for (int i = 0; i < 7; i++) p->sym_c_words[i] = nc_intern_cstr(&p->s->strtab, c_words[i]);
    for (int i = 0; i < 7; i++)
        if (t->v.sym == p->sym_c_words[i]) return true;
    return false;
}

static bool is_prim_token(nc_tok k)
{
    nc_prim unused;
    return nc_prim_from_token(k, &unused);
}

/* `unsigned long`, `char`, `long long int`, ...: report E3011 and recover
 * with the NovaC type that has the same meaning on LP64 targets (D-031). */
static nc_type_expr *c_type_name(nc_parser *p)
{
    nc_span span = nc_p_cur_span(p);
    int n_unsigned = 0, n_signed = 0, n_long = 0, n_short = 0, n_char = 0, n_float = 0, n_double = 0;
    char written[64] = "";
    while (nc_p_is_c_type_word(p, nc_p_cur(p)) || nc_p_at(p, NC_T_KW_INT)) {
        const nc_token *t = nc_p_advance(p);
        const char *w = t->kind == NC_T_KW_INT ? "int" : nc_sym_str(&p->s->strtab, t->v.sym);
        if (strlen(written) + strlen(w) + 2 < sizeof written) {
            if (written[0]) strcat(written, " ");
            strcat(written, w);
        }
        if (strcmp(w, "unsigned") == 0) n_unsigned++;
        else if (strcmp(w, "signed") == 0) n_signed++;
        else if (strcmp(w, "long") == 0) n_long++;
        else if (strcmp(w, "short") == 0) n_short++;
        else if (strcmp(w, "char") == 0) n_char++;
        else if (strcmp(w, "float") == 0) n_float++;
        else if (strcmp(w, "double") == 0) n_double++;
        span = nc_span_join(span, t->span);
    }
    nc_prim prim;
    const char *ffi;
    if (n_double) {
        prim = NC_PRIM_F64;
        ffi = n_long ? "long double is not supported (NOVAC_ABI.md §11.4)" : "c_double";
    } else if (n_float) {
        prim = NC_PRIM_F32;
        ffi = "c_float";
    } else if (n_char) {
        prim = n_signed ? NC_PRIM_I8 : NC_PRIM_U8;
        ffi = n_unsigned ? "c_uchar" : n_signed ? "c_schar" : "c_char (its signedness differs between targets)";
    } else if (n_short) {
        prim = n_unsigned ? NC_PRIM_U16 : NC_PRIM_I16;
        ffi = n_unsigned ? "c_ushort" : "c_short";
    } else if (n_long) {
        prim = n_unsigned ? NC_PRIM_U64 : NC_PRIM_I64;
        ffi = n_long > 1 ? (n_unsigned ? "c_ulonglong" : "c_longlong") : (n_unsigned ? "c_ulong" : "c_long");
    } else {
        prim = n_unsigned ? NC_PRIM_U32 : NC_PRIM_I32;
        ffi = n_unsigned ? "c_uint" : "c_int";
    }
    if (nc_p_recoverable(p, span, "E3011", "'%s' is a C type name, not a NovaC type", written)) {
        nc_diag_help(&p->s->diag, "use `%s`; for C interoperability use core.ffi: %s (D-031)",
                     nc_prim_name(prim), ffi);
    }
    nc_type_expr *t = nc_p_type_node(p, NC_TE_PRIM, span);
    t->u.prim = prim;
    return t;
}

bool nc_parse_generic_args(nc_parser *p, nc_generic_arg **out, uint32_t *n)
{
    /* at '<' */
    nc_p_advance(p);
    NC_VEC(nc_generic_arg) args = {0};
    if (!nc_p_at(p, NC_T_GT)) {
        do {
            nc_generic_arg a = {0};
            if (nc_p_at(p, NC_T_INT_LIT)) {
                /* Only the literal: `f::<u32, 4>(x)` must not parse `4 > (x)`. */
                const nc_token *t = nc_p_advance(p);
                a.expr = nc_p_expr(p, NC_E_INT, t->span);
                a.expr->u.int_lit.value = t->v.i.value;
                a.expr->u.int_lit.suffix = t->v.i.suffix;
            } else if (nc_p_at(p, NC_T_LPAREN)) {
                nc_p_advance(p);
                a.expr = nc_parse_expr(p);
                nc_p_expect(p, NC_T_RPAREN, "')'");
            } else {
                a.type = nc_parse_type(p);
            }
            nc_vec_push(&args, a);
        } while (nc_p_accept(p, NC_T_COMMA));
    }
    bool ok = nc_p_expect(p, NC_T_GT, "'>' to close the generic argument list");
    *n = (uint32_t)args.len;
    *out = args.len ? nc_arena_memdup(&p->s->arena, args.data, args.len * sizeof *args.data) : NULL;
    nc_vec_free(&args);
    return ok;
}

static nc_type_expr *parse_base(nc_parser *p)
{
    const nc_token *t = nc_p_cur(p);
    nc_prim prim;
    if (nc_prim_from_token(t->kind, &prim)) {
        nc_p_advance(p);
        nc_type_expr *te = nc_p_type_node(p, NC_TE_PRIM, t->span);
        te->u.prim = prim;
        return te;
    }
    if (t->kind == NC_T_KW_SELF_TYPE) {
        nc_p_advance(p);
        return nc_p_type_node(p, NC_TE_SELF, t->span);
    }
    if (nc_p_is_c_type_word(p, t)) return c_type_name(p);
    if (t->kind == NC_T_IDENT) {
        NC_VEC(nc_sym) parts = {0};
        nc_span span = t->span;
        nc_vec_push(&parts, nc_p_advance(p)->v.sym);
        while (nc_p_at(p, NC_T_DOT) && nc_p_peek(p, 1)->kind == NC_T_IDENT) {
            nc_p_advance(p);
            const nc_token *id = nc_p_advance(p);
            nc_vec_push(&parts, id->v.sym);
            span = nc_span_join(span, id->span);
        }
        nc_type_expr *te = nc_p_type_node(p, NC_TE_PATH, span);
        te->u.path.path.count = (uint32_t)parts.len;
        te->u.path.path.parts = nc_arena_memdup(&p->s->arena, parts.data, parts.len * sizeof *parts.data);
        te->u.path.path.span = span;
        nc_vec_free(&parts);
        if (nc_p_at(p, NC_T_LT)) {
            nc_parse_generic_args(p, &te->u.path.args, &te->u.path.nargs);
            te->span = nc_span_join(te->span, nc_p_prev(p)->span);
        }
        return te;
    }
    nc_p_expected(p, "a type");
    return nc_p_type_node(p, NC_TE_ERROR, t->span);
}

nc_type_expr *nc_parse_type(nc_parser *p)
{
    if (!nc_p_enter(p)) return nc_p_type_node(p, NC_TE_ERROR, nc_p_cur_span(p));
    nc_span start = nc_p_cur_span(p);
    bool leading_const = nc_p_accept(p, NC_T_KW_CONST);
    nc_type_expr *t = parse_base(p);
    t->is_const = leading_const;
    if (leading_const) t->span = nc_span_join(start, t->span);

    unsigned links = 0; /* each suffix adds one level to the type tree */
    for (;;) {
        nc_type_expr *n;
        bool suffix = nc_p_at(p, NC_T_STAR) || nc_p_at(p, NC_T_AMP) || nc_p_at(p, NC_T_LBRACKET) ||
                      nc_p_at(p, NC_T_KW_FN);
        if (!suffix) break;
        if (!nc_p_enter(p)) {
            t = nc_p_type_node(p, NC_TE_ERROR, t->span);
            break;
        }
        links++;
        if (nc_p_at(p, NC_T_STAR) || nc_p_at(p, NC_T_AMP)) {
            bool ptr = nc_p_at(p, NC_T_STAR);
            nc_p_advance(p);
            n = nc_p_type_node(p, ptr ? NC_TE_PTR : NC_TE_REF, nc_span_join(t->span, nc_p_prev(p)->span));
            n->u.inner = t;
            if (nc_p_accept(p, NC_T_KW_CONST)) {
                n->is_const = true;
                n->span = nc_span_join(n->span, nc_p_prev(p)->span);
            }
        } else if (nc_p_at(p, NC_T_LBRACKET)) {
            nc_p_advance(p);
            n = nc_p_type_node(p, NC_TE_ARRAY, t->span);
            n->u.array.elem = t;
            n->u.array.len = nc_parse_expr(p);
            nc_p_expect(p, NC_T_RBRACKET, "']'");
            n->span = nc_span_join(t->span, nc_p_prev(p)->span);
        } else if (nc_p_at(p, NC_T_KW_FN)) {
            nc_p_advance(p);
            n = nc_p_type_node(p, NC_TE_FN, t->span);
            n->u.fn.ret = t;
            NC_VEC(nc_type_expr *) params = {0};
            if (nc_p_expect(p, NC_T_LPAREN, "'(' after 'fn'")) {
                if (!nc_p_at(p, NC_T_RPAREN)) {
                    do {
                        if (nc_p_accept(p, NC_T_ELLIPSIS)) {
                            n->u.fn.variadic = true;
                            break;
                        }
                        nc_type_expr *pt = nc_parse_type(p);
                        nc_vec_push(&params, pt);
                    } while (nc_p_accept(p, NC_T_COMMA));
                }
                nc_p_expect(p, NC_T_RPAREN, "')'");
            }
            n->u.fn.nparams = (uint32_t)params.len;
            n->u.fn.params = params.len ? nc_arena_memdup(&p->s->arena, params.data, params.len * sizeof *params.data) : NULL;
            nc_vec_free(&params);
            n->span = nc_span_join(t->span, nc_p_prev(p)->span);
        } else {
            break;
        }
        t = n;
    }
    while (links--) nc_p_leave(p);
    nc_p_leave(p);
    return t;
}

/* ---- lookahead without building ---- */

static uint32_t skip_group(nc_parser *p, uint32_t pos, nc_tok open, nc_tok close)
{
    unsigned depth = 0;
    for (; pos < p->count; pos++) {
        nc_tok k = p->toks[pos].kind;
        if (k == open) depth++;
        else if (k == close) {
            if (--depth == 0) return pos + 1;
        } else if (k == NC_T_SEMI || k == NC_T_LBRACE || k == NC_T_RBRACE || k == NC_T_EOF) {
            return UINT32_MAX;
        }
    }
    return UINT32_MAX;
}

uint32_t nc_skip_type(nc_parser *p, uint32_t pos)
{
    if (pos < p->count && p->toks[pos].kind == NC_T_KW_CONST) pos++;
    if (pos >= p->count) return UINT32_MAX;
    const nc_token *t = &p->toks[pos];
    if (is_prim_token(t->kind) || t->kind == NC_T_KW_SELF_TYPE) {
        pos++;
    } else if (nc_p_is_c_type_word(p, t)) {
        while (pos < p->count && (nc_p_is_c_type_word(p, &p->toks[pos]) || p->toks[pos].kind == NC_T_KW_INT)) pos++;
    } else if (t->kind == NC_T_IDENT) {
        pos++;
        while (pos + 1 < p->count && p->toks[pos].kind == NC_T_DOT && p->toks[pos + 1].kind == NC_T_IDENT) pos += 2;
        if (pos < p->count && p->toks[pos].kind == NC_T_LT) {
            pos = skip_group(p, pos, NC_T_LT, NC_T_GT);
            if (pos == UINT32_MAX) return pos;
        }
    } else {
        return UINT32_MAX;
    }
    for (;;) {
        if (pos >= p->count) return UINT32_MAX;
        nc_tok k = p->toks[pos].kind;
        if (k == NC_T_STAR || k == NC_T_AMP) {
            pos++;
            if (pos < p->count && p->toks[pos].kind == NC_T_KW_CONST) pos++;
        } else if (k == NC_T_LBRACKET) {
            pos = skip_group(p, pos, NC_T_LBRACKET, NC_T_RBRACKET);
            if (pos == UINT32_MAX) return pos;
        } else if (k == NC_T_KW_FN) {
            pos++;
            if (pos >= p->count || p->toks[pos].kind != NC_T_LPAREN) return UINT32_MAX;
            pos = skip_group(p, pos, NC_T_LPAREN, NC_T_RPAREN);
            if (pos == UINT32_MAX) return pos;
        } else {
            return pos;
        }
    }
}
