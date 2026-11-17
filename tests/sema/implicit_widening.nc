// Lossless widening is implicit at coercion sites only (D-037).
i64 widen(i32 x) { return x; }
f64 to_f64(f32 x) { return x; }
void f(i8 a, u8 b, u32 c, i32 d, f32 e) {
    i16 x1 = a;
    i64 x2 = a;
    u16 x3 = b;
    i16 x4 = b;                   // unsigned to wider signed
    u64 x5 = c;
    usize x6 = c;
    isize x7 = d;
    f64 x8 = e;
    i64 x9 = widen(a);
    i64 sum = i64(d) + x2;        // operands are never widened implicitly
}
