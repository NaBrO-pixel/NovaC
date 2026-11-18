module shapes;

struct Point { i32 x; public i32 y; }
@packed struct Header { u8 tag; u32 len; };
struct Opaque;
union Value { i64 i; f64 f; }
enum Kind : u8 { Circle, Square = 4, Tri, }
typedef Point[4] Quad;
static const i32[3] PRIMES = {2, 3, [2] = 5};
static Point origin = {.x = 0, .y = 0};
constexpr usize N = 2 * 4;

i32 f(Kind k, Point p) {
    Quad q = {p, Point{1, 2}, {.y = 3}};
    switch (k) {
    case Kind.Circle, Kind.Square:
        q[0].x = sizeof(Point) + alignof(i64) + offsetof(Point, y);
        fallthrough;
    case Kind.Tri: break;
    default: return 'a';
    }
    return q[1].y;
}
