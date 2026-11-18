// Loops, break/continue, nested loops, recursion, short-circuit evaluation.
i32 boom(i32 x) { return 1 / x; }   // panics if called with 0

u64 fib(u64 n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

i32 gcd(i32 a, i32 b) {
    while (b != 0) {
        i32 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

int main(void) {
    if (fib(30) != 832040) return 1;
    if (gcd(1071, 462) != 21) return 2;
    i32 sum = 0;
    for (i32 i = 0; i < 100; i++) {
        if (i % 2 == 0) continue;
        if (i > 50) break;
        sum += i;
    }
    if (sum != 625) return 3;
    i32 count = 0;
    for (i32 i = 0; i < 10; i++)
        for (i32 j = 0; j < 10; j++)
            if (i == j) count++;
    if (count != 10) return 4;
    i32 k = 0;
    do { k++; } while (k < 5);
    if (k != 5) return 5;
    do { k++; } while (false);
    if (k != 6) return 6;
    // Short-circuit: the right operand must not run (it would panic).
    i32 zero = 0;
    if (false && boom(zero) == 1) return 7;
    if (!(true || boom(zero) == 1)) return 8;
    i32 c = zero != 0 ? boom(zero) : 9;
    if (c != 9) return 9;
    i32 w = 0;
    while (true) {
        w++;
        if (w == 3) break;
    }
    return w == 3 ? 0 : 10;
}
