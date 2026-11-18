// Aggregate types and places: named struct types (packed ones as packed
// LLVM structs), unions as sized integer arrays, statics as globals with
// constant initialisers (byte images for unions), in-bounds GEPs after a
// bounds check, align 1 inside @packed structs, switch, and the internal
// convention for aggregates: passed by address and copied by the callee,
// returned through a hidden pointer.
// ARGS: -O0
module cg;
struct Point { i32 x; i32 y; }
@packed struct Hdr { u8 t; u32 len; }
union U { u8 a; u64 b; }
enum E : u16 { A, B }
static const i32[3] TABLE = {1, 2, 3};
public static const Point ORIGIN = {.y = 1};
static const U UZ = {.a = 7};
i32 at(i32[3] a, usize i) { return a[i]; }
u32 len(Hdr h) { return h.len; }
i32 sw(E e) { switch (e) { case E.A: return TABLE[0]; case E.B: return ORIGIN.y; } }
U make() { return U{.b = 9}; }
i32 main() { return at(TABLE, 1) + i32(len(Hdr{1, 2})) + sw(E.B); }
// CHECK: %struct.Point = type { i32, i32 }
// CHECK: %struct.Hdr = type <{ i8, i32 }>
// CHECK: @_NC2cgM5TABLE = internal constant [3 x i32] [i32 1, i32 2, i32 3], align 4
// CHECK: @_NC2cgM6ORIGIN = constant %struct.Point { i32 0, i32 1 }, align 4
// CHECK: @_NC2cgM2UZ = internal constant [8 x i8] c"\07\00\00\00\00\00\00\00", align 8
// CHECK: define internal i32 @_NC2cgM2at(ptr %0, i64 %1)
// CHECK: call void @llvm.memcpy
// CHECK: icmp ult i64 %3, 3
// CHECK: getelementptr inbounds [3 x i32], ptr %a, i64 0, i64 %6
// CHECK: define internal i32 @_NC2cgM3len(ptr %0)
// CHECK: load i32, ptr %1, align 1
// CHECK: define internal i32 @_NC2cgM2sw(i16 %0)
// CHECK: switch i16 %1, label %bb4 [
// CHECK: i16 0, label %bb2
// CHECK: i16 1, label %bb3
// CHECK: define internal void @_NC2cgM4make(ptr %0)
// CHECK: call void @llvm.memset
// CHECK: store i64 9, ptr
// CHECK: ret void
