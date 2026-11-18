/* Symbols produced by semantic analysis and consumed by later stages
 * (NIR lowering). NAME expressions point at these through
 * nc_expr.binding; declarators and parameters through their `var` field;
 * function declarations through `u.fn.info`. */
#ifndef NC_SEMA_SYMBOLS_H
#define NC_SEMA_SYMBOLS_H

#include "sema/types.h"

/* A local variable or parameter. */
typedef struct nc_var {
    nc_sym name;
    const nc_type *type;
    bool is_const;
    bool initializing; /* inside its own initialiser: any use is E3003 */
    nc_span span;
} nc_var;

/* A function's resolved signature. NAME expressions bound to a function
 * point at one of these (binding_kind NC_BIND_FN). */
typedef struct nc_fn_info {
    nc_decl *decl;
    const nc_type *ret;
    const nc_type **params;
    uint32_t nparams;
} nc_fn_info;

#endif
