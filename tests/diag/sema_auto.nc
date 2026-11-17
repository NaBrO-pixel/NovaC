void f() {
    auto a;                    // ERROR E4064
    const auto b;              // ERROR E4064
    const i32 c;               // ERROR E4065
    i32 d;                     // deferred initialisation is allowed
    d = 1;
}
