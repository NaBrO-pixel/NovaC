/* Lowering of the type-checked AST to NIR.
 *
 * Every check NOVAC_TYPE_SYSTEM.md requires at run time becomes an
 * explicit `assert` terminator:
 *   - `+ - *` on integers, unary `-`, `++`, `--`: checked arithmetic;
 *   - `/` and `%`: divisor non-zero, and not MIN / -1 for signed types;
 *   - `<<`, `>>`: shift amount below the bit width;
 *   - `T(x)`: the value is representable in T (NaN for floats), and an
 *     integer converted to an enum is one of its enumerators;
 *   - `a[i]`: the index is below the array length.
 * Expressions that semantic analysis folded to constants are emitted as
 * constants; their subexpressions have no side effects. */
#include "nir/nir.h"

#include "sema/consteval.h"

#include "support/vec.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct var_map {
    const nc_var *var;
    nc_local_id local;
} var_map;

typedef struct loop_targets {
    nc_block_id brk, cont;
} loop_targets;

typedef struct lowerer {
    nc_session *s;
    nc_nir_module *m;
    nc_nir_fn *fn;
    nc_block_id cur;
    NC_VEC(var_map) vars;
    NC_VEC(loop_targets) loops;  /* loops and switches; a switch's `cont` is the enclosing loop's */
    nc_block_id fallthrough;     /* the next case's block, inside a switch */
} lowerer;

/* ---- construction helpers ---------------------------------------------------------- */

static nc_local_id new_local(lowerer *lx, const nc_type *type, nc_sym name, nc_span span, bool user)
{
    nc_nir_fn *f = lx->fn;
    if (f->nlocals == f->cap_locals) {
        f->cap_locals = f->cap_locals ? f->cap_locals * 2 : 16;
        f->locals = nc_xrealloc(f->locals, f->cap_locals * sizeof *f->locals);
    }
    nc_nir_local *l = &f->locals[f->nlocals];
    memset(l, 0, sizeof *l);
    l->type = type;
    l->name = name;
    l->span = span;
    l->is_user = user;
    return f->nlocals++;
}

static nc_block_id new_block(lowerer *lx)
{
    nc_nir_fn *f = lx->fn;
    if (f->nblocks == f->cap_blocks) {
        f->cap_blocks = f->cap_blocks ? f->cap_blocks * 2 : 8;
        f->blocks = nc_xrealloc(f->blocks, f->cap_blocks * sizeof *f->blocks);
    }
    memset(&f->blocks[f->nblocks], 0, sizeof f->blocks[0]);
    return f->nblocks++;
}

/* The current block, opening a fresh (unreachable) one if the current
 * block is already terminated, e.g. after `return`. */
static nc_nir_block *open_block(lowerer *lx)
{
    if (lx->fn->blocks[lx->cur].term.kind != NC_TERM_NONE) lx->cur = new_block(lx);
    return &lx->fn->blocks[lx->cur];
}

static void push_stmt(lowerer *lx, nc_nir_stmt st)
{
    nc_nir_block *b = open_block(lx);
    if (b->nstmts == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->stmts = nc_xrealloc(b->stmts, b->cap * sizeof *b->stmts);
    }
    b->stmts[b->nstmts++] = st;
}

static void terminate(lowerer *lx, nc_terminator t)
{
    nc_nir_block *b = open_block(lx);
    b->term = t;
}

static void goto_block(lowerer *lx, nc_block_id target, nc_span span)
{
    nc_terminator t = {0};
    t.kind = NC_TERM_GOTO;
    t.target = target;
    t.span = span;
    terminate(lx, t);
}

static void switch_to(lowerer *lx, nc_block_id b)
{
    lx->cur = b;
}

static nc_place place_local(lowerer *lx, nc_local_id id)
{
    nc_place p = {0};
    p.base = NC_PLACE_LOCAL;
    p.id = id;
    p.type = lx->fn->locals[id].type;
    return p;
}

static nc_operand op_place(nc_place p, nc_span span)
{
    nc_operand o = {0};
    o.kind = NC_OP_COPY;
    o.place = p;
    o.type = p.type;
    o.span = span;
    return o;
}

static nc_operand op_local(lowerer *lx, nc_local_id id, nc_span span)
{
    return op_place(place_local(lx, id), span);
}

static nc_operand op_zero(const nc_type *type, nc_span span)
{
    nc_operand o = {0};
    o.kind = NC_OP_ZERO;
    o.type = type;
    o.span = span;
    return o;
}

static nc_operand op_const(nc_cval v, const nc_type *type, nc_span span)
{
    nc_operand o = {0};
    o.kind = NC_OP_CONST;
    o.cval = v;
    o.type = type;
    o.span = span;
    return o;
}

static nc_operand op_int(int64_t v, const nc_type *type, nc_span span)
{
    nc_cval c = {0};
    c.kind = NC_CV_INT;
    c.neg = v < 0;
    c.mag = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    return op_const(c, type, span);
}

static nc_operand op_bool(bool b, nc_span span)
{
    nc_cval c = {0};
    c.kind = NC_CV_BOOL;
    c.b = b;
    return op_const(c, nc_ty_prim(NC_PRIM_BOOL), span);
}

static void assign_place(lowerer *lx, nc_place dest, nc_rvalue rv, nc_span span)
{
    nc_nir_stmt st = {0};
    st.kind = NC_ST_ASSIGN;
    st.dest = dest;
    st.rv = rv;
    st.span = span;
    push_stmt(lx, st);
}

static void assign(lowerer *lx, nc_local_id dest, nc_rvalue rv, nc_span span)
{
    assign_place(lx, place_local(lx, dest), rv, span);
}

static void copy_to(lowerer *lx, nc_place dest, nc_operand v, nc_span span)
{
    nc_rvalue use = {0};
    use.kind = NC_RV_USE;
    use.a = v;
    assign_place(lx, dest, use, span);
}

/* temp = rvalue; returns the temp as an operand. */
static nc_operand emit_rv(lowerer *lx, const nc_type *type, nc_rvalue rv, nc_span span)
{
    nc_local_id t = new_local(lx, type, 0, span, false);
    assign(lx, t, rv, span);
    return op_local(lx, t, span);
}

static nc_operand emit_binary(lowerer *lx, nc_nir_binop op, nc_operand a, nc_operand b, const nc_type *type, nc_span span)
{
    nc_rvalue rv = {0};
    rv.kind = NC_RV_BINARY;
    rv.binop = op;
    rv.a = a;
    rv.b = b;
    return emit_rv(lx, type, rv, span);
}

