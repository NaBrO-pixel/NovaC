void f(i32 v) {
    i8 a = 127i8 + 1i8;        // ERROR E4040
    i32 b = 2147483647 + v;    // fine: not constant
    i32 c = 1 / 0;             // ERROR E4040
    i32 d = 5 % 0;             // ERROR E4040
    u8 e = 1 << 8;             // ERROR E4040
    i32 g = -(-2147483648i32); // ERROR E4040
    i32 h = 18446744073709551615 * 2; // ERROR E4040
    i8 k = -128i8 / -1i8;      // ERROR E4040
    i8 m = -128i8 % -1i8;      // ERROR E4040
    u8 n = 300;                // ERROR E4041
    i8 o = -129;               // ERROR E4041
    u8 p = 256u8;              // ERROR E4041
    i8 q = -129i8;             // ERROR E4041
    auto r = 3000000000;       // ERROR E4041
    f32 s = 16777217;          // ERROR E4041
    i8 ok = -128i8;
    u8 ok2 = 255 +% 1 -% 1;
    u64 ok3 = 18446744073709551615;
    i64 ok4 = 1 << 62;
}
