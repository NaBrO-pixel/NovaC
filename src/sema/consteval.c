#include "sema/consteval.h"

#include <math.h>
#include <stdio.h>

nc_cval nc_cv_int(bool neg, uint64_t mag)
{
    nc_cval v = {0};
    v.kind = NC_CV_INT;
    v.neg = neg && mag != 0;
    v.mag = mag;
    return v;
}

nc_cval nc_cv_float(double f)
{
    nc_cval v = {0};
    v.kind = NC_CV_FLOAT;
    v.f = f;
    return v;
}

nc_cval nc_cv_bool(bool b)
{
    nc_cval v = {0};
    v.kind = NC_CV_BOOL;
    v.b = b;
    return v;
}

bool nc_cv_add(nc_cval a, nc_cval b, nc_cval *out)
{
    if (a.neg == b.neg) {
        if (a.mag > UINT64_MAX - b.mag) return false;
        *out = nc_cv_int(a.neg, a.mag + b.mag);
    } else if (a.mag >= b.mag) {
        *out = nc_cv_int(a.neg, a.mag - b.mag);
    } else {
        *out = nc_cv_int(b.neg, b.mag - a.mag);
    }
    return true;
}

nc_cval nc_cv_neg(nc_cval a)
{
    return nc_cv_int(!a.neg, a.mag);
}

bool nc_cv_sub(nc_cval a, nc_cval b, nc_cval *out)
{
    return nc_cv_add(a, nc_cv_neg(b), out);
}

bool nc_cv_mul(nc_cval a, nc_cval b, nc_cval *out)
{
    if (a.mag != 0 && b.mag > UINT64_MAX / a.mag) return false;
    *out = nc_cv_int(a.neg != b.neg, a.mag * b.mag);
    return true;
}

void nc_cv_divmod(nc_cval a, nc_cval b, nc_cval *quot, nc_cval *rem)
{
    *quot = nc_cv_int(a.neg != b.neg, a.mag / b.mag);
    *rem = nc_cv_int(a.neg, a.mag % b.mag);
}

int nc_cv_cmp(nc_cval a, nc_cval b)
{
    if (a.neg != b.neg) return a.neg ? -1 : 1;
    int c = a.mag < b.mag ? -1 : a.mag > b.mag ? 1 : 0;
    return a.neg ? -c : c;
}

bool nc_cv_fits(nc_cval v, const nc_type *t)
{
    unsigned bits = nc_ty_bits(t);
    if (bits == 0) return false;
    if (nc_ty_is_signed(t)) {
        uint64_t limit_pos = (UINT64_C(1) << (bits - 1)) - 1;
        uint64_t limit_neg = UINT64_C(1) << (bits - 1);
        return v.neg ? v.mag <= limit_neg : v.mag <= limit_pos;
    }
    if (v.neg) return false;
    return bits == 64 || v.mag <= (UINT64_C(1) << bits) - 1;
}

static uint64_t mask(unsigned bits)
{
    return bits >= 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}

uint64_t nc_cv_bits(nc_cval v, const nc_type *t)
{
    uint64_t x = v.neg ? (uint64_t)0 - v.mag : v.mag;
    return x & mask(nc_ty_bits(t));
}

nc_cval nc_cv_from_bits(uint64_t bits, const nc_type *t)
{
    unsigned w = nc_ty_bits(t);
    bits &= mask(w);
    if (nc_ty_is_signed(t) && w > 0 && (bits >> (w - 1)) & 1) {
        uint64_t mag = ((~bits) & mask(w)) + 1; /* two's complement magnitude */
        return nc_cv_int(true, mag);
    }
    return nc_cv_int(false, bits);
}

bool nc_cv_int_exact_in_float(nc_cval v, const nc_type *ft)
{
    if (ft->prim == NC_PRIM_F32) {
        float f = (float)v.mag;
        /* 2^64 is representable as a float but not as uint64_t. */
        if ((double)f >= 18446744073709551616.0) return false;
        return (uint64_t)f == v.mag;
    }
    double d = (double)v.mag;
    if (d >= 18446744073709551616.0) return false;
    return (uint64_t)d == v.mag;
}

double nc_cv_to_double(nc_cval v)
{
    if (v.kind == NC_CV_FLOAT) return v.f;
    double d = (double)v.mag;
    return v.neg ? -d : d;
}

void nc_cv_format(nc_cval v, char *buf, size_t n)
{
    switch (v.kind) {
    case NC_CV_INT: snprintf(buf, n, "%s%llu", v.neg ? "-" : "", (unsigned long long)v.mag); break;
    case NC_CV_FLOAT: snprintf(buf, n, "%.17g", v.f); break;
    case NC_CV_BOOL: snprintf(buf, n, "%s", v.b ? "true" : "false"); break;
    default: snprintf(buf, n, "?"); break;
    }
}
