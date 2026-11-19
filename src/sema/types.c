#include "sema/types.h"

#include <string.h>

static const nc_type prims[NC_PRIM_COUNT] = {
#define P(x, n) {NC_TY_PRIM, x, NULL, false, 0, NULL, NULL, n, NULL}
    P(NC_PRIM_I8, "i8"), P(NC_PRIM_I16, "i16"), P(NC_PRIM_I32, "i32"), P(NC_PRIM_I64, "i64"),
    P(NC_PRIM_ISIZE, "isize"), P(NC_PRIM_U8, "u8"), P(NC_PRIM_U16, "u16"), P(NC_PRIM_U32, "u32"),
    P(NC_PRIM_U64, "u64"), P(NC_PRIM_USIZE, "usize"), P(NC_PRIM_F32, "f32"), P(NC_PRIM_F64, "f64"),
    P(NC_PRIM_BOOL, "bool"), P(NC_PRIM_VOID, "void"),
#undef P
};
static const nc_type untyped_int = {NC_TY_UNTYPED_INT, NC_PRIM_VOID, NULL, false, 0, NULL, NULL, "{integer}", NULL};
static const nc_type untyped_float = {NC_TY_UNTYPED_FLOAT, NC_PRIM_VOID, NULL, false, 0, NULL, NULL, "{float}", NULL};
static const nc_type null_type = {NC_TY_NULL, NC_PRIM_VOID, NULL, false, 0, NULL, NULL, "null", NULL};
static const nc_type error_type = {NC_TY_ERROR, NC_PRIM_VOID, NULL, false, 0, NULL, NULL, "{error}", NULL};

const nc_type *nc_ty_prim(nc_prim p) { return &prims[p]; }
const nc_type *nc_ty_untyped_int(void) { return &untyped_int; }
const nc_type *nc_ty_untyped_float(void) { return &untyped_float; }
const nc_type *nc_ty_error(void) { return &error_type; }
const nc_type *nc_ty_null(void) { return &null_type; }

/* ---- interning (the list hangs off the session; nodes live in its arena) ---- */

static nc_type *new_type(nc_session *s, nc_type_kind kind)
{
    nc_type *t = nc_arena_alloc(&s->arena, sizeof *t);
    t->kind = kind;
    return t;
}

static const char *format_name(nc_session *s, const char *fmt, const char *a, uint64_t n)
{
    char buf[512];
    snprintf(buf, sizeof buf, fmt, a, (unsigned long long)n);
    return nc_arena_strndup(&s->arena, buf, strlen(buf));
}

const nc_type *nc_ty_ptr(nc_session *s, const nc_type *pointee, bool pointee_const)
{
    for (const nc_type *t = s->types; t; t = t->next)
        if (t->kind == NC_TY_PTR && t->elem == pointee && t->pointee_const == pointee_const) return t;
    nc_type *t = new_type(s, NC_TY_PTR);
    t->elem = pointee;
    t->pointee_const = pointee_const;
    t->name = format_name(s, pointee_const ? "const %s*" : "%s*", nc_type_name(pointee), 0);
    t->next = s->types;
    s->types = t;
    return t;
}

const nc_type *nc_ty_array(nc_session *s, const nc_type *elem, uint64_t len)
{
    for (const nc_type *t = s->types; t; t = t->next)
        if (t->kind == NC_TY_ARRAY && t->elem == elem && t->len == len) return t;
    nc_type *t = new_type(s, NC_TY_ARRAY);
    t->elem = elem;
    t->len = len;
    t->name = format_name(s, "%s[%llu]", nc_type_name(elem), len);
    t->next = s->types;
    s->types = t;
    return t;
}

nc_type *nc_ty_new_aggregate(nc_session *s, nc_aggregate *agg)
{
    nc_type *t = new_type(s, agg->is_union ? NC_TY_UNION : NC_TY_STRUCT);
    t->agg = agg;
    t->name = nc_sym_str(&s->strtab, agg->name);
    agg->type = t;
    return t;
}

nc_type *nc_ty_new_enum(nc_session *s, nc_enum_type *en)
{
    nc_type *t = new_type(s, NC_TY_ENUM);
    t->en = en;
    t->name = nc_sym_str(&s->strtab, en->name);
    en->type = t;
    return t;
}

/* ---- predicates ---- */