static nc_operand emit_unary(lowerer *lx, nc_nir_unop op, nc_operand a, const nc_type *type, nc_span span)
{
    nc_rvalue rv = {0};
    rv.kind = NC_RV_UNARY;
    rv.unop = op;
    rv.a = a;
    return emit_rv(lx, type, rv, span);
}

static nc_operand emit_cast(lowerer *lx, nc_cast_kind kind, nc_operand a, const nc_type *to, nc_span span)
{
    nc_rvalue rv = {0};
    rv.kind = NC_RV_CAST;
    rv.cast = kind;
    rv.a = a;
    return emit_rv(lx, to, rv, span);
}

/* if cond == expected continue, else panic with `message`. */
static void emit_assert(lowerer *lx, nc_operand cond, bool expected, const char *message, nc_span span)
{
    nc_block_id next = new_block(lx);
    nc_terminator t = {0};
    t.kind = NC_TERM_ASSERT;
    t.cond = cond;
    t.expected = expected;
    t.message = message;
    t.target = next;
    t.span = span;
    terminate(lx, t);
    switch_to(lx, next);
}

/* (dest, flag) = checked op(a, b); assert !flag. Returns dest; a new
 * temporary when `dest` is NULL. */
static nc_operand emit_checked(lowerer *lx, nc_nir_binop op, nc_operand a, nc_operand b, const nc_type *type,
                               const nc_place *dest_in, const char *message, nc_span span)
{
    nc_place dest = dest_in ? *dest_in : place_local(lx, new_local(lx, type, 0, span, false));
    nc_local_id flag = new_local(lx, nc_ty_prim(NC_PRIM_BOOL), 0, span, false);
    nc_nir_stmt st = {0};
    st.kind = NC_ST_CHECKED;
    st.dest = dest;
    st.flag = flag;
    st.op = op;
    st.rv.a = a;
    st.rv.b = b;
    st.span = span;
    push_stmt(lx, st);
    emit_assert(lx, op_local(lx, flag, span), false, message, span);
    return op_place(dest, span);
}

static const char *arena_message(lowerer *lx, const char *fmt, const char *arg)
{
    char buf[128];
    snprintf(buf, sizeof buf, fmt, arg);
    return nc_arena_strndup(&lx->s->arena, buf, strlen(buf));
}

static nc_local_id local_of(lowerer *lx, const nc_var *v)
{
    for (size_t i = lx->vars.len; i-- > 0;)
        if (lx->vars.data[i].var == v) return lx->vars.data[i].local;
    nc_ice("nir lowering", "variable without a local");
}

/* ---- expressions ------------------------------------------------------------------- */

static nc_operand lower_expr(lowerer *lx, nc_expr *e);

static int64_t signed_min(const nc_type *t)
{
    unsigned bits = nc_ty_bits(t);
    return bits == 64 ? INT64_MIN : -((int64_t)1 << (bits - 1));
}

/* Arithmetic, wrapping, bitwise, shift and comparison operators, shared by
 * binary expressions and compound assignment. `dest` receives the result
 * when not NULL (compound assignment writes the place directly). */
static nc_operand lower_binop(lowerer *lx, nc_binop op, nc_operand a, nc_operand b, const nc_type *result,
                              const nc_place *dest, nc_span span)
{
    const nc_type *t = a.type; /* operand type */
    bool is_int = nc_ty_is_integer(t);
    nc_nir_binop nb;
    if (nc_ty_is_ptr(t) && (op == NC_BIN_ADD || op == NC_BIN_SUB)) {
        nc_rvalue prv = {0};
        prv.kind = nc_ty_is_ptr(b.type) ? NC_RV_PTR_DIFF : NC_RV_PTR_OFFSET;
        prv.binop = op == NC_BIN_ADD ? NC_NB_ADD : NC_NB_SUB;
        prv.a = a;
        prv.b = b;
        if (dest) {
            assign_place(lx, *dest, prv, span);
            return op_place(*dest, span);
        }
        return emit_rv(lx, result, prv, span);
    }
    switch (op) {
    case NC_BIN_ADD: case NC_BIN_SUB: case NC_BIN_MUL:
        nb = op == NC_BIN_ADD ? NC_NB_ADD : op == NC_BIN_SUB ? NC_NB_SUB : NC_NB_MUL;
        if (is_int) {
            const char *msg = op == NC_BIN_ADD ? "integer overflow in '+'" : op == NC_BIN_SUB ? "integer overflow in '-'"
                                                                                             : "integer overflow in '*'";
            return emit_checked(lx, nb, a, b, t, dest, msg, span);
        }
        break;
    case NC_BIN_WADD: nb = NC_NB_ADD; break;
    case NC_BIN_WSUB: nb = NC_NB_SUB; break;
    case NC_BIN_WMUL: nb = NC_NB_MUL; break;
    case NC_BIN_DIV:
    case NC_BIN_MOD:
        nb = op == NC_BIN_DIV ? NC_NB_DIV : NC_NB_REM;
        if (is_int) {
            nc_operand zero = emit_binary(lx, NC_NB_NE, b, op_int(0, t, span), nc_ty_prim(NC_PRIM_BOOL), span);
            emit_assert(lx, zero, true, "division by zero", span);
            if (nc_ty_is_signed(t)) {
                nc_operand is_min = emit_binary(lx, NC_NB_EQ, a, op_int(signed_min(t), t, span), nc_ty_prim(NC_PRIM_BOOL), span);
                nc_operand is_m1 = emit_binary(lx, NC_NB_EQ, b, op_int(-1, t, span), nc_ty_prim(NC_PRIM_BOOL), span);
                nc_operand both = emit_binary(lx, NC_NB_AND, is_min, is_m1, nc_ty_prim(NC_PRIM_BOOL), span);
                emit_assert(lx, both, false, op == NC_BIN_DIV ? "integer overflow in '/'" : "integer overflow in '%'", span);
            }
        }
        break;
    case NC_BIN_SHL:
    case NC_BIN_SHR: {
        nb = op == NC_BIN_SHL ? NC_NB_SHL : NC_NB_SHR;
        /* The amount has its own unsigned type; check it, then give it the
         * shifted operand's type, as LLVM requires. */
        nc_operand ok = emit_binary(lx, NC_NB_LT, b, op_int((int64_t)nc_ty_bits(t), b.type, span),
                                    nc_ty_prim(NC_PRIM_BOOL), span);
        emit_assert(lx, ok, true, "shift amount too large", span);
        if (b.type != t) {
            if (b.kind == NC_OP_CONST) b.type = t;
            else b = emit_cast(lx, NC_CAST_INT_RESIZE, b, t, span);
        }
        break;
    }
    case NC_BIN_BITAND: nb = NC_NB_AND; break;
    case NC_BIN_BITOR: nb = NC_NB_OR; break;
    case NC_BIN_BITXOR: nb = NC_NB_XOR; break;
    case NC_BIN_EQ: nb = NC_NB_EQ; break;
    case NC_BIN_NE: nb = NC_NB_NE; break;
    case NC_BIN_LT: nb = NC_NB_LT; break;
    case NC_BIN_LE: nb = NC_NB_LE; break;
    case NC_BIN_GT: nb = NC_NB_GT; break;
    case NC_BIN_GE: nb = NC_NB_GE; break;
    default: nc_ice("nir lowering", "unexpected binary operator");
    }
    nc_rvalue rv = {0};
    rv.kind = NC_RV_BINARY;
    rv.binop = nb;
    rv.a = a;
    rv.b = b;
    if (dest) {
        assign_place(lx, *dest, rv, span);
        return op_place(*dest, span);
    }
    return emit_rv(lx, result, rv, span);
}

