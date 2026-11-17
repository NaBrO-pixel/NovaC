// Functions may be used before they are declared; inner scopes may shadow.
i32 main() {
    i32 x = helper(2);
    {
        i64 x = 5;
        x = x + 1;
    }
    for (i32 x = 0; x < 3; x++) { }
    return x;
}

i32 helper(i32 v) {
    const i32 k = 10;
    return v * k + recurse(v);
}

i32 recurse(i32 n) {
    if (n <= 0) return 0;
    return recurse(n - 1);
}
