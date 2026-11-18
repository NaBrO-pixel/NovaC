// Remainder by zero.
i32 f(i32 a, i32 b) { return a % b; } // PANIC: division by zero
int main(void) { return f(1, 0); }
