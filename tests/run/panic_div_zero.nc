// Division by zero.
u64 f(u64 a, u64 b) { return a / b; } // PANIC: division by zero
int main(void) { return i32(f(1, 0)); }
