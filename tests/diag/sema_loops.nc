void f() {
    break;                     // ERROR E4062
    continue;                  // ERROR E4062
    while (true) { break; }
    for (;;) { if (true) { continue; } break; }
}
