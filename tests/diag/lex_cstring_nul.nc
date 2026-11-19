// A C string cannot contain a NUL byte: C would see it end there.
const c_char* s = c"ab\0cd"; // ERROR E1033
