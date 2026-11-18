// Narrowing conversion of a value that does not fit.
u8 f(i32 x) { return u8(x); } // PANIC: value out of range in conversion to 'u8'
int main(void) { return i32(f(256)); }
