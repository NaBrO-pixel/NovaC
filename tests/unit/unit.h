/* Minimal unit-test harness: no dependencies beyond the C library. */
#ifndef NC_TESTS_UNIT_H
#define NC_TESTS_UNIT_H

#include <stdio.h>
#include <string.h>

extern int nc_test_failures;
extern int nc_test_checks;

#define CHECK(cond) \
    do { \
        nc_test_checks++; \
        if (!(cond)) { \
            nc_test_failures++; \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_STR(a, b) \
    do { \
        const char *a_ = (a), *b_ = (b); \
        nc_test_checks++; \
        if (strcmp(a_, b_) != 0) { \
            nc_test_failures++; \
            fprintf(stderr, "%s:%d: CHECK_STR failed:\n--- got ---\n%s\n--- expected ---\n%s\n", __FILE__, \
                    __LINE__, a_, b_); \
        } \
    } while (0)

void test_support(void);
void test_diag(void);
void test_consteval(void);
void test_lexer(void);

#endif
