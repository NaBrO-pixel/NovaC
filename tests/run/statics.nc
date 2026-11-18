// Immutable statics and constexpr: tables, structs, enums, nested
// aggregates, unions, used from functions; constexpr in array lengths.
struct Pair { u16 a; i64 b; }
union Word { u32 w; u8 b; }
enum Level : u8 { Low = 1, High = 4 }
constexpr usize N = 4;
constexpr i32 BASE = -100;
constexpr Level DEFAULT_LEVEL = Level.High;
static const i32[N] TABLE = {1, 1, 2, 3};
static const Pair[2] PAIRS = {{1, 2}, {.b = -9000000000}};
static const Word W = {.b = 0x7f};
static const f64 SCALE = 2.5;
static const bool FLAG = true;

i64 total() {
    i64 t = 0;
    for (usize i = 0; i < N; i++) t += i64(TABLE[i]);
    return t + PAIRS[1].b + i64(PAIRS[0].a);
}

int main(void) {
    if (total() != -8999999992) return 1;
    i32[N * 2] local = {};
    if (sizeof(local) != 32) return 2;
    if (BASE + 100 != 0) return 3;
    if (u8(DEFAULT_LEVEL) != 4) return 4;
    if (SCALE * 2.0 != 5.0 || !FLAG) return 5;
    Word copy = W;                    // the whole union is copied
    copy.w = 1;
    Pair p = PAIRS[0];
    return p.b == 2 ? 0 : 6;
}
