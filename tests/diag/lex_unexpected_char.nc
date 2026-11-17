i32 f() {
    i32 a = 1 $ 2;     // ERROR E1002
    i32 b = `x`;       // ERROR E1002, ERROR E1002
    i32 c = a # b;     // ERROR E1002
    i32 d = a × b;     // ERROR E1002
    return \ a;        // ERROR E1002
}
