bool f(i32 a, i32 b, i32 c) {
    bool x = a < b < c;          // ERROR E2010
    bool y = a == b == c;        // ERROR E2010
    bool z = a <= b > c;         // ERROR E2010
    bool ok = a < b && b < c;
    bool ok2 = a < b == (b < c); // different levels: not a chain
    return ok;
}
