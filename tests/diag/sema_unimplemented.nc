// Recognised constructs of later phases are rejected with E9001.
@inline i32 attr() { return 0; }       // ERROR E9001
extern "C" i32 ext(i32 a);             // ERROR E9001
unsafe void u() { }                    // ERROR E9001
T id<T>(T x) { return x; }             // ERROR E9001, ERROR E3001, ERROR E3001
void decl_only();                      // ERROR E9001
void f(i32 a) {
    i32* p = 0;                        // ERROR E9001
    i32& r = a;                        // ERROR E9001
    i32[4] arr = 0;                    // ERROR E9001
    i32 fn(i32) fp = 0;                // ERROR E9001
    slice<u8> s = 0;                   // ERROR E9001
    atomic<u32> at = 0;                // ERROR E9001
    a = 'x';                           // ERROR E9001
    a = "str";                         // ERROR E9001
    a = a.field;                       // ERROR E9001
    a = a[0];                          // ERROR E9001
    a = sizeof(i32);                   // ERROR E9001
    i32 l = { 1 };                     // ERROR E9001
    static i32 st = 0;                 // ERROR E9001
    i32 g = attr;                      // ERROR E9001
    a = *a;                            // ERROR E9001
    a = &a;                            // ERROR E9001
}
