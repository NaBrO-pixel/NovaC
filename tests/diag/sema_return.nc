i32 no_value() { return; }                 // ERROR E4008
void with_value() { return 1; }            // ERROR E4009
i32 missing(bool c) { if (c) { return 1; } }   // ERROR E4060
i32 empty() { }                            // ERROR E4060
i32 loop_break() { while (true) { break; } }   // ERROR E4060
i32 do_continue(bool c) { do { continue; } while (c); }  // ERROR E4060
i32 ok1(bool c) { if (c) { return 1; } else { return 2; } }
i32 ok2() { while (true) { } }
i32 ok3() { for (;;) { } }
i32 ok4() { do { return 1; } while (false); }
i32 ok5(bool c) { { return 3; } }
