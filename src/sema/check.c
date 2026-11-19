/* Declarations, statements, scopes and the semantic-analysis entry point. */
#include "sema/sema_internal.h"

#include <string.h>

const char *nc_sema_sym(nc_sema *sm, nc_sym s)
{
    return nc_sym_str(&sm->s->strtab, s);
}

void nc_sema_print_type(FILE *out, const nc_type *t)
{
    nc_type_print(out, t);
}

bool nc_sema_require_unsafe(nc_sema *sm, nc_span span, const char *what)
{
    if (sm->unsafe_depth || (sm->cur_fn && sm->cur_fn->decl->u.fn.is_unsafe)) return true;
    nc_diag_error(NC_DIAG(sm), span, "E6003", "%s requires an `unsafe` block or function", what);
    if (sm->mode == NC_MODE_SAFE)
        nc_diag_help(NC_DIAG(sm), "unsafe code needs a `@mode(controlled)` module (NOVAC_MEMORY_MODEL.md §2)");
    return false;
}

/* ---- scopes -------------------------------------------------------------------------------- */

void nc_scope_push(nc_sema *sm)
{
    nc_vec_push(&sm->marks, sm->scope.len);
}

void nc_scope_pop(nc_sema *sm)
{
    sm->scope.len = sm->marks.data[--sm->marks.len];
}

nc_var *nc_scope_lookup(nc_sema *sm, nc_sym name)
{
    for (size_t i = sm->scope.len; i-- > 0;)
        if (sm->scope.data[i].name == name) return sm->scope.data[i].var;
    return NULL;
}

nc_var *nc_scope_use(nc_sema *sm, nc_sym name, nc_span span, bool *poisoned)
{
    nc_var *v = nc_scope_lookup(sm, name);
    *poisoned = false;
    if (v && v->initializing) {
        /* In C the new variable is already in scope here and reading it is
         * undefined; in NovaC it would silently mean an outer variable.
         * Neither is acceptable (D-011). */
        nc_diag_error(NC_DIAG(sm), span, "E3003", "'%s' is used in its own initialiser", nc_sema_sym(sm, name));
        *poisoned = true;
    }
    return v;
}

nc_var *nc_scope_declare(nc_sema *sm, nc_sym name, const nc_type *type, bool is_const, nc_span span)
{
    size_t start = sm->marks.len ? sm->marks.data[sm->marks.len - 1] : 0;
    for (size_t i = start; i < sm->scope.len; i++) {
        if (sm->scope.data[i].name == name) {
            nc_diag_error(NC_DIAG(sm), span, "E3002", "redefinition of '%s' in the same scope", nc_sema_sym(sm, name));
            nc_diag_note(NC_DIAG(sm), sm->scope.data[i].var->span, "previous definition is here");
            break;
        }
    }
    nc_var *v = nc_arena_alloc(&sm->s->arena, sizeof *v);
    v->name = name;
    v->type = type;
    v->is_const = is_const;
    v->span = span;
    nc_scope_entry entry = {name, v};
    nc_vec_push(&sm->scope, entry);
    return v;
}

nc_fn_info *nc_sema_find_fn(nc_sema *sm, nc_sym name)
{
    for (size_t i = 0; i < sm->fns.len; i++)
        if (sm->fns.data[i]->decl->u.fn.name == name) return sm->fns.data[i];
    return NULL;
}

/* ---- statements ------------------------------------------------------------------------------ */

static bool is_true_const(const nc_expr *e)
{
    return e && e->cval.kind == NC_CV_BOOL && e->cval.b;
}

