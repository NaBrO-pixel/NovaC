// `>>=` checks its amount too.
i8 f(i8 a, u64 n) { a >>= n; return a; } // PANIC: shift amount too large
int main(void) { return i32(f(-1i8, 8)); }
