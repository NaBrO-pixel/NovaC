/* Statements (NOVAC_GRAMMAR.md §5). */
#include "parse/parser_internal.h"

#include <string.h>

/* "If a statement parses as a declaration, it is one" (D-005). */
bool nc_p_is_decl_start(nc_parser *p)
{
    const nc_token *t = nc_p_cur(p);
    nc_prim prim;
    switch (t->kind) {
    case NC_T_KW_AUTO:
    case NC_T_KW_STATIC:
    case NC_T_KW_CONST:
    case NC_T_KW_SELF_TYPE:
        return true;
    case NC_T_IDENT: {
        /* Also covers C type names: `long x`, `unsigned int x`. */
        uint32_t after = nc_skip_type(p, p->pos);
        return after != UINT32_MAX && after < p->count && p->toks[after].kind == NC_T_IDENT;
    }
    default:
        /* `i32(x)` is a conversion expression; `i32 x` a declaration. */
        return nc_prim_from_token(t->kind, &prim) && nc_p_peek(p, 1)->kind != NC_T_LPAREN;
    }
}

nc_stmt *nc_parse_var_decl(nc_parser *p, bool require_semi)
{
    nc_stmt *s = nc_p_stmt(p, NC_S_DECL, nc_p_cur_span(p));
    nc_var_decl *v = &s->u.decl;
    v->is_static = nc_p_accept(p, NC_T_KW_STATIC);
    if (nc_p_at(p, NC_T_KW_AUTO)) {
        nc_p_advance(p);
        v->is_auto = true;
    } else if (nc_p_at(p, NC_T_KW_CONST) && nc_p_peek(p, 1)->kind == NC_T_KW_AUTO) {
        nc_p_advance(p);
        nc_p_advance(p);
        v->is_auto = true;
        v->auto_const = true;
    } else {
        v->type = nc_parse_type(p);
    }

    NC_VEC(nc_declarator) decls = {0};
    do {
        nc_declarator d = {0};
        if (!nc_p_at(p, NC_T_IDENT)) {
            nc_p_expected(p, "a variable name");
            break;
        }
        const nc_token *id = nc_p_advance(p);
        d.name = id->v.sym;
        d.span = id->span;
        if (nc_p_at(p, NC_T_LBRACKET)) {
            /* C array declarator `int a[4]`, `int b[2][3]` (D-005). */
            uint32_t lo = nc_p_cur(p)->span.lo;
            while (nc_p_at(p, NC_T_LBRACKET)) nc_p_skip_balanced(p);
            uint32_t hi = nc_p_prev(p)->span.hi;
            const char *src = nc_source_get(&p->s->sources, p->file)->data;
            if (nc_p_recoverable(p, (nc_span){p->file, lo, hi}, "E2020",
                                 "array dimensions are written on the type, not after the name")) {
                nc_diag_help(&p->s->diag, "write `T%.*s %s` (D-005)", (int)(hi - lo), src + lo,
                             nc_sym_str(&p->s->strtab, d.name));
            }
        }
        if (nc_p_accept(p, NC_T_ASSIGN)) d.init = nc_parse_initializer(p);
        nc_vec_push(&decls, d);
    } while (nc_p_accept(p, NC_T_COMMA) && !p->panic);
    v->ndecls = (uint32_t)decls.len;
    v->decls = decls.len ? nc_arena_memdup(&p->s->arena, decls.data, decls.len * sizeof *decls.data) : NULL;
    nc_vec_free(&decls);
    if (require_semi) nc_p_expect(p, NC_T_SEMI, "';'");
    s->span = nc_span_join(s->span, nc_p_prev(p)->span);
    return s;
}

/* Recognised statements a later phase implements: report E9001, then skip
 * them syntactically (NOVAC_ROADMAP.md, Phase 2 scope). */
