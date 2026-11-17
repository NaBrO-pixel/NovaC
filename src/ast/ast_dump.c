/* S-expression dump of the AST (--dump-ast, --emit=typed). The format is
 * stable: golden tests in tests/parse and tests/sema depend on it. */
#include "ast/ast.h"

#include "lex/lexer.h"

typedef struct dumper {
    nc_session *s;
    nc_type_printer print_type;
    FILE *out;
} dumper;

static void expr(dumper *d, const nc_expr *e);
static void type_expr(dumper *d, const nc_type_expr *t);

static const char *sym(dumper *d, nc_sym s)
{
    return nc_sym_str(&d->s->strtab, s);
}

static void indent(dumper *d, int n)
{
    fputc('\n', d->out);
    for (int i = 0; i < n; i++) fputs("  ", d->out);
}

static void path(dumper *d, const nc_path *p)
{
    for (uint32_t i = 0; i < p->count; i++) {
        if (i) fputc('.', d->out);
        fputs(sym(d, p->parts[i]), d->out);
    }
}

static void generic_args(dumper *d, const nc_generic_arg *args, uint32_t n)
{
    fputs("(args", d->out);
    for (uint32_t i = 0; i < n; i++) {
        fputc(' ', d->out);
        if (args[i].type) type_expr(d, args[i].type);
        else expr(d, args[i].expr);
    }
    fputc(')', d->out);
}

static void type_expr(dumper *d, const nc_type_expr *t)
{
    if (!t) {
        fputs("-", d->out);
        return;
    }
    if (t->is_const) fputs("(const ", d->out);
    switch (t->kind) {
    case NC_TE_PRIM: fputs(nc_prim_name(t->u.prim), d->out); break;
    case NC_TE_SELF: fputs("Self", d->out); break;
    case NC_TE_ERROR: fputs("(error)", d->out); break;
    case NC_TE_PATH:
        fputs("(path ", d->out);
        path(d, &t->u.path.path);
        if (t->u.path.nargs) {
            fputc(' ', d->out);
            generic_args(d, t->u.path.args, t->u.path.nargs);
        }
        fputc(')', d->out);
        break;
    case NC_TE_PTR:
    case NC_TE_REF:
        fputs(t->kind == NC_TE_PTR ? "(ptr " : "(ref ", d->out);
        type_expr(d, t->u.inner);
        fputc(')', d->out);
        break;
    case NC_TE_ARRAY:
        fputs("(array ", d->out);
        type_expr(d, t->u.array.elem);
        fputc(' ', d->out);
        expr(d, t->u.array.len);
        fputc(')', d->out);
        break;
    case NC_TE_FN:
        fputs("(fn-type ", d->out);
        type_expr(d, t->u.fn.ret);
        fputs(" (params", d->out);
        for (uint32_t i = 0; i < t->u.fn.nparams; i++) {
            fputc(' ', d->out);
            type_expr(d, t->u.fn.params[i]);
        }
        if (t->u.fn.variadic) fputs(" ...", d->out);
        fputs("))", d->out);
        break;
    }
    if (t->is_const) fputc(')', d->out);
}

static void init(dumper *d, const nc_init *in);

static void init_items(dumper *d, const nc_init_item *items, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        fputc(' ', d->out);
        if (items[i].field) {
            fprintf(d->out, "(field %s ", sym(d, items[i].field));
            init(d, items[i].value);
            fputc(')', d->out);
        } else if (items[i].index) {
            fputs("(at ", d->out);
            expr(d, items[i].index);
            fputc(' ', d->out);
            init(d, items[i].value);
            fputc(')', d->out);
        } else {
            init(d, items[i].value);
        }
    }
}

static void init(dumper *d, const nc_init *in)
{
    switch (in->kind) {
    case NC_INIT_EXPR: expr(d, in->expr); break;
    case NC_INIT_UNINIT: fputs("(uninit)", d->out); break;
    case NC_INIT_LIST:
        fputs("(list", d->out);
        init_items(d, in->items, in->nitems);
        fputc(')', d->out);
        break;
    }
}

