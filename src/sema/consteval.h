/* Compile-time integer and float arithmetic for constant folding
 * (NOVAC_TYPE_SYSTEM.md §9). Integers are sign + 64-bit magnitude, so every
 * i64 and u64 value is representable, and untyped constants may range over
 * [-(2^64-1), 2^64-1]. Every operation reports overflow instead of wrapping
 * silently. */
#ifndef NC_SEMA_CONSTEVAL_H
#define NC_SEMA_CONSTEVAL_H

#include "sema/types.h"

nc_cval nc_cv_int(bool neg, uint64_t mag);
nc_cval nc_cv_float(double f);
nc_cval nc_cv_bool(bool b);

/* Exact arithmetic within [-(2^64-1), 2^64-1]; false on overflow. */
bool nc_cv_add(nc_cval a, nc_cval b, nc_cval *out);
bool nc_cv_sub(nc_cval a, nc_cval b, nc_cval *out);
bool nc_cv_mul(nc_cval a, nc_cval b, nc_cval *out);
/* Truncating division and remainder; b must be non-zero. */
void nc_cv_divmod(nc_cval a, nc_cval b, nc_cval *quot, nc_cval *rem);
nc_cval nc_cv_neg(nc_cval a);
int nc_cv_cmp(nc_cval a, nc_cval b); /* -1, 0, 1 */

/* Does the integer value fit the typed integer type `t`? */
bool nc_cv_fits(nc_cval v, const nc_type *t);

/* Two's-complement bit pattern of `v` (any value) truncated to `t`'s width,
 * and back. Used by wrapping and bitwise operations. */
uint64_t nc_cv_bits(nc_cval v, const nc_type *t);
nc_cval nc_cv_from_bits(uint64_t bits, const nc_type *t);

/* Is the integer exactly representable as f32/f64? */
bool nc_cv_int_exact_in_float(nc_cval v, const nc_type *ft);

double nc_cv_to_double(nc_cval v); /* INT or FLOAT */

/* Human-readable value for diagnostics ("-128", "300"). */
void nc_cv_format(nc_cval v, char *buf, size_t n);

#endif
