i32 g(i32 x, f64 y) { return x; }
void f(i64 big) {
    i32 a = i32(big);
    u8 b = u8(a);
    f64 c = f64(a) * 2.0;
    bool d = bool(a);
    int e = int(c);
    i32 r = g(1, 2.0);
    g(g(1, 2.0), f64(i32(3)));
}
