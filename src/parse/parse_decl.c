/* Module structure and top-level declarations (NOVAC_GRAMMAR.md §3). */
#include "parse/parser_internal.h"

#include <string.h>

typedef NC_VEC(nc_attr) attr_vec;

static void parse_attrs(nc_parser *p, attr_vec *out)
{
    while (nc_p_at(p, NC_T_AT)) {
        const nc_token *at = nc_p_advance(p);
        nc_attr a = {0};
        a.span = at->span;
        if (!nc_p_at(p, NC_T_IDENT)) {
            /* Drop the stray '@' and parse what follows normally. */
            char found[160];
            nc_p_describe(p, nc_p_cur(p), found, sizeof found);
            nc_p_recoverable(p, at->span, "E2001", "expected an attribute name after '@', found %s", found);
            continue;
        }
        a.name = nc_p_advance(p)->v.sym;
        if (nc_p_accept(p, NC_T_LPAREN)) {
            a.has_parens = true;
            NC_VEC(nc_attr_arg) args = {0};
            if (!nc_p_at(p, NC_T_RPAREN)) {
                do {
                    nc_attr_arg arg = {0};
                    if (nc_p_at(p, NC_T_IDENT) && nc_p_peek(p, 1)->kind == NC_T_ASSIGN) {
                        arg.name = nc_p_advance(p)->v.sym;
                        nc_p_advance(p);
                    }
                    arg.value = nc_parse_expr(p);
                    nc_vec_push(&args, arg);
                } while (nc_p_accept(p, NC_T_COMMA) && !p->panic);
            }
            nc_p_expect(p, NC_T_RPAREN, "')' to close the attribute arguments");
            a.nargs = (uint32_t)args.len;
            a.args = args.len ? nc_arena_memdup(&p->s->arena, args.data, args.len * sizeof *args.data) : NULL;
            nc_vec_free(&args);
        }
        a.span = nc_span_join(a.span, nc_p_prev(p)->span);
        nc_vec_push(out, a);
        if (p->panic) return;
    }
}

static void store_attrs(nc_parser *p, attr_vec *v, nc_attr **out, uint32_t *n)
{
    *n = (uint32_t)v->len;
    *out = v->len ? nc_arena_memdup(&p->s->arena, v->data, v->len * sizeof *v->data) : NULL;
    nc_vec_free(v);
}

static bool parse_path(nc_parser *p, nc_path *path)
{
    NC_VEC(nc_sym) parts = {0};
    path->span = nc_p_cur_span(p);
    bool ok = true;
    do {
        if (!nc_p_at(p, NC_T_IDENT)) {
            nc_p_expected(p, "a module name");
            ok = false;
            break;
        }
        nc_vec_push(&parts, nc_p_advance(p)->v.sym);
    } while (nc_p_accept(p, NC_T_DOT));
    path->count = (uint32_t)parts.len;
    path->parts = parts.len ? nc_arena_memdup(&p->s->arena, parts.data, parts.len * sizeof *parts.data) : NULL;
    path->span = nc_span_join(path->span, nc_p_prev(p)->span);
    nc_vec_free(&parts);
    return ok;
}

static void parse_generic_params(nc_parser *p, nc_decl *d)
{
    /* at '<': `<T: Bound + Other, constexpr usize N>` (Phase 6). Parsed for
     * syntax, counted, and rejected by semantic analysis. */
    const nc_token *open = nc_p_advance(p);
    uint32_t n = 0;
    if (!nc_p_at(p, NC_T_GT)) {
        do {
            if (nc_p_accept(p, NC_T_KW_CONSTEXPR)) {
                nc_parse_type(p);
                if (!nc_p_accept(p, NC_T_IDENT)) nc_p_expected(p, "a constant parameter name");
            } else if (nc_p_accept(p, NC_T_IDENT)) {
                if (nc_p_accept(p, NC_T_COLON)) {
                    do {
                        nc_path bound;
                        parse_path(p, &bound);
                    } while (nc_p_accept(p, NC_T_PLUS) && !p->panic);
                }
            } else {
                nc_p_expected(p, "a generic parameter");
                break;
            }
            n++;
        } while (nc_p_accept(p, NC_T_COMMA) && !p->panic);
    }
    nc_p_expect(p, NC_T_GT, "'>' to close the generic parameter list");
    d->u.fn.ngeneric = n ? n : 1;
    d->u.fn.generic_span = nc_span_join(open->span, nc_p_prev(p)->span);
}

