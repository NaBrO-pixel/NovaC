// Each broken statement yields exactly one error; parsing resumes after it.
i32 f(i32 a) {
    i32 b = a + 1      // ERROR E2001
    i32 c = (a + 2;    // ERROR E2001
    i32 d = a +;       // ERROR E2001
    if a > 0 { }       // ERROR E2001
    while (a) a = 1    // ERROR E2001
    return a;
}
i32 g( {               // ERROR E2001
    return 0;
}
i32 = 5;               // ERROR E2001
@ i32 h() { return 0; } // ERROR E2001
i32 k() { else { } return 1; } // ERROR E2001
i32 x;                 // ERROR E2001
module late;           // ERROR E2001
i32 m(i32 a, ) { return a; } // ERROR E2001
