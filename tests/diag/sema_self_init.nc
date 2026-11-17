i32 f(i32 a) {
    i32 x = x + 1;             // ERROR E3003
    auto y = y;                // ERROR E3003
    i32 z = 1;
    {
        i32 z = z * 2;         // ERROR E3003
    }
    i32 p = a, q = p;          // earlier declarators are initialised: allowed
    i32 g = g(1);              // ERROR E3003
    return q;
}
