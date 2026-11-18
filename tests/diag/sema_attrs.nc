// Attributes (NOVAC_SPEC.md §9).
@nosuch struct A { i32 x; }                 // ERROR E3040
@packed union U { i32 x; }                  // ERROR E3041
@packed(1) struct B { i32 x; }              // ERROR E3041
@repr(D) struct C { i32 x; }                // ERROR E3041
@repr(C) struct D { i32 x; }
@packed i32 f() { return 0; }               // ERROR E3041
@cold i32 g() { return 0; }                 // ERROR E9001
