// Aggregates are tracked whole: a field or element cannot be assigned or
// read before the whole variable is initialised (D-043).
struct P { i32 x; i32 y; }
i32 f(bool c) {
    P p;
    p.x = 1;                                // ERROR E5031
    i32[2] a;
    i32 r = a[0];                           // ERROR E5031
    P q;
    if (c) q = P{1, 2};
    r += q.y;                               // ERROR E5031
    P ok = {};
    ok.x = 3;
    P later;
    later = ok;
    later.y = 4;
    return r + ok.x + later.y;
}
