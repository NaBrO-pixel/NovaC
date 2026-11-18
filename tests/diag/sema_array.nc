// Array types and indexing.
usize n() { return 2; }
constexpr usize LEN = 3;
void f(i32[LEN] a, i32 i, usize u) {
    i32 x = a[3];                           // ERROR E5040
    i32 y = a[i];                           // ERROR E4001
    i32 z = a[-1];                          // ERROR E4041
    i32 w = i[0];                           // ERROR E4074
    i32[n()] dyn;                           // ERROR E4089
    i32[0] empty;                           // ERROR E4089
    void[2] v;                              // ERROR E4005
    a[u] = 1.5;                             // ERROR E4001
    i32[2] b = a;                           // ERROR E4001
    i32 ok = a[u] + a[2];
}
