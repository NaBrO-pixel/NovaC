#include "driver/frontend.h"

#include "lex/lexer.h"
#include "parse/parser.h"
#include "sema/sema.h"

bool nc_frontend_run(nc_session *s, uint32_t file, nc_action action, FILE *out)
{
    unsigned errors_before = s->diag.errors;
    nc_token_list toks = nc_lex(s, file);
    if (action == NC_ACT_TOKENS) {
        /* Tokens are printed even after lexical errors, to show recovery. */
        if (out) nc_lex_dump(s, toks, out);
        return s->diag.errors == errors_before;
    }

    /* Each stage runs only if the previous ones reported no errors: the lexer
     * drops invalid characters, and parsing what is left would mostly report
     * consequences of the same mistake. */
    if (s->diag.errors != errors_before) return false;
    nc_module_ast *m = nc_parse(s, file, toks);
    if (s->diag.errors != errors_before) return false;
    if (action == NC_ACT_AST) {
        if (out) nc_ast_dump(s, m, NULL, out);
        return true;
    }

    /* Semantic analysis runs only on syntactically valid modules, so it never
     * sees the parser's recovery artefacts. */
    if (!nc_sema_check(s, m)) return false;
    if (action == NC_ACT_TYPED && out) nc_ast_dump(s, m, nc_sema_print_type, out);
    return s->diag.errors == errors_before;
}
