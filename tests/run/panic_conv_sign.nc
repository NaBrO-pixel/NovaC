// u32 to i32: values above i32 MAX do not fit.
i32 f(u32 x) { return i32(x); } // PANIC: value out of range in conversion to 'i32'
int main(void) { return f(2147483648); }
