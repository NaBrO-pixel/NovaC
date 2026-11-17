#include "sema/types.h"

static const nc_type prims[NC_PRIM_COUNT] = {
#define P(x) {NC_TY_PRIM, x}
    P(NC_PRIM_I8), P(NC_PRIM_I16), P(NC_PRIM_I32), P(NC_PRIM_I64), P(NC_PRIM_ISIZE),
    P(NC_PRIM_U8), P(NC_PRIM_U16), P(NC_PRIM_U32), P(NC_PRIM_U64), P(NC_PRIM_USIZE),
    P(NC_PRIM_F32), P(NC_PRIM_F64), P(NC_PRIM_BOOL), P(NC_PRIM_VOID),
#undef P
};
static const nc_type untyped_int = {NC_TY_UNTYPED_INT, NC_PRIM_VOID};
static const nc_type untyped_float = {NC_TY_UNTYPED_FLOAT, NC_PRIM_VOID};
static const nc_type error_type = {NC_TY_ERROR, NC_PRIM_VOID};

const nc_type *nc_ty_prim(nc_prim p) { return &prims[p]; }
const nc_type *nc_ty_untyped_int(void) { return &untyped_int; }
const nc_type *nc_ty_untyped_float(void) { return &untyped_float; }
const nc_type *nc_ty_error(void) { return &error_type; }

bool nc_ty_is_error(const nc_type *t) { return !t || t->kind == NC_TY_ERROR; }
bool nc_ty_is_untyped(const nc_type *t) { return t && (t->kind == NC_TY_UNTYPED_INT || t->kind == NC_TY_UNTYPED_FLOAT); }

bool nc_ty_is_integer(const nc_type *t)
{
    return t && t->kind == NC_TY_PRIM && t->prim <= NC_PRIM_USIZE;
}

bool nc_ty_is_signed(const nc_type *t)
{
    return t && t->kind == NC_TY_PRIM && t->prim <= NC_PRIM_ISIZE;
}

bool nc_ty_is_float(const nc_type *t)
{
    return t && t->kind == NC_TY_PRIM && (t->prim == NC_PRIM_F32 || t->prim == NC_PRIM_F64);
}

bool nc_ty_is_numeric(const nc_type *t) { return nc_ty_is_integer(t) || nc_ty_is_float(t); }
bool nc_ty_is_bool(const nc_type *t) { return t && t->kind == NC_TY_PRIM && t->prim == NC_PRIM_BOOL; }
bool nc_ty_is_void(const nc_type *t) { return t && t->kind == NC_TY_PRIM && t->prim == NC_PRIM_VOID; }

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
    if (is_size(to)) {
        /* Only from types of at most 32 bits and the same signedness. */
        return fb <= 32 && fs == ts;
    }
    if (fs == ts) return tb > fb;
    return !fs && ts && tb > fb; /* unsigned -> wider signed */
}

const char *nc_type_name(const nc_type *t)
{
    if (!t) return "{unknown}";
    switch (t->kind) {
    case NC_TY_PRIM: return nc_prim_name(t->prim);
    case NC_TY_UNTYPED_INT: return "{integer}";
    case NC_TY_UNTYPED_FLOAT: return "{float}";
    case NC_TY_ERROR: return "{error}";
    }
    return "?";
}

void nc_type_print(FILE *out, const nc_type *t)
{
    fputs(nc_type_name(t), out);
}
