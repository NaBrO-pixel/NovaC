// Checked conversions that succeed, and implicit widening.
i64 widen(i32 x) { return x; }
f64 widen_f(f32 x) { return x; }
int main(void) {
    i32 a = -5;
    if (widen(a) != -5) return 1;
    u8 b = 200;
    i16 c = b;                       // unsigned to wider signed
    if (c != 200) return 2;
    if (u8(i32(255)) != 255) return 3;
    if (i8(i64(-128)) != -128) return 4;
    if (u32(i64(4000000000)) != 4000000000) return 5;
    if (i32(u32(2147483647)) != 2147483647) return 6;
    f64 d = 3.99;
    if (i32(d) != 3) return 7;
    if (i32(-d) != -3) return 8;     // truncation toward zero
    if (u8(255.9) != 255) return 9;
    if (i64(-9223372036854775808.0) != -9223372036854775807 - 1) return 10;
    if (f64(7) != 7.0) return 11;
    if (f32(0.1) == f32(0.2)) return 12;
    if (widen_f(1.5f32) != 1.5) return 13;
    if (i32(true) != 1 || u8(false) != 0) return 14;
    u64 huge = 18446744073709551615;
    if (f64(huge) != 18446744073709551616.0) return 15;
    i8 m1 = -1;
    if (i64(m1) != -1) return 16;
    return 0;
}
