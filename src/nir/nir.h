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
 * produces SSA. In Phase 3 places are whole locals (no projections), and
 * every local has a primitive type. */
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

typedef enum nc_operand_kind { NC_OP_COPY, NC_OP_CONST } nc_operand_kind;

typedef struct nc_operand {
    nc_operand_kind kind;
    nc_local_id local;   /* COPY */
    nc_cval cval;        /* CONST */
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
} nc_cast_kind;

typedef enum nc_rvalue_kind { NC_RV_USE, NC_RV_BINARY, NC_RV_UNARY, NC_RV_CAST } nc_rvalue_kind;

typedef struct nc_rvalue {
    nc_rvalue_kind kind;
    nc_nir_binop binop;
    nc_nir_unop unop;
    nc_cast_kind cast;
    nc_operand a, b;
} nc_rvalue;

typedef enum nc_stmt_kind_nir {
    NC_ST_ASSIGN,  /* dest = rvalue */
    NC_ST_CHECKED, /* dest = a op b (wrapping); flag = whether the exact result overflowed */
} nc_stmt_kind_nir;

typedef struct nc_nir_stmt {
    nc_stmt_kind_nir kind;
    nc_local_id dest;
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
    NC_TERM_UNREACHABLE, /* control cannot reach here (proved by sema) */
} nc_term_kind;

typedef struct nc_terminator {
    nc_term_kind kind;
    nc_span span;
    nc_operand cond;     /* BRANCH, ASSERT */
    nc_operand value;    /* RETURN */
    bool has_value;
    nc_block_id target;  /* GOTO, BRANCH (true), CALL, ASSERT */
    nc_block_id other;   /* BRANCH (false) */
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
    nc_fn_info *info;
    nc_sym name;
    nc_span span;
    bool is_public;
    const nc_type *ret;
    nc_nir_local *locals;   /* parameters first, in order */
    uint32_t nlocals, cap_locals;
    uint32_t nparams;
    nc_nir_block *blocks;   /* block 0 is the entry */
    uint32_t nblocks, cap_blocks;
} nc_nir_fn;

typedef struct nc_nir_module {
    nc_path name;           /* module path; from the file stem when undeclared */
    uint32_t file;
    nc_nir_fn **fns;
    uint32_t nfns;
} nc_nir_module;

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
