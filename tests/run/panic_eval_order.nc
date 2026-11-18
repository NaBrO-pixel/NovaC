// Arguments are evaluated left to right (D-024): the first operand
// panics before the second is evaluated.
i32 bad_first(i32 x) { return 10 / x; } // PANIC: division by zero
i32 bad_second(i32 x) { return 10 % x; }
i32 pair(i32 a, i32 b) { return a + b; }
int main(void) { i32 z = 0; return pair(bad_first(z), bad_second(z)); }
