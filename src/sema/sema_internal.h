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

/* A loop or a switch: `break` leaves the innermost of either; `continue`
 * goes to the innermost loop. */
typedef struct nc_loop_ctx {
    bool is_switch;
    bool has_break;
    bool has_continue;
} nc_loop_ctx;

/* Largest object size (2^47 bytes: beyond any version-1 address space),
 * so that sizes and offsets never overflow. */
#define NC_MAX_OBJECT_SIZE ((uint64_t)1 << 47)
/* Implementation limit on the elements initialiser lists expand to, per
 * module (E9005): each array list element costs a slot during checking. */
#define NC_MAX_LIST_ELEMS ((uint64_t)1 << 22)

typedef enum nc_mode { NC_MODE_SAFE, NC_MODE_CONTROLLED, NC_MODE_KERNEL } nc_mode;

typedef struct nc_sema {
    nc_session *s;
    nc_module_ast *m;
    NC_VEC(nc_fn_info *) fns;
    NC_VEC(nc_module_sym) msyms;   /* every module-level name */
    NC_VEC(nc_scope_entry) scope;  /* innermost last */
    NC_VEC(size_t) marks;          /* scope.len at each scope entry */
    NC_VEC(nc_loop_ctx) loops;
    nc_fn_info *cur_fn;
    bool fallthrough_ok;           /* checking the last statement of a non-final case */
    uint64_t list_elems;           /* array elements expanded by initialiser lists so far */
    nc_mode mode;                  /* the module's @mode (NOVAC_MEMORY_MODEL.md §2) */
    unsigned unsafe_depth;         /* nesting of `unsafe { }` blocks */
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
nc_module_sym *nc_sema_find_module_sym(nc_sema *sm, nc_sym name);
/* The value of a constexpr, evaluating it on first use (E3005 on cycles). */
nc_const_info *nc_sema_const(nc_sema *sm, nc_module_sym *ms);
/* The type of a static, resolving it on first use. */
nc_static_info *nc_sema_static(nc_sema *sm, nc_module_sym *ms);

/* ---- module declarations (decls.c) ---- */
/* Enter every module-level name (E3002 on duplicates). */
void nc_sema_collect(nc_sema *sm);
/* Resolve every non-function declaration (layouts, values), reporting errors
 * even in unused ones. */
void nc_sema_resolve_decls(nc_sema *sm);
enum { NC_ATTR_PACKED = 1u << 0, NC_ATTR_REPR = 1u << 1, NC_ATTR_MODE = 1u << 2 };
/* Is the current code in an unsafe context? If not, reports E6003 for
 * `what` ("dereferencing a raw pointer") and returns false. */
bool nc_sema_require_unsafe(nc_sema *sm, nc_span span, const char *what);
/* Checks attributes: unknown names are E3040, known but unimplemented ones
 * E9001, implemented ones outside `allowed` E3041. Returns the flags of the
 * accepted ones. */
unsigned nc_sema_attrs(nc_sema *sm, const nc_attr *a, uint32_t n, unsigned allowed);

/* The type a typedef names, resolving it on first use (E3005 on cycles). */
const nc_type *nc_sema_typedef(nc_sema *sm, nc_module_sym *ms);
/* The type named by a module symbol (struct, union, enum or typedef), or
 * NULL if it does not name a type. */
const nc_type *nc_sema_msym_type(nc_sema *sm, nc_module_sym *ms);

/* ---- types ---- */
/* Resolves a written type; reports unsupported kinds (E9001) and unknown
 * names (E3001). Sets *is_const for a top-level `const`. */
const nc_type *nc_sema_resolve_type(nc_sema *sm, const nc_type_expr *te, bool *is_const);
/* Completes the layout of a struct, union or enum (and of an array's
 * element type). Returns false, reporting at `span`, if the type cannot be
 * used by value (opaque, recursive or erroneous). */
bool nc_sema_complete(nc_sema *sm, const nc_type *t, nc_span span);
/* Resolves a type that a value will have (variable, field, parameter,
 * return, element): complete and not void unless `allow_void`. */
const nc_type *nc_sema_value_type(nc_sema *sm, const nc_type_expr *te, bool *is_const, bool allow_void, const char *what);

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
/* Check an initialiser for a value of type `t`: an expression, or a list
 * for an aggregate. Lists become NC_E_AGGREGATE expressions; afterwards
 * in->kind is NC_INIT_EXPR. Returns false on error. */
bool nc_sema_check_init(nc_sema *sm, nc_init *in, const nc_type *t, const char *what);
/* Is the checked expression a constant (static initialisers, case values)? */
bool nc_is_constant_expr(const nc_expr *e);

/* ---- statements ---- */
/* Returns true if the statement cannot complete normally (always returns
 * or loops forever). */
bool nc_check_stmt(nc_sema *sm, nc_stmt *s);

#endif