static void pattern(dumper *d, const nc_pattern *p)
{
    fputs("(pattern ", d->out);
    switch (p->kind) {
    case NC_PAT_SOME: fputs("some", d->out); break;
    case NC_PAT_NONE: fputs("none", d->out); break;
    case NC_PAT_NAMED: fputs(sym(d, p->name), d->out); break;
    }
    if (p->binding) fprintf(d->out, " %s", sym(d, p->binding));
    fputc(')', d->out);
}

static void close_expr(dumper *d, const nc_expr *e)
{
    if (d->print_type) {
        fputs(" :", d->out);
        if (e->binding_kind == NC_BIND_FN) fputs("fn", d->out); /* function values have no type yet */
        else d->print_type(d->out, e->type);
        /* Compile-time value, for golden tests of constant folding. */
        switch (e->cval.kind) {
        case NC_CV_INT: fprintf(d->out, " =%s%llu", e->cval.neg ? "-" : "", (unsigned long long)e->cval.mag); break;
        case NC_CV_FLOAT: fprintf(d->out, " =%.17g", e->cval.f); break;
        case NC_CV_BOOL: fprintf(d->out, " =%s", e->cval.b ? "true" : "false"); break;
        case NC_CV_NONE: break;
        }
    }
    fputc(')', d->out);
}

static void expr(dumper *d, const nc_expr *e)
{
    if (!e) {
        fputs("-", d->out);
        return;
    }
    switch (e->kind) {
    case NC_E_INT:
        fprintf(d->out, "(int %llu", (unsigned long long)e->u.int_lit.value);
        if (e->u.int_lit.suffix) fprintf(d->out, " %s", nc_suffix_name(e->u.int_lit.suffix));
        break;
    case NC_E_FLOAT:
        fprintf(d->out, "(float %.17g", e->u.float_lit.value);
        if (e->u.float_lit.suffix) fprintf(d->out, " %s", nc_suffix_name(e->u.float_lit.suffix));
        break;
    case NC_E_BOOL: fprintf(d->out, "(bool %s", e->u.bool_lit ? "true" : "false"); break;
    case NC_E_CHAR: fprintf(d->out, "(char %u", (unsigned)e->u.char_lit); break;
    case NC_E_STRING:
    case NC_E_CSTRING:
        fputs(e->kind == NC_E_STRING ? "(string " : "(cstring ", d->out);
        nc_print_escaped(d->out, sym(d, e->u.str), nc_sym_len(&d->s->strtab, e->u.str));
        break;
    case NC_E_NULL: fputs("(null", d->out); break;
    case NC_E_NONE: fputs("(none", d->out); break;
    case NC_E_SELF: fputs("(self", d->out); break;
    case NC_E_ERROR: fputs("(error", d->out); break;
    case NC_E_NAME: fprintf(d->out, "(name %s", sym(d, e->u.name)); break;
    case NC_E_UNARY:
        fprintf(d->out, "(unary %s ", nc_unop_spelling(e->u.unary.op));
        expr(d, e->u.unary.operand);
        break;
    case NC_E_BINARY:
        fprintf(d->out, "(binary %s ", nc_binop_spelling(e->u.binary.op));
        expr(d, e->u.binary.lhs);
        fputc(' ', d->out);
        expr(d, e->u.binary.rhs);
        break;
    case NC_E_ASSIGN:
        fprintf(d->out, "(assign %s ", nc_assignop_spelling(e->u.binary.op));
        expr(d, e->u.binary.lhs);
        fputc(' ', d->out);
        expr(d, e->u.binary.rhs);
        break;
    case NC_E_COND:
        fputs("(cond ", d->out);
        expr(d, e->u.cond.cond);
        fputc(' ', d->out);
        expr(d, e->u.cond.then_e);
        fputc(' ', d->out);
        expr(d, e->u.cond.else_e);
        break;
    case NC_E_CALL:
        fputs("(call ", d->out);
        expr(d, e->u.call.callee);
        for (uint32_t i = 0; i < e->u.call.nargs; i++) {
            fputc(' ', d->out);
            expr(d, e->u.call.args[i]);
        }
        break;
    case NC_E_INDEX:
        fputs("(index ", d->out);
        expr(d, e->u.index.base);
        fputc(' ', d->out);
        expr(d, e->u.index.index);
        break;
    case NC_E_MEMBER:
    case NC_E_ARROW:
        fputs(e->kind == NC_E_MEMBER ? "(member " : "(arrow ", d->out);
        expr(d, e->u.member.base);
        fprintf(d->out, " %s", sym(d, e->u.member.name));
        break;
    case NC_E_CONVERT:
        fputs("(convert ", d->out);
        type_expr(d, e->u.convert.type);
        fputc(' ', d->out);
        expr(d, e->u.convert.operand);
        break;
    case NC_E_IMPLICIT:
        fputs("(implicit ", d->out);
        expr(d, e->u.convert.operand);
        break;
    case NC_E_STRUCT_LIT:
        fputs("(struct-lit ", d->out);
        type_expr(d, e->u.struct_lit.type);
        init_items(d, e->u.struct_lit.items, e->u.struct_lit.nitems);
        break;
    case NC_E_SOME:
        fputs("(some ", d->out);
        expr(d, e->u.some);
        break;
    case NC_E_SIZEOF:
    case NC_E_ALIGNOF:
        fputs(e->kind == NC_E_SIZEOF ? "(sizeof " : "(alignof ", d->out);
        if (e->u.size.type) type_expr(d, e->u.size.type);
        else expr(d, e->u.size.expr);
        break;
    case NC_E_OFFSETOF:
        fputs("(offsetof ", d->out);
        type_expr(d, e->u.offsetof_.type);
        fprintf(d->out, " %s", sym(d, e->u.offsetof_.field));
        break;
    case NC_E_GENERIC:
        fputs("(generic ", d->out);
        expr(d, e->u.generic.base);
        fputc(' ', d->out);
        generic_args(d, e->u.generic.args, e->u.generic.nargs);
        break;
    case NC_E_IS:
        fputs("(is ", d->out);
        expr(d, e->u.is.operand);
        fputc(' ', d->out);
        pattern(d, &e->u.is.pattern);
        break;
    }
    close_expr(d, e);
}

