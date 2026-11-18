// Reading past the end of an array.
i32 get(i32[4] a, usize i) { return a[i]; } // PANIC: index out of bounds
int main(void) {
    i32[4] a = {1, 2, 3, 4};
    return get(a, 4);
}
