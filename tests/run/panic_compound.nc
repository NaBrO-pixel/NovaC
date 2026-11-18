// Compound assignment is checked too.
i16 f(i16 a) { a *= 300; return a; } // PANIC: integer overflow in '*'
int main(void) { return i32(f(300)); }
