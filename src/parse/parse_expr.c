/* Expressions (NOVAC_GRAMMAR.md §6): precedence climbing over the table in
 * §6, with non-associative comparisons (D-005) and C-style cast detection
 * (D-010). */
#include "parse/parser_internal.h"

#include <string.h>

/* Binary operator precedence levels; higher binds tighter. */
enum {
    PREC_NONE = 0,
    PREC_OR = 1,
    PREC_AND,
    PREC_BITOR,
    PREC_BITXOR,
    PREC_BITAND,
    PREC_EQ,     /* non-associative */
    PREC_REL,    /* non-associative, includes `is` */
    PREC_SHIFT,
    PREC_ADD,
    PREC_MUL,
};

typedef struct binop_info {
    nc_binop op;
    int prec;
    int ntoks; /* `>>` is two `>` tokens */
    bool is_is;
} binop_info;

/* `>` `>` (joint) is `>>`; `>` `>=` (joint) is `>>=`. */
static bool at_shr(nc_parser *p)
{
    return nc_p_at(p, NC_T_GT) && nc_p_cur(p)->joint && nc_p_peek(p, 1)->kind == NC_T_GT;
}

static bool at_shr_assign(nc_parser *p)
{
    return nc_p_at(p, NC_T_GT) && nc_p_cur(p)->joint && nc_p_peek(p, 1)->kind == NC_T_GE;
}

static binop_info binop_at(nc_parser *p)
{
    binop_info b = {NC_BIN_NONE, PREC_NONE, 1, false};
    if (at_shr(p)) return (binop_info){NC_BIN_SHR, PREC_SHIFT, 2, false};
    if (at_shr_assign(p)) return b;
    switch (nc_p_cur(p)->kind) {
    case NC_T_OROR: return (binop_info){NC_BIN_OR, PREC_OR, 1, false};
    case NC_T_ANDAND: return (binop_info){NC_BIN_AND, PREC_AND, 1, false};
    case NC_T_PIPE: return (binop_info){NC_BIN_BITOR, PREC_BITOR, 1, false};
    case NC_T_CARET: return (binop_info){NC_BIN_BITXOR, PREC_BITXOR, 1, false};
    case NC_T_AMP: return (binop_info){NC_BIN_BITAND, PREC_BITAND, 1, false};
    case NC_T_EQ: return (binop_info){NC_BIN_EQ, PREC_EQ, 1, false};
    case NC_T_NE: return (binop_info){NC_BIN_NE, PREC_EQ, 1, false};
    case NC_T_LT: return (binop_info){NC_BIN_LT, PREC_REL, 1, false};
    case NC_T_LE: return (binop_info){NC_BIN_LE, PREC_REL, 1, false};
    case NC_T_GT: return (binop_info){NC_BIN_GT, PREC_REL, 1, false};
    case NC_T_GE: return (binop_info){NC_BIN_GE, PREC_REL, 1, false};
    case NC_T_KW_IS: return (binop_info){NC_BIN_NONE, PREC_REL, 1, true};
    case NC_T_SHL: return (binop_info){NC_BIN_SHL, PREC_SHIFT, 1, false};
    case NC_T_PLUS: return (binop_info){NC_BIN_ADD, PREC_ADD, 1, false};
    case NC_T_MINUS: return (binop_info){NC_BIN_SUB, PREC_ADD, 1, false};
    case NC_T_PLUS_WRAP: return (binop_info){NC_BIN_WADD, PREC_ADD, 1, false};
    case NC_T_MINUS_WRAP: return (binop_info){NC_BIN_WSUB, PREC_ADD, 1, false};
    case NC_T_STAR: return (binop_info){NC_BIN_MUL, PREC_MUL, 1, false};
    case NC_T_SLASH: return (binop_info){NC_BIN_DIV, PREC_MUL, 1, false};
    case NC_T_PERCENT: return (binop_info){NC_BIN_MOD, PREC_MUL, 1, false};
    case NC_T_STAR_WRAP: return (binop_info){NC_BIN_WMUL, PREC_MUL, 1, false};
    default: return b;
    }
}

