i32 f() {
    i32 a = "abc;        // ERROR E1030
    i32 b = 'x;          // ERROR E1030
    return 0;
}