/* `&&` and `||` evaluate the right operand only when needed. */
static nc_operand lower_logical(lowerer *lx, nc_expr *e)
{
    bool is_and = e->u.binary.op == NC_BIN_AND;
    nc_local_id res = new_local(lx, nc_ty_prim(NC_PRIM_BOOL), 0, e->span, false);
    nc_operand l = lower_expr(lx, e->u.binary.lhs);
    nc_block_id rhs_b = new_block(lx), short_b = new_block(lx), join = new_block(lx);
    nc_terminator t = {0};
    t.kind = NC_TERM_BRANCH;
    t.cond = l;
    t.target = is_and ? rhs_b : short_b;
    t.other = is_and ? short_b : rhs_b;
    t.span = e->span;
    terminate(lx, t);

    switch_to(lx, rhs_b);
    nc_operand r = lower_expr(lx, e->u.binary.rhs);
    nc_rvalue use = {0};
    use.kind = NC_RV_USE;
    use.a = r;
    assign(lx, res, use, e->span);
    goto_block(lx, join, e->span);

    switch_to(lx, short_b);
    use.a = op_bool(!is_and, e->span);
    assign(lx, res, use, e->span);
    goto_block(lx, join, e->span);

    switch_to(lx, join);
    return op_local(lx, res, e->span);
}

static int cmp_cval(const void *a, const void *b)
{
    return nc_cv_cmp(*(const nc_cval *)a, *(const nc_cval *)b);
}

/* Assert that the integer `u` (of the enum's underlying type) is one of
 * the enumerators of `et`. */
static void check_enum_value(lowerer *lx, nc_operand u, const nc_type *et, nc_span span)
{
    const nc_type *b = nc_ty_prim(NC_PRIM_BOOL);
    const nc_type *ut = et->en->underlying;
    uint32_t n = et->en->nitems;
    nc_cval *vals = nc_xcalloc(n ? n : 1, sizeof *vals);
    for (uint32_t i = 0; i < n; i++) vals[i] = et->en->items[i].value;
    qsort(vals, n, sizeof *vals, cmp_cval);
    uint32_t k = 0; /* deduplicate */
    for (uint32_t i = 0; i < n; i++)
        if (k == 0 || nc_cv_cmp(vals[k - 1], vals[i]) != 0) vals[k++] = vals[i];
    n = k;
    const char *msg = arena_message(lx, "value is not an enumerator of '%s'", nc_type_name(et));
    nc_cval last = vals[n - 1], span_len;
    bool contiguous = nc_cv_sub(last, vals[0], &span_len) && !span_len.neg && span_len.mag == n - 1;
    nc_operand ok;
    if (contiguous) {
        nc_operand lo = emit_binary(lx, NC_NB_GE, u, op_const(vals[0], ut, span), b, span);
        nc_operand hi = emit_binary(lx, NC_NB_LE, u, op_const(last, ut, span), b, span);
        ok = emit_binary(lx, NC_NB_AND, lo, hi, b, span);
    } else {
        ok = emit_binary(lx, NC_NB_EQ, u, op_const(vals[0], ut, span), b, span);
        for (uint32_t i = 1; i < n; i++)
            ok = emit_binary(lx, NC_NB_OR, ok, emit_binary(lx, NC_NB_EQ, u, op_const(vals[i], ut, span), b, span), b, span);
    }
    free(vals);
    emit_assert(lx, ok, true, msg, span);
}

/* T(x) for numeric or enum T (NOVAC_TYPE_SYSTEM.md §5.2): checks, then
 * converts. */