static bool assign_op_at(nc_parser *p, nc_binop *op, int *ntoks)
{
    *ntoks = 1;
    if (at_shr_assign(p)) {
        *op = NC_BIN_SHR;
        *ntoks = 2;
        return true;
    }
    switch (nc_p_cur(p)->kind) {
    case NC_T_ASSIGN: *op = NC_BIN_NONE; return true;
    case NC_T_PLUS_ASSIGN: *op = NC_BIN_ADD; return true;
    case NC_T_MINUS_ASSIGN: *op = NC_BIN_SUB; return true;
    case NC_T_STAR_ASSIGN: *op = NC_BIN_MUL; return true;
    case NC_T_SLASH_ASSIGN: *op = NC_BIN_DIV; return true;
    case NC_T_PERCENT_ASSIGN: *op = NC_BIN_MOD; return true;
    case NC_T_PLUS_WRAP_ASSIGN: *op = NC_BIN_WADD; return true;
    case NC_T_MINUS_WRAP_ASSIGN: *op = NC_BIN_WSUB; return true;
    case NC_T_STAR_WRAP_ASSIGN: *op = NC_BIN_WMUL; return true;
    case NC_T_SHL_ASSIGN: *op = NC_BIN_SHL; return true;
    case NC_T_AMP_ASSIGN: *op = NC_BIN_BITAND; return true;
    case NC_T_PIPE_ASSIGN: *op = NC_BIN_BITOR; return true;
    case NC_T_CARET_ASSIGN: *op = NC_BIN_BITXOR; return true;
    default: return false;
    }
}

static nc_expr *error_expr(nc_parser *p, nc_span span)
{
    return nc_p_expr(p, NC_E_ERROR, span);
}

static nc_expr *parse_prefix(nc_parser *p);
static nc_expr *parse_binary(nc_parser *p, int min_prec);
static nc_expr *parse_cond(nc_parser *p);

/* Can this token begin an expression? (Used to recognise `(T)x` casts.) */
static bool starts_expr(nc_parser *p, const nc_token *t)
{
    switch (t->kind) {
    case NC_T_IDENT: case NC_T_INT_LIT: case NC_T_FLOAT_LIT: case NC_T_CHAR_LIT:
    case NC_T_STRING_LIT: case NC_T_CSTRING_LIT: case NC_T_LPAREN:
    case NC_T_KW_TRUE: case NC_T_KW_FALSE: case NC_T_KW_NULL: case NC_T_KW_NONE:
    case NC_T_KW_SELF: case NC_T_KW_SOME: case NC_T_KW_SIZEOF: case NC_T_KW_ALIGNOF:
    case NC_T_KW_OFFSETOF: case NC_T_KW_TRY:
    case NC_T_MINUS: case NC_T_PLUS: case NC_T_BANG: case NC_T_TILDE: case NC_T_STAR:
    case NC_T_AMP: case NC_T_PLUSPLUS: case NC_T_MINUSMINUS:
        return true;
    default: {
        nc_prim unused;
        (void)p;
        return nc_prim_from_token(t->kind, &unused);
    }
    }
}

/* ---- initialisers -------------------------------------------------------------- */

bool nc_parse_init_items(nc_parser *p, nc_init_item **out, uint32_t *n)
{
    /* at '{' */
    nc_p_advance(p);
    NC_VEC(nc_init_item) items = {0};
    while (!nc_p_at(p, NC_T_RBRACE) && !nc_p_at(p, NC_T_EOF)) {
        nc_init_item it = {0};
        it.span = nc_p_cur_span(p);
        if (nc_p_at(p, NC_T_DOT) && nc_p_peek(p, 1)->kind == NC_T_IDENT) {
            nc_p_advance(p);
            it.field = nc_p_advance(p)->v.sym;
            nc_p_expect(p, NC_T_ASSIGN, "'=' after a field designator");
        } else if (nc_p_at(p, NC_T_LBRACKET)) {
            nc_p_advance(p);
            it.index = nc_parse_expr(p);
            nc_p_expect(p, NC_T_RBRACKET, "']'");
            nc_p_expect(p, NC_T_ASSIGN, "'=' after an index designator");
        }
        it.value = nc_parse_initializer(p);
        it.span = nc_span_join(it.span, nc_p_prev(p)->span);
        nc_vec_push(&items, it);
        if (!nc_p_accept(p, NC_T_COMMA)) break;
        if (p->panic) break;
    }
    bool ok = nc_p_expect(p, NC_T_RBRACE, "'}' to close the initialiser list");
    *n = (uint32_t)items.len;
    *out = items.len ? nc_arena_memdup(&p->s->arena, items.data, items.len * sizeof *items.data) : NULL;
    nc_vec_free(&items);
    return ok;
}

