// Converting an integer that is not an enumerator.
enum Mode : u8 { Off = 0, On = 1, Auto = 4 }
Mode decode(u8 raw) { return Mode(raw); } // PANIC: value is not an enumerator of 'Mode'
int main(void) {
    if (decode(4) != Mode.Auto) return 1;
    return i32(decode(2));
}