static void check_var_decl(nc_sema *sm, nc_stmt *s)
{
    nc_var_decl *v = &s->u.decl;
    if (v->is_static) {
        nc_diag_error(NC_DIAG(sm), s->span, "E9001",
                      "`static` local variables are not implemented in this version; declare the static at module level");
        return;
    }
    const nc_type *declared = NULL;
    bool is_const = v->auto_const;
    if (!v->is_auto) {
        declared = nc_sema_resolve_type(sm, v->type, &is_const);
        if (!nc_ty_is_error(declared) && !nc_ty_is_void(declared) && !nc_sema_complete(sm, declared, v->type->span))
            declared = nc_ty_error();
    }

    for (uint32_t i = 0; i < v->ndecls; i++) {
        nc_declarator *d = &v->decls[i];
        const char *name = nc_sema_sym(sm, d->name);
        const nc_type *t = declared;
        if (t && nc_ty_is_void(t)) {
            nc_diag_error(NC_DIAG(sm), d->span, "E4005", "variable '%s' cannot have type 'void'", name);
            t = nc_ty_error();
        }
        /* The variable is in scope from its declarator on, as in C, but using
         * it inside its own initialiser is an error (E3003). */
        nc_var *var = nc_scope_declare(sm, d->name, t ? t : nc_ty_error(), is_const, d->span);
        var->initializing = true;
        char what[96];
        snprintf(what, sizeof what, "the initialiser of '%s'", name);
        if (d->init && d->init->kind == NC_INIT_UNINIT) {
            /* Storage the analysis treats as initialised (NOVAC_MEMORY_MODEL.md §5). */
            nc_sema_require_unsafe(sm, d->init->span, "`uninit`");
            if (!t) {
                nc_diag_error(NC_DIAG(sm), d->span, "E4064", "'auto' variable '%s' needs an initialiser to infer its type", name);
                t = nc_ty_error();
            }
        } else if (d->init && t) {
            nc_sema_check_init(sm, d->init, t, what);
        } else if (d->init && d->init->kind == NC_INIT_LIST) {
            nc_diag_error(NC_DIAG(sm), d->init->span, "E4087",
                          "the type of 'auto' variable '%s' cannot be inferred from an initialiser list", name);
            t = nc_ty_error();
        } else if (d->init) {
            nc_check_value(sm, &d->init->expr, NULL);
            nc_default_untyped(sm, &d->init->expr);
            t = d->init->expr->type;
        } else if (v->is_auto) {
            nc_diag_error(NC_DIAG(sm), d->span, "E4064", "'auto' variable '%s' needs an initialiser to infer its type", name);
        } else if (is_const) {
            nc_diag_error(NC_DIAG(sm), d->span, "E4065", "immutable variable '%s' needs an initialiser", name);
        }
        var->initializing = false;
        var->type = t ? t : nc_ty_error();
        d->var = var;
    }
}

static bool check_sub(nc_sema *sm, nc_stmt *s)
{
    nc_scope_push(sm);
    bool term = nc_check_stmt(sm, s);
    nc_scope_pop(sm);
    return term;
}

/* Returns whether the body cannot complete normally *other than* through
 * `continue` (which proceeds to the loop condition). */
static bool check_loop_body(nc_sema *sm, nc_stmt *body, bool *has_break)
{
    nc_loop_ctx ctx = {false, false, false};
    nc_vec_push(&sm->loops, ctx);
    bool term = check_sub(sm, body);
    nc_loop_ctx done = sm->loops.data[sm->loops.len - 1];
    *has_break = done.has_break;
    sm->loops.len--;
    return term && !done.has_continue;
}

static bool check_return(nc_sema *sm, nc_stmt *s)
{
    nc_fn_info *fn = sm->cur_fn;
    const char *name = nc_sema_sym(sm, fn->decl->u.fn.name);
    if (!s->u.ret) {
        if (!nc_ty_is_void(fn->ret) && !nc_ty_is_error(fn->ret))
            nc_diag_error(NC_DIAG(sm), s->span, "E4008", "function '%s' must return a value of type '%s'", name,
                          nc_type_name(fn->ret));
        return true;
    }
    if (nc_ty_is_void(fn->ret)) {
        nc_check_value(sm, &s->u.ret, NULL);
        nc_diag_error(NC_DIAG(sm), s->u.ret->span, "E4009", "function '%s' returns 'void'; `return` must not have a value",
                      name);
        return true;
    }
    char what[96];
    snprintf(what, sizeof what, "the return value of '%s'", name);
    nc_check_value(sm, &s->u.ret, fn->ret);
    nc_coerce(sm, &s->u.ret, fn->ret, what);
    return true;
}

/* ---- switch (D-025) ------------------------------------------------------------------------ */

static bool same_case_value(nc_cval a, nc_cval b)
{
    if (a.kind == NC_CV_BOOL) return a.b == b.b;
    return nc_cv_cmp(a, b) == 0;
}

