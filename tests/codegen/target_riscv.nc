// ARGS: --target=riscv64-linux-gnu
// RISC-V uses the lp64d ABI (D-023).
module rv;
public i32 f(i32 a) { return a; }
// CHECK: target triple = "riscv64-unknown-linux-gnu"
// CHECK: !"target-abi", !"lp64d"
