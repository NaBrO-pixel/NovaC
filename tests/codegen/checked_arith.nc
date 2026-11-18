// Checked arithmetic uses the overflow intrinsics and a noreturn panic call;
// wrapping arithmetic is a plain instruction.
module ca;
i32 add(i32 a, i32 b) { return a + b; }
u64 mul(u64 a, u64 b) { return a * b; }
u8 wrap(u8 a, u8 b) { return a +% b; }
// CHECK: define internal i32 @_NC2caM3add
// CHECK: call { i32, i1 } @llvm.sadd.with.overflow.i32
// CHECK: call void @__nova_panic(ptr @nova.panic.site
// CHECK: unreachable
// CHECK: define internal i64 @_NC2caM3mul
// CHECK: call { i64, i1 } @llvm.umul.with.overflow.i64
// CHECK: define internal i8 @_NC2caM4wrap
// CHECK-NOT: with.overflow
// CHECK: add i8
// CHECK: ret i8
