// Untyped constants take their type from context (NOVAC_TYPE_SYSTEM.md §5.1, §8).
void f(u8 small, i64 big) {
    u8 a = 200 + 55;              // folded exactly, then fits u8
    i64 b = 1 << 40;              // width from the expected type
    i64 c = big + 1;              // the literal adopts i64
    u8 d = small * 2;             // the literal adopts u8
    auto e = 7;                   // default i32
    auto g = 2.5;                 // default f64
    f32 h = 1;                    // exact integer to float
    f64 i = 1 / 2;                // integer division, then converted: 0
    f64 j = 1.0 / 2;              // mixed untyped: float 0.5
    u8 k = ~0;                    // ~ at the expected width: 255
    i32 m = -2147483648;          // i32::MIN
    i8 n = -128i8;                // negative suffixed literal
    u64 o = 18446744073709551615;
    u8 p = 255 +% 1;              // wrapping: 0
    i32 q = 7 / -2;               // truncation toward zero: -3
    i32 r = -7 % 2;               // remainder has the dividend's sign: -1
    bool s = 1 < 2 && 2.5 > 1;
    i32 t = true ? 1 : 2;
    i32 u = 0x7FFF_FFFF;
}
