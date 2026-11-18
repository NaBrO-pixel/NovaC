// Statics and constexprs.
i32 n() { return 1; }
static const i32 NO_INIT;                   // ERROR E4086
static const i32 DYN = n();                 // ERROR E4082
static const i32 TOO_BIG = 1 << 40;         // ERROR E4040
static const i32 OK = 4;
static const i32 FROM_STATIC = OK;          // ERROR E4082
constexpr i32 SELF = SELF + 1;              // ERROR E3005
constexpr i32 CYC_A = CYC_B;                // ERROR E3005
constexpr i32 CYC_B = CYC_A;
constexpr i32[2] ARR = 0;                   // ERROR E9001
constexpr i32 NOT_CONST = n();              // ERROR E4082
constexpr u8 WIDE = 300;                    // ERROR E4041
constexpr i32 GOOD = 1;
static const void V = 0;                    // ERROR E4005
void f() {
    OK = 5;                                 // ERROR E4004
    OK += 1;                                // ERROR E4004
    GOOD = 1;                               // ERROR E4004
}
