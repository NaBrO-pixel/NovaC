/* Private interface between the semantic analysis translation units. */
#ifndef NC_SEMA_SEMA_INTERNAL_H
#define NC_SEMA_SEMA_INTERNAL_H

#include "sema/consteval.h"
#include "sema/sema.h"
#include "sema/symbols.h"
#include "support/vec.h"

typedef struct nc_scope_entry {
    nc_sym name;
    nc_var *var;
} nc_scope_entry;

typedef struct nc_loop_ctx {
    bool has_break;
    bool has_continue;
} nc_loop_ctx;

typedef struct nc_sema {
    nc_session *s;
    nc_module_ast *m;
    NC_VEC(nc_fn_info *) fns;
    NC_VEC(nc_scope_entry) scope;  /* innermost last */
    NC_VEC(size_t) marks;          /* scope.len at each scope entry */
    NC_VEC(nc_loop_ctx) loops;
    nc_fn_info *cur_fn;
} nc_sema;

/* ---- diagnostics helpers ---- */
#define NC_DIAG(sm) (&(sm)->s->diag)
const char *nc_sema_sym(nc_sema *sm, nc_sym s);

/* ---- scopes ---- */
void nc_scope_push(nc_sema *sm);
void nc_scope_pop(nc_sema *sm);
nc_var *nc_scope_lookup(nc_sema *sm, nc_sym name);
/* Lookup for a use of `name` at `span`: reports E3003 for a variable used
 * in its own initialiser and then returns NULL-equivalent via *poisoned. */
nc_var *nc_scope_use(nc_sema *sm, nc_sym name, nc_span span, bool *poisoned);
/* Declares in the innermost scope; reports E3002 on redeclaration. */
nc_var *nc_scope_declare(nc_sema *sm, nc_sym name, const nc_type *type, bool is_const, nc_span span);
nc_fn_info *nc_sema_find_fn(nc_sema *sm, nc_sym name);

/* ---- types ---- */
/* Resolves a written type; reports unsupported kinds (E9001) and unknown
 * names (E3001). Sets *is_const for a top-level `const`. */
const nc_type *nc_sema_resolve_type(nc_sema *sm, const nc_type_expr *te, bool *is_const);

/* ---- expressions ---- */
/* Type-check `*slot` in a value context. `expected` (may be NULL) guides
 * untyped constants. Assignments in value position are E4030. */
const nc_type *nc_check_value(nc_sema *sm, nc_expr **slot, const nc_type *expected);
/* Type-check an expression statement's expression (assignments allowed). */
void nc_check_expr_stmt(nc_sema *sm, nc_expr **slot);
/* Implicitly convert `*slot` to `to`, inserting an NC_E_IMPLICIT node or
 * fixing an untyped constant's type. Reports E4001 / E4041. `what`
 * describes the context ("initialiser of 'x'"). */
bool nc_coerce(nc_sema *sm, nc_expr **slot, const nc_type *to, const char *what);
/* Give an untyped constant its default type (i32 / f64). */
void nc_default_untyped(nc_sema *sm, nc_expr **slot);
/* Check a `bool` condition (E4020). */
void nc_check_condition(nc_sema *sm, nc_expr **slot);

/* ---- statements ---- */
/* Returns true if the statement cannot complete normally (always returns
 * or loops forever). */
bool nc_check_stmt(nc_sema *sm, nc_stmt *s);

#endif
