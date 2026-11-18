// Symbol names and linkage (NOVAC_ABI.md §7): _NC<module>M<item>,
// private functions internal, public external; C `main` wraps NovaC main.
module app.core;
public i32 api(i32 x) { return helper(x); }
i32 helper(i32 x) { return x; }
int main(void) { return api(0); }
// CHECK: define weak void @__nova_panic(ptr %0)
// CHECK: define i32 @_NC3app4coreM3api(i32 %0)
// CHECK: define internal i32 @_NC3app4coreM6helper(i32 %0)
// CHECK: define internal i32 @_NC3app4coreM4main()
// CHECK: define i32 @main(i32 %0, ptr %1, ptr %2)
// CHECK: call i32 @_NC3app4coreM4main()
// CHECK: attributes #0 = { cold noreturn nounwind }
