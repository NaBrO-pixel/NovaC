// Integer arithmetic at every width, signed and unsigned (NOVAC_TYPE_SYSTEM.md §2.2).
i32 check(bool ok, i32 code) { return ok ? 0 : code; }

i64 mul3(i64 a, i64 b, i64 c) { return a * b * c; }
u8 add_u8(u8 a, u8 b) { return a + b; }
i16 sub_i16(i16 a, i16 b) { return a - b; }

int main(void) {
    i32 a = 7;
    i32 b = -3;
    if (a + b != 4) return 1;
    if (a - b != 10) return 2;
    if (a * b != -21) return 3;
    // Division truncates toward zero; the remainder has the dividend's sign.
    if (a / b != -2) return 4;
    if (a % b != 1) return 5;
    if (-a / 2 != -3) return 6;
    if (-a % 2 != -1) return 7;
    if (mul3(1000000, 1000000, 9000) != 9000000000000000) return 8;
    if (add_u8(200, 55) != 255) return 9;
    if (sub_i16(-32000, 768) != -32768) return 10;
    u64 big = 18446744073709551615;
    if (big / 3 != 6148914691236517205) return 11;
    if (big % 10 != 5) return 12;
    i64 min = -9223372036854775807 - 1;
    if (min / 2 != -4611686018427387904) return 13;
    usize n = 10;
    isize m = -10;
    if (n * 3 != 30 || m * 3 != -30) return 14;
    i32 x = 5;
    x += 3; x -= 1; x *= 4; x /= 3; x %= 5;
    if (x != 4) return 15;
    i32 y = 0;
    i32 r = y++ + ++y;   // 0 + 2; left-to-right evaluation
    if (r != 2 || y != 2) return 16;
    return check(-(-a) == 7, 17);
}
