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
static bool pointee_ok(nc_sema *sm, const nc_type *pt, nc_span span, const char *what);

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
    if (from->kind == NC_TY_NULL) {
        if (nc_ty_is_ptr(to)) {
            set_type(e, to);
            return true;
        }
        nc_diag_error(NC_DIAG(sm), e->span, "E4001", "mismatched types in %s: expected '%s', found `null`", what,
                      nc_type_name(to));
        fail(e);
        return false;
    }
    bool ptr_widens = nc_ty_is_ptr(from) && nc_ty_is_ptr(to) && from->elem == to->elem && to->pointee_const;
    if (ptr_widens || nc_ty_widens(from, to)) {
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
    else if (nc_ty_is_ptr(from) && nc_ty_is_ptr(to) && from->pointee_const && !to->pointee_const && from->elem == to->elem)
        nc_diag_help(NC_DIAG(sm), "a pointer to const cannot become a pointer to non-const (`ptr_cast_mut` is unsafe)");
    else if (nc_ty_is_ptr(from) && nc_ty_is_ptr(to))
        nc_diag_help(NC_DIAG(sm), "convert the pointer explicitly with `ptr_cast::<%s>(...)`", nc_type_name(to));
    fail(e);
    return false;
}

void nc_default_untyped(nc_sema *sm, nc_expr **slot)
{
    const nc_type *t = (*slot)->type;
    if (nc_ty_is_null(t)) {
        nc_diag_error(NC_DIAG(sm), (*slot)->span, "E4001", "the pointer type of `null` cannot be inferred here");
        nc_diag_help(NC_DIAG(sm), "write the type, e.g. `u8* p = null;`");
        fail(*slot);
        return;
    }
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
    if (nc_ty_is_untyped(lt) || nc_ty_is_null(lt)) return nc_coerce(sm, l, rt, what) ? rt : nc_ty_error();
    if (nc_ty_is_untyped(rt) || nc_ty_is_null(rt)) return nc_coerce(sm, r, lt, what) ? lt : nc_ty_error();
    if (lt == rt) return lt;
    if (nc_ty_is_ptr(lt) && nc_ty_is_ptr(rt) && lt->elem == rt->elem) {
        /* `T*` and `const T*` meet at `const T*`. */
        const nc_type *c = nc_ty_ptr(sm->s, lt->elem, true);
        bool ok = nc_coerce(sm, l, c, what);
        ok &= nc_coerce(sm, r, c, what);
        return ok ? c : nc_ty_error();
    }
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

/* `p + n`, `p - n` (pointer) and `p - q` (isize): unsafe (NOVAC_MEMORY_MODEL.md §6.1). */
static const nc_type *check_ptr_arith(nc_sema *sm, nc_expr *e)
{
    nc_binop op = e->u.binary.op;
    const nc_type *pt = e->u.binary.lhs->type;
    bool ok = pointee_ok(sm, pt, e->u.binary.lhs->span, "pointer arithmetic");
    nc_check_value(sm, &e->u.binary.rhs, NULL);
    nc_expr *peek = e->u.binary.rhs;
    if (op == NC_BIN_SUB && (nc_ty_is_ptr(peek->type) || nc_ty_is_null(peek->type))) {
        const nc_type *t = unify(sm, "-", e->span, &e->u.binary.lhs, &e->u.binary.rhs);
        if (nc_ty_is_error(t) || !ok) return fail(e);
        if (nc_ty_size(pt->elem) == 0) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4011", "the difference of pointers to the zero-sized type '%s' is undefined",
                          nc_type_name(pt->elem));
            return fail(e);
        }
        if (!nc_sema_require_unsafe(sm, e->span, "pointer arithmetic")) return fail(e);
        return set_type(e, nc_ty_prim(NC_PRIM_ISIZE));
    }
    /* Re-check the offset in its own context (an untyped constant becomes isize). */
    if (nc_ty_is_untyped(peek->type)) {
        ok &= nc_coerce(sm, &e->u.binary.rhs, nc_ty_prim(NC_PRIM_ISIZE), "a pointer offset");
    } else if (!nc_ty_is_error(peek->type) && peek->type != nc_ty_prim(NC_PRIM_ISIZE) &&
               peek->type != nc_ty_prim(NC_PRIM_USIZE)) {
        nc_diag_error(NC_DIAG(sm), peek->span, "E4001", "a pointer offset must have type 'isize' or 'usize', found '%s'",
                      nc_type_name(peek->type));
        ok = false;
    }
    if (!ok || nc_ty_is_error(e->u.binary.rhs->type)) return fail(e);
    if (!nc_sema_require_unsafe(sm, e->span, "pointer arithmetic")) return fail(e);
    return set_type(e, pt);
}

