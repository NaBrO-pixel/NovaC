/* Expression type checking and constant folding (NOVAC_TYPE_SYSTEM.md §2,
 * §5, §9).
 *
 * Untyped constants: integer and float literals without a suffix have an
 * "untyped" type and an exact value. Arithmetic between untyped constants
 * is folded exactly; when an untyped constant meets a typed operand or a
 * typed context it takes that type, and it is an error (E4041) if the
 * value does not fit. Operators that need a width (wrapping arithmetic,
 * bitwise operators, shifts, `~`) give untyped operands the expected type
 * if it is an integer type, otherwise `i32`.
 *
 * Implicit conversions between typed operands happen only at coercion
 * sites (initialisation, assignment, arguments, return), never between the
 * operands of a binary operator (D-037). */
#include "sema/sema_internal.h"

#include <math.h>
#include <string.h>

static const nc_type *check_expr(nc_sema *sm, nc_expr **slot, const nc_type *expected);

static const nc_type *set_type(nc_expr *e, const nc_type *t)
{
    e->type = t;
    return t;
}

static const nc_type *fail(nc_expr *e)
{
    e->cval.kind = NC_CV_NONE;
    return set_type(e, nc_ty_error());
}

static bool is_const(const nc_expr *e)
{
    return e->cval.kind != NC_CV_NONE;
}

/* `what` includes its verb: "string literals are", "indexing is". */
static const nc_type *unsupported(nc_sema *sm, nc_expr *e, const char *what, int phase)
{
    nc_diag_error(NC_DIAG(sm), e->span, "E9001", "%s not implemented in this version (Phase %d)", what, phase);
    return fail(e);
}

static const char *fmt_cval(nc_cval v, char *buf, size_t n)
{
    nc_cv_format(v, buf, n);
    return buf;
}

/* ---- coercion ------------------------------------------------------------------------ */

static void round_to(nc_expr *e, const nc_type *ft)
{
    double d = nc_cv_to_double(e->cval);
    if (ft->prim == NC_PRIM_F32) d = (double)(float)d;
    e->cval = nc_cv_float(d);
}

bool nc_coerce(nc_sema *sm, nc_expr **slot, const nc_type *to, const char *what)
{
    nc_expr *e = *slot;
    const nc_type *from = e->type;
    char buf[64];
    if (nc_ty_is_error(from) || nc_ty_is_error(to) || from == to) return true;

    if (from->kind == NC_TY_UNTYPED_INT) {
        if (nc_ty_is_integer(to)) {
            if (nc_cv_fits(e->cval, to)) {
                set_type(e, to);
                return true;
            }
            nc_diag_error(NC_DIAG(sm), e->span, "E4041", "integer constant %s does not fit in '%s' (%s)",
                          fmt_cval(e->cval, buf, sizeof buf), nc_type_name(to), what);
            fail(e);
            return false;
        }
        if (nc_ty_is_float(to)) {
            if (nc_cv_int_exact_in_float(e->cval, to)) {
                round_to(e, to);
                set_type(e, to);
                return true;
            }
            nc_diag_error(NC_DIAG(sm), e->span, "E4041",
                          "integer constant %s cannot be represented exactly in '%s' (%s)",
                          fmt_cval(e->cval, buf, sizeof buf), nc_type_name(to), what);
            fail(e);
            return false;
        }
        nc_diag_error(NC_DIAG(sm), e->span, "E4001", "mismatched types in %s: expected '%s', found an integer constant",
                      what, nc_type_name(to));
        fail(e);
        return false;
    }
    if (from->kind == NC_TY_UNTYPED_FLOAT) {
        if (nc_ty_is_float(to)) {
            round_to(e, to);
            set_type(e, to);
            return true;
        }
        nc_diag_error(NC_DIAG(sm), e->span, "E4001",
                      "mismatched types in %s: expected '%s', found a floating-point constant", what,
                      nc_type_name(to));
        if (nc_ty_is_integer(to))
            nc_diag_help(NC_DIAG(sm), "convert explicitly with `%s(...)`; it truncates toward zero", nc_type_name(to));
        fail(e);
        return false;
    }
    if (nc_ty_widens(from, to)) {
        nc_expr *conv = nc_arena_alloc(&sm->s->arena, sizeof *conv);
        conv->kind = NC_E_IMPLICIT;
        conv->span = e->span;
        conv->u.convert.operand = e;
        conv->type = to;
        conv->cval = e->cval; /* widening preserves the value */
        *slot = conv;
        return true;
    }
    nc_diag_error(NC_DIAG(sm), e->span, "E4001", "mismatched types in %s: expected '%s', found '%s'", what,
                  nc_type_name(to), nc_type_name(from));
    if (nc_ty_is_numeric(from) && nc_ty_is_numeric(to))
        nc_diag_help(NC_DIAG(sm), "convert explicitly with `%s(...)` (D-010)", nc_type_name(to));
    fail(e);
    return false;
}

void nc_default_untyped(nc_sema *sm, nc_expr **slot)
{
    const nc_type *t = (*slot)->type;
    if (t && t->kind == NC_TY_UNTYPED_INT)
        nc_coerce(sm, slot, nc_ty_prim(NC_PRIM_I32), "the default type 'i32' of an integer constant");
    else if (t && t->kind == NC_TY_UNTYPED_FLOAT)
        nc_coerce(sm, slot, nc_ty_prim(NC_PRIM_F64), "the default type 'f64' of a floating-point constant");
}

