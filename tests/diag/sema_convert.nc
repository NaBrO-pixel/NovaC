void f(i32 x, bool flag, f64 d) {
    bool b = bool(x);          // ERROR E4007
    f64 e = f64(flag);         // ERROR E4007
    u8 a = u8(300);            // ERROR E4040
    u8 n = u8(-1);             // ERROR E4040
    i32 big = i32(1e10);       // ERROR E4040
    i32 nan = i32(0.0 / 0.0);  // ERROR E4040
    u8 ok = u8(x);             // checked at run time
    i32 ok2 = i32(flag);       // 0 or 1
    i32 ok3 = i32(d);          // truncates; checked at run time
    i32 ok4 = i32(3.9);        // 3
}