nc_init *nc_parse_initializer(nc_parser *p)
{
    nc_init *in = nc_p_alloc(p, sizeof *in);
    in->span = nc_p_cur_span(p);
    if (nc_p_accept(p, NC_T_KW_UNINIT)) {
        in->kind = NC_INIT_UNINIT;
    } else if (nc_p_at(p, NC_T_LBRACE)) {
        if (!nc_p_enter(p)) {
            in->kind = NC_INIT_EXPR;
            in->expr = error_expr(p, in->span);
            return in;
        }
        in->kind = NC_INIT_LIST;
        nc_parse_init_items(p, &in->items, &in->nitems);
        nc_p_leave(p);
        in->span = nc_span_join(in->span, nc_p_prev(p)->span);
    } else {
        in->kind = NC_INIT_EXPR;
        in->expr = nc_parse_expr(p);
        in->span = in->expr->span;
    }
    return in;
}

/* ---- primary --------------------------------------------------------------------- */

static nc_expr *paren_or_cast(nc_parser *p)
{
    const nc_token *open = nc_p_cur(p);
    const nc_token *first = nc_p_peek(p, 1);
    nc_prim prim;
    bool prim_led = nc_prim_from_token(first->kind, &prim) || first->kind == NC_T_KW_CONST ||
                    nc_p_is_c_type_word(p, first);
    if (prim_led) {
        uint32_t after = nc_skip_type(p, p->pos + 1);
        if (after != UINT32_MAX && after < p->count && p->toks[after].kind == NC_T_RPAREN &&
            after + 1 < p->count && starts_expr(p, &p->toks[after + 1])) {
            /* C-style cast `(T)x`: report and recover as the conversion T(x). */
            nc_p_advance(p); /* ( */
            uint32_t type_lo = nc_p_cur(p)->span.lo;
            nc_type_expr *type = nc_parse_type(p);
            uint32_t type_hi = nc_p_prev(p)->span.hi;
            nc_p_expect(p, NC_T_RPAREN, "')'");
            nc_span cast_span = nc_span_join(open->span, nc_p_prev(p)->span);
            nc_expr *operand = parse_prefix(p);
            const char *src = nc_source_get(&p->s->sources, p->file)->data;
            if (nc_p_recoverable(p, cast_span, "E2031", "C-style casts are not supported")) {
                char fix[256];
                snprintf(fix, sizeof fix, "%.*s(...)", (int)(type_hi - type_lo), src + type_lo);
                nc_diag_help(&p->s->diag, "write a conversion `%s`; it panics if the value does not fit (D-010)", fix);
            }
            nc_expr *e = nc_p_expr(p, NC_E_CONVERT, nc_span_join(cast_span, operand->span));
            e->u.convert.type = type;
            e->u.convert.operand = operand;
            return e;
        }
    }
    nc_p_advance(p); /* ( */
    nc_expr *inner = nc_parse_expr(p);
    if (!nc_p_expect(p, NC_T_RPAREN, "')'")) return inner;
    /* Keep the parentheses in the span, for diagnostics. */
    inner->span = nc_span_join(open->span, nc_p_prev(p)->span);
    return inner;
}

static void parse_pattern(nc_parser *p, nc_pattern *pat)
{
    memset(pat, 0, sizeof *pat);
    pat->span = nc_p_cur_span(p);
    if (nc_p_accept(p, NC_T_KW_SOME)) {
        pat->kind = NC_PAT_SOME;
    } else if (nc_p_accept(p, NC_T_KW_NONE)) {
        pat->kind = NC_PAT_NONE;
        return;
    } else if (nc_p_at(p, NC_T_IDENT)) {
        pat->kind = NC_PAT_NAMED;
        pat->name = nc_p_advance(p)->v.sym;
    } else {
        nc_p_expected(p, "a pattern (`some(x)`, `none`, or a name)");
        return;
    }
    if (nc_p_accept(p, NC_T_LPAREN)) {
        pat->has_parens = true;
        if (nc_p_at(p, NC_T_IDENT)) pat->binding = nc_p_advance(p)->v.sym;
        else nc_p_expected(p, "a binding name");
        nc_p_expect(p, NC_T_RPAREN, "')'");
    }
    pat->span = nc_span_join(pat->span, nc_p_prev(p)->span);
}

