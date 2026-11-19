/* C calling-convention lowering (NOVAC_ABI.md §5); see abi.h. */
#include "target/abi.h"

#include <string.h>

/* ---- scalars ------------------------------------------------------------------------------ */

typedef enum leaf_kind { LEAF_INT, LEAF_F32, LEAF_F64 } leaf_kind;

typedef struct leaf {
    leaf_kind kind;
    unsigned size, offset;
} leaf;

static leaf_kind scalar_kind(const nc_type *t)
{
    if (nc_ty_is_float(t)) return t->prim == NC_PRIM_F32 ? LEAF_F32 : LEAF_F64;
    return LEAF_INT;
}

static nc_abi_ext scalar_ext(const nc_target *tg, const nc_type *t)
{
    t = nc_ty_repr(t);
    if (tg->c_abi == NC_CABI_AAPCS64 || nc_ty_is_float(t) || nc_ty_is_ptr(t)) return NC_EXT_NONE;
    if (nc_ty_is_bool(t)) return NC_EXT_ZERO;
    unsigned bits = nc_ty_bits(t);
    if (bits < 32) return nc_ty_is_signed(t) ? NC_EXT_SIGN : NC_EXT_ZERO;
    /* RV64 keeps every 32-bit value sign-extended in its 64-bit register,
     * whatever its signedness. */
    if (bits == 32 && tg->c_abi == NC_CABI_LP64D) return NC_EXT_SIGN;
    return NC_EXT_NONE;
}

static nc_abi_arg direct(const nc_target *tg, const nc_type *t)
{
    nc_abi_arg a;
    memset(&a, 0, sizeof a);
    a.kind = NC_ABI_DIRECT;
    a.ext = scalar_ext(tg, t);
    return a;
}

/* ---- aggregates as scalar leaves ------------------------------------------------------------ */

#define MAX_LEAVES 64

typedef struct leaves {
    leaf l[MAX_LEAVES];
    unsigned n;
    bool has_union;
    bool overflow;
} leaves;

static void flatten(const nc_type *t, unsigned base, leaves *out)
{
    if (out->overflow) return;
    switch (t->kind) {
    case NC_TY_ARRAY: {
        unsigned es = (unsigned)nc_ty_size(t->elem);
        for (uint64_t i = 0; i < t->len && !out->overflow; i++) flatten(t->elem, base + (unsigned)i * es, out);
        return;
    }
    case NC_TY_STRUCT:
    case NC_TY_UNION:
        if (t->kind == NC_TY_UNION) out->has_union = true;
        for (uint32_t i = 0; i < t->agg->nfields; i++)
            flatten(t->agg->fields[i].type, base + (unsigned)t->agg->fields[i].offset, out);
        return;
    default:
        if (out->n == MAX_LEAVES) {
            out->overflow = true;
            return;
        }
        out->l[out->n++] = (leaf){scalar_kind(t), (unsigned)nc_ty_size(t), base};
        return;
    }
}

static nc_abi_arg indirect(bool byval)
{
    nc_abi_arg a;
    memset(&a, 0, sizeof a);
    a.kind = NC_ABI_INDIRECT;
    a.byval = byval;
    return a;
}

static nc_abi_arg coerce(void)
{
    nc_abi_arg a;
    memset(&a, 0, sizeof a);
    a.kind = NC_ABI_COERCE;
    return a;
}

static void add_piece(nc_abi_arg *a, nc_abi_piece_kind k, unsigned size, unsigned offset)
{
    a->pieces[a->npieces++] = (nc_abi_piece){k, size, offset};
}

/* ---- x86-64 System V (psABI §3.2.3) ------------------------------------------------------------ */

typedef struct regs {
    unsigned gpr, fpr; /* still free */
} regs;

/* Classifies an aggregate into at most two eightbytes. Sets *ngpr / *nfpr
 * to the registers it needs; returns false for MEMORY. */