static nc_stmt *unsupported(nc_parser *p, const char *what, int phase)
{
    const nc_token *t = nc_p_advance(p);
    nc_stmt *s = nc_p_stmt(p, NC_S_UNSUPPORTED, t->span);
    s->u.unsupported = what;
    nc_p_recoverable(p, t->span, "E9001", "'%s' is not implemented in this version (Phase %d)", what, phase);
    switch (t->kind) {
    case NC_T_KW_UNSAFE:
        if (nc_p_at(p, NC_T_LBRACE)) nc_parse_block(p);
        break;
    case NC_T_KW_DEFER:
        nc_parse_stmt(p);
        break;
    default:
        nc_p_sync_stmt(p);
        break;
    }
    s->span = nc_span_join(s->span, nc_p_prev(p)->span);
    return s;
}

/* switch (expr) { case a, b: stmts  default: stmts } (D-025) */
static nc_stmt *switch_stmt(nc_parser *p)
{
    const nc_token *kw = nc_p_advance(p);
    nc_stmt *s = nc_p_stmt(p, NC_S_SWITCH, kw->span);
    if (!nc_p_expect(p, NC_T_LPAREN, "'(' after 'switch'")) return s;
    s->u.switch_.expr = nc_parse_expr(p);
    nc_p_expect(p, NC_T_RPAREN, "')'");
    if (!nc_p_expect(p, NC_T_LBRACE, "'{' to begin the cases")) return s;
    NC_VEC(nc_case) cases = {0};
    while (!nc_p_at(p, NC_T_RBRACE) && !nc_p_at(p, NC_T_EOF)) {
        nc_case c = {0};
        c.span = nc_p_cur_span(p);
        if (nc_p_accept(p, NC_T_KW_DEFAULT)) {
            c.is_default = true;
        } else if (nc_p_accept(p, NC_T_KW_CASE)) {
            nc_expr_vec vals = {0};
            do {
                nc_vec_push(&vals, nc_parse_expr(p));
            } while (nc_p_accept(p, NC_T_COMMA) && !p->panic);
            c.nvalues = (uint32_t)vals.len;
            c.values = nc_p_expr_list(p, &vals);
        } else {
            nc_p_expected(p, "'case' or 'default'");
            nc_p_sync_stmt(p);
            p->panic = false;
            continue;
        }
        if (!nc_p_expect(p, NC_T_COLON, "':' after the case label")) {
            nc_p_sync_stmt(p);
            p->panic = false;
        }
        nc_stmt_vec stmts = {0};
        while (!nc_p_at(p, NC_T_KW_CASE) && !nc_p_at(p, NC_T_KW_DEFAULT) && !nc_p_at(p, NC_T_RBRACE) &&
               !nc_p_at(p, NC_T_EOF)) {
            uint32_t before = p->pos;
            nc_stmt *st = nc_parse_stmt(p);
            if (st) nc_vec_push(&stmts, st);
            if (p->panic) {
                nc_tok last = nc_p_prev(p)->kind;
                if (p->pos == before || (last != NC_T_SEMI && last != NC_T_RBRACE)) nc_p_sync_stmt(p);
                p->panic = false;
            }
            if (p->pos == before) nc_p_advance(p);
        }
        c.nstmts = (uint32_t)stmts.len;
        c.stmts = stmts.len ? nc_arena_memdup(&p->s->arena, stmts.data, stmts.len * sizeof *stmts.data) : NULL;
        nc_vec_free(&stmts);
        nc_vec_push(&cases, c);
    }
    nc_p_expect(p, NC_T_RBRACE, "'}' to close the switch");
    s->u.switch_.ncases = (uint32_t)cases.len;
    s->u.switch_.cases = cases.len ? nc_arena_memdup(&p->s->arena, cases.data, cases.len * sizeof *cases.data) : NULL;
    nc_vec_free(&cases);
    s->span = nc_span_join(kw->span, nc_p_prev(p)->span);
    return s;
}