/* Is every value of `t` covered by a case? Reports the missing enumerators
 * of an enum (E4071) when `report`. */
static bool switch_exhaustive(nc_sema *sm, nc_stmt *s, const nc_type *t, bool report)
{
    nc_case *cs = s->u.switch_.cases;
    uint32_t nc = s->u.switch_.ncases;
    if (nc_ty_is_bool(t)) {
        bool seen[2] = {false, false};
        for (uint32_t i = 0; i < nc; i++)
            for (uint32_t k = 0; k < cs[i].nvalues; k++)
                if (cs[i].values[k]->cval.kind == NC_CV_BOOL) seen[cs[i].values[k]->cval.b] = true;
        return seen[0] && seen[1];
    }
    if (!nc_ty_is_enum(t)) return false;
    nc_enum_type *en = t->en;
    char missing[256];
    size_t len = 0;
    unsigned nmissing = 0;
    missing[0] = 0;
    for (uint32_t e = 0; e < en->nitems; e++) {
        bool found = false;
        for (uint32_t i = 0; i < nc && !found; i++)
            for (uint32_t k = 0; k < cs[i].nvalues && !found; k++)
                if (cs[i].values[k]->cval.kind == NC_CV_INT && same_case_value(cs[i].values[k]->cval, en->items[e].value))
                    found = true;
        /* An enumerator with the same value as an earlier one is covered with it. */
        for (uint32_t p = 0; p < e && !found; p++)
            if (same_case_value(en->items[p].value, en->items[e].value)) found = true;
        if (found) continue;
        if (nmissing < 4 && len < sizeof missing - 64)
            len += (size_t)snprintf(missing + len, sizeof missing - len, "%s'%s'", nmissing ? ", " : "",
                                    nc_sema_sym(sm, en->items[e].name));
        nmissing++;
    }
    if (nmissing == 0) return true;
    if (report) {
        nc_diag_error(NC_DIAG(sm), s->u.switch_.expr->span, "E4071",
                      "switch over enum '%s' does not handle %s%s", t->name, missing, nmissing > 4 ? ", ..." : "");
        nc_diag_help(NC_DIAG(sm), "add the missing cases or a `default:` case (D-025)");
    }
    return false;
}

