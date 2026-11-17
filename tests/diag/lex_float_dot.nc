f64 f() {
    f64 a = .5;          // ERROR E1021
    f64 b = 5.;          // ERROR E1021
    f64 c = 1.e5;        // ERROR E1021
    return 0.5;
}