static nc_operand lower_convert(lowerer *lx, nc_operand x, const nc_type *to, nc_span span)
{
    const nc_type *from = x.type;
    const nc_type *b = nc_ty_prim(NC_PRIM_BOOL);
    if (from == to) return x;
    if (nc_ty_is_enum(from)) /* an enum converts as its underlying integer */
        return lower_convert(lx, emit_cast(lx, NC_CAST_NOP, x, from->en->underlying, span), to, span);
    if (nc_ty_is_enum(to)) {
        nc_operand u = lower_convert(lx, x, to->en->underlying, span);
        check_enum_value(lx, u, to, span);
        return emit_cast(lx, NC_CAST_NOP, u, to, span);
    }
    const char *msg = arena_message(lx, "value out of range in conversion to '%s'", nc_type_name(to));

    if (nc_ty_is_bool(from)) return emit_cast(lx, NC_CAST_BOOL_TO_INT, x, to, span);

    if (nc_ty_is_integer(from) && nc_ty_is_integer(to)) {
        unsigned fb = nc_ty_bits(from), tb = nc_ty_bits(to);
        bool fs = nc_ty_is_signed(from), ts = nc_ty_is_signed(to);
        if (fs && !ts) { /* negative values never fit an unsigned type */
            nc_operand nonneg = emit_binary(lx, NC_NB_GE, x, op_int(0, from, span), b, span);
            emit_assert(lx, nonneg, true, msg, span);
        }
        nc_operand r = emit_cast(lx, NC_CAST_INT_RESIZE, x, to, span);
        if (tb < fb || (tb == fb && fs != ts)) {
            if (tb < fb) {
                /* Narrowing: the value survives iff converting back gives it. */
                nc_operand back = emit_cast(lx, NC_CAST_INT_RESIZE, r, from, span);
                nc_operand same = emit_binary(lx, NC_NB_EQ, back, x, b, span);
                emit_assert(lx, same, true, msg, span);
            }
            if (!fs && ts) { /* unsigned -> signed: the result must not look negative */
                nc_operand nonneg = emit_binary(lx, NC_NB_GE, r, op_int(0, to, span), b, span);
                emit_assert(lx, nonneg, true, msg, span);
            }
        }
        return r;
    }
    if (nc_ty_is_integer(from) && nc_ty_is_float(to)) return emit_cast(lx, NC_CAST_INT_TO_FLOAT, x, to, span);
    if (nc_ty_is_float(from) && nc_ty_is_float(to)) return emit_cast(lx, NC_CAST_FLOAT_RESIZE, x, to, span);

    /* float -> integer: in range (truncated toward zero) and not NaN. Compare
     * in f64; ordered comparisons are false for NaN. */
    const nc_type *f64 = nc_ty_prim(NC_PRIM_F64);
    nc_operand xd = from == f64 ? x : emit_cast(lx, NC_CAST_FLOAT_RESIZE, x, f64, span);
    unsigned tb = nc_ty_bits(to);
    double lo, hi; /* valid iff lo < x < hi (strict), with lo/hi exact in f64 */
    nc_nir_binop lo_op = NC_NB_GT;
    if (nc_ty_is_signed(to)) {
        hi = ldexp(1.0, (int)tb - 1);
        if (tb == 64) {
            lo = -ldexp(1.0, 63); /* -2^63 - 1 is not exact in f64; x >= -2^63 is equivalent */
            lo_op = NC_NB_GE;
        } else {
            lo = -ldexp(1.0, (int)tb - 1) - 1.0;
        }
    } else {
        hi = ldexp(1.0, (int)tb);
        lo = -1.0;
    }
    nc_cval lo_v = {0}, hi_v = {0};
    lo_v.kind = hi_v.kind = NC_CV_FLOAT;
    lo_v.f = lo;
    hi_v.f = hi;
    nc_operand above = emit_binary(lx, lo_op, xd, op_const(lo_v, f64, span), b, span);
    nc_operand below = emit_binary(lx, NC_NB_LT, xd, op_const(hi_v, f64, span), b, span);
    nc_operand ok = emit_binary(lx, NC_NB_AND, above, below, b, span);
    emit_assert(lx, ok, true, msg, span);
    return emit_cast(lx, NC_CAST_FLOAT_TO_INT, x, to, span);
}

/* ---- places ------------------------------------------------------------------------ */

static uint32_t static_index(lowerer *lx, const nc_static_info *st)
{
    for (uint32_t i = 0; i < lx->m->nstatics; i++)
        if (lx->m->statics[i].info == st) return i;
    nc_ice("nir lowering", "static without NIR");
}

static nc_place project(lowerer *lx, nc_place base, nc_proj pr)
{
    nc_proj *projs = nc_arena_alloc(&lx->s->arena, (base.nprojs + 1) * sizeof *projs);
    if (base.nprojs) memcpy(projs, base.projs, base.nprojs * sizeof *projs);
    projs[base.nprojs] = pr;
    base.projs = projs;
    base.nprojs++;
    base.type = pr.type;
    return base;
}

static nc_place lower_place(lowerer *lx, nc_expr *e);

static nc_place deref_place(lowerer *lx, nc_place ptr, const nc_type *pointee)
{
    nc_proj pr = {0};
    pr.kind = NC_PROJ_DEREF;
    pr.type = pointee;
    return project(lx, ptr, pr);
}

/* A place for `e`: `e` itself if it denotes one, else a temporary holding
 * its value (`f().x`). */
static nc_place place_of(lowerer *lx, nc_expr *e)
{
    bool is_place = (e->kind == NC_E_NAME && (e->binding_kind == NC_BIND_VAR || e->binding_kind == NC_BIND_STATIC)) ||
                    ((e->kind == NC_E_MEMBER || e->kind == NC_E_ARROW) && e->binding_kind == NC_BIND_FIELD) ||
                    e->kind == NC_E_INDEX || (e->kind == NC_E_UNARY && e->u.unary.op == NC_UN_DEREF);
    if (is_place && e->cval.kind == NC_CV_NONE) return lower_place(lx, e);
    nc_operand v = lower_expr(lx, e);
    if (v.kind == NC_OP_COPY && v.place.base == NC_PLACE_LOCAL && !lx->fn->locals[v.place.id].is_user) return v.place;
    nc_local_id t = new_local(lx, e->type, 0, e->span, false);
    copy_to(lx, place_local(lx, t), v, e->span);
    return place_local(lx, t);
}

static nc_place lower_place(lowerer *lx, nc_expr *e)
{
    switch (e->kind) {
    case NC_E_NAME:
        if (e->binding_kind == NC_BIND_VAR) return place_local(lx, local_of(lx, e->binding));
        if (e->binding_kind == NC_BIND_STATIC) {
            nc_place p = {0};
            p.base = NC_PLACE_STATIC;
            p.id = static_index(lx, e->binding);
            p.type = e->type;
            return p;
        }
        break;
    case NC_E_UNARY: /* *p */
        return deref_place(lx, place_of(lx, e->u.unary.operand), e->type);
    case NC_E_ARROW: {
        nc_place base = deref_place(lx, place_of(lx, e->u.member.base), e->u.member.base->type->elem);
        nc_proj pr = {0};
        pr.kind = NC_PROJ_FIELD;
        pr.field = (uint32_t)(uintptr_t)e->binding;
        pr.type = e->type;
        return project(lx, base, pr);
    }
    case NC_E_MEMBER: {
        nc_place base = place_of(lx, e->u.member.base);
        nc_proj pr = {0};
        pr.kind = NC_PROJ_FIELD;
        pr.field = (uint32_t)(uintptr_t)e->binding;
        pr.type = e->type;
        return project(lx, base, pr);
    }
    case NC_E_INDEX: {
        if (nc_ty_is_ptr(e->u.index.base->type)) { /* p[i] is *(p + i): unchecked */
            nc_operand p = lower_expr(lx, e->u.index.base);
            nc_operand i = lower_expr(lx, e->u.index.index);
            nc_rvalue rv = {0};
            rv.kind = NC_RV_PTR_OFFSET;
            rv.binop = NC_NB_ADD;
            rv.a = p;
            rv.b = i;
            nc_operand q = emit_rv(lx, p.type, rv, e->span);
            return deref_place(lx, q.place, e->type);
        }
        nc_place base = place_of(lx, e->u.index.base);
        nc_operand ix = lower_expr(lx, e->u.index.index);
        nc_proj pr = {0};
        pr.kind = NC_PROJ_INDEX;
        pr.type = e->type;
        if (ix.kind == NC_OP_CONST) {
            pr.index_is_const = true; /* below the length: checked by sema (E5040) */
            pr.index = ix.cval.mag;
        } else {
            /* Copy the index, so the checked value is the one used even if
             * the variable changes before the place is accessed. */
            const nc_type *usize = nc_ty_prim(NC_PRIM_USIZE);
            nc_local_id t = new_local(lx, usize, 0, e->span, false);
            copy_to(lx, place_local(lx, t), ix, e->span);
            uint64_t len = nc_ty_is_array(base.type) ? base.type->len : 0;
            nc_operand ok = emit_binary(lx, NC_NB_LT, op_local(lx, t, e->span), op_int((int64_t)len, usize, e->span),
                                        nc_ty_prim(NC_PRIM_BOOL), e->span);
            emit_assert(lx, ok, true, "index out of bounds", e->u.index.index->span);
            pr.index_local = t;
        }
        return project(lx, base, pr);
    }
    default: break;
    }
    nc_ice("nir lowering", "expression is not a place");
}