static bool check_switch(nc_sema *sm, nc_stmt *s)
{
    const nc_type *t = nc_check_value(sm, &s->u.switch_.expr, NULL);
    nc_default_untyped(sm, &s->u.switch_.expr);
    t = s->u.switch_.expr->type;
    bool ok_type = !nc_ty_is_error(t);
    if (ok_type && !nc_ty_is_integer(t) && !nc_ty_is_enum(t) && !nc_ty_is_bool(t)) {
        nc_diag_error(NC_DIAG(sm), s->u.switch_.expr->span, "E4073",
                      "switch operand must have an integer, enum or 'bool' type, found '%s'", nc_type_name(t));
        ok_type = false;
    }
    nc_case *cs = s->u.switch_.cases;
    uint32_t ncases = s->u.switch_.ncases;
    bool has_default = false;
    nc_span default_span = NC_NO_SPAN;
    for (uint32_t i = 0; i < ncases; i++) {
        nc_case *c = &cs[i];
        if (c->is_default) {
            if (has_default) {
                nc_diag_error(NC_DIAG(sm), c->span, "E4070", "duplicate `default` case");
                nc_diag_note(NC_DIAG(sm), default_span, "the first `default` is here");
            }
            has_default = true;
            default_span = c->span;
        }
        for (uint32_t k = 0; k < c->nvalues; k++) {
            nc_expr **slot = &c->values[k];
            nc_check_value(sm, slot, ok_type ? t : NULL);
            if (!ok_type) continue;
            if (!nc_coerce(sm, slot, t, "a case value") || nc_ty_is_error((*slot)->type)) continue;
            if ((*slot)->cval.kind == NC_CV_NONE) {
                nc_diag_error(NC_DIAG(sm), (*slot)->span, "E4082", "a case value must be a constant expression");
                (*slot)->type = nc_ty_error();
                continue;
            }
            for (uint32_t pi = 0; pi <= i; pi++) {
                uint32_t pk_end = pi == i ? k : cs[pi].nvalues;
                for (uint32_t pk = 0; pk < pk_end; pk++) {
                    nc_expr *prev = cs[pi].values[pk];
                    if (prev->cval.kind != NC_CV_NONE && !nc_ty_is_error(prev->type) &&
                        same_case_value(prev->cval, (*slot)->cval)) {
                        char buf[64];
                        if ((*slot)->cval.kind == NC_CV_BOOL) snprintf(buf, sizeof buf, "%s", (*slot)->cval.b ? "true" : "false");
                        else nc_cv_format((*slot)->cval, buf, sizeof buf);
                        nc_diag_error(NC_DIAG(sm), (*slot)->span, "E4070", "duplicate case value %s", buf);
                        nc_diag_note(NC_DIAG(sm), prev->span, "the value is first handled here");
                        goto next_value;
                    }
                }
            }
        next_value:;
        }
    }

    nc_loop_ctx ctx = {true, false, false};
    nc_vec_push(&sm->loops, ctx);
    bool last_term = false;
    for (uint32_t i = 0; i < ncases; i++) {
        nc_case *c = &cs[i];
        bool final_case = i + 1 == ncases;
        bool term = false;
        nc_scope_push(sm);
        for (uint32_t k = 0; k < c->nstmts; k++) {
            sm->fallthrough_ok = !final_case && k + 1 == c->nstmts;
            if (nc_check_stmt(sm, c->stmts[k])) term = true;
        }
        sm->fallthrough_ok = false;
        nc_scope_pop(sm);
        if (!final_case && c->nstmts && !term) {
            nc_diag_error(NC_DIAG(sm), c->span, "E2050", "this case falls through to the next one");
            nc_diag_help(NC_DIAG(sm), "end it with `break;`, or write `fallthrough;` if falling through is intended (D-025)");
        }
        if (final_case) last_term = term;
    }
    nc_loop_ctx done = sm->loops.data[--sm->loops.len];
    if (done.has_continue) {
        /* `continue` inside a switch continues the enclosing loop. */
        for (size_t i = sm->loops.len; i-- > 0;)
            if (!sm->loops.data[i].is_switch) {
                sm->loops.data[i].has_continue = true;
                break;
            }
    }

    bool exhaustive = has_default;
    if (!has_default && ok_type) exhaustive = switch_exhaustive(sm, s, t, nc_ty_is_enum(t));
    return exhaustive && !done.has_break && last_term && ncases > 0;
}

