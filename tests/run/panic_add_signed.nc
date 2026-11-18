// Signed overflow in `+` panics (D-009).
i32 f(i32 a, i32 b) { return a + b; } // PANIC: integer overflow in '+'
int main(void) { return f(2147483647, 1); }
