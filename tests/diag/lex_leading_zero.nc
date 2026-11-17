i32 f() {
    i32 mode = 0755;     // ERROR E1020
    i32 b = 00;          // ERROR E1020
    i32 c = 0_1;         // ERROR E1020
    i32 ok = 0;
    i32 ok2 = 0o755;
    return ok + ok2;
}
