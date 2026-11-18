// An aggregate argument's temporary lives in the entry block: calling in a
// loop must not grow the stack (100000 x 4 KiB would overflow it).
struct B { u8[4096] d; }
u8 first(B b) { return b.d[0]; }
i32 main() { u32 s = 0; for (i32 i = 0; i < 100000; i++) s += u32(first(B{})); return i32(s); }
