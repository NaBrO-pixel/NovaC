// Initialiser lists become aggregates (omitted members are zero), constant
// values of enumerators, constexprs, sizeof/offsetof, conversions to enums.
struct Point { i32 x; i32 y; }
enum Dir : u8 { North = 1, East, South = 8 }
constexpr i32 K = 3 * 7;
static const u16[3] T = {1, [2] = 9};

i32 f(Point p) {
    Point q = {.y = K};
    i32[3] a = {p.x, q.y};
    Dir d = Dir(8);
    usize s = sizeof(Point) + offsetof(Point, y);
    return a[1] + i32(T[2]) + i32(Dir.East) + i32(d) + i32(s);
}