static void parse_params(nc_parser *p, nc_decl *d)
{
    NC_VEC(nc_param) params = {0};
    if (nc_p_at(p, NC_T_KW_VOID) && nc_p_peek(p, 1)->kind == NC_T_RPAREN) {
        nc_p_advance(p); /* `(void)` is an empty list, as in C */
    } else if (!nc_p_at(p, NC_T_RPAREN)) {
        do {
            if (nc_p_accept(p, NC_T_ELLIPSIS)) {
                d->u.fn.variadic = true;
                break;
            }
            nc_param prm = {0};
            prm.span = nc_p_cur_span(p);
            prm.type = nc_parse_type(p);
            if (nc_p_at(p, NC_T_KW_SELF)) {
                nc_p_advance(p);
                prm.is_self = true;
            } else if (nc_p_at(p, NC_T_IDENT)) {
                prm.name = nc_p_advance(p)->v.sym;
            } else {
                nc_p_expected(p, "a parameter name");
                break;
            }
            prm.span = nc_span_join(prm.span, nc_p_prev(p)->span);
            nc_vec_push(&params, prm);
        } while (nc_p_accept(p, NC_T_COMMA) && !p->panic);
    }
    nc_p_expect(p, NC_T_RPAREN, "')' to close the parameter list");
    d->u.fn.nparams = (uint32_t)params.len;
    d->u.fn.params = params.len ? nc_arena_memdup(&p->s->arena, params.data, params.len * sizeof *params.data) : NULL;
    nc_vec_free(&params);
}

static nc_decl *parse_function(nc_parser *p, nc_decl *d)
{
    d->kind = NC_D_FUNCTION;
    if (nc_p_accept(p, NC_T_KW_EXTERN)) {
        d->u.fn.has_extern = true;
        if (nc_p_at(p, NC_T_STRING_LIT)) d->u.fn.extern_abi = nc_p_advance(p)->v.sym;
        else nc_p_expected(p, "an ABI string such as \"C\" after 'extern'");
    }
    d->u.fn.is_unsafe = nc_p_accept(p, NC_T_KW_UNSAFE);
    d->u.fn.ret = nc_parse_type(p);
    if (!nc_p_at(p, NC_T_IDENT)) {
        nc_p_expected(p, "a function name");
        return d;
    }
    const nc_token *name = nc_p_advance(p);
    d->u.fn.name = name->v.sym;
    d->u.fn.name_span = name->span;
    if (nc_p_at(p, NC_T_ASSIGN) || nc_p_at(p, NC_T_SEMI) || nc_p_at(p, NC_T_COMMA)) {
        if (nc_p_error(p, name->span, "E2001", "expected '(' after the function name '%s'",
                       nc_sym_str(&p->s->strtab, d->u.fn.name)))
            nc_diag_help(&p->s->diag, "module-level variables are declared with `static` (Phase 4)");
        return d;
    }
    if (nc_p_at(p, NC_T_LT)) parse_generic_params(p, d);
    if (!nc_p_expect(p, NC_T_LPAREN, "'(' after the function name")) return d;
    parse_params(p, d);
    if (p->panic) return d;
    if (nc_p_at(p, NC_T_LBRACE)) {
        d->u.fn.body = nc_parse_block(p);
    } else if (!nc_p_accept(p, NC_T_SEMI)) {
        nc_p_expected(p, "'{' to begin the function body");
    }
    return d;
}

static nc_decl *unsupported_decl(nc_parser *p, nc_decl *d, const char *what, int phase)
{
    d->kind = NC_D_UNSUPPORTED;
    d->u.unsupported = what;
    nc_p_recoverable(p, nc_p_cur_span(p), "E9001", "%s declarations are not implemented in this version (Phase %d)",
                     what, phase);
    nc_p_skip_decl(p);
    return d;
}