static const nc_type *check_arith(nc_sema *sm, nc_expr *e, const nc_type *expected)
{
    nc_binop op = e->u.binary.op;
    const char *sp = nc_binop_spelling(op);
    nc_check_value(sm, &e->u.binary.lhs, expected);
    if (nc_ty_is_ptr(e->u.binary.lhs->type) && (op == NC_BIN_ADD || op == NC_BIN_SUB)) return check_ptr_arith(sm, e);
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

static bool is_addr_of(const nc_expr *e)
{
    return e->kind == NC_E_UNARY && (e->u.unary.op == NC_UN_ADDR || e->u.unary.op == NC_UN_ADDR_MUT);
}

static const nc_type *check_compare(nc_sema *sm, nc_expr *e)
{
    nc_binop op = e->u.binary.op;
    const char *sp = nc_binop_spelling(op);
    /* `p == &x`: a pointer operand gives `&x` its expected type. */
    nc_expr **first = &e->u.binary.lhs, **second = &e->u.binary.rhs;
    if (is_addr_of(*first) && !is_addr_of(*second)) {
        first = &e->u.binary.rhs;
        second = &e->u.binary.lhs;
    }
    const nc_type *ft = nc_check_value(sm, first, NULL);
    nc_check_value(sm, second, nc_ty_is_ptr(ft) ? ft : NULL);
    const nc_type *t = unify(sm, sp, op_span(e), &e->u.binary.lhs, &e->u.binary.rhs);
    set_type(e, nc_ty_prim(NC_PRIM_BOOL));
    if (nc_ty_is_error(t)) return e->type;
    bool equality = op == NC_BIN_EQ || op == NC_BIN_NE;
    bool numeric = nc_ty_is_untyped(t) || nc_ty_is_numeric(t) || nc_ty_is_ptr(t); /* pointers: address order */
    if (!(numeric || (equality && (nc_ty_is_bool(t) || nc_ty_is_enum(t))))) {
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

/* ---- names, fields, indexing --------------------------------------------------------------- */

/* A NAME that is not a local variable: a module-level item. */
static const nc_type *check_module_name(nc_sema *sm, nc_expr *e)
{
    const char *name = nc_sema_sym(sm, e->u.name);
    nc_module_sym *ms = nc_sema_find_module_sym(sm, e->u.name);
    if (!ms) {
        nc_diag_error(NC_DIAG(sm), e->span, "E3001", "use of undeclared name '%s'", name);
        return fail(e);
    }
    switch (ms->kind) {
    case NC_MSYM_FN:
        e->binding_kind = NC_BIND_FN;
        e->binding = ms->info;
        nc_diag_error(NC_DIAG(sm), e->span, "E9001", "functions used as values are not implemented in this version (Phase 4)");
        nc_diag_help(NC_DIAG(sm), "to call '%s', write `%s(...)`", name, name);
        return fail(e);
    case NC_MSYM_CONST: {
        nc_const_info *c = nc_sema_const(sm, ms);
        if (c->state != NC_LAYOUT_DONE) return fail(e);
        e->cval = c->value;
        return set_type(e, c->type);
    }
    case NC_MSYM_STATIC: {
        nc_static_info *st = nc_sema_static(sm, ms);
        if (!st->type || nc_ty_is_error(st->type)) return fail(e);
        if (!st->is_const && !nc_sema_require_unsafe(sm, e->span, "accessing a mutable static")) return fail(e);
        e->binding_kind = NC_BIND_STATIC;
        e->binding = st;
        return set_type(e, st->type);
    }
    case NC_MSYM_AGGREGATE:
    case NC_MSYM_ENUM:
    case NC_MSYM_TYPEDEF: break;
    }
    nc_diag_error(NC_DIAG(sm), e->span, "E4083", "'%s' is a type, not a value", name);
    return fail(e);
}

static const nc_type *check_name(nc_sema *sm, nc_expr *e)
{
    bool poisoned;
    nc_var *v = nc_scope_use(sm, e->u.name, e->span, &poisoned);
    if (poisoned) return fail(e);
    if (v) {
        e->binding_kind = NC_BIND_VAR;
        e->binding = v;
        return set_type(e, v->type);
    }
    return check_module_name(sm, e);
}

/* The type a NAME denotes if it names a type rather than a value
 * (`Color.Red`, `sizeof(Point)`), else NULL. */
static const nc_type *name_as_type(nc_sema *sm, const nc_expr *e)
{
    if (e->kind != NC_E_NAME || nc_scope_lookup(sm, e->u.name)) return NULL;
    nc_module_sym *ms = nc_sema_find_module_sym(sm, e->u.name);
    if (ms) return nc_sema_msym_type(sm, ms);
    return nc_sema_ffi_type(sm, nc_sema_sym(sm, e->u.name)); /* c_int(x), sizeof(c_long) */
}

static const char *agg_kind(const nc_type *t)
{
    return nc_ty_is_union(t) ? "union" : "struct";
}

/* `base.name` where base has type bt. `reading`: the field's value is read
 * (union fields can only be written in safe code). */
static const nc_type *field_access(nc_sema *sm, nc_expr *e, const nc_type *bt, bool reading)
{
    const char *fname = nc_sema_sym(sm, e->u.member.name);
    if (nc_ty_is_error(bt)) return fail(e);
    if (!nc_ty_is_struct(bt) && !nc_ty_is_union(bt)) {
        nc_diag_error(NC_DIAG(sm), e->u.member.name_span, "E4075", "type '%s' has no fields, so it has no field '%s'",
                      nc_type_name(bt), fname);
        return fail(e);
    }
    nc_aggregate *agg = bt->agg;
    for (uint32_t i = 0; i < agg->nfields; i++) {
        if (agg->fields[i].name != e->u.member.name) continue;
        if (agg->is_union && reading && !nc_sema_require_unsafe(sm, e->span, "reading a union field")) return fail(e);
        e->binding_kind = NC_BIND_FIELD;
        e->binding = (void *)(uintptr_t)i;
        return set_type(e, agg->fields[i].type);
    }
    nc_diag_error(NC_DIAG(sm), e->u.member.name_span, "E4084", "%s '%s' has no field named '%s'", agg_kind(bt),
                  nc_type_name(bt), fname);
    nc_diag_note(NC_DIAG(sm), agg->span, "'%s' is declared here", nc_type_name(bt));
    return fail(e);
}

/* `E.name` for an enum type E. */
static const nc_type *enumerator(nc_sema *sm, nc_expr *e, const nc_type *et)
{
    nc_expr *base = e->u.member.base;
    if (!nc_ty_is_enum(et)) {
        nc_diag_error(NC_DIAG(sm), base->span, "E4083", "'%s' is a type, not a value", nc_sema_sym(sm, base->u.name));
        return fail(e);
    }
    base->type = et;
    if (!nc_sema_complete(sm, et, base->span)) return fail(e);
    nc_enum_type *en = et->en;
    for (uint32_t i = 0; i < en->nitems; i++)
        if (en->items[i].name == e->u.member.name) {
            e->cval = en->items[i].value;
            return set_type(e, et);
        }
    nc_diag_error(NC_DIAG(sm), e->u.member.name_span, "E4084", "enum '%s' has no enumerator named '%s'", et->name,
                  nc_sema_sym(sm, e->u.member.name));
    nc_diag_note(NC_DIAG(sm), en->span, "'%s' is declared here", et->name);
    return fail(e);
}

/* The pointee of a pointer about to be accessed through: not void, and
 * complete. Reports at `span`. */
static bool pointee_ok(nc_sema *sm, const nc_type *pt, nc_span span, const char *what)
{
    if (nc_ty_is_void(pt->elem)) {
        nc_diag_error(NC_DIAG(sm), span, "E4011", "%s is not possible through '%s': the pointee has no type", what,
                      nc_type_name(pt));
        nc_diag_help(NC_DIAG(sm), "convert it first, e.g. `ptr_cast::<u8*>(p)`");
        return false;
    }
    return nc_sema_complete(sm, pt->elem, span);
}

/* A pointer offset (`p + n`, `p[n]`): `isize` or `usize`; an untyped
 * constant becomes `isize`. */
static bool check_offset(nc_sema *sm, nc_expr **slot, const char *what)
{
    const nc_type *isize = nc_ty_prim(NC_PRIM_ISIZE), *usize = nc_ty_prim(NC_PRIM_USIZE);
    nc_check_value(sm, slot, isize);
    const nc_type *t = (*slot)->type;
    if (nc_ty_is_error(t)) return false;
    if (t->kind == NC_TY_UNTYPED_INT) return nc_coerce(sm, slot, isize, what) && !nc_ty_is_error((*slot)->type);
    if (t == isize || t == usize) return true;
    nc_diag_error(NC_DIAG(sm), (*slot)->span, "E4001", "%s must have type 'isize' or 'usize', found '%s'", what,
                  nc_type_name(t));
    if (nc_ty_is_integer(t)) nc_diag_help(NC_DIAG(sm), "convert it with `isize(...)` or `usize(...)`");
    fail(*slot);
    return false;
}

/* `p->f` where p has type pt. */
static const nc_type *arrow_access(nc_sema *sm, nc_expr *e, const nc_type *pt, bool reading)
{
    if (nc_ty_is_error(pt)) return fail(e);
    if (!nc_ty_is_ptr(pt)) {
        nc_diag_error(NC_DIAG(sm), e->u.member.base->span, "E4011", "'->' needs a pointer, found '%s'", nc_type_name(pt));
        if (nc_ty_is_struct(pt) || nc_ty_is_union(pt)) nc_diag_help(NC_DIAG(sm), "use '.' for a value (D-006)");
        return fail(e);
    }
    if (!pointee_ok(sm, pt, e->u.member.base->span, "member access")) return fail(e);
    if (!nc_sema_require_unsafe(sm, e->span, "member access through a raw pointer")) return fail(e);
    return field_access(sm, e, pt->elem, reading);
}

static const nc_type *check_member(nc_sema *sm, nc_expr *e)
{
    if (e->kind == NC_E_ARROW) return arrow_access(sm, e, nc_check_value(sm, &e->u.member.base, NULL), true);
    const nc_type *et = name_as_type(sm, e->u.member.base);
    if (et) return enumerator(sm, e, et);
    const nc_type *bt = nc_check_value(sm, &e->u.member.base, NULL);
    if (nc_ty_is_ptr(bt)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4011", "'.' cannot access a field through the pointer type '%s'",
                      nc_type_name(bt));
        nc_diag_help(NC_DIAG(sm), "use '->' (D-006)");
        return fail(e);
    }
    return field_access(sm, e, bt, true);
}

/* `base[index]` where base has type bt: a bounds-checked array element, or
 * (unsafe) an element through a pointer. */
static const nc_type *index_access(nc_sema *sm, nc_expr *e, const nc_type *bt)
{
    if (nc_ty_is_ptr(bt)) {
        bool ok = check_offset(sm, &e->u.index.index, "a pointer index");
        if (!ok || !pointee_ok(sm, bt, e->u.index.base->span, "indexing")) return fail(e);
        if (!nc_sema_require_unsafe(sm, e->span, "indexing a raw pointer")) return fail(e);
        return set_type(e, bt->elem);
    }
    const nc_type *usize = nc_ty_prim(NC_PRIM_USIZE);
    nc_check_value(sm, &e->u.index.index, usize);
    bool ok = nc_coerce(sm, &e->u.index.index, usize, "an array index") && !nc_ty_is_error(e->u.index.index->type);
    if (nc_ty_is_error(bt)) return fail(e);
    if (!nc_ty_is_array(bt)) {
        nc_diag_error(NC_DIAG(sm), e->u.index.base->span, "E4074", "cannot index a value of type '%s'", nc_type_name(bt));
        return fail(e);
    }
    nc_expr *ix = e->u.index.index;
    if (ok && ix->cval.kind == NC_CV_INT && ix->cval.mag >= bt->len) {
        nc_diag_error(NC_DIAG(sm), ix->span, "E5040", "index %llu is out of bounds for an array of length %llu",
                      (unsigned long long)ix->cval.mag, (unsigned long long)bt->len);
        return fail(e);
    }
    if (!ok) return fail(e);
    return set_type(e, bt->elem);
}

/* `*p` where p has type pt. */
static const nc_type *deref(nc_sema *sm, nc_expr *e, const nc_type *pt)
{
    if (nc_ty_is_error(pt)) return fail(e);
    if (!nc_ty_is_ptr(pt)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4011", "cannot dereference a value of type '%s'", nc_type_name(pt));
        return fail(e);
    }
    if (!pointee_ok(sm, pt, e->span, "dereferencing")) return fail(e);
    if (!nc_sema_require_unsafe(sm, e->span, "dereferencing a raw pointer")) return fail(e);
    return set_type(e, pt->elem);
}

/* How a place is used. Writes and read-writes need a mutable place; taking
 * an address (`&x`) does not; `&mut x` does. Only reads and writes of a
 * mutable static are unsafe, not taking its address. */
typedef enum place_use { USE_WRITE, USE_READ_WRITE, USE_ADDR, USE_ADDR_MUT } place_use;

typedef struct lvalue {
    bool is_place;
    bool immutable;
    int why;           /* immutable because of: 0 a const variable, 1 a const static, 2 a pointer to const */
    nc_sym name;
    nc_span decl_span;
} lvalue;

static const nc_type *check_lvalue(nc_sema *sm, nc_expr **slot, place_use use, lvalue *lv)
{
    nc_expr *e = *slot;
    bool reading = use == USE_READ_WRITE;
    switch (e->kind) {
    case NC_E_NAME: {
        bool poisoned;
        nc_var *v = nc_scope_use(sm, e->u.name, e->span, &poisoned);
        if (poisoned) return fail(e);
        if (v) {
            e->binding_kind = NC_BIND_VAR;
            e->binding = v;
            *lv = (lvalue){true, v->is_const, 0, v->name, v->span};
            return set_type(e, v->type);
        }
        nc_module_sym *ms = nc_sema_find_module_sym(sm, e->u.name);
        if (!ms || ms->kind != NC_MSYM_STATIC) break;
        nc_static_info *st = nc_sema_static(sm, ms);
        if (!st->type || nc_ty_is_error(st->type)) return fail(e);
        if (!st->is_const && use != USE_ADDR && use != USE_ADDR_MUT &&
            !nc_sema_require_unsafe(sm, e->span, "accessing a mutable static"))
            return fail(e);
        e->binding_kind = NC_BIND_STATIC;
        e->binding = st;
        *lv = (lvalue){true, st->is_const, 1, st->name, st->span};
        return set_type(e, st->type);
    }
    case NC_E_MEMBER: {
        if (name_as_type(sm, e->u.member.base)) break;
        const nc_type *bt = check_lvalue(sm, &e->u.member.base, use, lv);
        if (nc_ty_is_ptr(bt)) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4011", "'.' cannot access a field through the pointer type '%s'",
                          nc_type_name(bt));
            nc_diag_help(NC_DIAG(sm), "use '->' (D-006)");
            return fail(e);
        }
        return field_access(sm, e, bt, reading);
    }
    case NC_E_ARROW: {
        const nc_type *pt = nc_check_value(sm, &e->u.member.base, NULL);
        const nc_type *t = arrow_access(sm, e, pt, reading);
        if (nc_ty_is_ptr(pt)) *lv = (lvalue){true, pt->pointee_const, 2, 0, NC_NO_SPAN};
        return t;
    }
    case NC_E_INDEX: {
        /* The base is a place for an array, a value for a pointer. */
        lvalue base_lv = {0};
        const nc_type *bt = check_lvalue(sm, &e->u.index.base, use, &base_lv);
        if (nc_ty_is_ptr(bt)) *lv = (lvalue){true, bt->pointee_const, 2, 0, NC_NO_SPAN};
        else *lv = base_lv;
        return index_access(sm, e, bt);
    }
    case NC_E_UNARY:
        if (e->u.unary.op != NC_UN_DEREF) break;
        {
            const nc_type *pt = nc_check_value(sm, &e->u.unary.operand, NULL);
            const nc_type *t = deref(sm, e, pt);
            if (nc_ty_is_ptr(pt)) *lv = (lvalue){true, pt->pointee_const, 2, 0, NC_NO_SPAN};
            return t;
        }
    default: break;
    }
    lv->is_place = false;
    return check_expr(sm, slot, NULL);
}

