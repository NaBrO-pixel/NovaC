// ARGS: -O2
// Optimisation keeps checks it cannot prove redundant ...
module o2;
public i32 add(i32 a, i32 b) { return a + b; }
// CHECK: define i32 @_NC2o2M3add
// CHECK: @llvm.sadd.with.overflow.i32
// CHECK: tail call void @__nova_panic
