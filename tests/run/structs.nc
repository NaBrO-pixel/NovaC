// Structs: literals, designated and positional initialisers, nested
// structs, field assignment, compound assignment on fields, by-value
// parameters and returns (copies), typedefs.
struct Point { i32 x; i32 y; }
struct Rect { Point min; Point max; u8 tag; }
typedef Point Vec2;

Point add(Point a, Point b) { return Point{a.x + b.x, a.y + b.y}; }
i64 area(Rect r) { return i64(r.max.x - r.min.x) * i64(r.max.y - r.min.y); }
void clobber(Point p) { p.x = 999; }  // modifies its own copy

Rect grow(Rect r, i32 by) {
    r.min.x -= by;
    r.min.y -= by;
    r.max.x += by;
    r.max.y += by;
    return r;
}

int main(void) {
    Point a = {1, 2};
    Vec2 b = {.y = 20, .x = 10};
    Point c = add(a, b);
    if (c.x != 11 || c.y != 22) return 1;
    clobber(c);
    if (c.x != 11) return 2;
    Rect r = {.min = {0, 0}, .max = c, .tag = 7};
    if (area(r) != 242) return 3;
    Rect g = grow(r, 1);
    if (g.min.x != -1 || g.max.y != 23 || area(g) != 312) return 4;
    if (r.min.x != 0) return 5;          // grow worked on a copy
    Point z = {};                         // every field zero
    if (z.x != 0 || z.y != 0) return 6;
    Point partial = {.y = 5};             // omitted fields are zero (D-043)
    if (partial.x != 0 || partial.y != 5) return 7;
    Rect copy = r;
    copy.max.x = 0;
    if (r.max.x != 11) return 8;          // assignment copies
    copy = r;
    if (copy.max.x != 11) return 9;
    g.max.x++;
    ++g.max.y;
    if (g.max.x != 13 || g.max.y != 24) return 10;
    return r.tag == 7 ? 0 : 11;
}
