i32 f() {
    i32 a = 0x;          // ERROR E1024
    i32 b = 0b102;       // ERROR E1024
    i32 c = 0o78;        // ERROR E1024
    i32 d = 1__000;      // ERROR E1024
    i32 e = 1_;          // ERROR E1024
    i32 g = 0x_1;        // ERROR E1024
    f64 h = 0x1.8;       // ERROR E1024
    f64 k = 0x1p;        // ERROR E1024
    return 0;
}