static nc_operand lower_aggregate(lowerer *lx, nc_expr *e)
{
    if (e->u.agg.nelems == 0) return op_zero(e->type, e->span); /* `{}` */
    nc_rvalue rv = {0};
    rv.kind = NC_RV_AGGREGATE;
    rv.nelems = e->u.agg.nelems;
    rv.union_field = e->u.agg.union_field;
    rv.elems = nc_arena_alloc(&lx->s->arena, rv.nelems * sizeof *rv.elems);
    for (uint32_t i = 0; i < rv.nelems; i++) {
        nc_expr *x = e->u.agg.elems[i];
        if (x) {
            rv.elems[i] = lower_expr(lx, x);
            continue;
        }
        const nc_type *mt = nc_ty_is_array(e->type) ? e->type->elem : e->type->agg->fields[i].type;
        rv.elems[i] = op_zero(mt, e->span);
    }
    return emit_rv(lx, e->type, rv, e->span);
}

static nc_operand lower_unary(lowerer *lx, nc_expr *e)
{
    nc_unop op = e->u.unary.op;
    const nc_type *t = e->type;
    switch (op) {
    case NC_UN_PLUS: return lower_expr(lx, e->u.unary.operand);
    case NC_UN_NEG: {
        nc_operand x = lower_expr(lx, e->u.unary.operand);
        if (nc_ty_is_float(t)) return emit_unary(lx, NC_NU_NEG_FLOAT, x, t, e->span);
        return emit_checked(lx, NC_NB_SUB, op_int(0, t, e->span), x, t, NULL, "integer overflow in negation", e->span);
    }
    case NC_UN_NOT: return emit_unary(lx, NC_NU_NOT, lower_expr(lx, e->u.unary.operand), t, e->span);
    case NC_UN_BITNOT: return emit_unary(lx, NC_NU_BITNOT, lower_expr(lx, e->u.unary.operand), t, e->span);
    case NC_UN_PREINC: case NC_UN_PREDEC: case NC_UN_POSTINC: case NC_UN_POSTDEC: {
        nc_place v = lower_place(lx, e->u.unary.operand);
        bool inc = op == NC_UN_PREINC || op == NC_UN_POSTINC;
        bool post = op == NC_UN_POSTINC || op == NC_UN_POSTDEC;
        nc_operand cur = op_place(v, e->span);
        nc_operand old = cur;
        if (post) {
            nc_rvalue use = {0};
            use.kind = NC_RV_USE;
            use.a = cur;
            old = emit_rv(lx, t, use, e->span);
        }
        if (nc_ty_is_ptr(t)) {
            nc_rvalue rv = {0};
            rv.kind = NC_RV_PTR_OFFSET;
            rv.binop = inc ? NC_NB_ADD : NC_NB_SUB;
            rv.a = cur;
            rv.b = op_int(1, nc_ty_prim(NC_PRIM_ISIZE), e->span);
            assign_place(lx, v, rv, e->span);
        } else if (nc_ty_is_integer(t)) {
            emit_checked(lx, inc ? NC_NB_ADD : NC_NB_SUB, cur, op_int(1, t, e->span), t, &v,
                         inc ? "integer overflow in '++'" : "integer overflow in '--'", e->span);
        } else {
            nc_cval one = {0};
            one.kind = NC_CV_FLOAT;
            one.f = 1.0;
            nc_rvalue rv = {0};
            rv.kind = NC_RV_BINARY;
            rv.binop = inc ? NC_NB_ADD : NC_NB_SUB;
            rv.a = cur;
            rv.b = op_const(one, t, e->span);
            assign_place(lx, v, rv, e->span);
        }
        return post ? old : op_place(v, e->span);
    }
    default: nc_ice("nir lowering", "unary operator not supported in this phase");
    }
}

static nc_operand lower_cond_expr(lowerer *lx, nc_expr *e)
{
    nc_local_id res = new_local(lx, e->type, 0, e->span, false);
    nc_operand c = lower_expr(lx, e->u.cond.cond);
    nc_block_id then_b = new_block(lx), else_b = new_block(lx), join = new_block(lx);
    nc_terminator t = {0};
    t.kind = NC_TERM_BRANCH;
    t.cond = c;
    t.target = then_b;
    t.other = else_b;
    t.span = e->span;
    terminate(lx, t);
    nc_rvalue use = {0};
    use.kind = NC_RV_USE;
    switch_to(lx, then_b);
    use.a = lower_expr(lx, e->u.cond.then_e);
    assign(lx, res, use, e->span);
    goto_block(lx, join, e->span);
    switch_to(lx, else_b);
    use.a = lower_expr(lx, e->u.cond.else_e);
    assign(lx, res, use, e->span);
    goto_block(lx, join, e->span);
    switch_to(lx, join);
    return op_local(lx, res, e->span);
}

