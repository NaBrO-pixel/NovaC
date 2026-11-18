// A program in the Phase 2 subset of NovaC. Build and run it with
//   ncc -O2 examples/phase2.nc -o phase2 && ./phase2
// It exits with status 0 when every computed value is as expected.
module examples.phase2;

u64 factorial(u64 n) {
    u64 acc = 1;
    for (u64 i = 2; i <= n; i++) {
        acc *= i;               // panics on overflow at run time (D-009)
    }
    return acc;
}

bool is_prime(u32 n) {
    if (n < 2) return false;
    for (u32 d = 2; d * d <= n; d++) {
        if (n % d == 0) return false;
    }
    return true;
}

u32 hash(u32 x) {
    x ^= x >> 16;
    x *%= 0x45d9f3b;            // wrapping multiplication is explicit
    x ^= x >> 16;
    return x;
}

int main(void) {
    u64 f = factorial(20);
    i32 primes = 0;
    for (u32 n = 0; n < 100; n++) {
        if (is_prime(n)) primes++;
    }
    i64 mixed = i64(primes) + 1;        // operands are never widened implicitly (D-037)
    u8 low = u8(hash(7) & 0xFF);        // checked conversion
    return primes == 25 && f > 0 && mixed > 0 && low >= 0 ? 0 : 1;
}
