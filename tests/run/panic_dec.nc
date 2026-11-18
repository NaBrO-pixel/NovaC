// `--` on an unsigned zero.
usize f(usize a) { --a; return a; } // PANIC: integer overflow in '--'
int main(void) { return i32(f(0)); }