/* An assignable place: a mutable variable or static, a field or element of
 * one, or a place reached through a pointer to non-const. Returns its
 * type, or the error type (reported). */
static const nc_type *check_place(nc_sema *sm, nc_expr **slot, const char *action, place_use use)
{
    lvalue lv = {0};
    const nc_type *t = check_lvalue(sm, slot, use, &lv);
    if (nc_ty_is_error(t)) return t;
    nc_expr *e = *slot;
    if (!lv.is_place) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4004", "cannot %s this expression; it is not a variable", action);
        return fail(e);
    }
    if (use == USE_ADDR || !lv.immutable) return t;
    if (lv.why == 2) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4004", "cannot %s a place reached through a pointer to const", action);
        return fail(e);
    }
    const char *name = nc_sema_sym(sm, lv.name);
    nc_diag_error(NC_DIAG(sm), e->span, "E4004", "cannot %s immutable %s '%s'", action, lv.why ? "static" : "variable", name);
    nc_diag_note(NC_DIAG(sm), lv.decl_span, "'%s' is declared `const` here", name);
    return fail(e);
}

/* `&x` of a field of a @packed struct may be misaligned (E5050). */
static void check_packed_borrow(nc_sema *sm, const nc_expr *operand)
{
    uint64_t want = nc_ty_align(operand->type);
    if (want <= 1) return;
    for (const nc_expr *e = operand;;) {
        if ((e->kind == NC_E_MEMBER || e->kind == NC_E_ARROW) && e->binding_kind == NC_BIND_FIELD) {
            const nc_type *bt = e->kind == NC_E_ARROW ? e->u.member.base->type->elem : e->u.member.base->type;
            if (nc_ty_is_struct(bt) && bt->agg->packed) {
                nc_diag_error(NC_DIAG(sm), operand->span, "E5050",
                              "a pointer to this field of @packed struct '%s' may be misaligned", nc_type_name(bt));
                nc_diag_help(NC_DIAG(sm), "copy the field to a local variable and take the address of that");
                return;
            }
            if (e->kind == NC_E_ARROW) return;
            e = e->u.member.base;
        } else if (e->kind == NC_E_INDEX && nc_ty_is_array(e->u.index.base->type)) {
            e = e->u.index.base;
        } else {
            return;
        }
    }
}

