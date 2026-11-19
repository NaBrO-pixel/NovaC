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
    bool *param_const;
    uint32_t nparams;
    bool is_c;        /* extern "C": the C calling convention and an unmangled symbol */
    bool is_import;   /* extern "C" without a body: defined in C, unsafe to call */
    bool variadic;    /* `...` (imports only) */
    nc_sym symbol;    /* is_c: the linker symbol (@link_name, @export, or the name) */
} nc_fn_info;

/* A module-level `static`. Its initialiser is a checked constant
 * expression (literals, constants and aggregates of them). */
typedef struct nc_static_info {
    nc_decl *decl;
    nc_sym name;
    nc_span span;
    const nc_type *type;
    bool is_const;   /* `static const T x`: immutable, readable from safe code */
    bool is_public;
    nc_expr *init;   /* NULL until resolved */
    nc_sym export_symbol; /* @export("sym"): an unmangled external symbol, or 0 */
    nc_layout_state state;
} nc_static_info;

/* A `constexpr`: a compile-time constant without storage. */
typedef struct nc_const_info {
    nc_decl *decl;
    const nc_type *type;
    nc_cval value;
    nc_layout_state state;
} nc_const_info;

/* A `typedef`. */
typedef struct nc_typedef_info {
    nc_decl *decl;
    const nc_type *type;
    nc_layout_state state;
} nc_typedef_info;

typedef enum nc_module_sym_kind {
    NC_MSYM_FN,
    NC_MSYM_AGGREGATE, /* struct or union: info is nc_aggregate* */
    NC_MSYM_ENUM,      /* info is nc_enum_type* */
    NC_MSYM_TYPEDEF,
    NC_MSYM_STATIC,
    NC_MSYM_CONST,
} nc_module_sym_kind;

typedef struct nc_module_sym {
    nc_sym name;
    nc_module_sym_kind kind;
    nc_decl *decl;
    void *info;
} nc_module_sym;

#endif
