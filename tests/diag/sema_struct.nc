// Struct and union declarations and field access.
struct Point { i32 x; i32 y; }
struct Dup { i32 a; u8 a; }                 // ERROR E3002
struct Node { i32 v; Node next; }           // ERROR E4080
struct A { B b; }
struct B { A a; }                           // ERROR E4080
struct Opaque;
struct HasOpaque { Opaque o; }              // ERROR E4088
struct Bad { void v; }                      // ERROR E4005
struct Unknown { Nope n; }                  // ERROR E3001
typedef Loop2 Loop1;                        // ERROR E3005
typedef Loop1 Loop2;
i32 Point2() { return 0; }
typedef Point2 NotAType;                    // ERROR E3006
void f(Point p, i32 n) {
    i32 a = p.z;                            // ERROR E4084
    i32 b = n.x;                            // ERROR E4075
    i32 c = Point;                          // ERROR E4083
    Point q = Point{.w = 1};                // ERROR E4084
    Opaque o;                               // ERROR E4088
    i32 d = offsetof(Point, w);             // ERROR E4084
    usize e = offsetof(i32, x);             // ERROR E4075
    i32 g = p + p;                          // ERROR E4011
    bool h = p == p;                        // ERROR E4011
    i32 k = Point(n);                       // ERROR E4007
}