/* ---- sizes ---------------------------------------------------------------------------------- */

static const nc_type *check_size(nc_sema *sm, nc_expr *e)
{
    bool is_size = e->kind == NC_E_SIZEOF;
    const char *what = is_size ? "the operand of 'sizeof'" : "the operand of 'alignof'";
    const nc_type *t;
    bool c;
    if (e->u.size.type) {
        t = nc_sema_value_type(sm, e->u.size.type, &c, false, what);
    } else if ((t = name_as_type(sm, e->u.size.expr)) != NULL) {
        e->u.size.expr->type = t;
        if (!nc_sema_complete(sm, t, e->u.size.expr->span)) t = nc_ty_error();
    } else {
        /* The operand is type-checked but never evaluated. */
        nc_check_value(sm, &e->u.size.expr, NULL);
        nc_default_untyped(sm, &e->u.size.expr);
        t = e->u.size.expr->type;
    }
    if (nc_ty_is_error(t)) return fail(e);
    e->cval = nc_cv_int(false, is_size ? nc_ty_size(t) : nc_ty_align(t));
    return set_type(e, nc_ty_prim(NC_PRIM_USIZE));
}

static const nc_type *check_offsetof(nc_sema *sm, nc_expr *e)
{
    bool c;
    const nc_type *t = nc_sema_value_type(sm, e->u.offsetof_.type, &c, false, "the operand of 'offsetof'");
    if (nc_ty_is_error(t) || !e->u.offsetof_.field) return fail(e);
    if (!nc_ty_is_struct(t) && !nc_ty_is_union(t)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4075", "'offsetof' needs a struct or union type, found '%s'", nc_type_name(t));
        return fail(e);
    }
    for (uint32_t i = 0; i < t->agg->nfields; i++)
        if (t->agg->fields[i].name == e->u.offsetof_.field) {
            e->cval = nc_cv_int(false, t->agg->fields[i].offset);
            return set_type(e, nc_ty_prim(NC_PRIM_USIZE));
        }
    nc_diag_error(NC_DIAG(sm), e->span, "E4084", "%s '%s' has no field named '%s'", agg_kind(t), nc_type_name(t),
                  nc_sema_sym(sm, e->u.offsetof_.field));
    return fail(e);
}

/* ---- initialiser lists and struct literals (D-043) ------------------------------------------- */

/* Can a member of type t be left out of an initialiser list? It is then
 * zero-initialised, so zero must be a valid value of t. */
static bool zeroable(const nc_type *t)
{
    switch (t->kind) {
    case NC_TY_PRIM:
    case NC_TY_PTR:
    case NC_TY_UNION: return true;
    case NC_TY_ARRAY: return zeroable(t->elem);
    case NC_TY_STRUCT:
        for (uint32_t i = 0; i < t->agg->nfields; i++)
            if (!zeroable(t->agg->fields[i].type)) return false;
        return true;
    case NC_TY_ENUM:
        for (uint32_t i = 0; i < t->en->nitems; i++)
            if (t->en->items[i].value.mag == 0) return true;
        return false;
    default: return false;
    }
}

static nc_expr *new_aggregate(nc_sema *sm, const nc_type *t, uint32_t n, nc_span span)
{
    nc_expr *a = nc_arena_alloc(&sm->s->arena, sizeof *a);
    a->kind = NC_E_AGGREGATE;
    a->span = span;
    a->type = t;
    a->u.agg.nelems = n;
    a->u.agg.elems = n ? nc_arena_alloc(&sm->s->arena, n * sizeof *a->u.agg.elems) : NULL;
    return a;
}

static nc_expr *check_list(nc_sema *sm, nc_init *in, const nc_type *t, const char *what);

/* One member's initialiser: an expression or a nested list. */
static nc_expr *check_member_init(nc_sema *sm, nc_init *v, const nc_type *t, const char *what)
{
    if (v->kind == NC_INIT_LIST) return check_list(sm, v, t, what);
    if (v->kind == NC_INIT_UNINIT) {
        nc_diag_error(NC_DIAG(sm), v->span, "E9001", "`uninit` is not implemented in this version (Phase 4)");
        return NULL;
    }
    nc_check_value(sm, &v->expr, t);
    if (!nc_coerce(sm, &v->expr, t, what) || nc_ty_is_error(v->expr->type)) return NULL;
    return v->expr;
}