static bool sysv_classify(const nc_type *t, nc_abi_arg *out, unsigned *ngpr, unsigned *nfpr)
{
    uint64_t size = nc_ty_size(t);
    if (size > 16 || size == 0) return false;
    leaves lv = {0};
    flatten(t, 0, &lv);
    if (lv.overflow) return false;
    enum { NONE, INT, SSE } cls[2] = {NONE, NONE};
    bool all_f32[2] = {true, true};
    for (unsigned i = 0; i < lv.n; i++) {
        leaf *l = &lv.l[i];
        if (l->size && l->offset % l->size) return false; /* unaligned field: MEMORY */
        unsigned e = l->offset / 8;
        if (l->kind == LEAF_INT) cls[e] = INT;
        else if (cls[e] == NONE) cls[e] = SSE;
        if (l->kind != LEAF_F32) all_f32[e] = false;
    }
    *out = coerce();
    *ngpr = *nfpr = 0;
    unsigned n = (unsigned)((size + 7) / 8);
    for (unsigned e = 0; e < n; e++) {
        unsigned psize = (unsigned)(size - 8 * e < 8 ? size - 8 * e : 8);
        if (cls[e] == SSE) {
            add_piece(out, psize <= 4 ? NC_PIECE_F32 : all_f32[e] ? NC_PIECE_F32X2 : NC_PIECE_F64, psize, 8 * e);
            ++*nfpr;
        } else { /* INT, or padding only (NONE), passed in an integer register */
            add_piece(out, NC_PIECE_INT, psize, 8 * e);
            ++*ngpr;
        }
    }
    return true;
}

static nc_abi_arg sysv_arg(const nc_target *tg, const nc_type *t, regs *r)
{
    if (!nc_ty_is_aggregate(t)) {
        if (nc_ty_is_float(t)) {
            if (r->fpr) r->fpr--;
        } else if (r->gpr) {
            r->gpr--;
        }
        return direct(tg, t);
    }
    nc_abi_arg a;
    unsigned ng, nf;
    if (!sysv_classify(t, &a, &ng, &nf) || ng > r->gpr || nf > r->fpr)
        return indirect(true); /* MEMORY, or not enough registers left: the whole value on the stack */
    r->gpr -= ng;
    r->fpr -= nf;
    return a;
}

static nc_abi_arg sysv_ret(const nc_target *tg, const nc_type *t)
{
    if (!nc_ty_is_aggregate(t)) return direct(tg, t);
    nc_abi_arg a;
    unsigned ng, nf;
    if (!sysv_classify(t, &a, &ng, &nf)) return indirect(false);
    return a;
}

/* ---- AArch64 AAPCS64 ---------------------------------------------------------------------------- */

/* A homogeneous floating-point aggregate of 1-4 members: sets *base and *count. */
static bool aapcs_hfa(const nc_type *t, leaf_kind *base, unsigned *count)
{
    uint64_t size = nc_ty_size(t);
    leaves lv = {0};
    flatten(t, 0, &lv);
    if (lv.overflow || lv.n == 0 || lv.l[0].kind == LEAF_INT) return false;
    leaf_kind b = lv.l[0].kind;
    unsigned bs = b == LEAF_F32 ? 4 : 8;
    if (size % bs || size / bs > 4) return false;
    bool covered[4] = {false, false, false, false};
    for (unsigned i = 0; i < lv.n; i++) {
        if (lv.l[i].kind != b || lv.l[i].offset % bs) return false;
        covered[lv.l[i].offset / bs] = true;
    }
    for (unsigned i = 0; i < size / bs; i++)
        if (!covered[i]) return false;
    *base = b;
    *count = (unsigned)(size / bs);
    return true;
}

static nc_abi_arg aapcs(const nc_target *tg, const nc_type *t, bool is_ret)
{
    if (!nc_ty_is_aggregate(t)) return direct(tg, t);
    leaf_kind b;
    unsigned count;
    nc_abi_arg a = coerce();
    if (aapcs_hfa(t, &b, &count)) {
        for (unsigned i = 0; i < count; i++)
            add_piece(&a, b == LEAF_F32 ? NC_PIECE_F32 : NC_PIECE_F64, b == LEAF_F32 ? 4 : 8, i * (b == LEAF_F32 ? 4 : 8));
        a.as_array = !is_ret;
        a.align_stack = is_ret ? 0 : 8;
        return a;
    }
    uint64_t size = nc_ty_size(t);
    if (size > 16) return indirect(false); /* a pointer to a copy; results through x8 */
    if (size <= 8) {
        add_piece(&a, NC_PIECE_INT, is_ret ? (unsigned)size : 8, 0);
    } else {
        add_piece(&a, NC_PIECE_INT, 8, 0);
        add_piece(&a, NC_PIECE_INT, 8, 8);
        a.as_array = true;
    }
    return a;
}

/* ---- RISC-V LP64D (psABI "Hardware floating-point calling convention") -------------------------- */

