/* Abstract syntax tree (NOVAC_COMPILER_ARCHITECTURE.md §7.3).
 *
 * Nodes are allocated in the session arena and never freed individually.
 * The parser builds the full grammar of NOVAC_GRAMMAR.md for expressions,
 * types and the [P2] statements; constructs a later phase implements are
 * kept in the tree so semantic analysis can reject them precisely (E9001).
 *
 * Semantic analysis annotates expressions in place (`type`, `cval`,
 * `binding`). */
#ifndef NC_AST_AST_H
#define NC_AST_AST_H

#include "lex/token.h"
#include "session/session.h"

typedef struct nc_type nc_type; /* sema/types.h */
struct nc_var;                  /* sema/sema_internal.h */
typedef struct nc_expr nc_expr;
typedef struct nc_stmt nc_stmt;
typedef struct nc_decl nc_decl;
typedef struct nc_type_expr nc_type_expr;

/* ---- primitive types ------------------------------------------------------ */

typedef enum nc_prim {
    NC_PRIM_I8, NC_PRIM_I16, NC_PRIM_I32, NC_PRIM_I64, NC_PRIM_ISIZE,
    NC_PRIM_U8, NC_PRIM_U16, NC_PRIM_U32, NC_PRIM_U64, NC_PRIM_USIZE,
    NC_PRIM_F32, NC_PRIM_F64, NC_PRIM_BOOL, NC_PRIM_VOID,
    NC_PRIM_COUNT
} nc_prim;

const char *nc_prim_name(nc_prim p);
/* Maps a primitive-type keyword token (including `int`) to its primitive. */
bool nc_prim_from_token(nc_tok kind, nc_prim *out);

/* ---- paths and generic arguments -------------------------------------------- */

typedef struct nc_path {
    nc_sym *parts;
    uint32_t count;
    nc_span span;
} nc_path;

typedef struct nc_generic_arg {
    nc_type_expr *type; /* exactly one of type / expr is set */
    nc_expr *expr;
} nc_generic_arg;

/* ---- type expressions --------------------------------------------------------- */

typedef enum nc_type_expr_kind {
    NC_TE_PRIM,
    NC_TE_PATH,
    NC_TE_SELF,
    NC_TE_PTR,   /* inner* */
    NC_TE_REF,   /* inner& */
    NC_TE_ARRAY, /* elem[len] */
    NC_TE_FN,    /* ret fn(params) */
    NC_TE_ERROR,
} nc_type_expr_kind;

struct nc_type_expr {
    nc_type_expr_kind kind;
    nc_span span;
    /* PRIM/PATH/SELF: leading `const` on the base type.
     * PTR/REF/ARRAY/FN: `const` written after this level's suffix. */
    bool is_const;
    union {
        nc_prim prim;
        struct {
            nc_path path;
            nc_generic_arg *args;
            uint32_t nargs;
        } path;
        nc_type_expr *inner; /* PTR, REF */
        struct {
            nc_type_expr *elem;
            nc_expr *len;
        } array;
        struct {
            nc_type_expr *ret;
            nc_type_expr **params;
            uint32_t nparams;
            bool variadic;
        } fn;
    } u;
};

/* ---- initialisers ---------------------------------------------------------------- */

typedef struct nc_init nc_init;

typedef struct nc_init_item {
    nc_sym field;     /* `.field = ` designator, or 0 */
    nc_expr *index;   /* `[index] = ` designator, or NULL */
    nc_init *value;
    nc_span span;
} nc_init_item;

typedef enum nc_init_kind { NC_INIT_EXPR, NC_INIT_LIST, NC_INIT_UNINIT } nc_init_kind;

struct nc_init {
    nc_init_kind kind;
    nc_span span;
    nc_expr *expr;        /* EXPR */
    nc_init_item *items;  /* LIST */
    uint32_t nitems;
};

/* ---- patterns ------------------------------------------------------------------------ */

typedef enum nc_pattern_kind { NC_PAT_SOME, NC_PAT_NONE, NC_PAT_NAMED } nc_pattern_kind;

typedef struct nc_pattern {
    nc_pattern_kind kind;
    nc_sym name;     /* NAMED: the constructor name (ok, err, an enumerator) */
    nc_sym binding;  /* bound variable, 0 if none; `_` is interned as "_" */
    bool has_parens;
    nc_span span;
} nc_pattern;

/* ---- expressions --------------------------------------------------------------------- */