static nc_expr *struct_literal(nc_parser *p, nc_type_expr *type)
{
    nc_expr *e = nc_p_expr(p, NC_E_STRUCT_LIT, type->span);
    e->u.struct_lit.type = type;
    nc_parse_init_items(p, &e->u.struct_lit.items, &e->u.struct_lit.nitems);
    e->span = nc_span_join(e->span, nc_p_prev(p)->span);
    return e;
}

static nc_expr *parse_primary(nc_parser *p)
{
    const nc_token *t = nc_p_cur(p);
    nc_expr *e;
    nc_prim prim;
    switch (t->kind) {
    case NC_T_INT_LIT:
        nc_p_advance(p);
        e = nc_p_expr(p, NC_E_INT, t->span);
        e->u.int_lit.value = t->v.i.value;
        e->u.int_lit.suffix = t->v.i.suffix;
        return e;
    case NC_T_FLOAT_LIT:
        nc_p_advance(p);
        e = nc_p_expr(p, NC_E_FLOAT, t->span);
        e->u.float_lit.value = t->v.f.value;
        e->u.float_lit.suffix = t->v.f.suffix;
        return e;
    case NC_T_CHAR_LIT:
        nc_p_advance(p);
        e = nc_p_expr(p, NC_E_CHAR, t->span);
        e->u.char_lit = t->v.ch;
        return e;
    case NC_T_STRING_LIT:
    case NC_T_CSTRING_LIT:
        nc_p_advance(p);
        e = nc_p_expr(p, t->kind == NC_T_STRING_LIT ? NC_E_STRING : NC_E_CSTRING, t->span);
        e->u.str = t->v.sym;
        return e;
    case NC_T_KW_TRUE:
    case NC_T_KW_FALSE:
        nc_p_advance(p);
        e = nc_p_expr(p, NC_E_BOOL, t->span);
        e->u.bool_lit = t->kind == NC_T_KW_TRUE;
        return e;
    case NC_T_KW_NULL: nc_p_advance(p); return nc_p_expr(p, NC_E_NULL, t->span);
    case NC_T_KW_NONE: nc_p_advance(p); return nc_p_expr(p, NC_E_NONE, t->span);
    case NC_T_KW_SELF: nc_p_advance(p); return nc_p_expr(p, NC_E_SELF, t->span);
    case NC_T_IDENT:
        /* `Name{...}`, `Name<T>{...}`, `mod.Name{...}`: a struct literal. */
        if (nc_p_peek(p, 1)->kind == NC_T_LBRACE ||
            ((nc_p_peek(p, 1)->kind == NC_T_LT || nc_p_peek(p, 1)->kind == NC_T_DOT) &&
             nc_skip_type(p, p->pos) < p->count && p->toks[nc_skip_type(p, p->pos)].kind == NC_T_LBRACE &&
             !nc_p_is_c_type_word(p, t))) {
            nc_type_expr *type = nc_parse_type(p);
            return struct_literal(p, type);
        }
        nc_p_advance(p);
        e = nc_p_expr(p, NC_E_NAME, t->span);
        e->u.name = t->v.sym;
        return e;
    case NC_T_KW_SELF_TYPE:
        if (nc_p_peek(p, 1)->kind == NC_T_LBRACE) {
            nc_type_expr *type = nc_parse_type(p);
            return struct_literal(p, type);
        }
        nc_p_expected(p, "an expression (`Self` names a type)");
        return error_expr(p, t->span);
    case NC_T_LPAREN:
        return paren_or_cast(p);
    case NC_T_KW_SOME:
        nc_p_advance(p);
        e = nc_p_expr(p, NC_E_SOME, t->span);
        if (nc_p_expect(p, NC_T_LPAREN, "'(' after 'some'")) {
            e->u.some = nc_parse_expr(p);
            nc_p_expect(p, NC_T_RPAREN, "')'");
        } else {
            e->u.some = error_expr(p, t->span);
        }
        e->span = nc_span_join(t->span, nc_p_prev(p)->span);
        return e;
    case NC_T_KW_SIZEOF:
    case NC_T_KW_ALIGNOF: {
        nc_p_advance(p);
        e = nc_p_expr(p, t->kind == NC_T_KW_SIZEOF ? NC_E_SIZEOF : NC_E_ALIGNOF, t->span);
        if (!nc_p_expect(p, NC_T_LPAREN, "'('")) {
            e->u.size.expr = error_expr(p, t->span);
            return e;
        }
        uint32_t after = nc_skip_type(p, p->pos);
        bool lone_ident = nc_p_at(p, NC_T_IDENT) && after == p->pos + 1;
        if (t->kind == NC_T_KW_ALIGNOF ||
            (after != UINT32_MAX && after < p->count && p->toks[after].kind == NC_T_RPAREN && !lone_ident))
            e->u.size.type = nc_parse_type(p);
        else
            e->u.size.expr = nc_parse_expr(p); /* a lone name is resolved later (type or value) */
        nc_p_expect(p, NC_T_RPAREN, "')'");
        e->span = nc_span_join(t->span, nc_p_prev(p)->span);
        return e;
    }
    case NC_T_KW_OFFSETOF:
        nc_p_advance(p);
        e = nc_p_expr(p, NC_E_OFFSETOF, t->span);
        if (nc_p_expect(p, NC_T_LPAREN, "'('")) {
            e->u.offsetof_.type = nc_parse_type(p);
            if (nc_p_expect(p, NC_T_COMMA, "','") && nc_p_at(p, NC_T_IDENT))
                e->u.offsetof_.field = nc_p_advance(p)->v.sym;
            else
                nc_p_expected(p, "a field name");
            nc_p_expect(p, NC_T_RPAREN, "')'");
        }
        e->span = nc_span_join(t->span, nc_p_prev(p)->span);
        return e;
    default:
        break;
    }
    if (nc_prim_from_token(t->kind, &prim)) {
        /* Conversion T(x) (D-010). */
        nc_type_expr *type = nc_p_type_node(p, NC_TE_PRIM, t->span);
        type->u.prim = prim;
        nc_p_advance(p);
        e = nc_p_expr(p, NC_E_CONVERT, t->span);
        e->u.convert.type = type;
        char what[64];
        snprintf(what, sizeof what, "'(' after '%s' in a conversion", nc_tok_spelling(t->kind));
        if (nc_p_expect(p, NC_T_LPAREN, what)) {
            e->u.convert.operand = nc_parse_expr(p);
            nc_p_expect(p, NC_T_RPAREN, "')'");
        } else {
            e->u.convert.operand = error_expr(p, t->span);
        }
        e->span = nc_span_join(t->span, nc_p_prev(p)->span);
        return e;
    }
    nc_p_expected(p, "an expression");
    return error_expr(p, t->span);
}

