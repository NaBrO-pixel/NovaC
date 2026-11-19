// Pointer operations: offsets are plain (wrapping) GEPs with no inbounds
// assumption; differences divide the byte distance without `exact`; addr
// and ptr_from_addr are ptrtoint/inttoptr; `->` loads the pointer.
// ARGS: -O2
@mode(controlled) module pc;
struct S { i32 v; S* next; }
public unsafe i32* off(i32* p, isize n) { return p + n; }
public unsafe isize diff(i64* a, i64* b) { return a - b; }
public usize where_is(u8* p) { return addr(p); }
public unsafe u8* from(usize a) { return ptr_from_addr::<u8>(a); }
public unsafe i32 next_v(S* s) { return s->next->v; }
// CHECK: define ptr @_NC2pcM3off(ptr readnone %0, i64 %1)
// CHECK: getelementptr i32, ptr %0, i64 %1
// CHECK: define i64 @_NC2pcM4diff(ptr %0, ptr %1)
// CHECK: sub i64 %2, %3
// CHECK: sdiv i64 %4, 8
// CHECK-NOT: sdiv exact
// CHECK: define i64 @_NC2pcM8where_is(ptr %0)
// CHECK: ptrtoint ptr %0 to i64
// CHECK: define ptr @_NC2pcM4from(i64 %0)
// CHECK: inttoptr i64 %0 to ptr
// CHECK: define i32 @_NC2pcM6next_v
// CHECK: load ptr, ptr %1, align 8
