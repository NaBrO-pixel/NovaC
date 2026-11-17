i32 f() {
    i32 a = "\q";            // ERROR E1031
    i32 b = "\u{110000}";    // ERROR E1031
    i32 c = "\u{D800}";      // ERROR E1031
    i32 d = "\xZZ";          // ERROR E1031
    i32 e = '\u{41}';        // ERROR E1031
    i32 g = "\u{}";          // ERROR E1031
    return 0;
}
