// The other unsafe operations: union reads, mutable statics, `uninit`,
// unsafe functions and ptr_cast_mut.
@mode(controlled) module unsafe_features;

union Bits { u32 w; f32 f; u8[4] b; }
static u64 calls = 0;
static i32[3] table = {1, 2, 3};

unsafe void count() { calls += 1; }

unsafe u32 to_bits(f32 f) {
    Bits u = {.f = f};
    return u.w;
}

int main(void) {
    unsafe {
        if (to_bits(1.0) != 0x3f800000) return 1;
        Bits u = {.w = 0x01020304};
        if (u.b[0] != 4 || u.b[3] != 1) return 2;
        count();
        count();
        if (calls != 2) return 3;
        table[1] = 20;
        if (table[0] + table[1] + table[2] != 24) return 4;
        i32[1024] scratch = uninit;
        for (usize i = 0; i < 1024; i++) scratch[i] = i32(i);
        if (scratch[1023] != 1023) return 5;
        const i32 c = 7;
        const i32* cp = &c;
        i32* mp = ptr_cast_mut::<i32*>(cp);
        if (*mp != 7) return 6;
    }
    return 0;
}
