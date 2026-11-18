// Enums (explicit values, implicit increments, underlying types),
// conversions in both directions, switch with case lists, defaults,
// fallthrough, break and continue inside switch.
enum Color { Red, Green, Blue }
enum Op : u8 { Add = 1, Sub, Mul = 10, Neg }
enum Big : i64 { Low = -5, High = 9000000000 }

i32 apply(Op op, i32 a, i32 b) {
    switch (op) {
    case Op.Add: return a + b;
    case Op.Sub: return a - b;
    case Op.Mul: return a * b;
    case Op.Neg: return -a;
    }
}

i32 classify(i32 n) {
    i32 r = 0;
    switch (n) {
    case 0: r = 100; break;
    case 1, 2, 3: r = 10;
        fallthrough;
    case 4: r += 1; break;
    case 5:
    case 6: r = 56; break;        // consecutive labels share a body
    default: r = -1;
    }
    return r;
}

int main(void) {
    if (apply(Op.Add, 3, 4) != 7 || apply(Op.Mul, 3, 4) != 12 || apply(Op.Neg, 3, 0) != -3) return 1;
    if (u8(Op.Sub) != 2 || u8(Op.Neg) != 11) return 2;
    Op o = Op(10u8);
    if (o != Op.Mul) return 3;
    u8 raw = 2;
    if (Op(raw) != Op.Sub) return 4;
    if (classify(0) != 100 || classify(2) != 11 || classify(4) != 1 || classify(6) != 56 || classify(9) != -1) return 5;
    Color c = Color.Blue;
    i32 seen = 0;
    for (i32 i = 0; i < 6; i++) {
        switch (i) {
        case 1: continue;             // continues the loop
        case 4: break;                // leaves the switch only
        default: seen += i;
        }
        seen += 100;
    }
    if (seen != 510) return 6;
    if (i64(Big.High) != 9000000000 || i64(Big.Low) != -5) return 7;
    bool b = true;
    switch (b) {
    case true: seen = 1; break;
    case false: seen = 2; break;
    }
    if (seen != 1) return 8;
    return i32(c) == 2 ? 0 : 9;
}