/* ---- postfix and prefix ---------------------------------------------------------- */

static nc_expr *parse_postfix(nc_parser *p)
{
    nc_expr *e = parse_primary(p);
    unsigned links = 0; /* each postfix operation adds one level to the tree */
    for (;;) {
        const nc_token *t = nc_p_cur(p);
        nc_expr *n;
        bool postfix = t->kind == NC_T_LPAREN || t->kind == NC_T_LBRACKET || t->kind == NC_T_DOT ||
                       t->kind == NC_T_ARROW || t->kind == NC_T_PLUSPLUS || t->kind == NC_T_MINUSMINUS ||
                       (t->kind == NC_T_COLONCOLON && nc_p_peek(p, 1)->kind == NC_T_LT);
        if (!postfix) break;
        if (!nc_p_enter(p)) {
            e = error_expr(p, e->span);
            break;
        }
        links++;
        if (t->kind == NC_T_LPAREN) {
            nc_p_advance(p);
            n = nc_p_expr(p, NC_E_CALL, e->span);
            n->u.call.callee = e;
            nc_expr_vec args = {0};
            if (!nc_p_at(p, NC_T_RPAREN)) {
                do {
                    nc_vec_push(&args, nc_parse_expr(p));
                } while (nc_p_accept(p, NC_T_COMMA) && !p->panic);
            }
            n->u.call.nargs = (uint32_t)args.len;
            n->u.call.args = nc_p_expr_list(p, &args);
            nc_p_expect(p, NC_T_RPAREN, "')' to close the argument list");
        } else if (t->kind == NC_T_LBRACKET) {
            nc_p_advance(p);
            n = nc_p_expr(p, NC_E_INDEX, e->span);
            n->u.index.base = e;
            n->u.index.index = nc_parse_expr(p);
            nc_p_expect(p, NC_T_RBRACKET, "']'");
        } else if (t->kind == NC_T_DOT || t->kind == NC_T_ARROW) {
            nc_p_advance(p);
            n = nc_p_expr(p, t->kind == NC_T_DOT ? NC_E_MEMBER : NC_E_ARROW, e->span);
            n->u.member.base = e;
            if (nc_p_at(p, NC_T_IDENT)) {
                const nc_token *id = nc_p_advance(p);
                n->u.member.name = id->v.sym;
                n->u.member.name_span = id->span;
            } else {
                nc_p_expected(p, "a member name");
            }
        } else if (t->kind == NC_T_PLUSPLUS || t->kind == NC_T_MINUSMINUS) {
            nc_p_advance(p);
            n = nc_p_expr(p, NC_E_UNARY, e->span);
            n->u.unary.op = t->kind == NC_T_PLUSPLUS ? NC_UN_POSTINC : NC_UN_POSTDEC;
            n->u.unary.operand = e;
        } else if (t->kind == NC_T_COLONCOLON && nc_p_peek(p, 1)->kind == NC_T_LT) {
            nc_p_advance(p);
            n = nc_p_expr(p, NC_E_GENERIC, e->span);
            n->u.generic.base = e;
            nc_parse_generic_args(p, &n->u.generic.args, &n->u.generic.nargs);
        } else {
            break; /* unreachable: `postfix` covered every case */
        }
        n->span = nc_span_join(e->span, nc_p_prev(p)->span);
        e = n;
        if (p->panic) break;
    }
    while (links--) nc_p_leave(p);
    return e;
}