/* A struct of one floating-point field, two, or one and one integer of at
 * most XLEN bits passes in registers by field. */
static bool lp64d_fp_struct(const nc_type *t, nc_abi_arg *out, unsigned *ngpr, unsigned *nfpr)
{
    leaves lv = {0};
    flatten(t, 0, &lv);
    if (lv.overflow || lv.has_union || lv.n == 0 || lv.n > 2) return false;
    unsigned nf = 0, ng = 0;
    for (unsigned i = 0; i < lv.n; i++) {
        if (lv.l[i].kind == LEAF_INT) ng++;
        else nf++;
    }
    if (nf == 0) return false; /* integers only: the integer convention */
    *out = coerce();
    for (unsigned i = 0; i < lv.n; i++) {
        leaf *l = &lv.l[i];
        add_piece(out, l->kind == LEAF_INT ? NC_PIECE_INT : l->kind == LEAF_F32 ? NC_PIECE_F32 : NC_PIECE_F64, l->size,
                  l->offset);
    }
    *ngpr = ng;
    *nfpr = nf;
    return true;
}

static nc_abi_arg lp64d_int_convention(const nc_type *t, bool is_ret, regs *r)
{
    uint64_t size = nc_ty_size(t);
    if (size > 16) {
        if (!is_ret && r->gpr) r->gpr--;
        return indirect(false);
    }
    nc_abi_arg a = coerce();
    if (size <= 8) {
        add_piece(&a, NC_PIECE_INT, 8, 0);
        if (!is_ret && r->gpr) r->gpr--;
    } else {
        add_piece(&a, NC_PIECE_INT, 8, 0);
        add_piece(&a, NC_PIECE_INT, 8, 8);
        a.as_array = true;
        if (!is_ret) r->gpr = r->gpr >= 2 ? r->gpr - 2 : 0;
    }
    return a;
}

static nc_abi_arg lp64d(const nc_target *tg, const nc_type *t, bool is_ret, regs *r)
{
    if (!nc_ty_is_aggregate(t)) {
        if (!is_ret) {
            if (nc_ty_is_float(t) && r->fpr) r->fpr--;
            else if (r->gpr) r->gpr--;
        }
        return direct(tg, t);
    }
    nc_abi_arg a;
    unsigned ng, nf;
    if (nc_ty_size(t) <= 16 && lp64d_fp_struct(t, &a, &ng, &nf)) {
        if (is_ret) return a;
        if (nf <= r->fpr && ng <= r->gpr) {
            r->fpr -= nf;
            r->gpr -= ng;
            return a;
        }
    }
    return lp64d_int_convention(t, is_ret, r);
}

/* ---- entry points ------------------------------------------------------------------------------ */

static nc_abi_arg classify_ret(const nc_target *tg, const nc_type *ret)
{
    if (nc_ty_is_void(ret)) {
        nc_abi_arg a;
        memset(&a, 0, sizeof a);
        a.kind = NC_ABI_VOID;
        return a;
    }
    regs none = {0, 0};
    switch (tg->c_abi) {
    case NC_CABI_SYSV_X86_64: return sysv_ret(tg, ret);
    case NC_CABI_AAPCS64: return aapcs(tg, ret, true);
    case NC_CABI_LP64D: return lp64d(tg, ret, true, &none);
    }
    return direct(tg, ret);
}

void nc_abi_classify_fn(const nc_target *tg, const nc_type *ret, const nc_type *const *params, unsigned nparams,
                        nc_abi_fn *out)
{
    out->ret = classify_ret(tg, ret);
    out->nparams = nparams;
    regs r = {tg->c_abi == NC_CABI_SYSV_X86_64 ? 6u : 8u, 8u};
    if (out->ret.kind == NC_ABI_INDIRECT && tg->c_abi != NC_CABI_AAPCS64) r.gpr--; /* AArch64 uses x8 */
    for (unsigned i = 0; i < nparams; i++) {
        switch (tg->c_abi) {
        case NC_CABI_SYSV_X86_64: out->params[i] = sysv_arg(tg, params[i], &r); break;
        case NC_CABI_AAPCS64: out->params[i] = aapcs(tg, params[i], false); break;
        case NC_CABI_LP64D: out->params[i] = lp64d(tg, params[i], false, &r); break;
        }
    }
}

nc_abi_arg nc_abi_classify_variadic(const nc_target *tg, const nc_type *arg)
{
    return direct(tg, arg);
}
