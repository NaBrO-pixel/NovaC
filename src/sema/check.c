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

/* ---- types ---------------------------------------------------------------------------------- */

const nc_type *nc_sema_resolve_type(nc_sema *sm, const nc_type_expr *te, bool *is_const)
{
    *is_const = te->is_const;
    const char *what = NULL;
    int phase = 0;
    switch (te->kind) {
    case NC_TE_PRIM: return nc_ty_prim(te->u.prim);
    case NC_TE_ERROR: return nc_ty_error();
    case NC_TE_SELF: what = "`Self` is"; phase = 5; break;
    case NC_TE_PTR: what = "pointer types are"; phase = 4; break;
    case NC_TE_REF: what = "reference types are"; phase = 5; break;
    case NC_TE_ARRAY: what = "array types are"; phase = 4; break;
    case NC_TE_FN: what = "function pointer types are"; phase = 4; break;
    case NC_TE_PATH: {
        const nc_path *p = &te->u.path.path;
        const char *first = nc_sema_sym(sm, p->parts[0]);
        if (p->count > 1) {
            what = "types from other modules are";
            phase = 6;
        } else if (strcmp(first, "slice") == 0 || strcmp(first, "optional") == 0 ||
                   strcmp(first, "result") == 0 || strcmp(first, "str") == 0) {
            nc_diag_error(NC_DIAG(sm), te->span, "E9001", "'%s' is not implemented in this version (Phase 6)", first);
            return nc_ty_error();
        } else if (strcmp(first, "atomic") == 0) {
            nc_diag_error(NC_DIAG(sm), te->span, "E9001", "'atomic' is not implemented in this version (Phase 8)");
            return nc_ty_error();
        } else if (strcmp(first, "mmio") == 0) {
            nc_diag_error(NC_DIAG(sm), te->span, "E9001", "'mmio' is not implemented in this version (Phase 7)");
            return nc_ty_error();
        } else {
            nc_diag_error(NC_DIAG(sm), te->span, "E3001", "unknown type '%s'", first);
            return nc_ty_error();
        }
        break;
    }
    }
    nc_diag_error(NC_DIAG(sm), te->span, "E9001", "%s not implemented in this version (Phase %d)", what, phase);
    return nc_ty_error();
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
        nc_diag_error(NC_DIAG(sm), s->span, "E9001", "`static` local variables are not implemented in this version (Phase 4)");
        return;
    }
    const nc_type *declared = NULL;
    bool is_const = v->auto_const;
    if (!v->is_auto) declared = nc_sema_resolve_type(sm, v->type, &is_const);

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
        if (d->init && d->init->kind != NC_INIT_EXPR) {
            nc_diag_error(NC_DIAG(sm), d->init->span, "E9001", "%s not implemented in this version (Phase 4)",
                          d->init->kind == NC_INIT_LIST ? "initialiser lists are" : "`uninit` is");
        } else if (d->init) {
            char what[96];
            snprintf(what, sizeof what, "the initialiser of '%s'", name);
            nc_check_value(sm, &d->init->expr, t);
            if (t) nc_coerce(sm, &d->init->expr, t, what);
            else nc_default_untyped(sm, &d->init->expr);
            if (!t) t = d->init->expr->type;
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
    nc_loop_ctx ctx = {false, false};
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

bool nc_check_stmt(nc_sema *sm, nc_stmt *s)
{
    if (!s) return false;
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
    case NC_S_BREAK:
    case NC_S_CONTINUE:
        if (sm->loops.len == 0) {
            nc_diag_error(NC_DIAG(sm), s->span, "E4062", "'%s' outside of a loop",
                          s->kind == NC_S_BREAK ? "break" : "continue");
            return false;
        }
        if (s->kind == NC_S_BREAK) sm->loops.data[sm->loops.len - 1].has_break = true;
        else sm->loops.data[sm->loops.len - 1].has_continue = true;
        return true; /* control does not reach the next statement */
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

static void reject_attrs(nc_sema *sm, const nc_attr *a, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        nc_diag_error(NC_DIAG(sm), a[i].span, "E9001", "attribute '@%s' is not implemented in this version",
                      nc_sema_sym(sm, a[i].name));
}

static nc_fn_info *declare_function(nc_sema *sm, nc_decl *d)
{
    const char *name = nc_sema_sym(sm, d->u.fn.name);
    reject_attrs(sm, d->attrs, d->nattrs);
    if (d->u.fn.has_extern)
        nc_diag_error(NC_DIAG(sm), d->span, "E9001", "`extern` functions are not implemented in this version (Phase 4)");
    if (d->u.fn.is_unsafe)
        nc_diag_error(NC_DIAG(sm), d->span, "E9001", "`unsafe` functions are not implemented in this version (Phase 4)");
    if (d->u.fn.ngeneric)
        nc_diag_error(NC_DIAG(sm), d->u.fn.generic_span, "E9001", "generic functions are not implemented in this version (Phase 6)");
    if (d->u.fn.variadic)
        nc_diag_error(NC_DIAG(sm), d->span, "E9001", "variadic functions are not implemented in this version (Phase 4)");
    if (!d->u.fn.body && !d->u.fn.has_extern)
        nc_diag_error(NC_DIAG(sm), d->u.fn.name_span, "E9001",
                      "function declarations without a body are not implemented in this version (Phase 4)");

    nc_fn_info *fn = nc_arena_alloc(&sm->s->arena, sizeof *fn);
    fn->decl = d;
    bool c;
    fn->ret = nc_sema_resolve_type(sm, d->u.fn.ret, &c);
    fn->nparams = d->u.fn.nparams;
    fn->params = nc_arena_alloc(&sm->s->arena, (fn->nparams ? fn->nparams : 1) * sizeof *fn->params);
    for (uint32_t i = 0; i < fn->nparams; i++) {
        nc_param *p = &d->u.fn.params[i];
        const nc_type *t = nc_sema_resolve_type(sm, p->type, &c);
        if (p->is_self) {
            nc_diag_error(NC_DIAG(sm), p->span, "E9001", "`self` parameters are not implemented in this version (Phase 5)");
            t = nc_ty_error();
        } else if (nc_ty_is_void(t)) {
            nc_diag_error(NC_DIAG(sm), p->span, "E4005", "parameter '%s' cannot have type 'void'", nc_sema_sym(sm, p->name));
            t = nc_ty_error();
        }
        fn->params[i] = t;
    }

    nc_fn_info *prev = nc_sema_find_fn(sm, d->u.fn.name);
    if (prev) {
        nc_diag_error(NC_DIAG(sm), d->u.fn.name_span, "E3002", "redefinition of function '%s'", name);
        nc_diag_note(NC_DIAG(sm), prev->decl->u.fn.name_span, "previous definition is here");
        return NULL;
    }
    nc_vec_push(&sm->fns, fn);
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
        bool is_const = p->type->is_const && p->type->kind == NC_TE_PRIM;
        p->var = nc_scope_declare(sm, p->name, fn->params[i], is_const, p->span);
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

bool nc_sema_check(nc_session *s, nc_module_ast *m)
{
    nc_sema sm;
    memset(&sm, 0, sizeof sm);
    sm.s = s;
    sm.m = m;
    unsigned errors_before = s->diag.errors;

    reject_attrs(&sm, m->attrs, m->nattrs);

    NC_VEC(nc_fn_info *) to_check = {0};
    for (uint32_t i = 0; i < m->ndecls; i++) {
        nc_decl *d = m->decls[i];
        if (d->kind != NC_D_FUNCTION) continue;
        nc_fn_info *fn = declare_function(&sm, d);
        if (fn && d->u.fn.body) nc_vec_push(&to_check, fn);
    }
    for (size_t i = 0; i < to_check.len; i++) check_function(&sm, to_check.data[i]);

    nc_vec_free(&to_check);
    nc_vec_free(&sm.fns);
    nc_vec_free(&sm.scope);
    nc_vec_free(&sm.marks);
    nc_vec_free(&sm.loops);
    return s->diag.errors == errors_before;
}
