// `++` is checked like `+= 1`.
u8 f(u8 a) { a++; return a; } // PANIC: integer overflow in '++'
int main(void) { return i32(f(255)); }
