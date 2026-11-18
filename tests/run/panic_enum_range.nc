// The underlying range is checked before membership.
enum Tiny : u8 { A, B, C }
int main(void) {
    i32 big = 300;
    Tiny t = Tiny(big); // PANIC: value out of range in conversion to 'u8'
    return i32(t);
}
