// Every run-time check is an explicit `assert` in NIR.
i32 arith(i32 a, i32 b) { return a + b * 2; }
u32 wrapping(u32 a, u32 b) { return a +% b; }
i64 divide(i64 a, i64 b) { return a / b; }
u64 udivide(u64 a, u64 b) { return a % b; }
u16 shift(u16 a, u8 n) { return a << n; }
i32 negate(i32 a) { return -a; }
