// sizeof, alignof and offsetof follow the C layout of NOVAC_ABI.md §4,
// identical on x86-64, AArch64 and RISC-V 64.
struct A { u8 a; u32 b; u8 c; }           // 1 + 3 pad + 4 + 1 + 3 pad = 12
@packed struct P { u8 a; u32 b; u8 c; }   // 6, alignment 1
@repr(C) struct Q { u8 a; f64 b; }        // 16
union U { u8 a; u64 b; u16[5] c; }        // 10 rounded to 16
struct Empty { }
struct Nest { A x; P y; bool z; }         // 12 + 6 + 1 = 19, aligned to 4 -> 20
enum Small : u8 { X }

int main(void) {
    if (sizeof(A) != 12 || alignof(A) != 4 || offsetof(A, b) != 4 || offsetof(A, c) != 8) return 1;
    if (sizeof(P) != 6 || alignof(P) != 1 || offsetof(P, b) != 1 || offsetof(P, c) != 5) return 2;
    if (sizeof(Q) != 16 || offsetof(Q, b) != 8) return 3;
    if (sizeof(U) != 16 || alignof(U) != 8) return 4;
    if (sizeof(Empty) != 0) return 5;
    if (sizeof(Nest) != 20 || offsetof(Nest, y) != 12 || offsetof(Nest, z) != 18) return 6;
    if (sizeof(Small) != 1 || sizeof(bool) != 1 || sizeof(u16[3]) != 6) return 7;
    P p = {1, 0x01020304, 3};
    p.b += 1;
    if (p.b != 0x01020305 || p.c != 3) return 8;
    Nest n = {};
    n.y.b = 7;
    i32 x = 0;
    if (sizeof(x) != 4) return 9;
    return n.y.b == 7 && !n.z ? 0 : 10;
}
