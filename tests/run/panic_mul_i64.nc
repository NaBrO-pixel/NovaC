// i64 multiplication overflow.
i64 f(i64 a) { return a * a; } // PANIC: integer overflow in '*'
int main(void) { return i32(f(4294967296)); }
