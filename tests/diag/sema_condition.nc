void f(i32 n, f64 d) {
    if (n) { }                 // ERROR E4020
    while (d) { }              // ERROR E4020
    do { } while (1);          // ERROR E4020
    for (; n; ) { }            // ERROR E4020
    i32 x = n ? 1 : 2;         // ERROR E4020
}