nc_stmt *nc_parse_block(nc_parser *p)
{
    const nc_token *open = nc_p_cur(p);
    nc_stmt *s = nc_p_stmt(p, NC_S_BLOCK, open->span);
    if (!nc_p_expect(p, NC_T_LBRACE, "'{'")) return s;
    nc_stmt_vec stmts = {0};
    while (!nc_p_at(p, NC_T_RBRACE) && !nc_p_at(p, NC_T_EOF)) {
        uint32_t before = p->pos;
        nc_stmt *st = nc_parse_stmt(p);
        if (st) nc_vec_push(&stmts, st);
        if (p->panic) {
            /* Resynchronise unless the broken statement already reached its
             * terminator; skipping then would swallow the next statement. */
            nc_tok last = nc_p_prev(p)->kind;
            if (p->pos == before || (last != NC_T_SEMI && last != NC_T_RBRACE)) nc_p_sync_stmt(p);
            p->panic = false;
        }
        if (p->pos == before && !nc_p_at(p, NC_T_RBRACE)) nc_p_advance(p);
    }
    if (nc_p_at(p, NC_T_EOF)) {
        if (nc_p_error(p, nc_p_cur_span(p), "E2001", "expected '}' to close the block, found end of file"))
            nc_diag_note(&p->s->diag, open->span, "the block starts here");
    } else {
        nc_p_advance(p);
    }
    s->u.block.count = (uint32_t)stmts.len;
    s->u.block.stmts = stmts.len ? nc_arena_memdup(&p->s->arena, stmts.data, stmts.len * sizeof *stmts.data) : NULL;
    nc_vec_free(&stmts);
    s->span = nc_span_join(open->span, nc_p_prev(p)->span);
    return s;
}

static nc_expr *paren_condition(nc_parser *p, const char *keyword)
{
    char what[64];
    snprintf(what, sizeof what, "'(' after '%s'", keyword);
    if (!nc_p_expect(p, NC_T_LPAREN, what)) return nc_p_expr(p, NC_E_ERROR, nc_p_cur_span(p));
    nc_expr *c = nc_parse_expr(p);
    nc_p_expect(p, NC_T_RPAREN, "')'");
    return c;
}

static nc_stmt *sub_stmt(nc_parser *p)
{
    if (!nc_p_enter(p)) return nc_p_stmt(p, NC_S_EMPTY, nc_p_cur_span(p));
    nc_stmt *s = nc_parse_stmt(p);
    nc_p_leave(p);
    return s;
}

static void parse_expr_list(nc_parser *p, nc_expr ***out, uint32_t *n)
{
    nc_expr_vec v = {0};
    do {
        nc_vec_push(&v, nc_parse_expr(p));
    } while (nc_p_accept(p, NC_T_COMMA) && !p->panic);
    *n = (uint32_t)v.len;
    *out = nc_p_expr_list(p, &v);
}

static nc_stmt *for_stmt(nc_parser *p)
{
    const nc_token *kw = nc_p_advance(p);
    nc_stmt *s = nc_p_stmt(p, NC_S_FOR, kw->span);
    if (!nc_p_expect(p, NC_T_LPAREN, "'(' after 'for'")) return s;
    if (!nc_p_at(p, NC_T_SEMI)) {
        if (nc_p_is_decl_start(p)) s->u.for_.init_decl = nc_parse_var_decl(p, false);
        else parse_expr_list(p, &s->u.for_.init_exprs, &s->u.for_.ninit);
    }
    nc_p_expect(p, NC_T_SEMI, "';'");
    if (!nc_p_at(p, NC_T_SEMI)) s->u.for_.cond = nc_parse_expr(p);
    nc_p_expect(p, NC_T_SEMI, "';'");
    if (!nc_p_at(p, NC_T_RPAREN)) parse_expr_list(p, &s->u.for_.step, &s->u.for_.nstep);
    nc_p_expect(p, NC_T_RPAREN, "')'");
    s->u.for_.body = sub_stmt(p);
    s->span = nc_span_join(kw->span, nc_p_prev(p)->span);
    return s;
}

