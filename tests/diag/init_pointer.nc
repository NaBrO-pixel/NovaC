// Writing through a pointer reads the pointer, which must be initialised.
@mode(controlled) module init_pointer;
void f() {
    i32* p;
    unsafe { *p = 1; }                      // ERROR E5031
    i32 x;
    i32* q = &mut x;                        // ERROR E5031
}
