f64 f() {
    f64 big = 1e999;     // ERROR E1025
    f64 tiny = 1e-999;   // underflows to zero: not an error
    return tiny;
}
