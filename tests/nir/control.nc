// Short-circuit operators, conditionals and loops become branches.
bool both(bool a, bool b) { return a && b; }
i32 pick(bool c, i32 x, i32 y) { return c ? x : y; }
i32 count(i32 n) {
    i32 total = 0;
    for (i32 i = 0; i < n; i++) {
        if (i == 3) continue;
        total += i;
    }
    return total;
}
void forever() { while (true) { } }