/* Untyped integer operand of an operator that needs a width. */
static void concretize(nc_sema *sm, nc_expr **slot, const nc_type *expected, const char *what)
{
    if ((*slot)->type && (*slot)->type->kind == NC_TY_UNTYPED_INT) {
        const nc_type *t = nc_ty_is_integer(expected) ? expected : nc_ty_prim(NC_PRIM_I32);
        nc_coerce(sm, slot, t, what);
    }
}

/* ---- operator helpers ------------------------------------------------------------------ */

/* Bring two checked operands to one type. Untyped constants adopt the
 * other operand's type; two typed operands must already agree (D-037). */
static const nc_type *unify(nc_sema *sm, const char *op, nc_span op_span, nc_expr **l, nc_expr **r)
{
    const nc_type *lt = (*l)->type, *rt = (*r)->type;
    char what[64];
    snprintf(what, sizeof what, "an operand of '%s'", op);
    if (nc_ty_is_error(lt) || nc_ty_is_error(rt)) return nc_ty_error();
    if (nc_ty_is_untyped(lt) && nc_ty_is_untyped(rt)) {
        if (lt->kind == NC_TY_UNTYPED_FLOAT || rt->kind == NC_TY_UNTYPED_FLOAT) {
            if (lt->kind == NC_TY_UNTYPED_INT) {
                (*l)->cval = nc_cv_float(nc_cv_to_double((*l)->cval));
                set_type(*l, nc_ty_untyped_float());
            }
            if (rt->kind == NC_TY_UNTYPED_INT) {
                (*r)->cval = nc_cv_float(nc_cv_to_double((*r)->cval));
                set_type(*r, nc_ty_untyped_float());
            }
            return nc_ty_untyped_float();
        }
        return nc_ty_untyped_int();
    }
    if (nc_ty_is_untyped(lt)) return nc_coerce(sm, l, rt, what) ? rt : nc_ty_error();
    if (nc_ty_is_untyped(rt)) return nc_coerce(sm, r, lt, what) ? lt : nc_ty_error();
    if (lt == rt) return lt;
    nc_diag_error(NC_DIAG(sm), op_span, "E4010", "operands of '%s' have different types '%s' and '%s'", op,
                  nc_type_name(lt), nc_type_name(rt));
    if (nc_ty_is_numeric(lt) && nc_ty_is_numeric(rt)) {
        const nc_type *wide = nc_ty_widens(lt, rt) ? rt : nc_ty_widens(rt, lt) ? lt : NULL;
        if (wide)
            nc_diag_help(NC_DIAG(sm), "convert the other operand explicitly with `%s(...)`; operands are not "
                                      "converted implicitly (D-037)", nc_type_name(wide));
        else
            nc_diag_help(NC_DIAG(sm), "convert one operand explicitly with a conversion such as `%s(...)` (D-010)",
                         nc_type_name(lt));
    }
    return nc_ty_error();
}

static nc_span op_span(nc_expr *e)
{
    /* Point at the whole expression; the operator token is not stored. */
    return e->span;
}

static const nc_type *const_overflow(nc_sema *sm, nc_expr *e, const nc_type *t)
{
    nc_diag_error(NC_DIAG(sm), e->span, "E4040", "constant expression overflows '%s'", nc_type_name(t));
    nc_diag_help(NC_DIAG(sm), "this operation would always panic at run time; use `+%%`, `-%%` or `*%%` for "
                              "wrapping arithmetic (D-009)");
    return fail(e);
}

/* Fold `a op b` for integer constants of type t (typed or untyped). */
static const nc_type *fold_int_arith(nc_sema *sm, nc_expr *e, nc_binop op, nc_cval a, nc_cval b, const nc_type *t)
{
    nc_cval r = {0}, q, rem;
    bool ok = true;
    bool untyped = t->kind == NC_TY_UNTYPED_INT;
    switch (op) {
    case NC_BIN_ADD: ok = nc_cv_add(a, b, &r); break;
    case NC_BIN_SUB: ok = nc_cv_sub(a, b, &r); break;
    case NC_BIN_MUL: ok = nc_cv_mul(a, b, &r); break;
    case NC_BIN_DIV:
    case NC_BIN_MOD:
        if (b.mag == 0) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4040", "division by zero in a constant expression");
            return fail(e);
        }
        nc_cv_divmod(a, b, &q, &rem);
        r = op == NC_BIN_DIV ? q : rem;
        /* MIN % -1 panics like MIN / -1 (NOVAC_TYPE_SYSTEM.md §2.2). */
        if (op == NC_BIN_MOD && !untyped && b.neg && b.mag == 1 && !nc_cv_fits(nc_cv_neg(a), t)) ok = false;
        break;
    default: break;
    }
    if (!ok) {
        if (untyped) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4040", "constant expression is too large (beyond 64 bits)");
            return fail(e);
        }
        return const_overflow(sm, e, t);
    }
    if (!untyped && !nc_cv_fits(r, t)) return const_overflow(sm, e, t);
    e->cval = r;
    return set_type(e, t);
}

static const nc_type *fold_float_arith(nc_expr *e, nc_binop op, double a, double b, const nc_type *t)
{
    double r = 0;
    switch (op) {
    case NC_BIN_ADD: r = a + b; break;
    case NC_BIN_SUB: r = a - b; break;
    case NC_BIN_MUL: r = a * b; break;
    case NC_BIN_DIV: r = a / b; break;
    default: break;
    }
    if (t->kind == NC_TY_PRIM && t->prim == NC_PRIM_F32) r = (double)(float)r;
    e->cval = nc_cv_float(r);
    return set_type(e, t);
}

