// Raw pointer typing (NOVAC_TYPE_SYSTEM.md §3.6, §5).
@mode(controlled) module pointers;
@mode(fast) struct Bad { i32 x; }           // ERROR E3041
struct S { i32 v; }
struct Opaque;
struct Empty { }
@packed struct P { u8 a; u32 b; }
void f(i32* p, const i32* c, void* v, Opaque* o, u8* b, S s, Empty* z) {
    unsafe {
        *c = 1;                             // ERROR E4004
        c[0] = 1;                           // ERROR E4004
        i32 x = *v;                         // ERROR E4011
        Opaque copy = *o;                   // ERROR E4088, ERROR E4088
        i32 y = *s.v;                       // ERROR E4011
        i32 w = s->v;                       // ERROR E4011
        i32* q = p + 1i32;                  // ERROR E4001
        isize zd = z - z;                   // ERROR E4011
        isize mixed = p - b;                // ERROR E4010
    }
    i32* m = c;                             // ERROR E4001
    i32* other = b;                         // ERROR E4001
    i32 n = null;                           // ERROR E4001
    auto a = null;                          // ERROR E4001
    usize u = usize(p);                     // ERROR E4007
    i32* pc = ptr_cast::<i32*>(c);          // ERROR E4007
    i32 nc = ptr_cast::<i32>(p);            // ERROR E4007
    usize two = addr(p, p);                 // ERROR E4002
    usize gen = addr::<u8>(p);              // ERROR E4002
    u8* nogen = ptr_cast(p);                // ERROR E4002
    usize notp = addr(5);                   // ERROR E4001
    P pk = {};
    u32* mis = &mut pk.b;                   // ERROR E5050
    u8* fine = &mut pk.a;
    const i32 k = 1;
    i32* mk = &mut k;                       // ERROR E4004
    const i32* ck = &k;
    const i32* lit = &5;                    // ERROR E4004
    bool cmp = p == c;
    bool lt = p < p;
    S* sp = &mut s;
    S* sp2 = sp.v;                          // ERROR E4011
}
