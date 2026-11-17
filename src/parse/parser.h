/* Parser (NOVAC_GRAMMAR.md). Recursive descent for declarations and
 * statements, precedence climbing for expressions. Syntax errors (E2xxx)
 * are reported with recovery at statement and declaration boundaries, so
 * parsing always produces a module. */
#ifndef NC_PARSE_PARSER_H
#define NC_PARSE_PARSER_H

#include "ast/ast.h"
#include "lex/lexer.h"

nc_module_ast *nc_parse(nc_session *s, uint32_t file, nc_token_list toks);

#endif
