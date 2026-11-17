i32 dup() { return 1; }
i32 dup() { return 2; }        // ERROR E3002
void f(i32 a, i32 a) { }       // ERROR E3002
void g(i32 p) {
    i32 p = 1;                 // ERROR E3002
    i32 x = 1;
    i32 x = 2;                 // ERROR E3002
    i32 y = 1, y = 2;          // ERROR E3002
    {
        i32 x = 3;             // shadowing in an inner block is allowed
    }
}
