// Enum declarations, enumerators and conversions.
enum Float : f32 { A }                      // ERROR E4090
enum Wrap : u8 { X = 255, Y }               // ERROR E4040
enum Neg : u8 { M = -1 }                    // ERROR E4041
enum Twice { P, P }                         // ERROR E3002
enum Color { Red, Green }
enum Other { One }
i32 n() { return 1; }
enum Dyn { D = n() }                        // ERROR E4082
void f(Color c, i32 i) {
    Color a = Color.Blue;                   // ERROR E4084
    Color b = c + Color.Red;                // ERROR E4011
    bool lt = c < Color.Green;              // ERROR E4011
    Color d = 1;                            // ERROR E4001
    i32 e = c;                              // ERROR E4001
    Other o = Other(c);                     // ERROR E4007
    f32 fl = f32(c);                        // ERROR E4007
    Color g = Color(7);                     // ERROR E4040
    Color h = Color(1.5);                   // ERROR E4007
    Color k = Color(i);
    c++;                                    // ERROR E4011
}
