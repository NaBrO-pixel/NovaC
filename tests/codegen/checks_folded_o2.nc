// ARGS: -O2
// ... and removes the ones it can: a u8 widened to i32 cannot overflow when doubled.
module of;
public i32 twice(u8 a) { return i32(a) * 2; }
// CHECK: define i32 @_NC2ofM5twice
// CHECK-NOT: __nova_panic
// CHECK: ret i32
