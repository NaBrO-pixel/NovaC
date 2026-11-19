// The C calling convention on aarch64-linux-gnu, as Clang lowers the same C
// declarations (NOVAC_ABI.md §5): extension attributes, coerced, flattened
// and indirect aggregates. Behaviour is verified by tests/abi.
// ARGS: --target=aarch64-linux-gnu
@mode(controlled) module cabi;
struct F2 { f32 a; f32 b; }
struct I3 { i32 a; i32 b; i32 c; }
struct D4 { f64 a; f64 b; f64 c; f64 d; }
struct FI { f32 a; i32 b; }
extern "C" {
    i8 small(i8 a, u16 b, u32 c, bool d);
    F2 f2(F2 x);
    I3 i3(I3 x);
    D4 d4(D4 x);
    FI fi(FI x);
}
// CHECK: declare i8 @small(i8, i16, i32, i1)
// CHECK: declare { float, float } @f2([2 x float] alignstack(8))
// CHECK: declare [2 x i64] @i3([2 x i64])
// CHECK: declare { double, double, double, double } @d4([4 x double] alignstack(8))
// CHECK: declare i64 @fi(i64)
