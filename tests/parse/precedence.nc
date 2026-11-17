// One expression per precedence level of NOVAC_GRAMMAR.md §6, plus mixes.
void f(i32 a, i32 b, i32 c, bool p, bool q) {
    a = a * b / c % 2 *% 3;
    a = a + b - c +% 1 -% 2;
    a = a << 1 >> 2;
    p = a < b;
    p = a <= b;
    p = a > b;
    p = a >= b;
    p = a == b;
    p = a != b;
    a = a & b ^ c | a;
    p = p && q || !p;
    a = p ? a : b;
    a = p ? a : q ? b : c;
    a += 1; a -= 1; a *= 2; a /= 2; a %= 2;
    a +%= 1; a -%= 1; a *%= 1;
    a <<= 1; a >>= 1; a &= 1; a |= 1; a ^= 1;
    a = b = c;
    a = -a + +b * ~c;
    p = !!p;
    a = ++a + a++ - --a - a--;
    a = a + b * c;
    a = (a + b) * c;
    p = a + 1 < b * 2 == (c > 0);
    p = a & 1 == 0;
}
