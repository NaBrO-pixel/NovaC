/* NovaC Intermediate Representation (NOVAC_COMPILER_ARCHITECTURE.md §9.1).
 *
 * A control-flow graph per function, in the style of Rust's MIR:
 *   - locals: typed storage (parameters, user variables, temporaries);
 *   - statements: assignments of rvalues to locals;
 *   - terminators: control transfer, including `assert`, which panics with
 *     a static message when its condition is false.
 * Every run-time check of the language (overflow, division by zero, shift
 * amount, conversion range) is an explicit `assert` here, so later passes
 * (check elimination, Phase 5 borrow checking) and code generation see the
 * same program.
 *
 * NIR is not SSA: a local may be assigned many times. LLVM's mem2reg
 * produces SSA. A place is a local or a static followed by field and
 * element projections (`_3.x[_5]`); an element projection's index is a
 * constant or a `usize` local that a preceding assert bounds-checked. */
#ifndef NC_NIR_NIR_H
#define NC_NIR_NIR_H

#include "sema/symbols.h"
#include "session/session.h"

typedef uint32_t nc_local_id;
typedef uint32_t nc_block_id;
#define NC_NO_BLOCK UINT32_MAX

typedef struct nc_nir_local {
    const nc_type *type;
    nc_sym name;      /* user-visible name, 0 for temporaries */
    nc_span span;
    bool is_param;
    bool is_user;     /* a declared variable or parameter (checked for initialisation) */
} nc_nir_local;

typedef enum nc_place_base { NC_PLACE_LOCAL, NC_PLACE_STATIC } nc_place_base;
typedef enum nc_proj_kind {
    NC_PROJ_FIELD,
    NC_PROJ_INDEX,
    NC_PROJ_DEREF, /* the pointee of a raw pointer (only in unsafe code) */
} nc_proj_kind;

typedef struct nc_proj {
    nc_proj_kind kind;
    uint32_t field;           /* FIELD: index into the struct's or union's fields */
    bool index_is_const;      /* INDEX */
    uint64_t index;           /* INDEX, constant: below the array length (checked by sema) */
    nc_local_id index_local;  /* INDEX, otherwise: a usize local, already bounds-checked */
    const nc_type *type;      /* the type after this projection */
} nc_proj;

typedef struct nc_place {
    nc_place_base base;
    uint32_t id;              /* local id, or index into the module's statics */
    const nc_proj *projs;     /* session arena */
    uint32_t nprojs;
    const nc_type *type;      /* the type of the whole place */
} nc_place;

typedef enum nc_operand_kind {
    NC_OP_COPY,  /* the value stored in a place */
    NC_OP_CONST, /* a scalar constant */
    NC_OP_ZERO,  /* the all-zero value of `type` (omitted initialiser members, D-043; null) */
    NC_OP_UNDEF, /* `uninit`: no value; storing it leaves the destination unchanged */
    NC_OP_CSTRING, /* c"...": the address of a NUL-terminated constant array */
} nc_operand_kind;

typedef struct nc_operand {
    nc_operand_kind kind;
    nc_place place;      /* COPY */
    nc_cval cval;        /* CONST */
    nc_sym str;          /* CSTRING: the bytes, without the NUL */
    const nc_type *type;
    nc_span span;        /* source of the value, for diagnostics */
} nc_operand;

/* Operators of NIR rvalues. Arithmetic here never checks: checks are
 * separate asserts, and checked arithmetic uses NC_ST_CHECKED. */
typedef enum nc_nir_binop {
    NC_NB_ADD, NC_NB_SUB, NC_NB_MUL,           /* integer: wrapping; float: IEEE */
    NC_NB_DIV, NC_NB_REM,                      /* integer: divisor checked beforehand */
    NC_NB_SHL, NC_NB_SHR,                      /* amount checked beforehand; SHR arithmetic iff signed */
    NC_NB_AND, NC_NB_OR, NC_NB_XOR,
    NC_NB_EQ, NC_NB_NE, NC_NB_LT, NC_NB_LE, NC_NB_GT, NC_NB_GE,
} nc_nir_binop;

typedef enum nc_nir_unop { NC_NU_NEG_FLOAT, NC_NU_NOT, NC_NU_BITNOT } nc_nir_unop;

typedef enum nc_cast_kind {
    NC_CAST_INT_RESIZE,   /* integer to integer: truncate or extend by the source's signedness */
    NC_CAST_INT_TO_FLOAT,
    NC_CAST_FLOAT_TO_INT, /* truncates toward zero; range checked beforehand */
    NC_CAST_FLOAT_RESIZE,
    NC_CAST_BOOL_TO_INT,
    NC_CAST_NOP,          /* same representation: enum <-> underlying integer, pointer -> pointer */
    NC_CAST_PTR_TO_INT,   /* addr(p) */
    NC_CAST_INT_TO_PTR,   /* ptr_from_addr */
} nc_cast_kind;

typedef enum nc_rvalue_kind {
    NC_RV_USE,
    NC_RV_BINARY,
    NC_RV_UNARY,
    NC_RV_CAST,
    NC_RV_AGGREGATE,
    NC_RV_ADDR,       /* the address of place a.place (a is a COPY operand naming it) */
    NC_RV_PTR_OFFSET, /* a + b elements (binop ADD) or a - b elements (binop SUB); wrapping */
    NC_RV_PTR_DIFF,   /* (a - b) / sizeof(pointee), as isize */
} nc_rvalue_kind;

