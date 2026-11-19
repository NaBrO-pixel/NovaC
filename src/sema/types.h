/* Semantic types (NOVAC_TYPE_SYSTEM.md).
 *
 *   - primitives (static, shared);
 *   - pointers `T*` / `const T*` and arrays `T[N]`: structural, interned per
 *     session, so pointer equality is type equality;
 *   - structs, unions and enums: nominal, one type object per declaration,
 *     carrying their layout (NOVAC_ABI.md §4);
 *   - untyped integer / untyped float: literal constants until context fixes
 *     their type (NOVAC_TYPE_SYSTEM.md §5.1, §8);
 *   - error: assigned to erroneous expressions; compatible with everything,
 *     so one mistake produces one diagnostic. */
#ifndef NC_SEMA_TYPES_H
#define NC_SEMA_TYPES_H

#include "ast/ast.h"

typedef enum nc_type_kind {
    NC_TY_PRIM,
    NC_TY_PTR,
    NC_TY_ARRAY,
    NC_TY_STRUCT,
    NC_TY_UNION,
    NC_TY_ENUM,
    NC_TY_UNTYPED_INT,
    NC_TY_UNTYPED_FLOAT,
    NC_TY_NULL,          /* `null` until context gives it a pointer type */
    NC_TY_ERROR,
} nc_type_kind;

typedef struct nc_aggregate nc_aggregate;
typedef struct nc_enum_type nc_enum_type;

struct nc_type {
    nc_type_kind kind;
    nc_prim prim;            /* PRIM */
    const nc_type *elem;     /* PTR: pointee; ARRAY: element */
    bool pointee_const;      /* PTR: `const T*` */
    uint64_t len;            /* ARRAY */
    nc_aggregate *agg;       /* STRUCT, UNION */
    nc_enum_type *en;        /* ENUM */
    const char *name;        /* display name ("const u8*", "i32[4]", "Point") */
    const nc_type *next;     /* interning list */
};

typedef struct nc_field {
    nc_sym name;
    const nc_type *type;
    uint64_t offset;
    bool is_public;
    nc_span span;
} nc_field;

/* Layout state: an aggregate's fields are resolved on first use; reaching
 * an aggregate that is still being resolved means it contains itself by
 * value (E4080). */
typedef enum nc_layout_state { NC_LAYOUT_NONE, NC_LAYOUT_BUSY, NC_LAYOUT_DONE, NC_LAYOUT_FAILED } nc_layout_state;

struct nc_aggregate {
    nc_sym name;
    nc_span span;
    nc_decl *decl;
    bool is_union;
    bool opaque;     /* `struct Name;`: no fields, no size */
    bool packed;     /* @packed */
    nc_field *fields;
    uint32_t nfields;
    uint64_t size, align;
    nc_layout_state state;
    nc_type *type;
};

typedef struct nc_enumerator {
    nc_sym name;
    nc_cval value;   /* INT */
    nc_span span;
} nc_enumerator;

struct nc_enum_type {
    nc_sym name;
    nc_span span;
    nc_decl *decl;
    const nc_type *underlying;
    nc_enumerator *items;
    uint32_t nitems;
    nc_layout_state state;
    nc_type *type;
};

const nc_type *nc_ty_prim(nc_prim p);
const nc_type *nc_ty_untyped_int(void);
const nc_type *nc_ty_untyped_float(void);
const nc_type *nc_ty_error(void);
const nc_type *nc_ty_null(void);

/* Interned compound types (allocated in the session arena). */
const nc_type *nc_ty_ptr(nc_session *s, const nc_type *pointee, bool pointee_const);
const nc_type *nc_ty_array(nc_session *s, const nc_type *elem, uint64_t len);
/* A new nominal type for a struct, union or enum declaration. */
nc_type *nc_ty_new_aggregate(nc_session *s, nc_aggregate *agg);
nc_type *nc_ty_new_enum(nc_session *s, nc_enum_type *en);

bool nc_ty_is_error(const nc_type *t);
bool nc_ty_is_untyped(const nc_type *t);
bool nc_ty_is_integer(const nc_type *t); /* typed integer */
bool nc_ty_is_signed(const nc_type *t);  /* typed signed integer */
bool nc_ty_is_float(const nc_type *t);   /* typed float */
bool nc_ty_is_numeric(const nc_type *t); /* typed integer or float */
bool nc_ty_is_bool(const nc_type *t);
bool nc_ty_is_void(const nc_type *t);
bool nc_ty_is_ptr(const nc_type *t);
bool nc_ty_is_null(const nc_type *t);
bool nc_ty_is_array(const nc_type *t);
bool nc_ty_is_struct(const nc_type *t);
bool nc_ty_is_union(const nc_type *t);
bool nc_ty_is_enum(const nc_type *t);
bool nc_ty_is_aggregate(const nc_type *t); /* struct, union or array */
/* Values that occupy storage (not void, error or untyped). */
bool nc_ty_is_sized(const nc_type *t);
unsigned nc_ty_bits(const nc_type *t);   /* integer width; isize/usize are 64 */
/* The integer type behind an enum, or the type itself. */
const nc_type *nc_ty_repr(const nc_type *t);

/* Size and alignment (NOVAC_ABI.md §3, §4). Aggregates must have a
 * completed layout. */
uint64_t nc_ty_size(const nc_type *t);
uint64_t nc_ty_align(const nc_type *t);

/* Lossless implicit conversion between typed types (NOVAC_TYPE_SYSTEM.md
 * §5.1), excluding identity. */
bool nc_ty_widens(const nc_type *from, const nc_type *to);

const char *nc_type_name(const nc_type *t);
void nc_type_print(FILE *out, const nc_type *t);

#endif