static void stmt(dumper *d, const nc_stmt *s, int depth);

static void var_decl(dumper *d, const nc_var_decl *v)
{
    fputs("(decl", d->out);
    if (v->is_static) fputs(" static", d->out);
    fputc(' ', d->out);
    if (v->is_auto) fputs(v->auto_const ? "const-auto" : "auto", d->out);
    else type_expr(d, v->type);
    for (uint32_t i = 0; i < v->ndecls; i++) {
        fprintf(d->out, " (var %s", sym(d, v->decls[i].name));
        if (v->decls[i].init) {
            fputc(' ', d->out);
            init(d, v->decls[i].init);
        }
        fputc(')', d->out);
    }
    fputc(')', d->out);
}

static void expr_list(dumper *d, const char *tag, nc_expr **es, uint32_t n)
{
    fprintf(d->out, "(%s", tag);
    for (uint32_t i = 0; i < n; i++) {
        fputc(' ', d->out);
        expr(d, es[i]);
    }
    fputc(')', d->out);
}

static void stmt(dumper *d, const nc_stmt *s, int depth)
{
    indent(d, depth);
    switch (s->kind) {
    case NC_S_BLOCK:
        fputs("(block", d->out);
        for (uint32_t i = 0; i < s->u.block.count; i++) stmt(d, s->u.block.stmts[i], depth + 1);
        fputc(')', d->out);
        break;
    case NC_S_DECL: var_decl(d, &s->u.decl); break;
    case NC_S_IF:
        fputs("(if ", d->out);
        expr(d, s->u.if_.cond);
        stmt(d, s->u.if_.then_s, depth + 1);
        if (s->u.if_.else_s) stmt(d, s->u.if_.else_s, depth + 1);
        fputc(')', d->out);
        break;
    case NC_S_WHILE:
        fputs("(while ", d->out);
        expr(d, s->u.while_.cond);
        stmt(d, s->u.while_.body, depth + 1);
        fputc(')', d->out);
        break;
    case NC_S_DO:
        fputs("(do ", d->out);
        expr(d, s->u.while_.cond);
        stmt(d, s->u.while_.body, depth + 1);
        fputc(')', d->out);
        break;
    case NC_S_FOR:
        fputs("(for ", d->out);
        if (s->u.for_.init_decl) var_decl(d, &s->u.for_.init_decl->u.decl);
        else expr_list(d, "init", s->u.for_.init_exprs, s->u.for_.ninit);
        fputc(' ', d->out);
        expr(d, s->u.for_.cond);
        fputc(' ', d->out);
        expr_list(d, "step", s->u.for_.step, s->u.for_.nstep);
        stmt(d, s->u.for_.body, depth + 1);
        fputc(')', d->out);
        break;
    case NC_S_BREAK: fputs("(break)", d->out); break;
    case NC_S_CONTINUE: fputs("(continue)", d->out); break;
    case NC_S_RETURN:
        fputs("(return", d->out);
        if (s->u.ret) {
            fputc(' ', d->out);
            expr(d, s->u.ret);
        }
        fputc(')', d->out);
        break;
    case NC_S_EXPR:
        fputs("(expr ", d->out);
        expr(d, s->u.expr);
        fputc(')', d->out);
        break;
    case NC_S_EMPTY: fputs("(empty)", d->out); break;
    case NC_S_UNSUPPORTED: fprintf(d->out, "(unsupported %s)", s->u.unsupported); break;
    }
}

