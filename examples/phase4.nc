// Phase 4: structs, enums, switch, arrays, pointers, unsafe and C.
//
//   ncc -O2 examples/phase4.nc -o phase4 && ./phase4
//   ncc --target=x86_64-novaris --libnovac=<LibNovaC build>/target examples/phase4.nc -o phase4
@mode(controlled) module phase4;

extern "C" c_int printf(const c_char* fmt, ...);

enum Shape : u8 { Circle, Square, Triangle }

struct Item {
    Shape shape;
    f64 size;
    u32 count;
}

f64 area(Item it) {
    switch (it.shape) {
    case Shape.Circle: return 3.14159 * it.size * it.size;
    case Shape.Square: return it.size * it.size;
    case Shape.Triangle: return it.size * it.size * 0.5;
    }
}

// Doubles every count through a pointer: the caller's array changes.
void double_counts(Item* items, usize n) {
    unsafe {
        for (usize i = 0; i < n; i++) items[i].count *= 2;
    }
}

int main(void) {
    Item[3] items = {
        {Shape.Circle, 1.0, 2},
        {Shape.Square, 2.0, 1},
        {.shape = Shape.Triangle, .size = 3.0, .count = 4},
    };
    double_counts(&mut items[0], 3);
    f64 total = 0.0;
    for (usize i = 0; i < 3; i++) total += area(items[i]) * f64(items[i].count);
    unsafe { printf(c"total area: %.3f\n", total); }
    return 0;
}