static nc_expr *parse_prefix(nc_parser *p)
{
    const nc_token *t = nc_p_cur(p);
    nc_unop op;
    switch (t->kind) {
    case NC_T_MINUS: op = NC_UN_NEG; break;
    case NC_T_PLUS: op = NC_UN_PLUS; break;
    case NC_T_BANG: op = NC_UN_NOT; break;
    case NC_T_TILDE: op = NC_UN_BITNOT; break;
    case NC_T_STAR: op = NC_UN_DEREF; break;
    case NC_T_PLUSPLUS: op = NC_UN_PREINC; break;
    case NC_T_MINUSMINUS: op = NC_UN_PREDEC; break;
    case NC_T_KW_TRY: op = NC_UN_TRY; break;
    case NC_T_AMP:
        op = nc_p_peek(p, 1)->kind == NC_T_KW_MUT ? NC_UN_ADDR_MUT : NC_UN_ADDR;
        break;
    case NC_T_ANDAND: {
        /* `&&x` is `&(&x)`. */
        if (!nc_p_enter(p)) return error_expr(p, t->span);
        nc_p_advance(p);
        nc_expr *inner = nc_p_expr(p, NC_E_UNARY, t->span);
        inner->u.unary.op = NC_UN_ADDR;
        inner->u.unary.operand = parse_prefix(p);
        inner->span = nc_span_join(t->span, inner->u.unary.operand->span);
        nc_expr *outer = nc_p_expr(p, NC_E_UNARY, inner->span);
        outer->u.unary.op = NC_UN_ADDR;
        outer->u.unary.operand = inner;
        nc_p_leave(p);
        return outer;
    }
    default:
        return parse_postfix(p);
    }
    if (!nc_p_enter(p)) return error_expr(p, t->span);
    nc_p_advance(p);
    if (op == NC_UN_ADDR_MUT) nc_p_advance(p); /* mut */
    nc_expr *e = nc_p_expr(p, NC_E_UNARY, t->span);
    e->u.unary.op = op;
    e->u.unary.operand = parse_prefix(p);
    e->span = nc_span_join(t->span, e->u.unary.operand->span);
    nc_p_leave(p);
    return e;
}

