// Recognised constructs of later phases are rejected with E9001.
@inline i32 attr() { return 0; }       // ERROR E9001
extern "C" i32 ext(i32 a);             // ERROR E9001
T id<T>(T x) { return x; }             // ERROR E9001, ERROR E3001, ERROR E3001
void decl_only();                      // ERROR E9001
struct Gen<T> { i32 x; }               // ERROR E9001
void f(i32 a) {
    i32& r = a;                        // ERROR E9001
    i32 fn(i32) fp = 0;                // ERROR E9001
    slice<u8> s = 0;                   // ERROR E9001
    atomic<u32> at = 0;                // ERROR E9001
    mmio<u32> reg = 0;                 // ERROR E9001
    a = "str";                         // ERROR E9001
    static i32 st = 0;                 // ERROR E9001
    i32 g = attr;                      // ERROR E9001
    auto p = &a;                       // ERROR E9001
    bool same = &a == &a;              // ERROR E9001, ERROR E9001
}