static void attrs(dumper *d, const nc_attr *a, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        fprintf(d->out, " (attr %s", sym(d, a[i].name));
        for (uint32_t k = 0; k < a[i].nargs; k++) {
            fputc(' ', d->out);
            if (a[i].args[k].name) fprintf(d->out, "%s=", sym(d, a[i].args[k].name));
            expr(d, a[i].args[k].value);
        }
        fputc(')', d->out);
    }
}

static void decl(dumper *d, const nc_decl *dc)
{
    indent(d, 1);
    if (dc->kind == NC_D_UNSUPPORTED) {
        fprintf(d->out, "(unsupported %s)", dc->u.unsupported);
        return;
    }
    fprintf(d->out, "(fn %s", sym(d, dc->u.fn.name));
    if (dc->is_public) fputs(" public", d->out);
    if (dc->u.fn.has_extern) {
        fputs(" extern ", d->out);
        nc_print_escaped(d->out, sym(d, dc->u.fn.extern_abi), nc_sym_len(&d->s->strtab, dc->u.fn.extern_abi));
    }
    if (dc->u.fn.is_unsafe) fputs(" unsafe", d->out);
    attrs(d, dc->attrs, dc->nattrs);
    if (dc->u.fn.ngeneric) fprintf(d->out, " (generics %u)", dc->u.fn.ngeneric);
    fputs(" (ret ", d->out);
    type_expr(d, dc->u.fn.ret);
    fputs(") (params", d->out);
    for (uint32_t i = 0; i < dc->u.fn.nparams; i++) {
        const nc_param *p = &dc->u.fn.params[i];
        fputs(" (param ", d->out);
        type_expr(d, p->type);
        fprintf(d->out, " %s)", p->is_self ? "self" : sym(d, p->name));
    }
    if (dc->u.fn.variadic) fputs(" ...", d->out);
    fputc(')', d->out);
    if (dc->u.fn.body) stmt(d, dc->u.fn.body, 2);
    else fputs(" -", d->out);
    fputc(')', d->out);
}

void nc_ast_dump(nc_session *s, const nc_module_ast *m, nc_type_printer print_type, FILE *out)
{
    dumper d = {s, print_type, out};
    fputs("(module ", out);
    if (m->has_module_decl) path(&d, &m->name);
    else fputs("-", out);
    attrs(&d, m->attrs, m->nattrs);
    for (uint32_t i = 0; i < m->ndecls; i++) decl(&d, m->decls[i]);
    fputs(")\n", out);
}
