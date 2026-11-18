// Checked conversions: narrowing, sign changes, float to integer.
u8 narrow(i32 x) { return u8(x); }
u32 sign(i32 x) { return u32(x); }
i32 to_signed(u32 x) { return i32(x); }
i16 from_float(f32 x) { return i16(x); }
i64 widen(i8 x) { return x; }
f64 to_float(u64 x) { return f64(x); }