bool nc_check_stmt(nc_sema *sm, nc_stmt *s)
{
    if (!s) return false;
    bool fallthrough_ok = sm->fallthrough_ok;
    sm->fallthrough_ok = false; /* nested statements are never the last statement of a case */
    switch (s->kind) {
    case NC_S_BLOCK: {
        bool term = false;
        nc_scope_push(sm);
        for (uint32_t i = 0; i < s->u.block.count; i++)
            if (nc_check_stmt(sm, s->u.block.stmts[i])) term = true;
        nc_scope_pop(sm);
        return term;
    }
    case NC_S_DECL:
        check_var_decl(sm, s);
        return false;
    case NC_S_IF: {
        nc_check_condition(sm, &s->u.if_.cond);
        bool t1 = check_sub(sm, s->u.if_.then_s);
        bool t2 = s->u.if_.else_s ? check_sub(sm, s->u.if_.else_s) : false;
        return t1 && t2;
    }
    case NC_S_WHILE: {
        nc_check_condition(sm, &s->u.while_.cond);
        bool has_break;
        check_loop_body(sm, s->u.while_.body, &has_break);
        return is_true_const(s->u.while_.cond) && !has_break;
    }
    case NC_S_DO: {
        bool has_break;
        bool body_term = check_loop_body(sm, s->u.while_.body, &has_break);
        nc_check_condition(sm, &s->u.while_.cond);
        return (body_term || is_true_const(s->u.while_.cond)) && !has_break;
    }
    case NC_S_FOR: {
        nc_scope_push(sm);
        if (s->u.for_.init_decl) check_var_decl(sm, s->u.for_.init_decl);
        for (uint32_t i = 0; i < s->u.for_.ninit; i++) nc_check_expr_stmt(sm, &s->u.for_.init_exprs[i]);
        if (s->u.for_.cond) nc_check_condition(sm, &s->u.for_.cond);
        bool has_break;
        check_loop_body(sm, s->u.for_.body, &has_break);
        for (uint32_t i = 0; i < s->u.for_.nstep; i++) nc_check_expr_stmt(sm, &s->u.for_.step[i]);
        nc_scope_pop(sm);
        return (!s->u.for_.cond || is_true_const(s->u.for_.cond)) && !has_break;
    }
    case NC_S_SWITCH:
        return check_switch(sm, s);
    case NC_S_UNSAFE: {
        if (sm->mode == NC_MODE_SAFE) {
            nc_diag_error(NC_DIAG(sm), s->span, "E6001", "`unsafe` blocks are not allowed in a safe module");
            nc_diag_help(NC_DIAG(sm), "declare the module `@mode(controlled) module name;` (NOVAC_MEMORY_MODEL.md §2)");
        }
        sm->unsafe_depth++; /* checked as unsafe even after E6001, to avoid follow-on errors */
        bool term = check_sub(sm, s->u.unsafe_body);
        sm->unsafe_depth--;
        return term;
    }
    case NC_S_FALLTHROUGH:
        if (!fallthrough_ok) {
            nc_diag_error(NC_DIAG(sm), s->span, "E2051",
                          "`fallthrough` must be the last statement of a case that has a following case");
            return false;
        }
        return true;
    case NC_S_BREAK:
        if (sm->loops.len == 0) {
            nc_diag_error(NC_DIAG(sm), s->span, "E4062", "'break' outside of a loop or switch");
            return false;
        }
        sm->loops.data[sm->loops.len - 1].has_break = true;
        return true; /* control does not reach the next statement */
    case NC_S_CONTINUE: {
        size_t i = sm->loops.len;
        while (i > 0 && sm->loops.data[i - 1].is_switch) i--;
        if (i == 0) {
            nc_diag_error(NC_DIAG(sm), s->span, "E4062", "'continue' outside of a loop");
            return false;
        }
        /* Recorded on the innermost construct; a switch passes it outward. */
        sm->loops.data[sm->loops.len - 1].has_continue = true;
        return true;
    }
    case NC_S_RETURN:
        return check_return(sm, s);
    case NC_S_EXPR:
        nc_check_expr_stmt(sm, &s->u.expr);
        return false;
    case NC_S_EMPTY:
    case NC_S_UNSUPPORTED:
        return false;
    }
    return false;
}

/* ---- declarations ---------------------------------------------------------------------------- */

