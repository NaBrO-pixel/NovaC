// switch (D-025).
enum Color { Red, Green, Blue }
i32 g() { return 1; }
i32 f(Color c, i32 x, f64 d) {
    switch (d) { default: break; }          // ERROR E4073
    switch (x) {
    case 1: break;
    case 1: break;                          // ERROR E4070
    case g(): break;                        // ERROR E4082
    default: break;
    default: break;                         // ERROR E4070
    }
    switch (c) {                            // ERROR E4071
    case Color.Red: break;
    }
    switch (x) {
    case 1:                                 // ERROR E2050
        x = 2;
    case 2: break;
    }
    switch (x) {
    case 1:
        if (x == 1) fallthrough;            // ERROR E2051
        break;
    case 2:
        fallthrough;                        // ERROR E2051
    }
    fallthrough;                            // ERROR E2051
    switch (x) { case 1: continue; }        // ERROR E4062
    switch (x) { case 1.5: break; }         // ERROR E4001
    switch (c) { case 0: break; default: break; } // ERROR E4001
    switch (c) {
    case Color.Red, Color.Green: return 1;
    case Color.Blue: return 2;
    }
}
i32 h(i32 x) {                              // ERROR E4060
    switch (x) { case 1: return 1; }
}
