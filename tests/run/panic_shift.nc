// A shift amount at or above the width panics.
u32 f(u32 a, u32 n) { return a << n; } // PANIC: shift amount too large
int main(void) { return i32(f(1, 32)); }
