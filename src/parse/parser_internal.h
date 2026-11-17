/* Private interface shared by the parser's translation units. */
#ifndef NC_PARSE_PARSER_INTERNAL_H
#define NC_PARSE_PARSER_INTERNAL_H

#include "parse/parser.h"
#include "support/vec.h"

/* Limit on the depth of the trees the parser builds (expressions, types,
 * statements). Later stages walk these trees recursively, so the limit
 * counts tree depth, not only syntactic nesting: every operator in a chain
 * `a + b + c`, every postfix operation in `a.b().c` and every type suffix
 * in `i32**` adds a level. Deeper input is rejected with E9004 instead of
 * overflowing the compiler's stack (verified under AddressSanitizer, whose
 * stack frames are the largest; tests/diag/parse_depth_*.nc). */
#define NC_PARSE_MAX_DEPTH 1000

typedef struct nc_parser {
    nc_session *s;
    uint32_t file;
    const nc_token *toks;
    uint32_t count;
    uint32_t pos;
    bool panic;          /* an error was reported; suppress more until resync */
    unsigned depth;
    bool depth_reported;
    nc_sym sym_underscore;
    nc_sym sym_c_words[7]; /* char short long unsigned signed float double */
} nc_parser;

typedef NC_VEC(nc_expr *) nc_expr_vec;
typedef NC_VEC(nc_stmt *) nc_stmt_vec;

/* ---- token access ---- */
const nc_token *nc_p_cur(nc_parser *p);
const nc_token *nc_p_peek(nc_parser *p, uint32_t ahead);
const nc_token *nc_p_prev(nc_parser *p);
bool nc_p_at(nc_parser *p, nc_tok kind);
bool nc_p_accept(nc_parser *p, nc_tok kind);
const nc_token *nc_p_advance(nc_parser *p);
/* Reports E2001 "expected <what>" when the current token is not `kind`. */
bool nc_p_expect(nc_parser *p, nc_tok kind, const char *what);

/* ---- diagnostics ---- */
/* Reported only outside panic mode; enters panic mode (the caller will
 * resynchronise). Returns true if the diagnostic was emitted, so that help
 * lines are attached only to it. */
bool nc_p_error(nc_parser *p, nc_span span, const char *code, const char *fmt, ...) NC_PRINTF(4, 5);
/* For errors the parser fully recovers from in place (E2010, E2020, E2031,
 * E3011): reported unless in panic mode, does not enter panic mode. */
bool nc_p_recoverable(nc_parser *p, nc_span span, const char *code, const char *fmt, ...) NC_PRINTF(4, 5);
/* E2001 at the current token: "expected <what>, found <current token>". */
void nc_p_expected(nc_parser *p, const char *what);
nc_span nc_p_cur_span(nc_parser *p);
nc_span nc_p_prev_end(nc_parser *p);
void nc_p_describe(nc_parser *p, const nc_token *t, char *buf, size_t n);

/* ---- nesting guard ---- */
bool nc_p_enter(nc_parser *p); /* false (and E9004 once) when too deep */
void nc_p_leave(nc_parser *p);

/* ---- recovery ---- */
void nc_p_sync_stmt(nc_parser *p);
/* Skip a declaration: through a `;` at depth 0, or through a `{...}` body
 * (and one optional `;` after it). */
void nc_p_skip_decl(nc_parser *p);
void nc_p_skip_balanced(nc_parser *p); /* current token opens ( [ { */

/* ---- allocation helpers ---- */
void *nc_p_alloc(nc_parser *p, size_t size);
nc_expr *nc_p_expr(nc_parser *p, nc_expr_kind kind, nc_span span);
nc_stmt *nc_p_stmt(nc_parser *p, nc_stmt_kind kind, nc_span span);
nc_type_expr *nc_p_type_node(nc_parser *p, nc_type_expr_kind kind, nc_span span);
nc_expr **nc_p_expr_list(nc_parser *p, nc_expr_vec *v);

/* ---- grammar ---- */
nc_type_expr *nc_parse_type(nc_parser *p);
/* Position just after a syntactically valid type starting at `pos`, or
 * UINT32_MAX. Builds nothing and reports nothing (declaration lookahead). */
uint32_t nc_skip_type(nc_parser *p, uint32_t pos);
bool nc_p_is_c_type_word(nc_parser *p, const nc_token *t);
bool nc_parse_generic_args(nc_parser *p, nc_generic_arg **out, uint32_t *n);

nc_expr *nc_parse_expr(nc_parser *p);
nc_init *nc_parse_initializer(nc_parser *p);
bool nc_parse_init_items(nc_parser *p, nc_init_item **out, uint32_t *n); /* at '{' */

nc_stmt *nc_parse_stmt(nc_parser *p);
nc_stmt *nc_parse_block(nc_parser *p);
bool nc_p_is_decl_start(nc_parser *p);
nc_stmt *nc_parse_var_decl(nc_parser *p, bool require_semi);

#endif
