// A negative value never fits an unsigned type.
u64 f(i8 x) { return u64(x); } // PANIC: value out of range in conversion to 'u64'
int main(void) { return i32(f(-1i8)); }
