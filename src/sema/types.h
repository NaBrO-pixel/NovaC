/* Semantic types (NOVAC_TYPE_SYSTEM.md). Phase 2 has only the primitive
 * types plus three internal kinds:
 *   - untyped integer / untyped float: the type of literal constants until
 *     context fixes them (NOVAC_TYPE_SYSTEM.md §5.1, §8);
 *   - error: assigned to erroneous expressions; compatible with everything,
 *     so one mistake produces one diagnostic.
 * Types are canonical: pointer equality is type equality. */
#ifndef NC_SEMA_TYPES_H
#define NC_SEMA_TYPES_H

#include "ast/ast.h"

typedef enum nc_type_kind {
    NC_TY_PRIM,
    NC_TY_UNTYPED_INT,
    NC_TY_UNTYPED_FLOAT,
    NC_TY_ERROR,
} nc_type_kind;

struct nc_type {
    nc_type_kind kind;
    nc_prim prim; /* NC_TY_PRIM */
};

const nc_type *nc_ty_prim(nc_prim p);
const nc_type *nc_ty_untyped_int(void);
const nc_type *nc_ty_untyped_float(void);
const nc_type *nc_ty_error(void);

bool nc_ty_is_error(const nc_type *t);
bool nc_ty_is_untyped(const nc_type *t);
bool nc_ty_is_integer(const nc_type *t); /* typed integer */
bool nc_ty_is_signed(const nc_type *t);  /* typed signed integer */
bool nc_ty_is_float(const nc_type *t);   /* typed float */
bool nc_ty_is_numeric(const nc_type *t); /* typed integer or float */
bool nc_ty_is_bool(const nc_type *t);
bool nc_ty_is_void(const nc_type *t);
unsigned nc_ty_bits(const nc_type *t);   /* integer width; isize/usize are 64 */

/* Lossless implicit conversion between typed types (NOVAC_TYPE_SYSTEM.md
 * §5.1), excluding identity. */
bool nc_ty_widens(const nc_type *from, const nc_type *to);

const char *nc_type_name(const nc_type *t);
void nc_type_print(FILE *out, const nc_type *t);

#endif