/* ---- binary, conditional, assignment ------------------------------------------------ */

static nc_expr *parse_binary(nc_parser *p, int min_prec)
{
    nc_expr *lhs = parse_prefix(p);
    unsigned links = 0; /* each operator adds one level to the tree */
    for (;;) {
        binop_info b = binop_at(p);
        if (b.prec == PREC_NONE || b.prec < min_prec) break;
        if (!nc_p_enter(p)) {
            lhs = error_expr(p, lhs->span);
            break;
        }
        links++;
        const nc_token *optok = nc_p_cur(p);
        for (int i = 0; i < b.ntoks; i++) nc_p_advance(p);
        nc_expr *e;
        if (b.is_is) {
            e = nc_p_expr(p, NC_E_IS, lhs->span);
            e->u.is.operand = lhs;
            parse_pattern(p, &e->u.is.pattern);
        } else {
            nc_expr *rhs = parse_binary(p, b.prec + 1);
            e = nc_p_expr(p, NC_E_BINARY, lhs->span);
            e->u.binary.op = b.op;
            e->u.binary.lhs = lhs;
            e->u.binary.rhs = rhs;
            e->span = nc_span_join(lhs->span, rhs->span);
        }
        e->span = nc_span_join(e->span, nc_p_prev(p)->span);
        if (b.prec == PREC_EQ || b.prec == PREC_REL) {
            binop_info next = binop_at(p);
            if (next.prec == b.prec) {
                const nc_token *t2 = nc_p_cur(p);
                if (nc_p_recoverable(p, optok->span, "E2010",
                                     "comparison operators cannot be chained")) {
                    nc_diag_note(&p->s->diag, t2->span, "second comparison here");
                    nc_diag_help(&p->s->diag, "combine the comparisons with `&&`, e.g. `a < b && b < c`");
                }
                /* Recover: parse the rest of the chain, keep an error node. */
                while (binop_at(p).prec == b.prec && !p->panic) {
                    binop_info c = binop_at(p);
                    for (int i = 0; i < c.ntoks; i++) nc_p_advance(p);
                    if (c.is_is) {
                        nc_pattern unused;
                        parse_pattern(p, &unused);
                    } else {
                        parse_binary(p, b.prec + 1);
                    }
                }
                e = error_expr(p, nc_span_join(e->span, nc_p_prev(p)->span));
            }
        }
        lhs = e;
        if (p->panic) break;
    }
    while (links--) nc_p_leave(p);
    return lhs;
}

static nc_expr *parse_cond(nc_parser *p)
{
    nc_expr *c = parse_binary(p, PREC_OR);
    if (!nc_p_at(p, NC_T_QUESTION)) return c;
    if (!nc_p_enter(p)) return error_expr(p, c->span);
    nc_p_advance(p);
    nc_expr *e = nc_p_expr(p, NC_E_COND, c->span);
    e->u.cond.cond = c;
    e->u.cond.then_e = nc_parse_expr(p);
    nc_p_expect(p, NC_T_COLON, "':' in a conditional expression");
    e->u.cond.else_e = parse_cond(p);
    e->span = nc_span_join(c->span, e->u.cond.else_e->span);
    nc_p_leave(p);
    return e;
}

nc_expr *nc_parse_expr(nc_parser *p)
{
    if (!nc_p_enter(p)) return error_expr(p, nc_p_cur_span(p));
    nc_expr *lhs = parse_cond(p);
    nc_binop op;
    int ntoks;
    if (assign_op_at(p, &op, &ntoks)) {
        for (int i = 0; i < ntoks; i++) nc_p_advance(p);
        nc_expr *rhs = nc_parse_expr(p); /* right-associative */
        nc_expr *e = nc_p_expr(p, NC_E_ASSIGN, nc_span_join(lhs->span, rhs->span));
        e->u.binary.op = op;
        e->u.binary.lhs = lhs;
        e->u.binary.rhs = rhs;
        lhs = e;
    }
    nc_p_leave(p);
    return lhs;
}
