// Every unsafe operation outside an unsafe context is E6003
// (NOVAC_MEMORY_MODEL.md §6.1); the safe pointer operations are accepted.
@mode(controlled) module unsafe_ops;
struct S { i32 v; }
union U { i32 i; f32 f; }
static i32 counter = 0;
static const i32 FIXED = 1;
unsafe i32 risky() { return 1; }
void f(i32* p, S* s, const i32* c, U u) {
    i32 a = *p;                             // ERROR E6003
    i32 b = s->v;                           // ERROR E6003
    i32 d = p[1];                           // ERROR E6003
    i32* e = p + 1;                         // ERROR E6003
    isize g = p - p;                        // ERROR E6003
    p++;                                    // ERROR E6003
    p += 2isize;                            // ERROR E6003
    i32 h = risky();                        // ERROR E6003
    i32 i = u.i;                            // ERROR E6003
    i32 j = counter;                        // ERROR E6003
    counter = 2;                            // ERROR E6003
    i32 k = uninit;                         // ERROR E6003
    i32* m = ptr_cast_mut::<i32*>(c);       // ERROR E6003
    i32* n = ptr_from_addr::<i32>(0);       // ERROR E6003
    *p = 3;                                 // ERROR E6003
    u.f = 1.0;                              // writing a union field is safe
    i32* sp = &mut counter;                 // taking the address of a mutable static is safe
    i32 fixed = FIXED;
    unsafe {
        i32 ok = *p + s->v + p[0] + risky() + u.i + counter;
        counter = ok;
    }
}
unsafe void inside(i32* p) { *p = 1; }      // an unsafe function body is an unsafe context
