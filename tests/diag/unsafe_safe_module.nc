// A safe module (the default) may use raw pointers but contains no unsafe
// code (NOVAC_MEMORY_MODEL.md §2.1).
struct S { i32 v; }
unsafe void danger() { }                    // ERROR E6001
void f(i32* p, S* s) {
    unsafe { *p = 1; }                      // ERROR E6001
    i32 x = *p;                             // ERROR E6003
    i32 y = s->v;                           // ERROR E6003
    bool same = p == null;
    i32* q = p;
    const i32* r = q;
    usize a = addr(p);
    u8* b = ptr_cast::<u8*>(p);
}