static nc_fn_info *declare_function(nc_sema *sm, nc_decl *d)
{
    const char *name = nc_sema_sym(sm, d->u.fn.name);
    bool is_c = d->u.fn.has_extern;
    bool is_import = is_c && !d->u.fn.body;
    unsigned allowed = !is_c ? 0 : is_import ? NC_ATTR_LINK_NAME : NC_ATTR_EXPORT;
    unsigned attrs = nc_sema_attrs(sm, d->attrs, d->nattrs, allowed);
    if (is_c && d->u.fn.extern_abi && strcmp(nc_sema_sym(sm, d->u.fn.extern_abi), "C") != 0) {
        nc_diag_error(NC_DIAG(sm), d->span, "E7034", "unsupported ABI \"%s\": only `extern \"C\"` exists",
                      nc_sema_sym(sm, d->u.fn.extern_abi));
        is_c = false;
    }
    if (d->u.fn.is_unsafe && sm->mode == NC_MODE_SAFE) {
        nc_diag_error(NC_DIAG(sm), d->u.fn.name_span, "E6001", "`unsafe` functions are not allowed in a safe module");
        nc_diag_help(NC_DIAG(sm), "declare the module `@mode(controlled) module name;` (NOVAC_MEMORY_MODEL.md §2)");
    }
    if (d->u.fn.ngeneric)
        nc_diag_error(NC_DIAG(sm), d->u.fn.generic_span, "E9001", "generic functions are not implemented in this version (Phase 6)");
    if (d->u.fn.variadic && !is_import) {
        nc_diag_error(NC_DIAG(sm), d->u.fn.name_span, "E7035",
                      "only C functions declared with `extern \"C\"` can be variadic; NovaC cannot define one");
        nc_diag_help(NC_DIAG(sm), "pass a pointer and a count, or a slice once they exist (Phase 6)");
    }
    if (!d->u.fn.body && !d->u.fn.has_extern)
        nc_diag_error(NC_DIAG(sm), d->u.fn.name_span, "E9001",
                      "function declarations without a body are not implemented in this version (Phase 4)");

    nc_fn_info *fn = nc_arena_alloc(&sm->s->arena, sizeof *fn);
    fn->decl = d;
    fn->is_c = is_c;
    fn->is_import = is_c && is_import;
    fn->variadic = d->u.fn.variadic && fn->is_import;
    if (is_c) {
        nc_sym sym = attrs & NC_ATTR_LINK_NAME ? nc_sema_attr_string(sm, d->attrs, d->nattrs, "link_name")
                     : attrs & NC_ATTR_EXPORT  ? nc_sema_attr_string(sm, d->attrs, d->nattrs, "export")
                                               : 0;
        fn->symbol = sym ? sym : d->u.fn.name;
    }
    bool c;
    fn->ret = nc_sema_value_type(sm, d->u.fn.ret, &c, true, "a return value");
    fn->nparams = d->u.fn.nparams;
    fn->params = nc_arena_alloc(&sm->s->arena, (fn->nparams ? fn->nparams : 1) * sizeof *fn->params);
    fn->param_const = nc_arena_alloc(&sm->s->arena, (fn->nparams ? fn->nparams : 1) * sizeof *fn->param_const);
    for (uint32_t i = 0; i < fn->nparams; i++) {
        nc_param *p = &d->u.fn.params[i];
        const nc_type *t = nc_sema_resolve_type(sm, p->type, &fn->param_const[i]);
        if (p->is_self) {
            nc_diag_error(NC_DIAG(sm), p->span, "E9001", "`self` parameters are not implemented in this version (Phase 5)");
            t = nc_ty_error();
        } else if (nc_ty_is_void(t)) {
            nc_diag_error(NC_DIAG(sm), p->span, "E4005", "parameter '%s' cannot have type 'void'", nc_sema_sym(sm, p->name));
            t = nc_ty_error();
        } else if (!nc_ty_is_error(t) && !nc_sema_complete(sm, t, p->type->span)) {
            t = nc_ty_error();
        }
        fn->params[i] = t;
    }
    if (is_c) {
        char what[160];
        for (uint32_t i = 0; i < fn->nparams; i++) {
            snprintf(what, sizeof what, "parameter '%s' of '%s'", nc_sema_sym(sm, d->u.fn.params[i].name), name);
            nc_sema_check_ffi(sm, fn->params[i], d->u.fn.params[i].span, what);
        }
        snprintf(what, sizeof what, "the result of '%s'", name);
        nc_sema_check_ffi(sm, fn->ret, d->u.fn.ret->span, what);
    }
    nc_vec_push(&sm->fns, fn);
    d->u.fn.info = fn;
    d->sema = fn;
    return fn;
}

static void check_function(nc_sema *sm, nc_fn_info *fn)
{
    nc_decl *d = fn->decl;
    sm->cur_fn = fn;
    nc_scope_push(sm);
    for (uint32_t i = 0; i < d->u.fn.nparams; i++) {
        nc_param *p = &d->u.fn.params[i];
        if (p->is_self) continue;
        p->var = nc_scope_declare(sm, p->name, fn->params[i], fn->param_const[i], p->span);
    }
    /* Parameters and the body's outermost block share one scope, as in C. */
    bool term = false;
    nc_stmt *body = d->u.fn.body;
    for (uint32_t i = 0; i < body->u.block.count; i++)
        if (nc_check_stmt(sm, body->u.block.stmts[i])) term = true;
    if (!term && !nc_ty_is_void(fn->ret) && !nc_ty_is_error(fn->ret)) {
        nc_diag_error(NC_DIAG(sm), d->u.fn.name_span, "E4060",
                      "function '%s' does not return a value on every path", nc_sema_sym(sm, d->u.fn.name));
        nc_diag_help(NC_DIAG(sm), "add a `return` of type '%s' at the end of the function", nc_type_name(fn->ret));
    }
    nc_scope_pop(sm);
    sm->cur_fn = NULL;
}

