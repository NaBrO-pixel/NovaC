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

/* ---- verification ---- */

static bool check_operand(const nc_nir_fn *f, const nc_operand *o, char *why, size_t n)
{
    if (!o->type) {
        snprintf(why, n, "operand without a type");
        return false;
    }
    if (o->kind == NC_OP_COPY) {
        if (o->local >= f->nlocals) {
            snprintf(why, n, "operand refers to local _%u of %u", o->local, f->nlocals);
            return false;
        }
        if (f->locals[o->local].type != o->type) {
            snprintf(why, n, "operand type %s differs from local _%u type %s", nc_type_name(o->type), o->local,
                     nc_type_name(f->locals[o->local].type));
            return false;
        }
    } else if (o->cval.kind == NC_CV_NONE) {
        snprintf(why, n, "constant operand without a value");
        return false;
    }
    if (nc_ty_is_untyped(o->type) || nc_ty_is_error(o->type)) {
        snprintf(why, n, "operand of type %s reached NIR", nc_type_name(o->type));
        return false;
    }
    return true;
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
                if (st->dest >= f->nlocals) {
                    snprintf(why, n, "bb%u: assignment to nonexistent local", b);
                    return false;
                }
                if (!check_operand(f, &st->rv.a, why, n)) return false;
                bool binary = st->kind == NC_ST_CHECKED || st->rv.kind == NC_RV_BINARY;
                if (binary && !check_operand(f, &st->rv.b, why, n)) return false;
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
                if (!check_operand(f, &t->cond, why, n)) return false;
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
                if (t->has_value && !check_operand(f, &t->value, why, n)) return false;
                break;
            case NC_TERM_CALL:
                if (!check_block_id(f, t->target, why, n)) return false;
                if (!t->callee || t->nargs != t->callee->nparams) {
                    snprintf(why, n, "bb%u: call with a wrong argument count", b);
                    return false;
                }
                for (uint32_t a = 0; a < t->nargs; a++) {
                    if (!check_operand(f, &t->args[a], why, n)) return false;
                    if (t->args[a].type != t->callee->locals[a].type) {
                        snprintf(why, n, "bb%u: argument %u has the wrong type", b, a);
                        return false;
                    }
                }
                break;
            case NC_TERM_UNREACHABLE:
                break;
            }
        }
    }
    return true;
}

/* ---- dump ---- */

static void operand(nc_session *s, FILE *out, const nc_operand *o)
{
    (void)s;
    if (o->kind == NC_OP_COPY) {
        fprintf(out, "_%u", o->local);
        return;
    }
    switch (o->cval.kind) {
    case NC_CV_INT: fprintf(out, "const %s%" PRIu64 "%s", o->cval.neg ? "-" : "", o->cval.mag, nc_type_name(o->type)); break;
    case NC_CV_FLOAT: fprintf(out, "const %.17g%s", o->cval.f, nc_type_name(o->type)); break;
    case NC_CV_BOOL: fprintf(out, "const %s", o->cval.b ? "true" : "false"); break;
    default: fputs("const ?", out); break;
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
    }
}

void nc_nir_dump(nc_session *s, const nc_nir_module *m, FILE *out)
{
    fputs("module ", out);
    for (uint32_t i = 0; i < m->name.count; i++)
        fprintf(out, "%s%s", i ? "." : "", nc_sym_str(&s->strtab, m->name.parts[i]));
    fputc('\n', out);
    for (uint32_t i = 0; i < m->nfns; i++) {
        const nc_nir_fn *f = m->fns[i];
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
                    fprintf(out, "    (_%u, _%u) = checked_%s(", st->dest, st->flag, nc_nir_binop_name(st->op));
                    operand(s, out, &st->rv.a);
                    fputs(", ", out);
                    operand(s, out, &st->rv.b);
                    fputs(")\n", out);
                } else {
                    fprintf(out, "    _%u = ", st->dest);
                    rvalue(s, out, &st->rv, f->locals[st->dest].type);
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
            case NC_TERM_UNREACHABLE: fputs("unreachable", out); break;
            }
            fputc('\n', out);
        }
        fputs("}\n", out);
    }
}
