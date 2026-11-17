/* Semantic analysis (NOVAC_COMPILER_ARCHITECTURE.md §7.4): name
 * resolution, type checking and constant evaluation. Annotates the AST in
 * place. Returns false if any error was reported for the module. */
#ifndef NC_SEMA_SEMA_H
#define NC_SEMA_SEMA_H

#include "ast/ast.h"

bool nc_sema_check(nc_session *s, nc_module_ast *m);

/* Type printer for nc_ast_dump (--emit=typed). */
void nc_sema_print_type(FILE *out, const nc_type *t);

#endif
