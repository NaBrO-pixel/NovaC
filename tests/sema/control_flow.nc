i32 classify(i32 n) {
    if (n < 0) {
        return -1;
    } else if (n == 0) {
        return 0;
    }
    return 1;
}

u64 factorial(u64 n) {
    u64 acc = 1;
    for (u64 i = 2; i <= n; i++) {
        acc *= i;
    }
    return acc;
}

i32 loops(i32 limit) {
    i32 total = 0;
    i32 i = 0;
    while (i < limit) {
        i++;
        if (i % 2 == 0) continue;
        total += i;
    }
    do {
        total -= 1;
    } while (total > 100);
    for (;;) {
        if (total > 0) break;
        total++;
    }
    return total;
}

i32 forever() {
    while (true) { }
}