typedef enum nc_expr_kind {
    NC_E_INT, NC_E_FLOAT, NC_E_BOOL, NC_E_CHAR, NC_E_STRING, NC_E_CSTRING,
    NC_E_NULL, NC_E_NONE, NC_E_SELF,
    NC_E_NAME,
    NC_E_UNARY,
    NC_E_BINARY,
    NC_E_ASSIGN,
    NC_E_COND,
    NC_E_CALL,
    NC_E_INDEX,
    NC_E_MEMBER,   /* a.b */
    NC_E_ARROW,    /* a->b */
    NC_E_CONVERT,  /* T(x), written by the programmer */
    NC_E_IMPLICIT, /* conversion inserted by semantic analysis */
    NC_E_STRUCT_LIT,
    NC_E_SOME,
    NC_E_SIZEOF,
    NC_E_ALIGNOF,
    NC_E_OFFSETOF,
    NC_E_GENERIC,  /* name::<args> */
    NC_E_IS,
    NC_E_ERROR,
} nc_expr_kind;

typedef enum nc_unop {
    NC_UN_NEG, NC_UN_PLUS, NC_UN_NOT, NC_UN_BITNOT,
    NC_UN_DEREF, NC_UN_ADDR, NC_UN_ADDR_MUT, NC_UN_TRY,
    NC_UN_PREINC, NC_UN_PREDEC, NC_UN_POSTINC, NC_UN_POSTDEC,
} nc_unop;

typedef enum nc_binop {
    NC_BIN_MUL, NC_BIN_DIV, NC_BIN_MOD, NC_BIN_WMUL,
    NC_BIN_ADD, NC_BIN_SUB, NC_BIN_WADD, NC_BIN_WSUB,
    NC_BIN_SHL, NC_BIN_SHR,
    NC_BIN_LT, NC_BIN_LE, NC_BIN_GT, NC_BIN_GE,
    NC_BIN_EQ, NC_BIN_NE,
    NC_BIN_BITAND, NC_BIN_BITXOR, NC_BIN_BITOR,
    NC_BIN_AND, NC_BIN_OR,
    NC_BIN_NONE, /* plain `=` in NC_E_ASSIGN */
} nc_binop;

const char *nc_unop_spelling(nc_unop op);
const char *nc_binop_spelling(nc_binop op);
/* "=" for NC_BIN_NONE, otherwise "+=", "<<=", ... */
const char *nc_assignop_spelling(nc_binop op);

/* Compile-time constant value attached by semantic analysis. Integers are
 * sign + magnitude so the full range of i64 and u64 is representable. */
typedef enum nc_cval_kind { NC_CV_NONE, NC_CV_INT, NC_CV_FLOAT, NC_CV_BOOL } nc_cval_kind;

typedef struct nc_cval {
    nc_cval_kind kind;
    bool neg;      /* INT: value is negative (mag > 0) */
    uint64_t mag;  /* INT: magnitude */
    double f;      /* FLOAT */
    bool b;        /* BOOL */
} nc_cval;

typedef enum nc_binding_kind { NC_BIND_NONE, NC_BIND_VAR, NC_BIND_FN } nc_binding_kind;

struct nc_expr {
    nc_expr_kind kind;
    nc_span span;
    const nc_type *type;  /* set by sema */
    nc_cval cval;         /* set by sema for constant expressions */
    nc_binding_kind binding_kind;
    void *binding;        /* NAME: struct nc_var* or nc_decl* */
    union {
        struct {
            uint64_t value;
            nc_suffix suffix;
        } int_lit;
        struct {
            double value;
            nc_suffix suffix;
        } float_lit;
        bool bool_lit;
        uint8_t char_lit;
        nc_sym str; /* STRING, CSTRING */
        nc_sym name;
        struct {
            nc_unop op;
            nc_expr *operand;
        } unary;
        struct {
            nc_binop op;
            nc_expr *lhs, *rhs;
        } binary; /* BINARY and ASSIGN (op NC_BIN_NONE for `=`) */
        struct {
            nc_expr *cond, *then_e, *else_e;
        } cond;
        struct {
            nc_expr *callee;
            nc_expr **args;
            uint32_t nargs;
        } call;
        struct {
            nc_expr *base, *index;
        } index;
        struct {
            nc_expr *base;
            nc_sym name;
            nc_span name_span;
        } member; /* MEMBER, ARROW */
        struct {
            nc_type_expr *type; /* CONVERT: written type */
            nc_expr *operand;
        } convert; /* CONVERT, IMPLICIT (type NULL; target is expr->type) */
        struct {
            nc_type_expr *type;
            nc_init_item *items;
            uint32_t nitems;
        } struct_lit;
        nc_expr *some;
        struct {
            nc_type_expr *type; /* exactly one of type / expr */
            nc_expr *expr;
        } size; /* SIZEOF, ALIGNOF */
        struct {
            nc_type_expr *type;
            nc_sym field;
        } offsetof_;
        struct {
            nc_expr *base;
            nc_generic_arg *args;
            uint32_t nargs;
        } generic;
        struct {
            nc_expr *operand;
            nc_pattern pattern;
        } is;
    } u;
};