static const nc_type *check_arith(nc_sema *sm, nc_expr *e, const nc_type *expected)
{
    nc_binop op = e->u.binary.op;
    const char *sp = nc_binop_spelling(op);
    nc_check_value(sm, &e->u.binary.lhs, expected);
    nc_check_value(sm, &e->u.binary.rhs, expected);
    const nc_type *t = unify(sm, sp, op_span(e), &e->u.binary.lhs, &e->u.binary.rhs);
    if (nc_ty_is_error(t)) return fail(e);
    nc_expr *l = e->u.binary.lhs, *r = e->u.binary.rhs;
    bool is_float = t->kind == NC_TY_UNTYPED_FLOAT || nc_ty_is_float(t);
    bool is_int = t->kind == NC_TY_UNTYPED_INT || nc_ty_is_integer(t);
    if (!is_float && !is_int) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '%s' cannot be applied to type '%s'", sp, nc_type_name(t));
        return fail(e);
    }
    if (is_float && op == NC_BIN_MOD) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '%%' cannot be applied to floating-point type '%s'",
                      nc_type_name(t));
        return fail(e);
    }
    if (!(is_const(l) && is_const(r))) {
        if (is_int && (op == NC_BIN_DIV || op == NC_BIN_MOD) && is_const(r) && r->cval.mag == 0) {
            nc_diag_warning(NC_DIAG(sm), r->span, "W4002", "division by zero always panics at run time");
        }
        return set_type(e, t);
    }
    if (is_float) return fold_float_arith(e, op, nc_cv_to_double(l->cval), nc_cv_to_double(r->cval), t);
    return fold_int_arith(sm, e, op, l->cval, r->cval, t);
}

/* Wrapping arithmetic and bitwise operators: need a concrete width. */
static const nc_type *check_bits_op(nc_sema *sm, nc_expr *e, const nc_type *expected)
{
    nc_binop op = e->u.binary.op;
    const char *sp = nc_binop_spelling(op);
    bool bitwise = op == NC_BIN_BITAND || op == NC_BIN_BITOR || op == NC_BIN_BITXOR;
    nc_check_value(sm, &e->u.binary.lhs, expected);
    nc_check_value(sm, &e->u.binary.rhs, expected);
    char what[64];
    snprintf(what, sizeof what, "an operand of '%s'", sp);
    /* Two untyped operands: pick the width now; one untyped adopts the other. */
    if (nc_ty_is_untyped(e->u.binary.lhs->type) && nc_ty_is_untyped(e->u.binary.rhs->type)) {
        concretize(sm, &e->u.binary.lhs, expected, what);
        concretize(sm, &e->u.binary.rhs, expected, what);
    }
    const nc_type *t = unify(sm, sp, op_span(e), &e->u.binary.lhs, &e->u.binary.rhs);
    if (nc_ty_is_error(t)) return fail(e);
    nc_expr *l = e->u.binary.lhs, *r = e->u.binary.rhs;
    if (bitwise && nc_ty_is_bool(t)) {
        if (is_const(l) && is_const(r)) {
            bool a = l->cval.b, b = r->cval.b;
            e->cval = nc_cv_bool(op == NC_BIN_BITAND ? (a && b) : op == NC_BIN_BITOR ? (a || b) : (a != b));
        }
        return set_type(e, t);
    }
    if (!nc_ty_is_integer(t)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '%s' cannot be applied to type '%s'", sp, nc_type_name(t));
        return fail(e);
    }
    if (is_const(l) && is_const(r)) {
        uint64_t a = nc_cv_bits(l->cval, t), b = nc_cv_bits(r->cval, t), x = 0;
        switch (op) {
        case NC_BIN_WADD: x = a + b; break;
        case NC_BIN_WSUB: x = a - b; break;
        case NC_BIN_WMUL: x = a * b; break;
        case NC_BIN_BITAND: x = a & b; break;
        case NC_BIN_BITOR: x = a | b; break;
        case NC_BIN_BITXOR: x = a ^ b; break;
        default: break;
        }
        e->cval = nc_cv_from_bits(x, t);
    }
    return set_type(e, t);
}

/* Check the right operand of a shift: an unsigned integer (E4013). */
static bool check_shift_amount(nc_sema *sm, nc_expr **slot)
{
    nc_check_value(sm, slot, NULL);
    nc_expr *r = *slot;
    if (nc_ty_is_error(r->type)) return false;
    if (r->type->kind == NC_TY_UNTYPED_INT) {
        if (r->cval.neg) {
            nc_diag_error(NC_DIAG(sm), r->span, "E4013", "shift amount must not be negative");
            fail(r);
            return false;
        }
        return nc_coerce(sm, slot, nc_ty_prim(NC_PRIM_U32), "a shift amount");
    }
    if (!nc_ty_is_integer(r->type) || nc_ty_is_signed(r->type)) {
        nc_diag_error(NC_DIAG(sm), r->span, "E4013", "shift amount must have an unsigned integer type, found '%s'",
                      nc_type_name(r->type));
        if (nc_ty_is_integer(r->type))
            nc_diag_help(NC_DIAG(sm), "convert it with `u32(...)`, which panics if it is negative");
        fail(r);
        return false;
    }
    return true;
}