/* Unmangled symbols (extern "C" functions, @export statics) must be unique
 * and must not use the implementation's reserved prefixes. */
static void check_symbols(nc_sema *sm)
{
    NC_VEC(nc_sym) syms = {0};
    NC_VEC(nc_span) spans = {0};
    /* A NovaC `main` gets a C `main` wrapper (NOVAC_ABI.md §7). */
    nc_fn_info *main_fn = nc_sema_find_fn(sm, nc_intern_cstr(&sm->s->strtab, "main"));
    if (main_fn && !main_fn->is_c) {
        nc_vec_push(&syms, nc_intern_cstr(&sm->s->strtab, "main"));
        nc_vec_push(&spans, main_fn->decl->u.fn.name_span);
    }
    for (size_t i = 0; i < sm->msyms.len; i++) {
        nc_module_sym *ms = &sm->msyms.data[i];
        nc_sym sym = 0;
        nc_span span = NC_NO_SPAN;
        if (ms->kind == NC_MSYM_FN && ms->info && ((nc_fn_info *)ms->info)->is_c) {
            sym = ((nc_fn_info *)ms->info)->symbol;
            span = ms->decl->u.fn.name_span;
        } else if (ms->kind == NC_MSYM_STATIC && ((nc_static_info *)ms->info)->export_symbol) {
            sym = ((nc_static_info *)ms->info)->export_symbol;
            span = ms->decl->u.var.name_span;
        }
        if (!sym) continue;
        const char *str = nc_sema_sym(sm, sym);
        if (strncmp(str, "__nova", 6) == 0 || strncmp(str, "_NC", 3) == 0) {
            nc_diag_error(NC_DIAG(sm), span, "E3020", "the symbol '%s' is reserved for the implementation", str);
            continue;
        }
        bool dup = false;
        for (size_t k = 0; k < syms.len && !dup; k++)
            if (syms.data[k] == sym) {
                nc_diag_error(NC_DIAG(sm), span, "E3002", "the symbol '%s' is defined twice", str);
                nc_diag_note(NC_DIAG(sm), spans.data[k], "it is first defined here");
                dup = true;
            }
        if (dup) continue;
        nc_vec_push(&syms, sym);
        nc_vec_push(&spans, span);
    }
    nc_vec_free(&syms);
    nc_vec_free(&spans);
}

bool nc_sema_check(nc_session *s, nc_module_ast *m)
{
    nc_sema sm;
    memset(&sm, 0, sizeof sm);
    sm.s = s;
    sm.m = m;
    unsigned errors_before = s->diag.errors;

    nc_sema_attrs(&sm, m->attrs, m->nattrs, NC_ATTR_MODE);
    if ((unsigned)sm.mode > s->max_mode) {
        static const char *const names[] = {"safe", "controlled", "kernel"};
        nc_diag_error(&s->diag, m->name.span, "E6004", "module mode '%s' exceeds the limit --max-mode=%s", names[sm.mode],
                      names[s->max_mode]);
    }
    nc_sema_collect(&sm);

    /* Signatures first, so that every function can be called from anywhere;
     * then every other declaration, so that errors in unused ones are
     * reported; then the bodies. */
    NC_VEC(nc_fn_info *) to_check = {0};
    for (size_t i = 0; i < sm.msyms.len; i++) {
        if (sm.msyms.data[i].kind != NC_MSYM_FN) continue;
        nc_fn_info *fn = declare_function(&sm, sm.msyms.data[i].decl);
        sm.msyms.data[i].info = fn;
        if (fn->decl->u.fn.body) nc_vec_push(&to_check, fn);
    }
    nc_sema_resolve_decls(&sm);
    check_symbols(&sm);
    for (size_t i = 0; i < to_check.len; i++) check_function(&sm, to_check.data[i]);

    nc_vec_free(&to_check);
    nc_vec_free(&sm.fns);
    nc_vec_free(&sm.msyms);
    nc_vec_free(&sm.scope);
    nc_vec_free(&sm.marks);
    nc_vec_free(&sm.loops);
    return s->diag.errors == errors_before;
}
