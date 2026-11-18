// A panic deep in a loop reports the right location.
int main(void) {
    u8 total = 0;
    for (u8 i = 0; i < 100; i++) {
        total += i; // PANIC: integer overflow in '+'
    }
    return i32(total);
}
