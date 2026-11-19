// C interoperability (NOVAC_ABI.md §11).
@mode(controlled) module ffi;
struct Ok { c_int a; f64 b; }
struct Empty { }
@packed struct Packed { u8 a; u32 b; }
enum Plain { A, B }
enum Sized : c_int { X, Y }
struct HasPlain { Plain p; }
extern "C" {
    c_int ok(Ok s, Sized e, const c_char* p, c_void* v, f32 f, bool b);
    void arr(i32[4] a);                         // ERROR E7030
    void empty(Empty e);                        // ERROR E7030
    void packed(Packed p);                      // ERROR E7030
    void plain(Plain p);                        // ERROR E7030
    HasPlain nested();                          // ERROR E7030
    c_int printf(const c_char* fmt, ...);
}
extern "Rust" void other();                     // ERROR E7034
extern "C" void vdef(c_int n, ...) { }          // ERROR E7035
i32 nova_variadic(i32 n, ...) { return n; }     // ERROR E7035
@link_name("x") extern "C" void exported() { }  // ERROR E3041
@export("y") void plain_fn() { }                // ERROR E3041
@export(3) extern "C" void bad_arg() { }        // ERROR E3041
extern "C" void dup() { }
@export("dup") extern "C" void dup2() { }       // ERROR E3002
@export("__nova_mine") extern "C" void res() { } // ERROR E3020
void f(f32 x, i8 c, bool b, Ok s) {
    printf(c"x");                               // ERROR E6003
    unsafe {
        printf(c"%f", x);                       // ERROR E7033
        printf(c"%d", c);                       // ERROR E7033
        printf(c"%d", b);                       // ERROR E7033
        printf(c"%d", s);                       // ERROR E7033
        printf(c"%p", null);                    // ERROR E4001
        printf();                               // ERROR E4002
        printf(c"%d %f %s %d", 1, 2.0, c"three", c_int(c));
        c_void* v = null;
        c_void copy = *v;                       // ERROR E4088, ERROR E4088
    }
}
