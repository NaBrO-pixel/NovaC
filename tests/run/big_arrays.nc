// Large aggregates are copied and zeroed with memcpy/memset (compile time
// stays small), and copies are independent.
struct Buf { u32 len; u8[200000] data; }
static const u16[50000] ZEROS = {};

u32 checksum(Buf b) {
    u32 s = 0;
    for (usize i = 0; i < usize(b.len); i++) s +%= u32(b.data[i]);
    return s;
}

int main(void) {
    Buf a = {.len = 100000};
    for (usize i = 0; i < 100000; i++) a.data[i] = u8(i % 251);
    Buf b = a;
    b.data[0] = 200;
    if (a.data[0] != 0 || b.data[0] != 200) return 1;
    if (checksum(a) + 200 != checksum(b)) return 2;
    a = Buf{};
    if (a.len != 0 || a.data[99999] != 0) return 3;
    if (ZEROS[49999] != 0) return 4;
    return 0;
}