static const nc_type *check_shift(nc_sema *sm, nc_expr *e, const nc_type *expected)
{
    nc_binop op = e->u.binary.op;
    nc_check_value(sm, &e->u.binary.lhs, expected);
    concretize(sm, &e->u.binary.lhs, expected, "the left operand of a shift");
    bool amount_ok = check_shift_amount(sm, &e->u.binary.rhs);
    nc_expr *l = e->u.binary.lhs, *r = e->u.binary.rhs;
    const nc_type *t = l->type;
    if (nc_ty_is_error(t) || !amount_ok) return fail(e);
    if (!nc_ty_is_integer(t)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '%s' cannot be applied to type '%s'",
                      nc_binop_spelling(op), nc_type_name(t));
        return fail(e);
    }
    if (is_const(r) && r->cval.mag >= nc_ty_bits(t)) {
        char buf[32];
        nc_diag_error(NC_DIAG(sm), r->span, "E4040", "shift amount %s is too large for '%s' (%u bits)",
                      fmt_cval(r->cval, buf, sizeof buf), nc_type_name(t), nc_ty_bits(t));
        return fail(e);
    }
    if (is_const(l) && is_const(r)) {
        unsigned n = (unsigned)r->cval.mag;
        if (op == NC_BIN_SHL) {
            e->cval = nc_cv_from_bits(nc_cv_bits(l->cval, t) << n, t);
        } else {
            uint64_t x = l->cval.neg ? (uint64_t)0 - l->cval.mag : l->cval.mag; /* 64-bit pattern */
            uint64_t y = l->cval.neg ? ~((~x) >> n) : x >> n;                     /* arithmetic for negatives */
            e->cval = nc_cv_from_bits(y, t);
        }
    }
    return set_type(e, t);
}

static const nc_type *check_compare(nc_sema *sm, nc_expr *e)
{
    nc_binop op = e->u.binary.op;
    const char *sp = nc_binop_spelling(op);
    nc_check_value(sm, &e->u.binary.lhs, NULL);
    nc_check_value(sm, &e->u.binary.rhs, NULL);
    const nc_type *t = unify(sm, sp, op_span(e), &e->u.binary.lhs, &e->u.binary.rhs);
    set_type(e, nc_ty_prim(NC_PRIM_BOOL));
    if (nc_ty_is_error(t)) return e->type;
    bool equality = op == NC_BIN_EQ || op == NC_BIN_NE;
    bool numeric = nc_ty_is_untyped(t) || nc_ty_is_numeric(t);
    if (!(numeric || (equality && nc_ty_is_bool(t)))) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '%s' cannot be applied to type '%s'", sp, nc_type_name(t));
        return e->type;
    }
    nc_expr *l = e->u.binary.lhs, *r = e->u.binary.rhs;
    if (!is_const(l) || !is_const(r)) return e->type;
    int c;
    bool unordered = false;
    if (nc_ty_is_bool(t)) {
        c = l->cval.b == r->cval.b ? 0 : 1;
    } else if (t->kind == NC_TY_UNTYPED_FLOAT || nc_ty_is_float(t)) {
        double a = nc_cv_to_double(l->cval), b = nc_cv_to_double(r->cval);
        unordered = isnan(a) || isnan(b);
        c = a < b ? -1 : a > b ? 1 : 0;
    } else {
        c = nc_cv_cmp(l->cval, r->cval);
    }
    bool v = false;
    switch (op) {
    case NC_BIN_LT: v = !unordered && c < 0; break;
    case NC_BIN_LE: v = !unordered && c <= 0; break;
    case NC_BIN_GT: v = !unordered && c > 0; break;
    case NC_BIN_GE: v = !unordered && c >= 0; break;
    case NC_BIN_EQ: v = !unordered && c == 0; break;
    case NC_BIN_NE: v = unordered || c != 0; break;
    default: break;
    }
    e->cval = nc_cv_bool(v);
    return e->type;
}

static bool require_bool(nc_sema *sm, nc_expr *operand, const char *op)
{
    if (nc_ty_is_error(operand->type) || nc_ty_is_bool(operand->type)) return true;
    nc_diag_error(NC_DIAG(sm), operand->span, "E4011", "operator '%s' requires 'bool' operands, found '%s'", op,
                  nc_type_name(operand->type));
    return false;
}

static const nc_type *check_logical(nc_sema *sm, nc_expr *e)
{
    const nc_type *b = nc_ty_prim(NC_PRIM_BOOL);
    const char *sp = nc_binop_spelling(e->u.binary.op);
    nc_check_value(sm, &e->u.binary.lhs, b);
    nc_check_value(sm, &e->u.binary.rhs, b);
    bool ok = require_bool(sm, e->u.binary.lhs, sp);
    ok &= require_bool(sm, e->u.binary.rhs, sp);
    if (!ok || nc_ty_is_error(e->u.binary.lhs->type) || nc_ty_is_error(e->u.binary.rhs->type)) {
        e->cval.kind = NC_CV_NONE;
        return set_type(e, ok ? b : nc_ty_error());
    }
    nc_expr *l = e->u.binary.lhs, *r = e->u.binary.rhs;
    if (is_const(l) && is_const(r))
        e->cval = nc_cv_bool(e->u.binary.op == NC_BIN_AND ? l->cval.b && r->cval.b : l->cval.b || r->cval.b);
    return set_type(e, b);
}

static const nc_type *check_binary(nc_sema *sm, nc_expr *e, const nc_type *expected)
{
    switch (e->u.binary.op) {
    case NC_BIN_ADD: case NC_BIN_SUB: case NC_BIN_MUL: case NC_BIN_DIV: case NC_BIN_MOD:
        return check_arith(sm, e, expected);
    case NC_BIN_WADD: case NC_BIN_WSUB: case NC_BIN_WMUL:
    case NC_BIN_BITAND: case NC_BIN_BITOR: case NC_BIN_BITXOR:
        return check_bits_op(sm, e, expected);
    case NC_BIN_SHL: case NC_BIN_SHR:
        return check_shift(sm, e, expected);
    case NC_BIN_LT: case NC_BIN_LE: case NC_BIN_GT: case NC_BIN_GE: case NC_BIN_EQ: case NC_BIN_NE:
        return check_compare(sm, e);
    case NC_BIN_AND: case NC_BIN_OR:
        return check_logical(sm, e);
    case NC_BIN_NONE: break;
    }
    return fail(e);
}

