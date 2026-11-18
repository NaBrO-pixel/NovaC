// Negating the minimum value overflows.
i8 f(i8 a) { return -a; } // PANIC: integer overflow in negation
int main(void) { return i32(f(-128i8)); }
