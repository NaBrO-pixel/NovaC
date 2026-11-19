/* NIR utilities: release, verification and the --emit=nir dump. */
#include "nir/nir.h"

#include <inttypes.h>
#include <stdlib.h>

void nc_nir_free(nc_nir_module *m)
{
    if (!m) return;
    for (uint32_t i = 0; i < m->nfns; i++) {
        nc_nir_fn *f = m->fns[i];
        for (uint32_t b = 0; b < f->nblocks; b++) {
            free(f->blocks[b].stmts);
            free(f->blocks[b].term.args);
        }
        free(f->blocks);
        free(f->locals);
        free(f);
    }
    free(m->fns);
    free(m->statics);
    free(m);
}

const char *nc_nir_binop_name(nc_nir_binop op)
{
    static const char *const names[] = {
        "add", "sub", "mul", "div", "rem", "shl", "shr", "and", "or", "xor",
        "eq", "ne", "lt", "le", "gt", "ge",
    };
    return (unsigned)op < sizeof names / sizeof names[0] ? names[op] : "?";
}

const nc_type *nc_nir_place_base_type(const nc_nir_module *m, const nc_nir_fn *f, const nc_place *p)
{
    return p->base == NC_PLACE_LOCAL ? f->locals[p->id].type : m->statics[p->id].type;
}

/* ---- verification ---- */

static bool check_place(const nc_nir_module *m, const nc_nir_fn *f, const nc_place *p, char *why, size_t n)
{
    if (p->base == NC_PLACE_LOCAL ? p->id >= f->nlocals : p->id >= m->nstatics) {
        snprintf(why, n, "place refers to nonexistent %s %u", p->base == NC_PLACE_LOCAL ? "local" : "static", p->id);
        return false;
    }
    const nc_type *t = nc_nir_place_base_type(m, f, p);
    for (uint32_t i = 0; i < p->nprojs; i++) {
        const nc_proj *pr = &p->projs[i];
        const nc_type *next = NULL;
        if (pr->kind == NC_PROJ_FIELD) {
            if ((nc_ty_is_struct(t) || nc_ty_is_union(t)) && pr->field < t->agg->nfields) next = t->agg->fields[pr->field].type;
        } else if (pr->kind == NC_PROJ_DEREF) {
            if (nc_ty_is_ptr(t) && !nc_ty_is_void(t->elem)) next = t->elem;
        } else if (nc_ty_is_array(t)) {
            bool ok_index = pr->index_is_const ? pr->index < t->len
                                               : pr->index_local < f->nlocals &&
                                                     f->locals[pr->index_local].type == nc_ty_prim(NC_PRIM_USIZE);
            if (ok_index) next = t->elem;
        }
        if (!next || next != pr->type) {
            snprintf(why, n, "invalid projection %u of a place of type %s", i, nc_type_name(t));
            return false;
        }
        t = next;
    }
    if (t != p->type) {
        snprintf(why, n, "place type %s differs from its projected type %s", nc_type_name(p->type), nc_type_name(t));
        return false;
    }
    return true;
}

static bool check_operand(const nc_nir_module *m, const nc_nir_fn *f, const nc_operand *o, char *why, size_t n)
{
    if (!o->type) {
        snprintf(why, n, "operand without a type");
        return false;
    }
    if (o->kind == NC_OP_COPY) {
        if (!check_place(m, f, &o->place, why, n)) return false;
        if (o->place.type != o->type) {
            snprintf(why, n, "operand type %s differs from its place's type %s", nc_type_name(o->type),
                     nc_type_name(o->place.type));
            return false;
        }
    } else if (o->kind == NC_OP_CONST && o->cval.kind == NC_CV_NONE) {
        snprintf(why, n, "constant operand without a value");
        return false;
    }
    if (nc_ty_is_untyped(o->type) || nc_ty_is_error(o->type)) {
        snprintf(why, n, "operand of type %s reached NIR", nc_type_name(o->type));
        return false;
    }
    return true;
}

