// ARGS: -Ofast
// -Ofast relaxes floating point without nnan/ninf, which would make NaN and
// infinity poison (D-040), and keeps integer checks (D-016).
module of;
public f64 f(f64 a, f64 b) { return a * b + a; }
public i32 g(i32 a, i32 b) { return a + b; }
// CHECK: fmul reassoc nsz arcp contract afn double
// CHECK-NOT: nnan
// CHECK: @llvm.sadd.with.overflow.i32