static nc_expr *check_array_list(nc_sema *sm, nc_init *in, const nc_type *t, const char *what)
{
    if (in->nitems) {
        if (t->len > NC_MAX_LIST_ELEMS - sm->list_elems) {
            nc_diag_error(NC_DIAG(sm), in->span, "E9005",
                          "initialiser lists for arrays in this module exceed the implementation limit of %llu elements",
                          (unsigned long long)NC_MAX_LIST_ELEMS);
            nc_diag_help(NC_DIAG(sm), "initialise large arrays with `{}` and assign the elements");
            sm->list_elems = NC_MAX_LIST_ELEMS;
            return NULL;
        }
        sm->list_elems += t->len;
    }
    nc_expr *agg = new_aggregate(sm, t, in->nitems ? (uint32_t)t->len : 0, in->span);
    const nc_type *usize = nc_ty_prim(NC_PRIM_USIZE);
    uint64_t cursor = 0;
    bool ok = true;
    for (uint32_t i = 0; i < in->nitems; i++) {
        nc_init_item *it = &in->items[i];
        if (it->field) {
            nc_diag_error(NC_DIAG(sm), it->span, "E4087", "a field designator cannot initialise an element of '%s'",
                          nc_type_name(t));
            ok = false;
            continue;
        }
        if (it->index) {
            nc_check_value(sm, &it->index, usize);
            if (!nc_coerce(sm, &it->index, usize, "an element designator") || nc_ty_is_error(it->index->type)) {
                ok = false;
                continue;
            }
            if (it->index->cval.kind != NC_CV_INT) {
                nc_diag_error(NC_DIAG(sm), it->index->span, "E4082", "an element designator must be a constant expression");
                ok = false;
                continue;
            }
            if (it->index->cval.mag >= t->len) {
                nc_diag_error(NC_DIAG(sm), it->index->span, "E5040", "index %llu is out of bounds for an array of length %llu",
                              (unsigned long long)it->index->cval.mag, (unsigned long long)t->len);
                ok = false;
                continue;
            }
            cursor = it->index->cval.mag;
        }
        if (cursor >= t->len) {
            nc_diag_error(NC_DIAG(sm), it->span, "E4087", "too many elements in %s: '%s' has %llu", what, nc_type_name(t),
                          (unsigned long long)t->len);
            return NULL;
        }
        if (agg->u.agg.elems[cursor]) {
            nc_diag_error(NC_DIAG(sm), it->span, "E4085", "element %llu is initialised more than once",
                          (unsigned long long)cursor);
            ok = false;
        }
        nc_expr *v = check_member_init(sm, it->value, t->elem, what);
        if (!v) ok = false;
        agg->u.agg.elems[cursor++] = v;
    }
    if (!ok) return NULL;
    if (!zeroable(t->elem)) {
        for (uint64_t k = 0; k < t->len; k++)
            if (!in->nitems || !agg->u.agg.elems[k]) {
                nc_diag_error(NC_DIAG(sm), in->span, "E4081", "element %llu of '%s' is not initialised", (unsigned long long)k,
                              nc_type_name(t));
                nc_diag_help(NC_DIAG(sm), "elements of type '%s' cannot be zero-initialised; give every element a value",
                             nc_type_name(t->elem));
                return NULL;
            }
    }
    return agg;
}

static nc_expr *check_struct_list(nc_sema *sm, nc_init *in, const nc_type *t, const char *what)
{
    nc_aggregate *a = t->agg;
    nc_expr *agg = new_aggregate(sm, t, a->nfields, in->span);
    bool *set = nc_arena_alloc(&sm->s->arena, (a->nfields ? a->nfields : 1) * sizeof *set);
    uint32_t cursor = 0;
    bool ok = true;
    for (uint32_t i = 0; i < in->nitems; i++) {
        nc_init_item *it = &in->items[i];
        if (it->index) {
            nc_diag_error(NC_DIAG(sm), it->span, "E4087", "an element designator cannot initialise a field of '%s'",
                          nc_type_name(t));
            ok = false;
            continue;
        }
        if (it->field) {
            uint32_t f = 0;
            while (f < a->nfields && a->fields[f].name != it->field) f++;
            if (f == a->nfields) {
                nc_diag_error(NC_DIAG(sm), it->span, "E4084", "struct '%s' has no field named '%s'", nc_type_name(t),
                              nc_sema_sym(sm, it->field));
                ok = false;
                continue;
            }
            cursor = f;
        }
        if (cursor >= a->nfields) {
            nc_diag_error(NC_DIAG(sm), it->span, "E4087", "too many values in %s: '%s' has %u field%s", what,
                          nc_type_name(t), a->nfields, a->nfields == 1 ? "" : "s");
            return NULL;
        }
        if (set[cursor]) {
            nc_diag_error(NC_DIAG(sm), it->span, "E4085", "field '%s' is initialised more than once",
                          nc_sema_sym(sm, a->fields[cursor].name));
            ok = false;
        }
        set[cursor] = true;
        char fwhat[128];
        snprintf(fwhat, sizeof fwhat, "field '%s' of '%s'", nc_sema_sym(sm, a->fields[cursor].name), nc_type_name(t));
        nc_expr *v = check_member_init(sm, it->value, a->fields[cursor].type, fwhat);
        if (!v) ok = false;
        agg->u.agg.elems[cursor++] = v;
    }
    if (!ok) return NULL;
    for (uint32_t f = 0; f < a->nfields; f++) {
        if (set[f] || zeroable(a->fields[f].type)) continue;
        nc_diag_error(NC_DIAG(sm), in->span, "E4081", "field '%s' of '%s' is not initialised", nc_sema_sym(sm, a->fields[f].name),
                      nc_type_name(t));
        nc_diag_help(NC_DIAG(sm), "a field of type '%s' cannot be zero-initialised; give it a value",
                     nc_type_name(a->fields[f].type));
        return NULL;
    }
    return agg;
}

static nc_expr *check_union_list(nc_sema *sm, nc_init *in, const nc_type *t, const char *what)
{
    nc_aggregate *a = t->agg;
    if (in->nitems == 0) return new_aggregate(sm, t, 0, in->span);
    if (in->nitems > 1) {
        nc_diag_error(NC_DIAG(sm), in->items[1].span, "E4087", "an initialiser for union '%s' sets exactly one field",
                      nc_type_name(t));
        return NULL;
    }
    nc_init_item *it = &in->items[0];
    uint32_t f = 0;
    if (it->index) {
        nc_diag_error(NC_DIAG(sm), it->span, "E4087", "an element designator cannot initialise a field of '%s'", nc_type_name(t));
        return NULL;
    }
    if (it->field) {
        while (f < a->nfields && a->fields[f].name != it->field) f++;
        if (f == a->nfields) {
            nc_diag_error(NC_DIAG(sm), it->span, "E4084", "union '%s' has no field named '%s'", nc_type_name(t),
                          nc_sema_sym(sm, it->field));
            return NULL;
        }
    }
    (void)what;
    char fwhat[128];
    snprintf(fwhat, sizeof fwhat, "field '%s' of '%s'", nc_sema_sym(sm, a->fields[f].name), nc_type_name(t));
    nc_expr *v = check_member_init(sm, it->value, a->fields[f].type, fwhat);
    if (!v) return NULL;
    nc_expr *agg = new_aggregate(sm, t, 1, in->span);
    agg->u.agg.elems[0] = v;
    agg->u.agg.union_field = f;
    return agg;
}