/* ---- places ------------------------------------------------------------------------------ */

/* An assignable place: in Phase 2, a mutable local variable or parameter.
 * Returns its type, or the error type (reported). */
static const nc_type *check_place(nc_sema *sm, nc_expr **slot, const char *action)
{
    nc_expr *e = *slot;
    if (e->kind == NC_E_NAME) {
        bool poisoned;
        nc_var *v = nc_scope_use(sm, e->u.name, e->span, &poisoned);
        if (poisoned) return fail(e);
        if (v) {
            e->binding_kind = NC_BIND_VAR;
            e->binding = v;
            set_type(e, v->type);
            if (v->is_const) {
                nc_diag_error(NC_DIAG(sm), e->span, "E4004", "cannot %s immutable variable '%s'", action,
                              nc_sema_sym(sm, v->name));
                nc_diag_note(NC_DIAG(sm), v->span, "'%s' is declared `const` here", nc_sema_sym(sm, v->name));
                return fail(e);
            }
            return v->type;
        }
    }
    const nc_type *t = check_expr(sm, slot, NULL);
    if (nc_ty_is_error(t)) return t;
    nc_diag_error(NC_DIAG(sm), (*slot)->span, "E4004", "cannot %s this expression; it is not a variable", action);
    return fail(*slot);
}

/* ---- unary ----------------------------------------------------------------------------- */

static const nc_type *check_unary(nc_sema *sm, nc_expr *e, const nc_type *expected)
{
    nc_unop op = e->u.unary.op;
    nc_expr *operand = e->u.unary.operand;
    char buf[64];
    switch (op) {
    case NC_UN_DEREF: return unsupported(sm, e, "pointer dereference is", 4);
    case NC_UN_ADDR:
    case NC_UN_ADDR_MUT: return unsupported(sm, e, "borrowing (`&`, `&mut`) is", 5);
    case NC_UN_TRY: return unsupported(sm, e, "`try` is", 6);
    case NC_UN_PREINC: case NC_UN_PREDEC: case NC_UN_POSTINC: case NC_UN_POSTDEC: {
        bool inc = op == NC_UN_PREINC || op == NC_UN_POSTINC;
        const nc_type *t = check_place(sm, &e->u.unary.operand, inc ? "increment" : "decrement");
        if (nc_ty_is_error(t)) return fail(e);
        if (!nc_ty_is_numeric(t)) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '%s' cannot be applied to type '%s'",
                          inc ? "++" : "--", nc_type_name(t));
            return fail(e);
        }
        return set_type(e, t);
    }
    case NC_UN_NEG:
        /* `-128i8`: negate the literal before range-checking it. */
        if (operand->kind == NC_E_INT && operand->u.int_lit.suffix != NC_SUF_NONE) {
            nc_suffix suf = operand->u.int_lit.suffix;
            const nc_type *t = nc_ty_prim((nc_prim)(NC_PRIM_I8 + (suf - NC_SUF_I8)));
            nc_cval v = nc_cv_int(true, operand->u.int_lit.value);
            set_type(operand, t);
            operand->cval = nc_cv_int(false, operand->u.int_lit.value);
            if (!nc_ty_is_signed(t)) {
                nc_diag_error(NC_DIAG(sm), e->span, "E4012", "unary '-' cannot be applied to unsigned type '%s'",
                              nc_type_name(t));
                return fail(e);
            }
            if (!nc_cv_fits(v, t)) {
                nc_diag_error(NC_DIAG(sm), e->span, "E4041", "integer literal %s does not fit in '%s'",
                              fmt_cval(v, buf, sizeof buf), nc_type_name(t));
                return fail(e);
            }
            e->cval = v;
            return set_type(e, t);
        }
        {
            const nc_type *t = nc_check_value(sm, &e->u.unary.operand, expected);
            operand = e->u.unary.operand;
            if (nc_ty_is_error(t)) return fail(e);
            if (nc_ty_is_integer(t) && !nc_ty_is_signed(t)) {
                nc_diag_error(NC_DIAG(sm), e->span, "E4012", "unary '-' cannot be applied to unsigned type '%s'",
                              nc_type_name(t));
                nc_diag_help(NC_DIAG(sm), "for wrapping negation write `0 -%% x` (D-009)");
                return fail(e);
            }
            if (!nc_ty_is_untyped(t) && !nc_ty_is_numeric(t)) {
                nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '-' cannot be applied to type '%s'",
                              nc_type_name(t));
                return fail(e);
            }
            if (is_const(operand)) {
                if (operand->cval.kind == NC_CV_FLOAT) {
                    e->cval = nc_cv_float(-operand->cval.f);
                } else {
                    nc_cval v = nc_cv_neg(operand->cval);
                    if (!nc_ty_is_untyped(t) && !nc_cv_fits(v, t)) return const_overflow(sm, e, t);
                    e->cval = v;
                }
            }
            return set_type(e, t);
        }
    case NC_UN_PLUS: {
        const nc_type *t = nc_check_value(sm, &e->u.unary.operand, expected);
        if (nc_ty_is_error(t)) return fail(e);
        if (!nc_ty_is_untyped(t) && !nc_ty_is_numeric(t)) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '+' cannot be applied to type '%s'", nc_type_name(t));
            return fail(e);
        }
        e->cval = e->u.unary.operand->cval;
        return set_type(e, t);
    }
    case NC_UN_NOT: {
        const nc_type *t = nc_check_value(sm, &e->u.unary.operand, nc_ty_prim(NC_PRIM_BOOL));
        if (nc_ty_is_error(t)) return fail(e);
        if (!nc_ty_is_bool(t)) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '!' requires a 'bool' operand, found '%s'",
                          nc_type_name(t));
            if (nc_ty_is_untyped(t) || nc_ty_is_numeric(t))
                nc_diag_help(NC_DIAG(sm), "compare explicitly, e.g. `x == 0`; for bitwise complement use `~`");
            return fail(e);
        }
        if (is_const(e->u.unary.operand)) e->cval = nc_cv_bool(!e->u.unary.operand->cval.b);
        return set_type(e, t);
    }
    case NC_UN_BITNOT: {
        nc_check_value(sm, &e->u.unary.operand, expected);
        concretize(sm, &e->u.unary.operand, expected, "the operand of '~'");
        const nc_type *t = e->u.unary.operand->type;
        if (nc_ty_is_error(t)) return fail(e);
        if (!nc_ty_is_integer(t)) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '~' cannot be applied to type '%s'", nc_type_name(t));
            if (nc_ty_is_bool(t)) nc_diag_help(NC_DIAG(sm), "for logical negation use `!`");
            return fail(e);
        }
        if (is_const(e->u.unary.operand))
            e->cval = nc_cv_from_bits(~nc_cv_bits(e->u.unary.operand->cval, t), t);
        return set_type(e, t);
    }
    }
    return fail(e);
}

