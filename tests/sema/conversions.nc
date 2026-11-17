void f(i64 big, f64 real, bool flag) {
    i32 a = i32(big);             // checked at run time
    u8 b = u8(200);               // constant: checked now
    i32 c = i32(real);
    f32 d = f32(real);
    i32 e = i32(flag);
    i32 g = i32(-3.9);            // truncates toward zero: -3
    f64 h = f64(1 << 20);
    u64 k = u64(255u8);
}
