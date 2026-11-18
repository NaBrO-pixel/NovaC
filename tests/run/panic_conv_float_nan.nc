// NaN converts to no integer.
i32 f(f64 x) { return i32(x / x); } // PANIC: value out of range in conversion to 'i32'
int main(void) { return f(0.0); }