nc_stmt *nc_parse_stmt(nc_parser *p)
{
    const nc_token *t = nc_p_cur(p);
    nc_stmt *s;
    switch (t->kind) {
    case NC_T_LBRACE: {
        if (!nc_p_enter(p)) {
            nc_p_skip_balanced(p);
            return nc_p_stmt(p, NC_S_EMPTY, t->span);
        }
        s = nc_parse_block(p);
        nc_p_leave(p);
        return s;
    }
    case NC_T_SEMI:
        nc_p_advance(p);
        return nc_p_stmt(p, NC_S_EMPTY, t->span);
    case NC_T_KW_IF:
        nc_p_advance(p);
        s = nc_p_stmt(p, NC_S_IF, t->span);
        s->u.if_.cond = paren_condition(p, "if");
        s->u.if_.then_s = sub_stmt(p);
        if (nc_p_accept(p, NC_T_KW_ELSE)) s->u.if_.else_s = sub_stmt(p);
        break;
    case NC_T_KW_WHILE:
        nc_p_advance(p);
        s = nc_p_stmt(p, NC_S_WHILE, t->span);
        s->u.while_.cond = paren_condition(p, "while");
        s->u.while_.body = sub_stmt(p);
        break;
    case NC_T_KW_DO:
        nc_p_advance(p);
        s = nc_p_stmt(p, NC_S_DO, t->span);
        s->u.while_.body = sub_stmt(p);
        if (nc_p_expect(p, NC_T_KW_WHILE, "'while' after the body of 'do'")) {
            s->u.while_.cond = paren_condition(p, "while");
            nc_p_expect(p, NC_T_SEMI, "';'");
        } else {
            s->u.while_.cond = nc_p_expr(p, NC_E_ERROR, t->span);
        }
        break;
    case NC_T_KW_FOR:
        return for_stmt(p);
    case NC_T_KW_BREAK:
    case NC_T_KW_CONTINUE:
        nc_p_advance(p);
        s = nc_p_stmt(p, t->kind == NC_T_KW_BREAK ? NC_S_BREAK : NC_S_CONTINUE, t->span);
        nc_p_expect(p, NC_T_SEMI, "';'");
        break;
    case NC_T_KW_RETURN:
        nc_p_advance(p);
        s = nc_p_stmt(p, NC_S_RETURN, t->span);
        if (!nc_p_at(p, NC_T_SEMI)) s->u.ret = nc_parse_expr(p);
        nc_p_expect(p, NC_T_SEMI, "';'");
        break;
    case NC_T_KW_SWITCH: {
        if (!nc_p_enter(p)) {
            nc_p_sync_stmt(p);
            return nc_p_stmt(p, NC_S_EMPTY, t->span);
        }
        s = switch_stmt(p);
        nc_p_leave(p);
        return s;
    }
    case NC_T_KW_FALLTHROUGH:
        nc_p_advance(p);
        s = nc_p_stmt(p, NC_S_FALLTHROUGH, t->span);
        nc_p_expect(p, NC_T_SEMI, "';'");
        break;
    case NC_T_KW_UNSAFE: return unsupported(p, "unsafe", 4);
    case NC_T_KW_DEFER: return unsupported(p, "defer", 6);
    case NC_T_KW_ASM: return unsupported(p, "asm", 7);
    case NC_T_KW_GOTO:
        nc_p_advance(p);
        s = nc_p_stmt(p, NC_S_UNSUPPORTED, t->span);
        s->u.unsupported = "goto";
        if (nc_p_recoverable(p, t->span, "E2060", "'goto' is not supported in NovaC"))
            nc_diag_help(&p->s->diag, "use structured control flow, `defer`, or destructors (D-034)");
        nc_p_sync_stmt(p);
        return s;
    case NC_T_KW_ELSE:
        nc_p_error(p, t->span, "E2001", "'else' without a matching 'if'");
        nc_p_advance(p);
        return NULL;
    default:
        if (nc_p_is_decl_start(p)) return nc_parse_var_decl(p, true);
        s = nc_p_stmt(p, NC_S_EXPR, t->span);
        s->u.expr = nc_parse_expr(p);
        nc_p_expect(p, NC_T_SEMI, "';'");
        break;
    }
    s->span = nc_span_join(s->span, nc_p_prev(p)->span);
    return s;
}