static nc_expr *check_list(nc_sema *sm, nc_init *in, const nc_type *t, const char *what)
{
    if (nc_ty_is_error(t)) return NULL;
    if (nc_ty_is_array(t)) return check_array_list(sm, in, t, what);
    if (nc_ty_is_struct(t)) return check_struct_list(sm, in, t, what);
    if (nc_ty_is_union(t)) return check_union_list(sm, in, t, what);
    nc_diag_error(NC_DIAG(sm), in->span, "E4087", "an initialiser list cannot initialise a value of type '%s' (%s)",
                  nc_type_name(t), what);
    nc_diag_help(NC_DIAG(sm), "braces initialise arrays, structs and unions; write the value without braces");
    return NULL;
}

bool nc_sema_check_init(nc_sema *sm, nc_init *in, const nc_type *t, const char *what)
{
    if (in->kind == NC_INIT_EXPR) {
        nc_check_value(sm, &in->expr, t);
        return nc_coerce(sm, &in->expr, t, what) && !nc_ty_is_error(in->expr->type);
    }
    if (in->kind == NC_INIT_UNINIT) {
        nc_diag_error(NC_DIAG(sm), in->span, "E9001", "`uninit` is not implemented in this version (Phase 4)");
        return false;
    }
    nc_expr *agg = check_list(sm, in, t, what);
    if (!agg) return false;
    in->kind = NC_INIT_EXPR;
    in->expr = agg;
    return true;
}

static const nc_type *check_struct_lit(nc_sema *sm, nc_expr **slot)
{
    nc_expr *e = *slot;
    bool c;
    const nc_type *t = nc_sema_value_type(sm, e->u.struct_lit.type, &c, false, "a struct literal");
    if (nc_ty_is_error(t)) return fail(e);
    if (!nc_ty_is_struct(t) && !nc_ty_is_union(t) && !nc_ty_is_array(t)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4087", "'%s' cannot be written as a literal with braces", nc_type_name(t));
        return fail(e);
    }
    nc_init in = {NC_INIT_LIST, e->span, NULL, e->u.struct_lit.items, e->u.struct_lit.nitems};
    char what[96];
    snprintf(what, sizeof what, "a literal of type '%s'", nc_type_name(t));
    nc_expr *agg = check_list(sm, &in, t, what);
    if (!agg) return fail(e);
    *slot = agg;
    return agg->type;
}

bool nc_is_constant_expr(const nc_expr *e)
{
    if (!e) return true; /* a zero-initialised member */
    if (e->cval.kind != NC_CV_NONE || e->kind == NC_E_NULL || e->kind == NC_E_CSTRING) return true;
    if (e->kind == NC_E_IMPLICIT) return nc_is_constant_expr(e->u.convert.operand);
    if (e->kind != NC_E_AGGREGATE) return false;
    for (uint32_t i = 0; i < e->u.agg.nelems; i++)
        if (!nc_is_constant_expr(e->u.agg.elems[i])) return false;
    return true;
}

/* ---- unary ----------------------------------------------------------------------------- */

