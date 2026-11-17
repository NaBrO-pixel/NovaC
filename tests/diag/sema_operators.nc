void f(i32 a, u8 b, f64 d, bool p, u32 u, i64 w) {
    i32 x = a + b;             // ERROR E4010
    f64 y = d * a;             // ERROR E4010
    bool q = a < w;            // ERROR E4010
    f64 r = d % d;             // ERROR E4011
    bool s = p < p;            // ERROR E4011
    i32 t = !a;                // ERROR E4011
    bool v = ~p;               // ERROR E4011
    bool z = p + p;            // ERROR E4011
    bool aa = a && p;          // ERROR E4011
    f64 bb = d << 1u32;        // ERROR E4011
    f64 cc = d +% d;           // ERROR E4011
    u32 neg = -u;              // ERROR E4012
    u8 neg2 = -1u8;            // ERROR E4012
    i32 sh = a << a;           // ERROR E4013
    i32 sh2 = a << -1;         // ERROR E4013
    bool ok = p == p;
    bool ok2 = (p & p) | (p ^ p);
    i32 ok3 = a << u;
}
