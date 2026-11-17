#include "ast/ast.h"

const char *nc_prim_name(nc_prim p)
{
    static const char *const names[NC_PRIM_COUNT] = {
        "i8", "i16", "i32", "i64", "isize", "u8", "u16", "u32", "u64", "usize",
        "f32", "f64", "bool", "void",
    };
    return (unsigned)p < NC_PRIM_COUNT ? names[p] : "?";
}

bool nc_prim_from_token(nc_tok kind, nc_prim *out)
{
    switch (kind) {
    case NC_T_KW_I8: *out = NC_PRIM_I8; return true;
    case NC_T_KW_I16: *out = NC_PRIM_I16; return true;
    case NC_T_KW_I32: *out = NC_PRIM_I32; return true;
    case NC_T_KW_INT: *out = NC_PRIM_I32; return true; /* D-031 */
    case NC_T_KW_I64: *out = NC_PRIM_I64; return true;
    case NC_T_KW_ISIZE: *out = NC_PRIM_ISIZE; return true;
    case NC_T_KW_U8: *out = NC_PRIM_U8; return true;
    case NC_T_KW_U16: *out = NC_PRIM_U16; return true;
    case NC_T_KW_U32: *out = NC_PRIM_U32; return true;
    case NC_T_KW_U64: *out = NC_PRIM_U64; return true;
    case NC_T_KW_USIZE: *out = NC_PRIM_USIZE; return true;
    case NC_T_KW_F32: *out = NC_PRIM_F32; return true;
    case NC_T_KW_F64: *out = NC_PRIM_F64; return true;
    case NC_T_KW_BOOL: *out = NC_PRIM_BOOL; return true;
    case NC_T_KW_VOID: *out = NC_PRIM_VOID; return true;
    default: return false;
    }
}

const char *nc_unop_spelling(nc_unop op)
{
    switch (op) {
    case NC_UN_NEG: return "-";
    case NC_UN_PLUS: return "+";
    case NC_UN_NOT: return "!";
    case NC_UN_BITNOT: return "~";
    case NC_UN_DEREF: return "*";
    case NC_UN_ADDR: return "&";
    case NC_UN_ADDR_MUT: return "&mut";
    case NC_UN_TRY: return "try";
    case NC_UN_PREINC: return "++";
    case NC_UN_PREDEC: return "--";
    case NC_UN_POSTINC: return "post++";
    case NC_UN_POSTDEC: return "post--";
    }
    return "?";
}

const char *nc_binop_spelling(nc_binop op)
{
    switch (op) {
    case NC_BIN_MUL: return "*";
    case NC_BIN_DIV: return "/";
    case NC_BIN_MOD: return "%";
    case NC_BIN_WMUL: return "*%";
    case NC_BIN_ADD: return "+";
    case NC_BIN_SUB: return "-";
    case NC_BIN_WADD: return "+%";
    case NC_BIN_WSUB: return "-%";
    case NC_BIN_SHL: return "<<";
    case NC_BIN_SHR: return ">>";
    case NC_BIN_LT: return "<";
    case NC_BIN_LE: return "<=";
    case NC_BIN_GT: return ">";
    case NC_BIN_GE: return ">=";
    case NC_BIN_EQ: return "==";
    case NC_BIN_NE: return "!=";
    case NC_BIN_BITAND: return "&";
    case NC_BIN_BITXOR: return "^";
    case NC_BIN_BITOR: return "|";
    case NC_BIN_AND: return "&&";
    case NC_BIN_OR: return "||";
    case NC_BIN_NONE: return "";
    }
    return "?";
}

const char *nc_assignop_spelling(nc_binop op)
{
    switch (op) {
    case NC_BIN_NONE: return "=";
    case NC_BIN_MUL: return "*=";
    case NC_BIN_DIV: return "/=";
    case NC_BIN_MOD: return "%=";
    case NC_BIN_WMUL: return "*%=";
    case NC_BIN_ADD: return "+=";
    case NC_BIN_SUB: return "-=";
    case NC_BIN_WADD: return "+%=";
    case NC_BIN_WSUB: return "-%=";
    case NC_BIN_SHL: return "<<=";
    case NC_BIN_SHR: return ">>=";
    case NC_BIN_BITAND: return "&=";
    case NC_BIN_BITXOR: return "^=";
    case NC_BIN_BITOR: return "|=";
    default: return "?=";
    }
}