bool nc_ty_is_error(const nc_type *t) { return !t || t->kind == NC_TY_ERROR; }
bool nc_ty_is_untyped(const nc_type *t) { return t && (t->kind == NC_TY_UNTYPED_INT || t->kind == NC_TY_UNTYPED_FLOAT); }
bool nc_ty_is_integer(const nc_type *t) { return t && t->kind == NC_TY_PRIM && t->prim <= NC_PRIM_USIZE; }
bool nc_ty_is_signed(const nc_type *t) { return t && t->kind == NC_TY_PRIM && t->prim <= NC_PRIM_ISIZE; }
bool nc_ty_is_float(const nc_type *t)
{
    return t && t->kind == NC_TY_PRIM && (t->prim == NC_PRIM_F32 || t->prim == NC_PRIM_F64);
}
bool nc_ty_is_numeric(const nc_type *t) { return nc_ty_is_integer(t) || nc_ty_is_float(t); }
bool nc_ty_is_bool(const nc_type *t) { return t && t->kind == NC_TY_PRIM && t->prim == NC_PRIM_BOOL; }
bool nc_ty_is_void(const nc_type *t) { return t && t->kind == NC_TY_PRIM && t->prim == NC_PRIM_VOID; }
bool nc_ty_is_ptr(const nc_type *t) { return t && t->kind == NC_TY_PTR; }
bool nc_ty_is_null(const nc_type *t) { return t && t->kind == NC_TY_NULL; }
bool nc_ty_is_array(const nc_type *t) { return t && t->kind == NC_TY_ARRAY; }
bool nc_ty_is_struct(const nc_type *t) { return t && t->kind == NC_TY_STRUCT; }
bool nc_ty_is_union(const nc_type *t) { return t && t->kind == NC_TY_UNION; }
bool nc_ty_is_enum(const nc_type *t) { return t && t->kind == NC_TY_ENUM; }
bool nc_ty_is_aggregate(const nc_type *t)
{
    return t && (t->kind == NC_TY_STRUCT || t->kind == NC_TY_UNION || t->kind == NC_TY_ARRAY);
}

bool nc_ty_is_sized(const nc_type *t)
{
    if (!t || nc_ty_is_error(t) || nc_ty_is_untyped(t) || nc_ty_is_null(t) || nc_ty_is_void(t)) return false;
    if (t->kind == NC_TY_STRUCT || t->kind == NC_TY_UNION) return !t->agg->opaque && t->agg->state == NC_LAYOUT_DONE;
    return true;
}

const nc_type *nc_ty_repr(const nc_type *t)
{
    return t && t->kind == NC_TY_ENUM && t->en->underlying ? t->en->underlying : t;
}

unsigned nc_ty_bits(const nc_type *t)
{
    if (!nc_ty_is_integer(t)) return 0;
    switch (t->prim) {
    case NC_PRIM_I8: case NC_PRIM_U8: return 8;
    case NC_PRIM_I16: case NC_PRIM_U16: return 16;
    case NC_PRIM_I32: case NC_PRIM_U32: return 32;
    default: return 64;
    }
}

uint64_t nc_ty_size(const nc_type *t)
{
    switch (t->kind) {
    case NC_TY_PRIM:
        switch (t->prim) {
        case NC_PRIM_I8: case NC_PRIM_U8: case NC_PRIM_BOOL: return 1;
        case NC_PRIM_I16: case NC_PRIM_U16: return 2;
        case NC_PRIM_I32: case NC_PRIM_U32: case NC_PRIM_F32: return 4;
        case NC_PRIM_VOID: return 0;
        default: return 8;
        }
    case NC_TY_PTR: return 8; /* every version-1 target is 64-bit (D-023) */
    case NC_TY_ARRAY: return t->len * nc_ty_size(t->elem);
    case NC_TY_STRUCT: case NC_TY_UNION: return t->agg->size;
    case NC_TY_ENUM: return nc_ty_size(nc_ty_repr(t));
    default: return 0;
    }
}

uint64_t nc_ty_align(const nc_type *t)
{
    switch (t->kind) {
    case NC_TY_ARRAY: return nc_ty_align(t->elem);
    case NC_TY_STRUCT: case NC_TY_UNION: return t->agg->align;
    case NC_TY_ENUM: return nc_ty_align(nc_ty_repr(t));
    default: {
        uint64_t s = nc_ty_size(t);
        return s ? s : 1;
    }
    }
}

static bool is_size(const nc_type *t)
{
    return t->prim == NC_PRIM_ISIZE || t->prim == NC_PRIM_USIZE;
}

bool nc_ty_widens(const nc_type *from, const nc_type *to)
{
    if (from == to || from->kind != NC_TY_PRIM || to->kind != NC_TY_PRIM) return false;
    if (from->prim == NC_PRIM_F32 && to->prim == NC_PRIM_F64) return true;
    if (!nc_ty_is_integer(from) || !nc_ty_is_integer(to)) return false;
    unsigned fb = nc_ty_bits(from), tb = nc_ty_bits(to);
    bool fs = nc_ty_is_signed(from), ts = nc_ty_is_signed(to);
    if (is_size(from)) return false; /* isize/usize never widen implicitly */
    if (is_size(to)) return fb <= 32 && fs == ts; /* only types of at most 32 bits, same signedness */
    if (fs == ts) return tb > fb;
    return !fs && ts && tb > fb; /* unsigned -> wider signed */
}

const char *nc_type_name(const nc_type *t)
{
    return t && t->name ? t->name : "{unknown}";
}

void nc_type_print(FILE *out, const nc_type *t)
{
    fputs(nc_type_name(t), out);
}