static bool check_rvalue(const nc_nir_module *m, const nc_nir_fn *f, const nc_nir_stmt *st, char *why, size_t n)
{
    const nc_rvalue *rv = &st->rv;
    const nc_type *dt = st->dest.type;
    if (st->kind == NC_ST_CHECKED)
        return check_operand(m, f, &rv->a, why, n) && check_operand(m, f, &rv->b, why, n);
    switch (rv->kind) {
    case NC_RV_USE:
        if (!check_operand(m, f, &rv->a, why, n)) return false;
        if (rv->a.type != dt) {
            snprintf(why, n, "assignment of %s to a place of type %s", nc_type_name(rv->a.type), nc_type_name(dt));
            return false;
        }
        return true;
    case NC_RV_BINARY: return check_operand(m, f, &rv->a, why, n) && check_operand(m, f, &rv->b, why, n);
    case NC_RV_ADDR:
        if (!check_operand(m, f, &rv->a, why, n)) return false;
        if (rv->a.kind != NC_OP_COPY || !nc_ty_is_ptr(dt) || dt->elem != rv->a.type) {
            snprintf(why, n, "malformed address-of producing %s", nc_type_name(dt));
            return false;
        }
        return true;
    case NC_RV_PTR_OFFSET:
    case NC_RV_PTR_DIFF: {
        if (!check_operand(m, f, &rv->a, why, n) || !check_operand(m, f, &rv->b, why, n)) return false;
        bool ok = nc_ty_is_ptr(rv->a.type);
        if (rv->kind == NC_RV_PTR_OFFSET)
            ok = ok && dt == rv->a.type &&
                 (rv->b.type == nc_ty_prim(NC_PRIM_ISIZE) || rv->b.type == nc_ty_prim(NC_PRIM_USIZE));
        else
            ok = ok && rv->b.type == rv->a.type && dt == nc_ty_prim(NC_PRIM_ISIZE);
        if (!ok) {
            snprintf(why, n, "malformed pointer arithmetic producing %s", nc_type_name(dt));
            return false;
        }
        return true;
    }
    case NC_RV_UNARY:
    case NC_RV_CAST: return check_operand(m, f, &rv->a, why, n);
    case NC_RV_AGGREGATE: {
        uint32_t expect = nc_ty_is_array(dt) ? (uint32_t)dt->len : nc_ty_is_struct(dt) ? dt->agg->nfields : 1;
        if (!nc_ty_is_aggregate(dt) || rv->nelems != expect ||
            (nc_ty_is_union(dt) && rv->union_field >= dt->agg->nfields)) {
            snprintf(why, n, "malformed aggregate of type %s", nc_type_name(dt));
            return false;
        }
        for (uint32_t i = 0; i < rv->nelems; i++) {
            if (!check_operand(m, f, &rv->elems[i], why, n)) return false;
            const nc_type *mt = nc_ty_is_array(dt)   ? dt->elem
                                : nc_ty_is_union(dt) ? dt->agg->fields[rv->union_field].type
                                                     : dt->agg->fields[i].type;
            if (rv->elems[i].type != mt) {
                snprintf(why, n, "aggregate member %u of %s has type %s", i, nc_type_name(dt), nc_type_name(rv->elems[i].type));
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

static bool check_block_id(const nc_nir_fn *f, nc_block_id b, char *why, size_t n)
{
    if (b < f->nblocks) return true;
    snprintf(why, n, "branch to nonexistent block bb%u", b);
    return false;
}

bool nc_nir_verify(const nc_nir_module *m, char *why, size_t n)
{
    for (uint32_t i = 0; i < m->nfns; i++) {
        const nc_nir_fn *f = m->fns[i];
        if (f->nblocks == 0) {
            snprintf(why, n, "function without blocks");
            return false;
        }
        for (uint32_t b = 0; b < f->nblocks; b++) {
            const nc_nir_block *blk = &f->blocks[b];
            for (uint32_t s = 0; s < blk->nstmts; s++) {
                const nc_nir_stmt *st = &blk->stmts[s];
                if (!check_place(m, f, &st->dest, why, n)) return false;
                if (st->dest.base == NC_PLACE_STATIC && m->statics[st->dest.id].is_const) {
                    snprintf(why, n, "bb%u: assignment to an immutable static", b);
                    return false;
                }
                if (!check_rvalue(m, f, st, why, n)) return false;
                if (st->kind == NC_ST_CHECKED && (st->flag >= f->nlocals || !nc_ty_is_bool(f->locals[st->flag].type))) {
                    snprintf(why, n, "bb%u: checked operation without a bool flag", b);
                    return false;
                }
            }
            const nc_terminator *t = &blk->term;
            switch (t->kind) {
            case NC_TERM_NONE:
                snprintf(why, n, "bb%u has no terminator", b);
                return false;
            case NC_TERM_GOTO:
                if (!check_block_id(f, t->target, why, n)) return false;
                break;
            case NC_TERM_BRANCH:
            case NC_TERM_ASSERT:
                if (!check_operand(m, f, &t->cond, why, n)) return false;
                if (!nc_ty_is_bool(t->cond.type)) {
                    snprintf(why, n, "bb%u: condition is not bool", b);
                    return false;
                }
                if (!check_block_id(f, t->target, why, n)) return false;
                if (t->kind == NC_TERM_BRANCH && !check_block_id(f, t->other, why, n)) return false;
                break;
            case NC_TERM_RETURN:
                if (t->has_value != !nc_ty_is_void(f->ret)) {
                    snprintf(why, n, "bb%u: return value does not match the function type", b);
                    return false;
                }
                if (t->has_value && !check_operand(m, f, &t->value, why, n)) return false;
                break;
            case NC_TERM_CALL:
                if (!check_block_id(f, t->target, why, n)) return false;
                if (!t->callee || t->nargs != t->callee->nparams) {
                    snprintf(why, n, "bb%u: call with a wrong argument count", b);
                    return false;
                }
                for (uint32_t a = 0; a < t->nargs; a++) {
                    if (!check_operand(m, f, &t->args[a], why, n)) return false;
                    if (t->args[a].type != t->callee->locals[a].type) {
                        snprintf(why, n, "bb%u: argument %u has the wrong type", b, a);
                        return false;
                    }
                }
                break;
            case NC_TERM_SWITCH:
                if (!check_operand(m, f, &t->cond, why, n)) return false;
                if (!check_block_id(f, t->other, why, n)) return false;
                for (uint32_t c = 0; c < t->ncases; c++)
                    if (!check_block_id(f, t->case_targets[c], why, n)) return false;
                break;
            case NC_TERM_UNREACHABLE:
                break;
            }
        }
    }
    return true;
}

/* ---- dump ---- */

/* The module and function being dumped, for the names of statics and fields. */
static const nc_nir_module *dump_module;
static const nc_nir_fn *dump_fn;

static void place(nc_session *s, FILE *out, const nc_place *p)
{
    if (p->base == NC_PLACE_LOCAL) fprintf(out, "_%u", p->id);
    else fprintf(out, "@%s", nc_sym_str(&s->strtab, dump_module->statics[p->id].name));
    for (uint32_t i = 0; i < p->nprojs; i++) {
        const nc_proj *pr = &p->projs[i];
        const nc_type *parent = i ? p->projs[i - 1].type : nc_nir_place_base_type(dump_module, dump_fn, p);
        if (pr->kind == NC_PROJ_FIELD) {
            fprintf(out, ".%s", nc_sym_str(&s->strtab, parent->agg->fields[pr->field].name));
        } else if (pr->kind == NC_PROJ_DEREF) {
            fputs(".*", out);
        } else if (pr->index_is_const) {
            fprintf(out, "[%" PRIu64 "]", pr->index);
        } else {
            fprintf(out, "[_%u]", pr->index_local);
        }
    }
}

static void cval(FILE *out, nc_cval v, const nc_type *t)
{
    switch (v.kind) {
    case NC_CV_INT: fprintf(out, "const %s%" PRIu64 "%s", v.neg ? "-" : "", v.mag, nc_type_name(t)); break;
    case NC_CV_FLOAT: fprintf(out, "const %.17g%s", v.f, nc_type_name(t)); break;
    case NC_CV_BOOL: fprintf(out, "const %s", v.b ? "true" : "false"); break;
    default: fputs("const ?", out); break;
    }
}

static void operand(nc_session *s, FILE *out, const nc_operand *o)
{
    switch (o->kind) {
    case NC_OP_COPY: place(s, out, &o->place); return;
    case NC_OP_ZERO: fprintf(out, "zeroed %s", nc_type_name(o->type)); return;
    case NC_OP_UNDEF: fprintf(out, "uninit %s", nc_type_name(o->type)); return;
    case NC_OP_CONST: cval(out, o->cval, o->type); return;
    }
}

static const char *cast_name(nc_cast_kind k)
{
    switch (k) {
    case NC_CAST_INT_RESIZE: return "int_resize";
    case NC_CAST_INT_TO_FLOAT: return "int_to_float";
    case NC_CAST_FLOAT_TO_INT: return "float_to_int";
    case NC_CAST_FLOAT_RESIZE: return "float_resize";
    case NC_CAST_BOOL_TO_INT: return "bool_to_int";
    case NC_CAST_NOP: return "nop";
    case NC_CAST_PTR_TO_INT: return "ptr_to_int";
    case NC_CAST_INT_TO_PTR: return "int_to_ptr";
    }
    return "?";
}

static void rvalue(nc_session *s, FILE *out, const nc_rvalue *rv, const nc_type *dest_type)
{
    switch (rv->kind) {
    case NC_RV_USE: operand(s, out, &rv->a); break;
    case NC_RV_BINARY:
        fprintf(out, "%s(", nc_nir_binop_name(rv->binop));
        operand(s, out, &rv->a);
        fputs(", ", out);
        operand(s, out, &rv->b);
        fputc(')', out);
        break;
    case NC_RV_UNARY:
        fputs(rv->unop == NC_NU_NEG_FLOAT ? "fneg(" : rv->unop == NC_NU_NOT ? "not(" : "bitnot(", out);
        operand(s, out, &rv->a);
        fputc(')', out);
        break;
    case NC_RV_CAST:
        fprintf(out, "%s(", cast_name(rv->cast));
        operand(s, out, &rv->a);
        fprintf(out, " as %s)", nc_type_name(dest_type));
        break;
    case NC_RV_ADDR:
        fputs("addr(", out);
        operand(s, out, &rv->a);
        fputc(')', out);
        break;
    case NC_RV_PTR_OFFSET:
    case NC_RV_PTR_DIFF:
        fputs(rv->kind == NC_RV_PTR_DIFF ? "ptr_diff(" : rv->binop == NC_NB_ADD ? "ptr_add(" : "ptr_sub(", out);
        operand(s, out, &rv->a);
        fputs(", ", out);
        operand(s, out, &rv->b);
        fputc(')', out);
        break;
    case NC_RV_AGGREGATE:
        fprintf(out, "%s {", nc_type_name(dest_type));
        if (nc_ty_is_union(dest_type))
            fprintf(out, " .%s =", nc_sym_str(&s->strtab, dest_type->agg->fields[rv->union_field].name));
        for (uint32_t i = 0; i < rv->nelems; i++) {
            fputs(i ? ", " : " ", out);
            operand(s, out, &rv->elems[i]);
        }
        fputs(" }", out);
        break;
    }
}

void nc_nir_dump(nc_session *s, const nc_nir_module *m, FILE *out)
{
    fputs("module ", out);
    for (uint32_t i = 0; i < m->name.count; i++)
        fprintf(out, "%s%s", i ? "." : "", nc_sym_str(&s->strtab, m->name.parts[i]));
    fputc('\n', out);
    dump_module = m;
    for (uint32_t i = 0; i < m->nstatics; i++) {
        const nc_nir_static *st = &m->statics[i];
        fprintf(out, "\nstatic %s%s%s: %s\n", st->is_public ? "public " : "", st->is_const ? "const " : "",
                nc_sym_str(&s->strtab, st->name), nc_type_name(st->type));
    }
    for (uint32_t i = 0; i < m->nfns; i++) {
        const nc_nir_fn *f = m->fns[i];
        dump_fn = f;
        fprintf(out, "\nfn %s%s(", f->is_public ? "public " : "", nc_sym_str(&s->strtab, f->name));
        for (uint32_t p = 0; p < f->nparams; p++)
            fprintf(out, "%s_%u: %s", p ? ", " : "", p, nc_type_name(f->locals[p].type));
        fprintf(out, ") -> %s {\n", nc_type_name(f->ret));
        for (uint32_t l = f->nparams; l < f->nlocals; l++) {
            fprintf(out, "    let _%u: %s;", l, nc_type_name(f->locals[l].type));
            if (f->locals[l].name) fprintf(out, " // %s", nc_sym_str(&s->strtab, f->locals[l].name));
            fputc('\n', out);
        }
        for (uint32_t b = 0; b < f->nblocks; b++) {
            const nc_nir_block *blk = &f->blocks[b];
            fprintf(out, "  bb%u:\n", b);
            for (uint32_t k = 0; k < blk->nstmts; k++) {
                const nc_nir_stmt *st = &blk->stmts[k];
                if (st->kind == NC_ST_CHECKED) {
                    fputs("    (", out);
                    place(s, out, &st->dest);
                    fprintf(out, ", _%u) = checked_%s(", st->flag, nc_nir_binop_name(st->op));
                    operand(s, out, &st->rv.a);
                    fputs(", ", out);
                    operand(s, out, &st->rv.b);
                    fputs(")\n", out);
                } else {
                    fputs("    ", out);
                    place(s, out, &st->dest);
                    fputs(" = ", out);
                    rvalue(s, out, &st->rv, st->dest.type);
                    fputc('\n', out);
                }
            }
            const nc_terminator *t = &blk->term;
            fputs("    ", out);
            switch (t->kind) {
            case NC_TERM_NONE: fputs("<unterminated>", out); break;
            case NC_TERM_GOTO: fprintf(out, "goto bb%u", t->target); break;
            case NC_TERM_BRANCH:
                fputs("branch ", out);
                operand(s, out, &t->cond);
                fprintf(out, " -> bb%u, bb%u", t->target, t->other);
                break;
            case NC_TERM_RETURN:
                fputs("return", out);
                if (t->has_value) {
                    fputc(' ', out);
                    operand(s, out, &t->value);
                }
                break;
            case NC_TERM_CALL:
                if (t->dest != UINT32_MAX) fprintf(out, "_%u = ", t->dest);
                fprintf(out, "call %s(", nc_sym_str(&s->strtab, t->callee->name));
                for (uint32_t a = 0; a < t->nargs; a++) {
                    if (a) fputs(", ", out);
                    operand(s, out, &t->args[a]);
                }
                fprintf(out, ") -> bb%u", t->target);
                break;
            case NC_TERM_ASSERT:
                fputs("assert ", out);
                if (!t->expected) fputc('!', out);
                operand(s, out, &t->cond);
                fprintf(out, ", \"%s\" -> bb%u", t->message, t->target);
                break;
            case NC_TERM_SWITCH:
                fputs("switch ", out);
                operand(s, out, &t->cond);
                fputs(" [", out);
                for (uint32_t c = 0; c < t->ncases; c++) {
                    if (c) fputs(", ", out);
                    cval(out, t->case_values[c], t->cond.type);
                    fprintf(out, " -> bb%u", t->case_targets[c]);
                }
                fprintf(out, "] otherwise bb%u", t->other);
                break;
            case NC_TERM_UNREACHABLE: fputs("unreachable", out); break;
            }
            fputc('\n', out);
        }
        fputs("}\n", out);
    }
}
