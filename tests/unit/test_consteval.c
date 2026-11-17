#include "unit.h"

#include "sema/consteval.h"

static const nc_type *T(nc_prim p) { return nc_ty_prim(p); }

void test_consteval(void)
{
    /* Range checks at the edges of every integer type. */
    CHECK(nc_cv_fits(nc_cv_int(true, 128), T(NC_PRIM_I8)));
    CHECK(!nc_cv_fits(nc_cv_int(true, 129), T(NC_PRIM_I8)));
    CHECK(nc_cv_fits(nc_cv_int(false, 127), T(NC_PRIM_I8)));
    CHECK(!nc_cv_fits(nc_cv_int(false, 128), T(NC_PRIM_I8)));
    CHECK(nc_cv_fits(nc_cv_int(false, 255), T(NC_PRIM_U8)));
    CHECK(!nc_cv_fits(nc_cv_int(false, 256), T(NC_PRIM_U8)));
    CHECK(!nc_cv_fits(nc_cv_int(true, 1), T(NC_PRIM_U64)));
    CHECK(nc_cv_fits(nc_cv_int(false, UINT64_MAX), T(NC_PRIM_U64)));
    CHECK(!nc_cv_fits(nc_cv_int(false, UINT64_MAX), T(NC_PRIM_I64)));
    CHECK(nc_cv_fits(nc_cv_int(true, UINT64_C(1) << 63), T(NC_PRIM_I64)));
    CHECK(nc_cv_fits(nc_cv_int(true, 0), T(NC_PRIM_U8))); /* -0 is 0 */

    /* Exact arithmetic and its overflow bound. */
    nc_cval r;
    CHECK(nc_cv_add(nc_cv_int(true, 5), nc_cv_int(false, 3), &r) && r.neg && r.mag == 2);
    CHECK(nc_cv_sub(nc_cv_int(false, 3), nc_cv_int(false, 5), &r) && r.neg && r.mag == 2);
    CHECK(!nc_cv_add(nc_cv_int(false, UINT64_MAX), nc_cv_int(false, 1), &r));
    CHECK(nc_cv_mul(nc_cv_int(true, 4), nc_cv_int(true, 4), &r) && !r.neg && r.mag == 16);
    CHECK(!nc_cv_mul(nc_cv_int(false, UINT64_C(1) << 33), nc_cv_int(false, UINT64_C(1) << 33), &r));
    nc_cval q, m;
    nc_cv_divmod(nc_cv_int(true, 7), nc_cv_int(false, 2), &q, &m); /* C99 truncation */
    CHECK(q.neg && q.mag == 3 && m.neg && m.mag == 1);
    CHECK(nc_cv_cmp(nc_cv_int(true, 1), nc_cv_int(false, 0)) < 0);
    CHECK(nc_cv_cmp(nc_cv_int(true, 5), nc_cv_int(true, 2)) < 0);

    /* Two's complement round trips. */
    CHECK(nc_cv_bits(nc_cv_int(true, 1), T(NC_PRIM_I8)) == 0xFF);
    nc_cval back = nc_cv_from_bits(0xFF, T(NC_PRIM_I8));
    CHECK(back.neg && back.mag == 1);
    back = nc_cv_from_bits(0x80, T(NC_PRIM_I8));
    CHECK(back.neg && back.mag == 128);
    back = nc_cv_from_bits(0x1FF, T(NC_PRIM_U8));
    CHECK(!back.neg && back.mag == 0xFF);
    back = nc_cv_from_bits(UINT64_C(1) << 63, T(NC_PRIM_I64));
    CHECK(back.neg && back.mag == UINT64_C(1) << 63);

    /* Floats */
    CHECK(nc_cv_int_exact_in_float(nc_cv_int(false, 16777216), T(NC_PRIM_F32)));
    CHECK(!nc_cv_int_exact_in_float(nc_cv_int(false, 16777217), T(NC_PRIM_F32)));
    CHECK(nc_cv_int_exact_in_float(nc_cv_int(false, UINT64_C(1) << 53), T(NC_PRIM_F64)));
    CHECK(!nc_cv_int_exact_in_float(nc_cv_int(false, (UINT64_C(1) << 53) + 1), T(NC_PRIM_F64)));
    CHECK(!nc_cv_int_exact_in_float(nc_cv_int(false, UINT64_MAX), T(NC_PRIM_F64)));

    /* Implicit widening table (NOVAC_TYPE_SYSTEM.md §5.1). */
    CHECK(nc_ty_widens(T(NC_PRIM_I8), T(NC_PRIM_I64)));
    CHECK(nc_ty_widens(T(NC_PRIM_U8), T(NC_PRIM_I16)));
    CHECK(!nc_ty_widens(T(NC_PRIM_U32), T(NC_PRIM_I32)));
    CHECK(!nc_ty_widens(T(NC_PRIM_I8), T(NC_PRIM_U64)));
    CHECK(nc_ty_widens(T(NC_PRIM_U32), T(NC_PRIM_USIZE)));
    CHECK(!nc_ty_widens(T(NC_PRIM_U64), T(NC_PRIM_USIZE)));
    CHECK(nc_ty_widens(T(NC_PRIM_I32), T(NC_PRIM_ISIZE)));
    CHECK(!nc_ty_widens(T(NC_PRIM_U32), T(NC_PRIM_ISIZE)));
    CHECK(!nc_ty_widens(T(NC_PRIM_USIZE), T(NC_PRIM_U64)));
    CHECK(nc_ty_widens(T(NC_PRIM_F32), T(NC_PRIM_F64)));
    CHECK(!nc_ty_widens(T(NC_PRIM_F64), T(NC_PRIM_F32)));
    CHECK(!nc_ty_widens(T(NC_PRIM_I32), T(NC_PRIM_F64)));
    CHECK(!nc_ty_widens(T(NC_PRIM_BOOL), T(NC_PRIM_I32)));
}
