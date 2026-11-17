i32 f() {
    i32 a = 10q;         // ERROR E1022
    f32 b = 1f32;        // ERROR E1022
    u8 c = 1.0u8;        // ERROR E1022
    i32 d = 1e;          // ERROR E1022
    i32 e = 5_i32;       // ERROR E1024
    return 0;
}
