void f(i32 a, u32 b, f64 c, bool p, bool q) {
    i32 x = a * a / 3 % 2 - a + 1;
    u32 y = b << 3 >> 1u32;
    u32 z = b & 0xFF | b ^ 1;
    u32 w = b +% b -% 1 *% b;
    bool cmp = a < 0 || a >= 10 && !p;
    bool eq = (p == q) != false;
    f64 neg = -c + +c;
    i32 bits = ~a;
    a += 1; a -= 1; a *= 2; a /= 2; a %= 2;
    b <<= 1u32; b >>= 1; b &= 3; b |= 4; b ^= 5;
    b +%= 1; b -%= 1; b *%= 2;
    p &= q; p |= q; p ^= q;
    a++; --a;
    c++;
}
