// Places with field and element projections, bounds checks on variable
// indices (constant ones are checked by sema), aggregates with zeroed
// members, statics, enum conversions and switch.
struct Pair { i32 a; i32[4] v; }
enum E : u8 { A = 1, B = 2, C = 5 }
static const i32[2] S = {7, 8};

i32 get(Pair p, usize i) {
    p.v[i] += p.a;
    return p.v[2] + S[1];
}

Pair make(i32 x) { return Pair{.v = {x}}; }

i32 sw(u8 raw) {
    switch (E(raw)) {
    case E.A, E.B: return 1;
    case E.C: return 2;
    }
}