static nc_decl *parse_top_decl(nc_parser *p, attr_vec *attrs)
{
    nc_decl *d = nc_p_alloc(p, sizeof *d);
    d->span = attrs->len ? attrs->data[0].span : nc_p_cur_span(p);
    store_attrs(p, attrs, &d->attrs, &d->nattrs);
    d->is_public = nc_p_accept(p, NC_T_KW_PUBLIC);
    const nc_token *t = nc_p_cur(p);
    switch (t->kind) {
    case NC_T_KW_STRUCT: unsupported_decl(p, d, "struct", 4); break;
    case NC_T_KW_UNION: unsupported_decl(p, d, "union", 4); break;
    case NC_T_KW_ENUM: unsupported_decl(p, d, "enum", 4); break;
    case NC_T_KW_TYPEDEF: unsupported_decl(p, d, "typedef", 4); break;
    case NC_T_KW_STATIC: unsupported_decl(p, d, "static", 4); break;
    case NC_T_KW_CONSTEXPR: unsupported_decl(p, d, "constexpr", 4); break;
    case NC_T_KW_IMPL: unsupported_decl(p, d, "impl", 5); break;
    case NC_T_KW_INTERFACE: unsupported_decl(p, d, "interface", 6); break;
    case NC_T_KW_IMPORT: unsupported_decl(p, d, "import", 6); break;
    case NC_T_KW_EXTERN:
        if (nc_p_peek(p, 1)->kind == NC_T_STRING_LIT && nc_p_peek(p, 2)->kind == NC_T_LBRACE) {
            unsupported_decl(p, d, "extern block", 4);
            break;
        }
        parse_function(p, d);
        break;
    case NC_T_KW_MODULE:
        nc_p_error(p, t->span, "E2001", "the module declaration must be the first declaration in the file");
        d->kind = NC_D_UNSUPPORTED;
        d->u.unsupported = "module";
        nc_p_skip_decl(p);
        break;
    default:
        if (t->kind == NC_T_KW_UNSAFE || nc_skip_type(p, p->pos) != UINT32_MAX) {
            parse_function(p, d);
        } else {
            nc_p_expected(p, "a declaration");
            d->kind = NC_D_UNSUPPORTED;
            d->u.unsupported = "error";
            nc_p_skip_decl(p);
        }
    }
    d->span = nc_span_join(d->span, nc_p_prev(p)->span);
    return d;
}

nc_module_ast *nc_parse(nc_session *s, uint32_t file, nc_token_list toks)
{
    nc_parser p;
    memset(&p, 0, sizeof p);
    p.s = s;
    p.file = file;
    p.toks = toks.tokens;
    p.count = toks.count;
    p.sym_underscore = nc_intern_cstr(&s->strtab, "_");

    nc_module_ast *m = nc_arena_alloc(&s->arena, sizeof *m);
    m->file = file;

    attr_vec attrs = {0};
    parse_attrs(&p, &attrs);
    if (nc_p_at(&p, NC_T_KW_MODULE)) {
        nc_p_advance(&p);
        m->has_module_decl = true;
        store_attrs(&p, &attrs, &m->attrs, &m->nattrs);
        parse_path(&p, &m->name);
        nc_p_expect(&p, NC_T_SEMI, "';' after the module declaration");
        if (p.panic) {
            nc_p_skip_decl(&p);
            p.panic = false;
        }
    }

    NC_VEC(nc_decl *) decls = {0};
    while (!nc_p_at(&p, NC_T_EOF)) {
        uint32_t before = p.pos;
        if (!attrs.len) parse_attrs(&p, &attrs);
        if (nc_p_at(&p, NC_T_EOF)) {
            if (attrs.len)
                nc_p_error(&p, attrs.data[attrs.len - 1].span, "E2001", "attribute is not followed by a declaration");
            break;
        }
        nc_decl *d = parse_top_decl(&p, &attrs);
        nc_vec_push(&decls, d);
        if (p.panic) {
            if (d->kind == NC_D_FUNCTION) nc_p_skip_decl(&p);
            p.panic = false;
        }
        if (p.pos == before) nc_p_advance(&p);
    }
    nc_vec_free(&attrs);
    m->ndecls = (uint32_t)decls.len;
    m->decls = decls.len ? nc_arena_memdup(&s->arena, decls.data, decls.len * sizeof *decls.data) : NULL;
    nc_vec_free(&decls);
    return m;
}
