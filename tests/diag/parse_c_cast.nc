i32 f(i64 x) {
    i32 a = (i32)x;        // ERROR E2031
    f64 b = (f64)(a);      // ERROR E2031
    u8 c = (unsigned char)a; // ERROR E2031, ERROR E3011
    i32 d = (a);           // a parenthesised expression, not a cast
    return d;
}