static const nc_type *check_unary(nc_sema *sm, nc_expr *e, const nc_type *expected)
{
    nc_unop op = e->u.unary.op;
    nc_expr *operand = e->u.unary.operand;
    char buf[64];
    switch (op) {
    case NC_UN_DEREF: return deref(sm, e, nc_check_value(sm, &e->u.unary.operand, NULL));
    case NC_UN_ADDR:
    case NC_UN_ADDR_MUT: {
        /* Until references exist (Phase 5), `&x` is only accepted where a raw
         * pointer is expected, and gives `const T*` (`&mut x`: `T*`). */
        bool mut = op == NC_UN_ADDR_MUT;
        if (!expected || !nc_ty_is_ptr(expected)) {
            unsupported(sm, e, "borrowing (`&`, `&mut`) is", 5);
            nc_diag_help(NC_DIAG(sm), "`&x` already gives a raw pointer where a pointer type is expected, e.g. `%s p = %s;`",
                         mut ? "T*" : "const T*", mut ? "&mut x" : "&x");
            return e->type;
        }
        const nc_type *t = check_place(sm, &e->u.unary.operand, mut ? "take a mutable pointer to" : "take the address of",
                                       mut ? USE_ADDR_MUT : USE_ADDR);
        if (nc_ty_is_error(t)) return fail(e);
        check_packed_borrow(sm, e->u.unary.operand);
        return set_type(e, nc_ty_ptr(sm->s, t, !mut));
    }
    case NC_UN_TRY: return unsupported(sm, e, "`try` is", 6);
    case NC_UN_PREINC: case NC_UN_PREDEC: case NC_UN_POSTINC: case NC_UN_POSTDEC: {
        bool inc = op == NC_UN_PREINC || op == NC_UN_POSTINC;
        const nc_type *t = check_place(sm, &e->u.unary.operand, inc ? "increment" : "decrement", USE_READ_WRITE);
        if (nc_ty_is_error(t)) return fail(e);
        if (nc_ty_is_ptr(t)) {
            if (!pointee_ok(sm, t, e->span, "pointer arithmetic") ||
                !nc_sema_require_unsafe(sm, e->span, "pointer arithmetic"))
                return fail(e);
            return set_type(e, t);
        }
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
    const nc_type *lt = check_place(sm, &e->u.binary.lhs, "assign to", op != NC_BIN_NONE ? USE_READ_WRITE : USE_WRITE);
    char what[96];
    if (e->u.binary.lhs->kind == NC_E_NAME)
        snprintf(what, sizeof what, "the assignment to '%s'", nc_sema_sym(sm, e->u.binary.lhs->u.name));
    else
        snprintf(what, sizeof what, "an assignment");

    if (nc_ty_is_ptr(lt) && (op == NC_BIN_ADD || op == NC_BIN_SUB)) {
        if (check_offset(sm, &e->u.binary.rhs, "a pointer offset") && pointee_ok(sm, lt, e->span, "pointer arithmetic"))
            nc_sema_require_unsafe(sm, e->span, "pointer arithmetic");
        return e->type;
    }
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

static const nc_type *check_convert(nc_sema *sm, nc_expr *e);

/* An argument in the variadic part of a C call: C's default argument
 * promotions are written explicitly, as in Rust (NOVAC_ABI.md §11.1). */
static void check_variadic_arg(nc_sema *sm, nc_expr **slot, const nc_fn_info *fn)
{
    nc_expr *e = *slot;
    const nc_type *t = e->type;
    if (t && t->kind == NC_TY_UNTYPED_INT) {
        nc_coerce(sm, slot, nc_ty_prim(NC_PRIM_I32), "a variadic argument (c_int)");
        return;
    }
    if (t && t->kind == NC_TY_UNTYPED_FLOAT) {
        nc_coerce(sm, slot, nc_ty_prim(NC_PRIM_F64), "a variadic argument (f64)");
        return;
    }
    nc_default_untyped(sm, slot);
    t = (*slot)->type;
    if (nc_ty_is_error(t)) return;
    const char *fname = nc_sema_sym(sm, fn->decl->u.fn.name);
    const nc_type *r = nc_ty_repr(t);
    if (nc_ty_is_aggregate(t)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E7033", "a '%s' cannot be passed to the variadic part of '%s'",
                      nc_type_name(t), fname);
        nc_diag_help(NC_DIAG(sm), "pass a pointer to it");
    } else if (nc_ty_is_float(r) && r->prim == NC_PRIM_F32) {
        nc_diag_error(NC_DIAG(sm), e->span, "E7033", "an 'f32' passed to the variadic part of '%s' must be promoted", fname);
        nc_diag_help(NC_DIAG(sm), "C promotes it to double: write `f64(x)`");
    } else if (nc_ty_is_bool(r) || (nc_ty_is_integer(r) && nc_ty_bits(r) < 32)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E7033", "a '%s' passed to the variadic part of '%s' must be promoted",
                      nc_type_name(t), fname);
        nc_diag_help(NC_DIAG(sm), "C promotes it to int: write `c_int(x)`");
    }
}

/* Is `name` a builtin here, i.e. not shadowed by a local or module item? */
static bool is_builtin_name(nc_sema *sm, const nc_expr *e, const char *name)
{
    return e->kind == NC_E_NAME && strcmp(nc_sema_sym(sm, e->u.name), name) == 0 && !nc_scope_lookup(sm, e->u.name) &&
           !nc_sema_find_module_sym(sm, e->u.name);
}

/* addr(p), ptr_cast::<U*>(p), ptr_cast_mut::<U*>(p), ptr_from_addr::<T>(a)
 * (NOVAC_TYPE_SYSTEM.md §5.3). Returns NULL if the call is not one. */
static const nc_type *check_builtin_call(nc_sema *sm, nc_expr *e)
{
    nc_expr *callee = e->u.call.callee;
    nc_expr *base = callee->kind == NC_E_GENERIC ? callee->u.generic.base : callee;
    static const char *const names[] = {"addr", "ptr_cast", "ptr_cast_mut", "ptr_from_addr"};
    int kind = -1;
    for (int k = 0; k < 4; k++)
        if (is_builtin_name(sm, base, names[k])) kind = k;
    if (kind < 0) return NULL;
    const char *name = names[kind];
    bool generic = kind != NC_BUILTIN_ADDR;
    nc_type_expr *te = NULL;
    if (generic) {
        if (callee->kind != NC_E_GENERIC || callee->u.generic.nargs != 1 || !callee->u.generic.args[0].type) {
            nc_diag_error(NC_DIAG(sm), callee->span, "E4002", "'%s' takes one type argument: `%s::<T>(...)`", name, name);
            return fail(e);
        }
        te = callee->u.generic.args[0].type;
    } else if (callee->kind == NC_E_GENERIC) {
        nc_diag_error(NC_DIAG(sm), callee->span, "E4002", "'addr' takes no type argument");
        return fail(e);
    }
    if (e->u.call.nargs != 1) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4002", "'%s' takes exactly one argument", name);
        for (uint32_t i = 0; i < e->u.call.nargs; i++) nc_check_value(sm, &e->u.call.args[i], NULL);
        return fail(e);
    }
    nc_expr *arg = e->u.call.args[0];
    const nc_type *target = NULL;
    if (te) {
        bool c;
        target = nc_sema_resolve_type(sm, te, &c);
        if (nc_ty_is_error(target)) return fail(e);
    }
    e->kind = NC_E_BUILTIN;
    e->u.builtin.kind = (nc_builtin)kind;
    e->u.builtin.type = te;
    e->u.builtin.operand = arg;
    nc_expr **slot = &e->u.builtin.operand;
    if (kind == NC_BUILTIN_PTR_FROM_ADDR) {
        const nc_type *usize = nc_ty_prim(NC_PRIM_USIZE);
        nc_check_value(sm, slot, usize);
        if (!nc_coerce(sm, slot, usize, "the argument of 'ptr_from_addr'") || nc_ty_is_error((*slot)->type)) return fail(e);
        if (!nc_sema_require_unsafe(sm, e->span, "'ptr_from_addr'")) return fail(e);
        return set_type(e, nc_ty_ptr(sm->s, target, false));
    }
    /* `ptr_cast::<U*>(&x)`: any pointer type lets `&x` be a pointer (D-044). */
    const nc_type *any_ptr = target && nc_ty_is_ptr(target) ? target : nc_ty_ptr(sm->s, nc_ty_prim(NC_PRIM_U8), true);
    const nc_type *from = nc_check_value(sm, slot, any_ptr);
    if (nc_ty_is_error(from)) return fail(e);
    if (!nc_ty_is_ptr(from)) {
        nc_diag_error(NC_DIAG(sm), (*slot)->span, "E4001", "the argument of '%s' must be a pointer, found '%s'", name,
                      nc_type_name(from));
        return fail(e);
    }
    if (kind == NC_BUILTIN_ADDR) return set_type(e, nc_ty_prim(NC_PRIM_USIZE));
    if (!nc_ty_is_ptr(target)) {
        nc_diag_error(NC_DIAG(sm), te->span, "E4007", "'%s' converts to a pointer type, not '%s'", name, nc_type_name(target));
        return fail(e);
    }
    if (kind == NC_BUILTIN_PTR_CAST && from->pointee_const && !target->pointee_const) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4007", "'ptr_cast' cannot remove `const` from '%s'", nc_type_name(from));
        nc_diag_help(NC_DIAG(sm), "`ptr_cast_mut` can, in unsafe code");
        return fail(e);
    }
    if (kind == NC_BUILTIN_PTR_CAST_MUT && !nc_sema_require_unsafe(sm, e->span, "'ptr_cast_mut'")) return fail(e);
    return set_type(e, target);
}

