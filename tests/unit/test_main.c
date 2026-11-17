#include "unit.h"

int nc_test_failures;
int nc_test_checks;

int main(void)
{
    test_support();
    test_diag();
    test_consteval();
    test_lexer();
    if (nc_test_failures) {
        fprintf(stderr, "unit tests: %d of %d checks FAILED\n", nc_test_failures, nc_test_checks);
        return 1;
    }
    printf("unit tests: all %d checks passed\n", nc_test_checks);
    return 0;
}
