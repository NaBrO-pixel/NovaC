/* Lexer (NOVAC_GRAMMAR.md §2). Turns one source file into an array of
 * tokens terminated by NC_T_EOF. Lexical errors are reported (E1xxx) and
 * the offending characters skipped, so lexing always completes. */
#ifndef NC_LEX_LEXER_H
#define NC_LEX_LEXER_H

#include "lex/token.h"
#include "session/session.h"

typedef struct nc_token_list {
    nc_token *tokens; /* allocated in the session arena */
    uint32_t count;   /* including the final EOF token */
} nc_token_list;

nc_token_list nc_lex(nc_session *s, uint32_t file);

/* --dump-tokens format: one token per line, "line:col kind [value]". */
void nc_lex_dump(nc_session *s, nc_token_list toks, FILE *out);

/* Print bytes as a double-quoted string with C-style escapes. */
void nc_print_escaped(FILE *out, const char *s, size_t n);

#endif