/* ---- assignment ---------------------------------------------------------------------------- */

static const nc_type *check_assign(nc_sema *sm, nc_expr *e)
{
    nc_binop op = e->u.binary.op;
    const char *sp = nc_assignop_spelling(op);
    set_type(e, nc_ty_prim(NC_PRIM_VOID));
    const nc_type *lt = check_place(sm, &e->u.binary.lhs, "assign to");
    char what[96];
    if (e->u.binary.lhs->kind == NC_E_NAME)
        snprintf(what, sizeof what, "the assignment to '%s'", nc_sema_sym(sm, e->u.binary.lhs->u.name));
    else
        snprintf(what, sizeof what, "an assignment");

    if (op == NC_BIN_SHL || op == NC_BIN_SHR) {
        bool ok = check_shift_amount(sm, &e->u.binary.rhs);
        if (ok && !nc_ty_is_error(lt) && !nc_ty_is_integer(lt))
            nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '%s' cannot be applied to type '%s'", sp, nc_type_name(lt));
        else if (ok && is_const(e->u.binary.rhs) && !nc_ty_is_error(lt) && e->u.binary.rhs->cval.mag >= nc_ty_bits(lt))
            nc_diag_error(NC_DIAG(sm), e->u.binary.rhs->span, "E4040", "shift amount is too large for '%s' (%u bits)",
                          nc_type_name(lt), nc_ty_bits(lt));
        return e->type;
    }
    nc_check_value(sm, &e->u.binary.rhs, nc_ty_is_error(lt) ? NULL : lt);
    if (nc_ty_is_error(lt)) return e->type;
    if (!nc_coerce(sm, &e->u.binary.rhs, lt, what)) return e->type;
    if (op == NC_BIN_NONE) return e->type;

    bool ok;
    switch (op) {
    case NC_BIN_ADD: case NC_BIN_SUB: case NC_BIN_MUL: case NC_BIN_DIV: ok = nc_ty_is_numeric(lt); break;
    case NC_BIN_MOD: case NC_BIN_WADD: case NC_BIN_WSUB: case NC_BIN_WMUL: ok = nc_ty_is_integer(lt); break;
    case NC_BIN_BITAND: case NC_BIN_BITOR: case NC_BIN_BITXOR: ok = nc_ty_is_integer(lt) || nc_ty_is_bool(lt); break;
    default: ok = false; break;
    }
    if (!ok) nc_diag_error(NC_DIAG(sm), e->span, "E4011", "operator '%s' cannot be applied to type '%s'", sp, nc_type_name(lt));
    return e->type;
}

/* ---- calls and conversions ----------------------------------------------------------------- */

static const nc_type *check_call(nc_sema *sm, nc_expr *e)
{
    nc_expr *callee = e->u.call.callee;
    nc_fn_info *fn = NULL;
    if (callee->kind == NC_E_NAME) {
        bool poisoned;
        nc_var *v = nc_scope_use(sm, callee->u.name, callee->span, &poisoned);
        if (poisoned) {
            fail(callee);
        } else if (v) {
            callee->binding_kind = NC_BIND_VAR;
            callee->binding = v;
            set_type(callee, v->type);
            nc_diag_error(NC_DIAG(sm), callee->span, "E4003", "'%s' is a variable of type '%s', not a function",
                          nc_sema_sym(sm, v->name), nc_type_name(v->type));
        } else {
            fn = nc_sema_find_fn(sm, callee->u.name);
            if (!fn) {
                nc_diag_error(NC_DIAG(sm), callee->span, "E3001", "use of undeclared function '%s'",
                              nc_sema_sym(sm, callee->u.name));
                fail(callee);
            } else {
                callee->binding_kind = NC_BIND_FN;
                callee->binding = fn;
                set_type(callee, nc_ty_error()); /* function values have no type until Phase 4 */
            }
        }
    } else {
        const nc_type *t = check_expr(sm, &e->u.call.callee, NULL);
        if (!nc_ty_is_error(t))
            nc_diag_error(NC_DIAG(sm), e->u.call.callee->span, "E4003",
                          "called expression is not a function (it has type '%s')", nc_type_name(t));
    }

    if (fn && e->u.call.nargs != fn->nparams) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4002", "function '%s' takes %u argument%s, but %u %s given",
                      nc_sema_sym(sm, fn->decl->u.fn.name), fn->nparams, fn->nparams == 1 ? "" : "s",
                      e->u.call.nargs, e->u.call.nargs == 1 ? "was" : "were");
        nc_diag_note(NC_DIAG(sm), fn->decl->u.fn.name_span, "'%s' is declared here",
                     nc_sema_sym(sm, fn->decl->u.fn.name));
    }
    for (uint32_t i = 0; i < e->u.call.nargs; i++) {
        const nc_type *pt = fn && i < fn->nparams ? fn->params[i] : NULL;
        nc_check_value(sm, &e->u.call.args[i], pt);
        if (pt) {
            char what[96];
            snprintf(what, sizeof what, "argument %u of '%s'", i + 1, nc_sema_sym(sm, fn->decl->u.fn.name));
            nc_coerce(sm, &e->u.call.args[i], pt, what);
        } else {
            nc_default_untyped(sm, &e->u.call.args[i]);
        }
    }
    if (!fn) return fail(e);
    return set_type(e, fn->ret);
}

