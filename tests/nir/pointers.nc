// Dereference projections, addresses, pointer arithmetic and casts.
@mode(controlled) module pointers;
struct S { i32 v; S* next; }
i32 get(S* s, i32* p, usize i) {
    i32 r = 0;
    unsafe {
        r = s->next->v + p[i] + *(p + 1);
        *p = r;
        p++;
    }
    return r;
}
usize locate(S s) {
    const S* q = &s;
    return addr(q) + usize(sizeof(S));
}