static nc_nir_fn *nir_fn_of(lowerer *lx, const nc_fn_info *info)
{
    for (uint32_t i = 0; i < lx->m->nfns; i++)
        if (lx->m->fns[i]->info == info) return lx->m->fns[i];
    nc_ice("nir lowering", "call to a function without NIR");
}

static nc_operand lower_call(lowerer *lx, nc_expr *e)
{
    nc_nir_fn *callee = nir_fn_of(lx, e->u.call.callee->binding);
    nc_operand *args = e->u.call.nargs ? nc_xcalloc(e->u.call.nargs, sizeof *args) : NULL;
    for (uint32_t i = 0; i < e->u.call.nargs; i++) args[i] = lower_expr(lx, e->u.call.args[i]); /* left to right */
    nc_terminator t = {0};
    t.kind = NC_TERM_CALL;
    t.callee = callee;
    t.args = args;
    t.nargs = e->u.call.nargs;
    t.span = e->span;
    t.dest = nc_ty_is_void(e->type) ? UINT32_MAX : new_local(lx, e->type, 0, e->span, false);
    nc_block_id next = new_block(lx);
    t.target = next;
    terminate(lx, t);
    switch_to(lx, next);
    if (t.dest == UINT32_MAX) return op_bool(false, e->span); /* void: never used as a value */
    return op_local(lx, t.dest, e->span);
}

/* The right operand is evaluated first, then the place (as in Rust); a
 * compound assignment evaluates the place once. */
static nc_operand lower_assign(lowerer *lx, nc_expr *e)
{
    nc_operand r = lower_expr(lx, e->u.binary.rhs);
    nc_place v = lower_place(lx, e->u.binary.lhs);
    if (e->u.binary.op == NC_BIN_NONE) copy_to(lx, v, r, e->span);
    else lower_binop(lx, e->u.binary.op, op_place(v, e->span), r, v.type, &v, e->span);
    return op_bool(false, e->span); /* assignments have type void */
}

static nc_operand lower_expr(lowerer *lx, nc_expr *e)
{
    if (e->cval.kind != NC_CV_NONE) {
        if (nc_ty_is_untyped(e->type)) nc_ice("nir lowering", "untyped constant reached lowering");
        return op_const(e->cval, e->type, e->span);
    }
    switch (e->kind) {
    case NC_E_NAME:
    case NC_E_MEMBER:
    case NC_E_ARROW:
    case NC_E_INDEX: return op_place(lower_place(lx, e), e->span);
    case NC_E_NULL: return op_zero(e->type, e->span);
    case NC_E_CSTRING: {
        nc_operand o = {0};
        o.kind = NC_OP_CSTRING;
        o.str = e->u.str;
        o.type = e->type;
        o.span = e->span;
        return o;
    }
    case NC_E_BUILTIN: {
        nc_operand x = lower_expr(lx, e->u.builtin.operand);
        nc_cast_kind k = e->u.builtin.kind == NC_BUILTIN_ADDR            ? NC_CAST_PTR_TO_INT
                         : e->u.builtin.kind == NC_BUILTIN_PTR_FROM_ADDR ? NC_CAST_INT_TO_PTR
                                                                         : NC_CAST_NOP;
        return emit_cast(lx, k, x, e->type, e->span);
    }
    case NC_E_AGGREGATE: return lower_aggregate(lx, e);
    case NC_E_UNARY:
        if (e->u.unary.op == NC_UN_DEREF) return op_place(lower_place(lx, e), e->span);
        if (e->u.unary.op == NC_UN_ADDR || e->u.unary.op == NC_UN_ADDR_MUT) {
            nc_rvalue rv = {0};
            rv.kind = NC_RV_ADDR;
            rv.a = op_place(lower_place(lx, e->u.unary.operand), e->span);
            return emit_rv(lx, e->type, rv, e->span);
        }
        return lower_unary(lx, e);
    case NC_E_BINARY:
        if (e->u.binary.op == NC_BIN_AND || e->u.binary.op == NC_BIN_OR) return lower_logical(lx, e);
        {
            nc_operand a = lower_expr(lx, e->u.binary.lhs);
            nc_operand b = lower_expr(lx, e->u.binary.rhs);
            return lower_binop(lx, e->u.binary.op, a, b, e->type, NULL, e->span);
        }
    case NC_E_ASSIGN: return lower_assign(lx, e);
    case NC_E_COND: return lower_cond_expr(lx, e);
    case NC_E_CALL: return lower_call(lx, e);
    case NC_E_CONVERT: return lower_convert(lx, lower_expr(lx, e->u.convert.operand), e->type, e->span);
    case NC_E_IMPLICIT: {
        nc_operand x = lower_expr(lx, e->u.convert.operand);
        nc_cast_kind k = nc_ty_is_float(e->type) ? NC_CAST_FLOAT_RESIZE
                         : nc_ty_is_ptr(e->type) ? NC_CAST_NOP /* T* -> const T* */
                                                 : NC_CAST_INT_RESIZE;
        return emit_cast(lx, k, x, e->type, e->span); /* lossless by construction */
    }
    default: nc_ice("nir lowering", "expression kind not supported in this phase");
    }
}

/* ---- statements -------------------------------------------------------------------- */

static void lower_stmt(lowerer *lx, nc_stmt *s);

static void bind_var(lowerer *lx, const nc_var *v)
{
    var_map vm = {v, new_local(lx, v->type, v->name, v->span, true)};
    nc_vec_push(&lx->vars, vm);
}

static void lower_decl(lowerer *lx, nc_var_decl *d)
{
    for (uint32_t i = 0; i < d->ndecls; i++) {
        nc_declarator *dc = &d->decls[i];
        nc_operand v = {0};
        bool has_init = dc->init && dc->init->kind != NC_INIT_LIST;
        if (dc->init && dc->init->kind == NC_INIT_EXPR) {
            v = lower_expr(lx, dc->init->expr); /* before the variable exists (D-038) */
        } else if (has_init) { /* uninit: counts as initialised (NOVAC_MEMORY_MODEL.md §5) */
            v.kind = NC_OP_UNDEF;
            v.type = dc->var->type;
            v.span = dc->init->span;
        }
        bind_var(lx, dc->var);
        if (has_init) {
            nc_rvalue use = {0};
            use.kind = NC_RV_USE;
            use.a = v;
            assign(lx, local_of(lx, dc->var), use, dc->span);
        }
    }
}

