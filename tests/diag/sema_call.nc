i32 two(i32 a, i32 b) { return a + b; }
i32 f() {
    i32 x = two(1);            // ERROR E4002
    i32 y = two(1, 2, 3);      // ERROR E4002
    i32 v = 5;
    i32 z = v(1);              // ERROR E4003
    i32 w = (1 + 2)(3);        // ERROR E4003
    return 0;
}
