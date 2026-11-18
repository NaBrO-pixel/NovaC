// Wrapping operators, bitwise operators and shifts.
int main(void) {
    u8 a = 250;
    if (a +% 10 != 4) return 1;
    u8 z = 0;
    if (z -% 1 != 255) return 2;
    i8 m = 127;
    if (m +% 1 != -128) return 3;
    i32 big = 65536;
    if (big *% big != 0) return 4;
    u32 h = 0x12345678;
    h *%= 0x45d9f3b;
    if (h != 0xFD5C75A8) return 5;
    u32 bits = 0xF0F0;
    if ((bits & 0xFF) != 0xF0) return 6;
    if ((bits | 0x0F) != 0xF0FF) return 7;
    if ((bits ^ 0xFFFF) != 0x0F0F) return 8;
    if (~bits != 0xFFFF0F0F) return 9;
    i32 neg = -16;
    u32 n4 = 4;
    if (neg >> n4 != -1) return 10;          // arithmetic shift for signed
    u32 un = 0x80000000;
    if (un >> 31u32 != 1) return 11;         // logical shift for unsigned
    if ((1i64 << 62u8) != 4611686018427387904) return 12;
    u8 s = 1;
    u64 amount = 7;
    if (s << amount != 128) return 13;       // shift amount of a different width
    if ((s << 7u32) << 1u32 != 0) return 14; // `<<` discards high bits
    bool p = true, q = false;
    if ((p & q) || !(p | q) || !(p ^ q)) return 15;
    return 0;
}