static void branch_on(lowerer *lx, nc_expr *cond, nc_block_id yes, nc_block_id no, nc_span span)
{
    nc_terminator t = {0};
    t.kind = NC_TERM_BRANCH;
    t.cond = lower_expr(lx, cond);
    t.target = yes;
    t.other = no;
    t.span = span;
    terminate(lx, t);
}

static void lower_loop_body(lowerer *lx, nc_stmt *body, nc_block_id brk, nc_block_id cont)
{
    loop_targets lt = {brk, cont};
    nc_vec_push(&lx->loops, lt);
    size_t mark = lx->vars.len;
    lower_stmt(lx, body);
    lx->vars.len = mark;
    lx->loops.len--;
}

/* switch (D-025): one block per case; an empty case and `fallthrough`
 * continue into the next case's block. */
static void lower_switch(lowerer *lx, nc_stmt *s)
{
    nc_operand v = lower_expr(lx, s->u.switch_.expr);
    uint32_t ncases = s->u.switch_.ncases;
    nc_case *cs = s->u.switch_.cases;
    nc_block_id exit = new_block(lx);
    nc_block_id *bodies = nc_xcalloc(ncases ? ncases : 1, sizeof *bodies);
    uint32_t nvalues = 0;
    nc_block_id otherwise = exit;
    for (uint32_t i = 0; i < ncases; i++) {
        bodies[i] = new_block(lx);
        nvalues += cs[i].nvalues;
        if (cs[i].is_default) otherwise = bodies[i];
    }
    nc_terminator t = {0};
    t.kind = NC_TERM_SWITCH;
    t.span = s->span;
    t.cond = v;
    t.ncases = nvalues;
    t.case_values = nc_arena_alloc(&lx->s->arena, (nvalues ? nvalues : 1) * sizeof *t.case_values);
    t.case_targets = nc_arena_alloc(&lx->s->arena, (nvalues ? nvalues : 1) * sizeof *t.case_targets);
    uint32_t k = 0;
    for (uint32_t i = 0; i < ncases; i++)
        for (uint32_t j = 0; j < cs[i].nvalues; j++) {
            t.case_values[k] = cs[i].values[j]->cval;
            t.case_targets[k++] = bodies[i];
        }
    const nc_type *vt = s->u.switch_.expr->type;
    bool has_default = otherwise != exit;
    if (!has_default && (nc_ty_is_enum(vt) || nc_ty_is_bool(vt)) && nvalues) {
        /* Exhaustive (checked by sema, E4071). A bool cannot hold another
         * value; an enum can only through unsafe code, which panics here. */
        bool all_bool = nc_ty_is_bool(vt);
        bool covered = true;
        if (all_bool) {
            bool seen[2] = {false, false};
            for (uint32_t q = 0; q < nvalues; q++) seen[t.case_values[q].b] = true;
            covered = seen[0] && seen[1];
        }
        if (covered) {
            otherwise = new_block(lx);
            nc_block_id save = lx->cur;
            switch_to(lx, otherwise);
            if (!all_bool) emit_assert(lx, op_bool(false, s->span), true, "invalid enum value in switch", s->span);
            nc_terminator u = {0};
            u.kind = NC_TERM_UNREACHABLE;
            u.span = s->span;
            terminate(lx, u);
            switch_to(lx, save);
        }
    }
    t.other = otherwise;
    terminate(lx, t);

    loop_targets lt = {exit, lx->loops.len ? lx->loops.data[lx->loops.len - 1].cont : NC_NO_BLOCK};
    nc_vec_push(&lx->loops, lt);
    nc_block_id saved_ft = lx->fallthrough;
    for (uint32_t i = 0; i < ncases; i++) {
        nc_block_id next = i + 1 < ncases ? bodies[i + 1] : exit;
        lx->fallthrough = next;
        switch_to(lx, bodies[i]);
        size_t mark = lx->vars.len;
        for (uint32_t j = 0; j < cs[i].nstmts; j++) lower_stmt(lx, cs[i].stmts[j]);
        lx->vars.len = mark;
        goto_block(lx, next, cs[i].span); /* an empty case, or the end of the last one */
    }
    lx->fallthrough = saved_ft;
    lx->loops.len--;
    free(bodies);
    switch_to(lx, exit);
}

static void lower_stmt(lowerer *lx, nc_stmt *s)
{
    switch (s->kind) {
    case NC_S_BLOCK: {
        size_t mark = lx->vars.len;
        for (uint32_t i = 0; i < s->u.block.count; i++) lower_stmt(lx, s->u.block.stmts[i]);
        lx->vars.len = mark;
        break;
    }
    case NC_S_DECL: lower_decl(lx, &s->u.decl); break;
    case NC_S_EXPR: lower_expr(lx, s->u.expr); break;
    case NC_S_EMPTY: break;
    case NC_S_IF: {
        nc_block_id then_b = new_block(lx), join = new_block(lx);
        nc_block_id else_b = s->u.if_.else_s ? new_block(lx) : join;
        branch_on(lx, s->u.if_.cond, then_b, else_b, s->span);
        size_t mark = lx->vars.len;
        switch_to(lx, then_b);
        lower_stmt(lx, s->u.if_.then_s);
        lx->vars.len = mark;
        goto_block(lx, join, s->span);
        if (s->u.if_.else_s) {
            switch_to(lx, else_b);
            lower_stmt(lx, s->u.if_.else_s);
            lx->vars.len = mark;
            goto_block(lx, join, s->span);
        }
        switch_to(lx, join);
        break;
    }
    case NC_S_WHILE: {
        nc_block_id head = new_block(lx), body = new_block(lx), exit = new_block(lx);
        goto_block(lx, head, s->span);
        switch_to(lx, head);
        branch_on(lx, s->u.while_.cond, body, exit, s->span);
        switch_to(lx, body);
        lower_loop_body(lx, s->u.while_.body, exit, head);
        goto_block(lx, head, s->span);
        switch_to(lx, exit);
        break;
    }
    case NC_S_DO: {
        nc_block_id body = new_block(lx), cond = new_block(lx), exit = new_block(lx);
        goto_block(lx, body, s->span);
        switch_to(lx, body);
        lower_loop_body(lx, s->u.while_.body, exit, cond);
        goto_block(lx, cond, s->span);
        switch_to(lx, cond);
        branch_on(lx, s->u.while_.cond, body, exit, s->span);
        switch_to(lx, exit);
        break;
    }
    case NC_S_FOR: {
        size_t mark = lx->vars.len;
        if (s->u.for_.init_decl) lower_decl(lx, &s->u.for_.init_decl->u.decl);
        for (uint32_t i = 0; i < s->u.for_.ninit; i++) lower_expr(lx, s->u.for_.init_exprs[i]);
        nc_block_id head = new_block(lx), body = new_block(lx), step = new_block(lx), exit = new_block(lx);
        goto_block(lx, head, s->span);
        switch_to(lx, head);
        if (s->u.for_.cond) branch_on(lx, s->u.for_.cond, body, exit, s->span);
        else goto_block(lx, body, s->span);
        switch_to(lx, body);
        lower_loop_body(lx, s->u.for_.body, exit, step);
        goto_block(lx, step, s->span);
        switch_to(lx, step);
        for (uint32_t i = 0; i < s->u.for_.nstep; i++) lower_expr(lx, s->u.for_.step[i]);
        goto_block(lx, head, s->span);
        switch_to(lx, exit);
        lx->vars.len = mark;
        break;
    }
    case NC_S_BREAK:
    case NC_S_CONTINUE: {
        loop_targets lt = lx->loops.data[lx->loops.len - 1];
        goto_block(lx, s->kind == NC_S_BREAK ? lt.brk : lt.cont, s->span);
        break;
    }
    case NC_S_RETURN: {
        nc_terminator t = {0};
        t.kind = NC_TERM_RETURN;
        t.span = s->span;
        if (s->u.ret) {
            t.value = lower_expr(lx, s->u.ret);
            t.has_value = true;
        }
        terminate(lx, t);
        break;
    }
    case NC_S_SWITCH: lower_switch(lx, s); break;
    case NC_S_UNSAFE: lower_stmt(lx, s->u.unsafe_body); break;
    case NC_S_FALLTHROUGH: goto_block(lx, lx->fallthrough, s->span); break;
    case NC_S_UNSUPPORTED: nc_ice("nir lowering", "unsupported statement reached lowering");
    }
}