static const nc_type *check_convert(nc_sema *sm, nc_expr *e)
{
    bool is_const_ty;
    const nc_type *to = nc_sema_resolve_type(sm, e->u.convert.type, &is_const_ty);
    const nc_type *from = nc_check_value(sm, &e->u.convert.operand, NULL);
    nc_expr *x = e->u.convert.operand;
    if (nc_ty_is_error(to) || nc_ty_is_error(from)) return fail(e);
    char buf[64];
    if (nc_ty_is_void(to) || nc_ty_is_bool(to) || (nc_ty_is_bool(from) && !nc_ty_is_integer(to))) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4007", "cannot convert '%s' to '%s'", nc_type_name(from), nc_type_name(to));
        if (nc_ty_is_bool(to)) nc_diag_help(NC_DIAG(sm), "compare instead, e.g. `x != 0`");
        return fail(e);
    }
    set_type(e, to);
    if (!is_const(x)) return to;

    nc_cval v = x->cval;
    if (v.kind == NC_CV_BOOL) {
        e->cval = nc_cv_int(false, v.b ? 1 : 0);
        return to;
    }
    if (nc_ty_is_integer(to)) {
        if (v.kind == NC_CV_FLOAT) {
            double t = trunc(v.f);
            if (isnan(v.f) || fabs(t) >= 18446744073709551616.0) {
                nc_diag_error(NC_DIAG(sm), e->span, "E4040", "conversion of %s to '%s' always panics (value out of range)",
                              fmt_cval(v, buf, sizeof buf), nc_type_name(to));
                return fail(e);
            }
            v = nc_cv_int(t < 0, (uint64_t)fabs(t));
        }
        if (!nc_cv_fits(v, to)) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4040", "conversion of %s to '%s' always panics (value out of range)",
                          fmt_cval(v, buf, sizeof buf), nc_type_name(to));
            return fail(e);
        }
        e->cval = v;
        return to;
    }
    /* to float */
    double d = nc_cv_to_double(v);
    if (to->prim == NC_PRIM_F32) d = (double)(float)d;
    e->cval = nc_cv_float(d);
    return to;
}

static const nc_type *check_cond(nc_sema *sm, nc_expr *e, const nc_type *expected)
{
    nc_check_condition(sm, &e->u.cond.cond);
    nc_check_value(sm, &e->u.cond.then_e, expected);
    nc_check_value(sm, &e->u.cond.else_e, expected);
    const nc_type *t;
    if (expected && !nc_ty_is_error(expected) && nc_ty_is_untyped(e->u.cond.then_e->type) &&
        nc_ty_is_untyped(e->u.cond.else_e->type) && nc_ty_is_numeric(expected)) {
        bool ok = nc_coerce(sm, &e->u.cond.then_e, expected, "a branch of '?:'");
        ok &= nc_coerce(sm, &e->u.cond.else_e, expected, "a branch of '?:'");
        t = ok ? expected : nc_ty_error();
    } else {
        t = unify(sm, "?:", e->span, &e->u.cond.then_e, &e->u.cond.else_e);
    }
    if (nc_ty_is_error(t)) return fail(e);
    nc_expr *c = e->u.cond.cond, *a = e->u.cond.then_e, *b = e->u.cond.else_e;
    if (is_const(c) && c->cval.kind == NC_CV_BOOL && is_const(a) && is_const(b)) e->cval = c->cval.b ? a->cval : b->cval;
    return set_type(e, t);
}

/* ---- dispatch -------------------------------------------------------------------------------- */

