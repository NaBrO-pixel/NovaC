// A float above the range of the integer type.
i64 f(f32 x) { return i64(x); } // PANIC: value out of range in conversion to 'i64'
int main(void) { return i32(i64(f(1e19f32)) % 2); }
