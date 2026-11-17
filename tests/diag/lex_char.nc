i32 f() {
    u8 a = '';           // ERROR E1032
    u8 b = 'ab';         // ERROR E1032
    u8 c = 'é';          // ERROR E1032
    u8 d = '\x80';       // ERROR E1032
    return 0;
}
