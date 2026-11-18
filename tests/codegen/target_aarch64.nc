// ARGS: --target=aarch64-linux-gnu
// AArch64 keeps frame records (NOVAC_ABI.md §3).
module a64;
public i32 f(i32 a) { return a; }
// CHECK: target triple = "aarch64-unknown-linux-gnu"
// CHECK: "frame-pointer"="non-leaf"
