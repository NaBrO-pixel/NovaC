#include "unit.h"

#include "diag/diag.h"

#include <stdlib.h>

/* Render diagnostics into a temporary file and return its contents. */
static char *slurp(FILE *f)
{
    long n = ftell(f);
    rewind(f);
    char *buf = calloc((size_t)n + 1, 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    return buf;
}

void test_diag(void)
{
    nc_source_map m;
    nc_source_init(&m);
    const char *text = "i32 x = a + b;\n\tbad();\n";
    uint32_t f = nc_source_add(&m, "demo.nc", text, strlen(text));

    FILE *out = tmpfile();
    nc_diag d;
    nc_diag_init(&d, &m, out);
    nc_diag_error(&d, (nc_span){f, 8, 13}, "E4010", "operands of '%s' differ", "+");
    nc_diag_help(&d, "convert one");
    nc_diag_note(&d, (nc_span){f, 16, 19}, "tab-indented");
    nc_diag_note(&d, NC_NO_SPAN, "plain note");
    nc_diag_warning(&d, (nc_span){f, 0, 0}, "W4001", "zero-width");
    nc_diag_error(&d, NC_NO_SPAN, "E0002", "unknown option '%s'", "-x");
    char *got = slurp(out);
    CHECK_STR(got,
              "demo.nc:1:9: error[E4010]: operands of '+' differ\n"
              "   1 | i32 x = a + b;\n"
              "     |         ^~~~~\n"
              "      = help: convert one\n"
              "demo.nc:2:2: note: tab-indented\n"
              "   2 | \tbad();\n"
              "     | \t^~~\n"
              "      = note: plain note\n"
              "demo.nc:1:1: warning[W4001]: zero-width\n"
              "   1 | i32 x = a + b;\n"
              "     | ^\n"
              "ncc: error[E0002]: unknown option '-x'\n");
    CHECK(d.errors == 2 && d.warnings == 1);
    free(got);
    fclose(out);

    /* Error limit: the third error is suppressed with its notes. */
    out = tmpfile();
    nc_diag_init(&d, &m, out);
    d.error_limit = 2;
    nc_diag_error(&d, NC_NO_SPAN, "E0001", "one");
    nc_diag_error(&d, NC_NO_SPAN, "E0001", "two");
    nc_diag_error(&d, NC_NO_SPAN, "E0001", "three");
    nc_diag_help(&d, "never shown");
    got = slurp(out);
    CHECK_STR(got,
              "ncc: error[E0001]: one\n"
              "ncc: error[E0001]: two\n"
              "ncc: error: too many errors (limit 2); stopping\n");
    CHECK(d.limit_reached && d.errors == 2);
    free(got);
    fclose(out);

    /* -Werror */
    out = tmpfile();
    nc_diag_init(&d, &m, out);
    d.werror = true;
    nc_diag_warning(&d, NC_NO_SPAN, "W4001", "promoted");
    got = slurp(out);
    CHECK_STR(got, "ncc: error[W4001]: promoted\n");
    CHECK(d.errors == 1 && nc_diag_has_errors(&d));
    free(got);
    fclose(out);

    nc_source_free(&m);
}
