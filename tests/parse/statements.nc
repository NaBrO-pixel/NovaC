void f(i32 n) {
    {
        ;
    }
    if (n > 0) n = 1;
    if (n > 0) { n = 2; } else { n = 3; }
    if (n == 1) n = 4; else if (n == 2) n = 5; else n = 6;
    if (n == 1) if (n == 2) n = 7; else n = 8;   // else binds to the inner if
    while (n < 10) n++;
    do { n--; } while (n > 0);
    do n++; while (n < 3);
    for (i32 i = 0; i < 10; i++) { continue; }
    for (i32 i = 0, j = 10; i < j; i++, j--) { }
    for (n = 0; n < 3; ++n) { break; }
    for (;;) { break; }
    return;
}
