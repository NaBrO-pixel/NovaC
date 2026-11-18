// Definite initialisation (NOVAC_MEMORY_MODEL.md §5.2): every read needs an
// assignment on every path before it.
i32 never() {
    i32 x;
    return x;                    // ERROR E5031
}
i32 one_path(bool c) {
    i32 x;
    if (c) { x = 1; }
    return x;                    // ERROR E5031
}
i32 loop_only(i32 n) {
    i32 x;
    for (i32 i = 0; i < n; i++) { x = i; }
    return x;                    // ERROR E5031
}
i32 reported_once() {
    i32 y;
    i32 a = y + y;               // ERROR E5031
    return a + y;
}
i32 both_paths(bool c) {
    i32 x;
    if (c) { x = 1; } else { x = 2; }
    return x;                    // initialised on every path: fine
}
i32 do_body() {
    i32 x;
    do { x = 3; } while (false);
    return x;                    // a do body always runs: fine
}
i32 after_infinite_loop() {
    i32 x;
    while (true) { x = 1; break; }
    return x;                    // the only exit is after the assignment: fine
}
i32 compound() {
    i32 z;
    z += 1;                      // ERROR E5031
    return z;
}