static const nc_type *check_expr(nc_sema *sm, nc_expr **slot, const nc_type *expected)
{
    nc_expr *e = *slot;
    char buf[64];
    switch (e->kind) {
    case NC_E_INT:
        e->cval = nc_cv_int(false, e->u.int_lit.value);
        if (e->u.int_lit.suffix == NC_SUF_NONE) return set_type(e, nc_ty_untyped_int());
        {
            const nc_type *t = nc_ty_prim((nc_prim)(NC_PRIM_I8 + (e->u.int_lit.suffix - NC_SUF_I8)));
            if (!nc_cv_fits(e->cval, t)) {
                nc_diag_error(NC_DIAG(sm), e->span, "E4041", "integer literal %s does not fit in '%s'",
                              fmt_cval(e->cval, buf, sizeof buf), nc_type_name(t));
                return fail(e);
            }
            return set_type(e, t);
        }
    case NC_E_FLOAT:
        e->cval = nc_cv_float(e->u.float_lit.value);
        if (e->u.float_lit.suffix == NC_SUF_F32) {
            e->cval.f = (double)(float)e->cval.f;
            return set_type(e, nc_ty_prim(NC_PRIM_F32));
        }
        if (e->u.float_lit.suffix == NC_SUF_F64) return set_type(e, nc_ty_prim(NC_PRIM_F64));
        return set_type(e, nc_ty_untyped_float());
    case NC_E_BOOL:
        e->cval = nc_cv_bool(e->u.bool_lit);
        return set_type(e, nc_ty_prim(NC_PRIM_BOOL));
    case NC_E_NAME: {
        bool poisoned;
        nc_var *v = nc_scope_use(sm, e->u.name, e->span, &poisoned);
        if (poisoned) return fail(e);
        if (v) {
            e->binding_kind = NC_BIND_VAR;
            e->binding = v;
            return set_type(e, v->type);
        }
        nc_fn_info *fn = nc_sema_find_fn(sm, e->u.name);
        if (fn) {
            e->binding_kind = NC_BIND_FN;
            e->binding = fn;
            nc_diag_error(NC_DIAG(sm), e->span, "E9001",
                          "functions used as values are not implemented in this version (Phase 4)");
            nc_diag_help(NC_DIAG(sm), "to call '%s', write `%s(...)`", nc_sema_sym(sm, e->u.name), nc_sema_sym(sm, e->u.name));
            return fail(e);
        }
        nc_diag_error(NC_DIAG(sm), e->span, "E3001", "use of undeclared name '%s'", nc_sema_sym(sm, e->u.name));
        return fail(e);
    }
    case NC_E_UNARY: return check_unary(sm, e, expected);
    case NC_E_BINARY: return check_binary(sm, e, expected);
    case NC_E_ASSIGN: return check_assign(sm, e);
    case NC_E_COND: return check_cond(sm, e, expected);
    case NC_E_CALL: return check_call(sm, e);
    case NC_E_CONVERT: return check_convert(sm, e);
    case NC_E_IMPLICIT: return e->type;
    case NC_E_ERROR: return fail(e);
    case NC_E_CHAR: return unsupported(sm, e, "character literals are", 4);
    case NC_E_STRING: return unsupported(sm, e, "string literals are", 6);
    case NC_E_CSTRING: return unsupported(sm, e, "C string literals are", 4);
    case NC_E_NULL: return unsupported(sm, e, "`null` (raw pointers) is", 4);
    case NC_E_NONE: return unsupported(sm, e, "`none` (optional types) is", 6);
    case NC_E_SOME: return unsupported(sm, e, "`some` (optional types) is", 6);
    case NC_E_SELF: return unsupported(sm, e, "`self` is", 5);
    case NC_E_INDEX: return unsupported(sm, e, "indexing is", 4);
    case NC_E_MEMBER: return unsupported(sm, e, "member access is", 4);
    case NC_E_ARROW: return unsupported(sm, e, "member access through pointers is", 4);
    case NC_E_STRUCT_LIT: return unsupported(sm, e, "struct literals are", 4);
    case NC_E_SIZEOF: return unsupported(sm, e, "`sizeof` is", 4);
    case NC_E_ALIGNOF: return unsupported(sm, e, "`alignof` is", 4);
    case NC_E_OFFSETOF: return unsupported(sm, e, "`offsetof` is", 4);
    case NC_E_GENERIC: return unsupported(sm, e, "explicit generic arguments are", 6);
    case NC_E_IS: return unsupported(sm, e, "`is` patterns are", 6);
    }
    return fail(e);
}

const nc_type *nc_check_value(nc_sema *sm, nc_expr **slot, const nc_type *expected)
{
    check_expr(sm, slot, expected);
    nc_expr *e = *slot;
    if (e->kind == NC_E_ASSIGN) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4030", "an assignment cannot be used as a value");
        nc_diag_help(NC_DIAG(sm), "assignments are statements in NovaC (D-024); to compare, use `==`");
        return fail(e);
    }
    if (nc_ty_is_void(e->type)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4006", "expression of type 'void' used as a value");
        return fail(e);
    }
    return e->type;
}

void nc_check_expr_stmt(nc_sema *sm, nc_expr **slot)
{
    const nc_type *t = check_expr(sm, slot, NULL);
    nc_expr *e = *slot;
    bool effect = e->kind == NC_E_ASSIGN || e->kind == NC_E_CALL || e->kind == NC_E_ERROR ||
                  (e->kind == NC_E_UNARY && (e->u.unary.op == NC_UN_PREINC || e->u.unary.op == NC_UN_PREDEC ||
                                             e->u.unary.op == NC_UN_POSTINC || e->u.unary.op == NC_UN_POSTDEC));
    if (!effect && !nc_ty_is_error(t))
        nc_diag_warning(NC_DIAG(sm), e->span, "W4001", "expression result is unused");
    nc_default_untyped(sm, slot);
}

void nc_check_condition(nc_sema *sm, nc_expr **slot)
{
    const nc_type *t = nc_check_value(sm, slot, nc_ty_prim(NC_PRIM_BOOL));
    if (nc_ty_is_error(t) || nc_ty_is_bool(t)) return;
    nc_diag_error(NC_DIAG(sm), (*slot)->span, "E4020", "condition must have type 'bool', found '%s'", nc_type_name(t));
    if (nc_ty_is_untyped(t) || nc_ty_is_numeric(t))
        nc_diag_help(NC_DIAG(sm), "compare explicitly, e.g. `x != 0`; there is no implicit conversion to 'bool'");
    fail(*slot);
}
