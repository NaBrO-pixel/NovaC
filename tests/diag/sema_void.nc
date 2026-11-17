void nothing() { }
void f(void v) {               // ERROR E4005
    void x;                    // ERROR E4005
    i32 a = nothing();         // ERROR E4006
    i32 b = 1 + nothing();     // ERROR E4006
}