static const nc_type *check_call(nc_sema *sm, nc_expr *e)
{
    nc_expr *callee = e->u.call.callee;
    nc_fn_info *fn = NULL;
    const nc_type *bt = check_builtin_call(sm, e);
    if (bt) return bt;
    if (name_as_type(sm, callee)) {
        /* `Color(x)`, `Vec2(p)`: a conversion to a named type, which the
         * parser cannot tell from a call. */
        if (e->u.call.nargs != 1) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4002", "a conversion to '%s' takes exactly one value",
                          nc_sema_sym(sm, callee->u.name));
            for (uint32_t i = 0; i < e->u.call.nargs; i++) nc_check_value(sm, &e->u.call.args[i], NULL);
            return fail(e);
        }
        nc_type_expr *te = nc_arena_alloc(&sm->s->arena, sizeof *te);
        te->kind = NC_TE_PATH;
        te->span = callee->span;
        te->u.path.path.count = 1;
        te->u.path.path.parts = nc_arena_alloc(&sm->s->arena, sizeof(nc_sym));
        te->u.path.path.parts[0] = callee->u.name;
        te->u.path.path.span = callee->span;
        nc_expr *arg = e->u.call.args[0];
        e->kind = NC_E_CONVERT;
        e->u.convert.type = te;
        e->u.convert.operand = arg;
        return check_convert(sm, e);
    }
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

    if (fn && (fn->variadic ? e->u.call.nargs < fn->nparams : e->u.call.nargs != fn->nparams)) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4002", "function '%s' takes %s%u argument%s, but %u %s given",
                      nc_sema_sym(sm, fn->decl->u.fn.name), fn->variadic ? "at least " : "", fn->nparams,
                      fn->nparams == 1 ? "" : "s", e->u.call.nargs, e->u.call.nargs == 1 ? "was" : "were");
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
        } else if (fn && fn->variadic) {
            check_variadic_arg(sm, &e->u.call.args[i], fn);
        } else {
            nc_default_untyped(sm, &e->u.call.args[i]);
        }
    }
    if (!fn) return fail(e);
    if (fn->is_import) {
        char what[96];
        snprintf(what, sizeof what, "calling the C function '%s'", nc_sema_sym(sm, fn->decl->u.fn.name));
        if (!nc_sema_require_unsafe(sm, e->span, what)) return fail(e);
    } else if (fn->decl->u.fn.is_unsafe) {
        char what[96];
        snprintf(what, sizeof what, "calling the unsafe function '%s'", nc_sema_sym(sm, fn->decl->u.fn.name));
        if (!nc_sema_require_unsafe(sm, e->span, what)) return fail(e);
    }
    return set_type(e, fn->ret);
}

static bool is_enumerator_value(const nc_type *et, nc_cval v)
{
    for (uint32_t i = 0; i < et->en->nitems; i++)
        if (nc_cv_cmp(et->en->items[i].value, v) == 0) return true;
    return false;
}

static const nc_type *check_convert(nc_sema *sm, nc_expr *e)
{
    bool is_const_ty;
    const nc_type *to = nc_sema_value_type(sm, e->u.convert.type, &is_const_ty, true, "a conversion");
    const nc_type *from = nc_check_value(sm, &e->u.convert.operand, NULL);
    nc_expr *x = e->u.convert.operand;
    if (nc_ty_is_error(to) || nc_ty_is_error(from)) return fail(e);
    char buf[64];
    if (from == to) { /* identity: allowed for every type */
        e->cval = x->cval;
        return set_type(e, to);
    }
    bool to_enum = nc_ty_is_enum(to), from_enum = nc_ty_is_enum(from);
    bool from_int = nc_ty_is_integer(from) || from->kind == NC_TY_UNTYPED_INT;
    bool from_scalar = nc_ty_is_untyped(from) || nc_ty_is_numeric(from) || nc_ty_is_bool(from) || from_enum;
    bool to_scalar = nc_ty_is_numeric(to) || to_enum;
    if (!from_scalar || !to_scalar || (to_enum && !from_int) || (from_enum && !nc_ty_is_integer(to)) ||
        (nc_ty_is_bool(from) && !nc_ty_is_integer(to))) {
        nc_diag_error(NC_DIAG(sm), e->span, "E4007", "cannot convert '%s' to '%s'", nc_type_name(from), nc_type_name(to));
        if (nc_ty_is_bool(to)) nc_diag_help(NC_DIAG(sm), "compare instead, e.g. `x != 0`");
        else if (to_enum && from_enum) nc_diag_help(NC_DIAG(sm), "convert through the underlying integer type");
        else if (nc_ty_is_ptr(from) && nc_ty_is_integer(to)) nc_diag_help(NC_DIAG(sm), "use `addr(p)` for the address");
        else if (nc_ty_is_ptr(to)) nc_diag_help(NC_DIAG(sm), "use `ptr_cast::<%s>(p)` or `ptr_from_addr::<T>(a)`", nc_type_name(to));
        return fail(e);
    }
    set_type(e, to);
    if (!is_const(x)) return to;

    /* An enum converts like its underlying integer type, and the result must
     * be one of its enumerators (NOVAC_TYPE_SYSTEM.md §5.2). */
    const nc_type *target = to_enum ? to->en->underlying : to;
    nc_cval v = x->cval;
    if (v.kind == NC_CV_BOOL) {
        e->cval = nc_cv_int(false, v.b ? 1 : 0);
        return to;
    }
    if (nc_ty_is_integer(target)) {
        if (v.kind == NC_CV_FLOAT) {
            double t = trunc(v.f);
            if (isnan(v.f) || fabs(t) >= 18446744073709551616.0) {
                nc_diag_error(NC_DIAG(sm), e->span, "E4040", "conversion of %s to '%s' always panics (value out of range)",
                              fmt_cval(v, buf, sizeof buf), nc_type_name(to));
                return fail(e);
            }
            v = nc_cv_int(t < 0, (uint64_t)fabs(t));
        }
        if (!nc_cv_fits(v, target) || (to_enum && !is_enumerator_value(to, v))) {
            nc_diag_error(NC_DIAG(sm), e->span, "E4040", "conversion of %s to '%s' always panics (%s)",
                          fmt_cval(v, buf, sizeof buf), nc_type_name(to),
                          to_enum && nc_cv_fits(v, target) ? "not a value of the enum" : "value out of range");
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
    case NC_E_NAME: return check_name(sm, e);
    case NC_E_UNARY: return check_unary(sm, e, expected);
    case NC_E_BINARY: return check_binary(sm, e, expected);
    case NC_E_ASSIGN: return check_assign(sm, e);
    case NC_E_COND: return check_cond(sm, e, expected);
    case NC_E_CALL: return check_call(sm, e);
    case NC_E_CONVERT: return check_convert(sm, e);
    case NC_E_IMPLICIT: return e->type;
    case NC_E_ERROR: return fail(e);
    case NC_E_CHAR:
        e->cval = nc_cv_int(false, e->u.char_lit);
        return set_type(e, nc_ty_prim(NC_PRIM_U8));
    case NC_E_STRING: return unsupported(sm, e, "string literals are", 6);
    case NC_E_CSTRING: /* c"...": a NUL-terminated constant array, as `const c_char*` */
        return set_type(e, nc_ty_ptr(sm->s, nc_sema_ffi_type(sm, "c_char"), true));
    case NC_E_NULL: return set_type(e, nc_ty_null());
    case NC_E_NONE: return unsupported(sm, e, "`none` (optional types) is", 6);
    case NC_E_SOME: return unsupported(sm, e, "`some` (optional types) is", 6);
    case NC_E_SELF: return unsupported(sm, e, "`self` is", 5);
    case NC_E_INDEX: return index_access(sm, e, nc_check_value(sm, &e->u.index.base, NULL));
    case NC_E_MEMBER: return check_member(sm, e);
    case NC_E_ARROW: return check_member(sm, e);
    case NC_E_STRUCT_LIT: return check_struct_lit(sm, slot);
    case NC_E_SIZEOF:
    case NC_E_ALIGNOF: return check_size(sm, e);
    case NC_E_OFFSETOF: return check_offsetof(sm, e);
    case NC_E_AGGREGATE:
    case NC_E_BUILTIN: return e->type;
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
