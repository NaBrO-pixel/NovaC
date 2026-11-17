// The parser already accepts the full expression and type grammar; semantic
// analysis rejects what later phases implement (E9001). This test checks
// only the syntax tree.
@inline @export("vec_len") public usize len(const Vec<T>& self, slice<const u8> s) { return s.len; }
extern "C" unsafe i32 printf(const u8* fmt, ...);
T max<T: Ord + Copy, constexpr usize N>(T a, T b) { return a > b ? a : b; }
void types(u8* p, const u8* q, u8* const r, const u8* const* s, i32[4][2] m,
           i32 fn(i32, i32) cb, void fn()* pcb, optional<slice<u8>> o, Self me, a.b.C qualified) { }
void exprs(i32 a) {
    a = p->x.y[3](1, 2)++;
    a = *p + &a + &mut a + &&a;
    a = try parse(s);
    a = sizeof(i32) + sizeof(x) + sizeof(u8*) + alignof(u64) + offsetof(Point, y);
    a = make::<u32, 4>(1);
    p = Point{ .x = 1, .y = 2 };
    p = Self{ };
    v = Vec<i32>{ 1, 2, [5] = 3 };
    o = some(1);
    o = none;
    q = null;
    if (o is some(v)) { }
    if (r is err(_)) { }
    if (o is none) { }
    c = 'a' + '\n';
    s = "text";
    z = c"zero";
    b = a >> 2 > 1;
    a >>= 3;
    i32 arr = { 1, { 2, 3 }, .f = 4 };
    u8 raw = uninit;
}
