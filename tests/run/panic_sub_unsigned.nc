// Unsigned underflow in `-` panics; C would wrap silently (D-011).
u32 f(u32 a, u32 b) { return a - b; } // PANIC: integer overflow in '-'
int main(void) { return i32(f(1, 2)); }
