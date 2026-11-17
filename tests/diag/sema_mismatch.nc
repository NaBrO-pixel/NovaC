void take_i32(i32 v) { }
i64 f(i64 big, f64 real, bool flag, u32 u) {
    i32 a = big;               // ERROR E4001
    i32 b = real;              // ERROR E4001
    i32 c = flag;              // ERROR E4001
    bool d = 1;                // ERROR E4001
    i32 e = 1.5;               // ERROR E4001
    i32 g = u;                 // ERROR E4001
    take_i32(big);             // ERROR E4001
    f32 h = real;              // ERROR E4001
    i64 ok = a;                // widening is implicit
    f64 ok2 = 1;               // exact integer constant to float
    return big;
}