/* ---- functions and modules ---------------------------------------------------------- */

static void lower_function(lowerer *lx, nc_nir_fn *f)
{
    nc_decl *d = f->info->decl;
    lx->fn = f;
    lx->vars.len = 0;
    for (uint32_t i = 0; i < d->u.fn.nparams; i++) {
        bind_var(lx, d->u.fn.params[i].var);
        f->locals[i].is_param = true;
    }
    f->nparams = d->u.fn.nparams;
    lx->cur = new_block(lx);
    lower_stmt(lx, d->u.fn.body);
    nc_nir_block *last = &f->blocks[lx->cur];
    if (last->term.kind == NC_TERM_NONE) {
        if (nc_ty_is_void(f->ret)) {
            nc_terminator t = {0};
            t.kind = NC_TERM_RETURN;
            t.span = d->span;
            terminate(lx, t);
        } else {
            /* Semantic analysis proved this point unreachable (E4060). Should
             * that ever be wrong, panic rather than invoke undefined behaviour. */
            emit_assert(lx, op_bool(false, d->span), true, "control reached the end of a function without returning a value",
                        d->span);
            nc_terminator t = {0};
            t.kind = NC_TERM_UNREACHABLE;
            t.span = d->span;
            terminate(lx, t);
        }
    }
    /* Blocks opened after a terminator and never used still need one. */
    for (uint32_t b = 0; b < f->nblocks; b++)
        if (f->blocks[b].term.kind == NC_TERM_NONE) f->blocks[b].term.kind = NC_TERM_UNREACHABLE;
}

static void module_name(nc_session *s, nc_module_ast *m, nc_path *out)
{
    if (m->has_module_decl) {
        *out = m->name;
        return;
    }
    /* The file stem; semantic analysis checked it is an identifier. */
    const char *path = nc_source_get(&s->sources, m->file)->name;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    out->count = 1;
    out->parts = nc_arena_alloc(&s->arena, sizeof *out->parts);
    out->parts[0] = nc_intern(&s->strtab, base, len);
    out->span = NC_NO_SPAN;
}

nc_nir_module *nc_nir_lower(nc_session *s, nc_module_ast *m)
{
    nc_nir_module *nm = nc_xcalloc(1, sizeof *nm);
    nm->file = m->file;
    module_name(s, m, &nm->name);

    uint32_t nfn = 0;
    for (uint32_t i = 0; i < m->ndecls; i++)
        if (m->decls[i]->kind == NC_D_FUNCTION && m->decls[i]->u.fn.info) nfn++;
    nm->fns = nc_xcalloc(nfn ? nfn : 1, sizeof *nm->fns);
    uint32_t nst = 0;
    for (uint32_t i = 0; i < m->ndecls; i++)
        if (m->decls[i]->kind == NC_D_STATIC && m->decls[i]->sema) nst++;
    nm->statics = nc_xcalloc(nst ? nst : 1, sizeof *nm->statics);
    for (uint32_t i = 0; i < m->ndecls; i++) {
        nc_decl *d = m->decls[i];
        if (d->kind != NC_D_STATIC || !d->sema) continue;
        nc_static_info *st = d->sema;
        nc_nir_static *ns = &nm->statics[nm->nstatics++];
        ns->info = st;
        ns->name = st->name;
        ns->span = st->span;
        ns->type = st->type;
        ns->is_const = st->is_const;
        ns->is_public = st->is_public;
        ns->init = st->init;
        ns->export_symbol = st->export_symbol;
        if (st->export_symbol) ns->is_public = true;
    }
    for (uint32_t i = 0; i < m->ndecls; i++) {
        nc_decl *d = m->decls[i];
        if (d->kind != NC_D_FUNCTION || !d->u.fn.info) continue;
        nc_nir_fn *f = nc_xcalloc(1, sizeof *f);
        f->info = d->u.fn.info;
        f->name = d->u.fn.name;
        f->span = d->u.fn.name_span;
        f->is_public = d->is_public;
        f->ret = f->info->ret;
        f->nparams = d->u.fn.nparams;
        f->is_c = f->info->is_c;
        f->is_import = f->info->is_import;
        if (f->is_c) f->is_public = true; /* C symbols are external */
        nm->fns[nm->nfns++] = f;
    }

    lowerer lx;
    memset(&lx, 0, sizeof lx);
    lx.s = s;
    lx.m = nm;
    for (uint32_t i = 0; i < nm->nfns; i++)
        if (!nm->fns[i]->is_import) lower_function(&lx, nm->fns[i]);
    nc_vec_free(&lx.vars);
    nc_vec_free(&lx.loops);
    return nm;
}
