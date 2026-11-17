i32 g() { return 1; }
void f(const i32 p) {
    const i32 c = 1;
    c = 2;                     // ERROR E4004
    c += 1;                    // ERROR E4004
    c++;                       // ERROR E4004
    p = 3;                     // ERROR E4004
    g() = 4;                   // ERROR E4004
    i32 a = 0;
    i32 b = 0;
    a = b = 1;                 // ERROR E4030
    if (a = 1) { }             // ERROR E4030
    i32 c2 = (a = 2);          // ERROR E4030
}