/* ---- statements -------------------------------------------------------------------- */

typedef enum nc_stmt_kind {
    NC_S_BLOCK, NC_S_DECL, NC_S_IF, NC_S_WHILE, NC_S_DO, NC_S_FOR,
    NC_S_BREAK, NC_S_CONTINUE, NC_S_RETURN, NC_S_EXPR, NC_S_EMPTY,
    NC_S_UNSUPPORTED, /* switch, defer, unsafe, asm, fallthrough: recognised, skipped */
} nc_stmt_kind;

typedef struct nc_declarator {
    nc_sym name;
    nc_span span;
    nc_init *init; /* may be NULL */
    struct nc_var *var; /* set by sema */
} nc_declarator;

typedef struct nc_var_decl {
    bool is_static;
    bool is_auto;        /* `auto` / `const auto` */
    bool auto_const;     /* `const auto` */
    nc_type_expr *type;  /* NULL when is_auto */
    nc_declarator *decls;
    uint32_t ndecls;
} nc_var_decl;

struct nc_stmt {
    nc_stmt_kind kind;
    nc_span span;
    union {
        struct {
            nc_stmt **stmts;
            uint32_t count;
        } block;
        nc_var_decl decl;
        struct {
            nc_expr *cond;
            nc_stmt *then_s, *else_s;
        } if_;
        struct {
            nc_expr *cond;
            nc_stmt *body;
        } while_; /* WHILE and DO */
        struct {
            nc_stmt *init_decl;   /* DECL statement, or NULL */
            nc_expr **init_exprs; /* expression list, when init is not a declaration */
            uint32_t ninit;
            nc_expr *cond;        /* NULL when omitted */
            nc_expr **step;
            uint32_t nstep;
            nc_stmt *body;
        } for_;
        nc_expr *ret; /* RETURN: NULL for `return;` */
        nc_expr *expr;
        const char *unsupported; /* construct name */
    } u;
};

/* ---- attributes and declarations ---------------------------------------------------- */

typedef struct nc_attr_arg {
    nc_sym name; /* `name = value`, or 0 */
    nc_expr *value;
} nc_attr_arg;

typedef struct nc_attr {
    nc_sym name;
    nc_span span;
    nc_attr_arg *args;
    uint32_t nargs;
    bool has_parens;
} nc_attr;

typedef struct nc_param {
    nc_type_expr *type;
    nc_sym name;   /* 0 for `self` */
    bool is_self;
    nc_span span;
    struct nc_var *var; /* set by sema */
} nc_param;

typedef enum nc_decl_kind { NC_D_FUNCTION, NC_D_UNSUPPORTED } nc_decl_kind;

struct nc_decl {
    nc_decl_kind kind;
    nc_span span;
    nc_attr *attrs;
    uint32_t nattrs;
    bool is_public;
    union {
        struct {
            nc_sym name;
            nc_span name_span;
            nc_sym extern_abi;  /* "C" in `extern "C"`, or 0 */
            bool has_extern;
            bool is_unsafe;
            nc_type_expr *ret;
            uint32_t ngeneric;  /* generic parameters (Phase 6) */
            nc_span generic_span;
            nc_param *params;
            uint32_t nparams;
            bool variadic;
            nc_stmt *body;      /* NULL for a declaration without body */
        } fn;
        const char *unsupported; /* "struct", "import", ... */
    } u;
};

typedef struct nc_module_ast {
    uint32_t file;
    bool has_module_decl;
    nc_path name;
    nc_attr *attrs; /* on the module declaration */
    uint32_t nattrs;
    nc_decl **decls;
    uint32_t ndecls;
} nc_module_ast;

/* ---- dump (NOVAC_COMPILER_ARCHITECTURE.md §7.3) ----------------------------------- */

/* When `print_type` is non-NULL every expression is annotated with its type
 * (" :i32"); semantic analysis supplies the printer, keeping this module
 * independent of sema. */
typedef void (*nc_type_printer)(FILE *out, const nc_type *t);
void nc_ast_dump(nc_session *s, const nc_module_ast *m, nc_type_printer print_type, FILE *out);

#endif