typedef struct nc_rvalue {
    nc_rvalue_kind kind;
    nc_nir_binop binop;
    nc_nir_unop unop;
    nc_cast_kind cast;
    nc_operand a, b;
    /* AGGREGATE: a struct (one operand per field), an array (one per
     * element) or a union (one operand, for field `union_field`). */
    nc_operand *elems;    /* session arena */
    uint32_t nelems;
    uint32_t union_field;
} nc_rvalue;

typedef enum nc_stmt_kind_nir {
    NC_ST_ASSIGN,  /* dest = rvalue */
    NC_ST_CHECKED, /* dest = a op b (wrapping); flag = whether the exact result overflowed */
} nc_stmt_kind_nir;

typedef struct nc_nir_stmt {
    nc_stmt_kind_nir kind;
    nc_place dest;
    nc_local_id flag;     /* CHECKED */
    nc_nir_binop op;      /* CHECKED: ADD, SUB or MUL */
    nc_rvalue rv;         /* ASSIGN; CHECKED uses rv.a and rv.b */
    nc_span span;
} nc_nir_stmt;

typedef enum nc_term_kind {
    NC_TERM_NONE,        /* not yet terminated (only during construction) */
    NC_TERM_GOTO,
    NC_TERM_BRANCH,      /* if cond goto target else goto other */
    NC_TERM_RETURN,      /* has_value: return value */
    NC_TERM_CALL,        /* dest = fn(args); goto target */
    NC_TERM_ASSERT,      /* if cond == expected goto target else panic(message) */
    NC_TERM_SWITCH,      /* goto the target of the case equal to cond, else other */
    NC_TERM_UNREACHABLE, /* control cannot reach here (proved by sema) */
} nc_term_kind;

typedef struct nc_terminator {
    nc_term_kind kind;
    nc_span span;
    nc_operand cond;     /* BRANCH, ASSERT, SWITCH */
    nc_operand value;    /* RETURN */
    bool has_value;
    nc_block_id target;  /* GOTO, BRANCH (true), CALL, ASSERT */
    nc_block_id other;   /* BRANCH (false), SWITCH (no case matches) */
    nc_cval *case_values;      /* SWITCH (session arena) */
    nc_block_id *case_targets; /* SWITCH (session arena) */
    uint32_t ncases;
    struct nc_nir_fn *callee;
    nc_operand *args;
    uint32_t nargs;
    nc_local_id dest;    /* CALL; UINT32_MAX for a void call */
    bool expected;       /* ASSERT */
    const char *message; /* ASSERT: panic message (static or session arena) */
} nc_terminator;

typedef struct nc_nir_block {
    nc_nir_stmt *stmts;
    uint32_t nstmts, cap;
    nc_terminator term;
} nc_nir_block;

typedef struct nc_nir_fn {
    nc_fn_info *info;          /* info->params: the parameter types (imports have no locals) */
    nc_sym name;
    nc_span span;
    bool is_public;
    bool is_c;                 /* the C calling convention, symbol info->symbol */
    bool is_import;            /* defined in C: no blocks */
    const nc_type *ret;
    nc_nir_local *locals;   /* parameters first, in order */
    uint32_t nlocals, cap_locals;
    uint32_t nparams;
    nc_nir_block *blocks;   /* block 0 is the entry */
    uint32_t nblocks, cap_blocks;
} nc_nir_fn;

/* A module-level static: storage with a constant initialiser. */
typedef struct nc_nir_static {
    nc_static_info *info;
    nc_sym name;
    nc_span span;
    const nc_type *type;
    bool is_const;
    bool is_public;
    const nc_expr *init;    /* a constant expression (nc_is_constant_expr) */
    nc_sym export_symbol;   /* @export: the unmangled symbol, or 0 */
} nc_nir_static;

typedef struct nc_nir_module {
    nc_path name;           /* module path; from the file stem when undeclared */
    uint32_t file;
    nc_nir_fn **fns;
    uint32_t nfns;
    nc_nir_static *statics;
    uint32_t nstatics;
} nc_nir_module;

/* The type of a place's base (a local's or a static's type). */
const nc_type *nc_nir_place_base_type(const nc_nir_module *m, const nc_nir_fn *f, const nc_place *p);

/* Lower a type-checked module (NOVAC_COMPILER_ARCHITECTURE.md §9.1).
 * Memory comes from the session arena and the C heap; release with
 * nc_nir_free. */
nc_nir_module *nc_nir_lower(nc_session *s, nc_module_ast *m);
void nc_nir_free(nc_nir_module *m);

/* Structural checks: every block terminated, every target and local in
 * range, operand types consistent. Returns false and reports an internal
 * error description in `why` on failure. */
bool nc_nir_verify(const nc_nir_module *m, char *why, size_t n);

/* --emit=nir */
void nc_nir_dump(nc_session *s, const nc_nir_module *m, FILE *out);

const char *nc_nir_binop_name(nc_nir_binop op);

#endif
