// Initialiser lists and struct literals (D-043).
struct P { i32 x; i32 y; }
enum NoZero { A = 1, B = 2 }
struct NZ { NoZero k; i32 v; }
union U { i32 i; f32 f; }
void f() {
    P a = {1, 2, 3};                        // ERROR E4087
    P b = {.x = 1, .x = 2};                 // ERROR E4085
    P c = {.z = 1};                         // ERROR E4084
    P d = {[0] = 1};                        // ERROR E4087
    NZ e = {.v = 1};                        // ERROR E4081
    NZ ok = {.k = NoZero.A};
    NoZero[2] g = {NoZero.A};               // ERROR E4081
    i32[2] h = {1, 2, 3};                   // ERROR E4087
    i32[2] k = {[2] = 1};                   // ERROR E5040
    usize j = 0;
    i32[2] m = {[j] = 1};                   // ERROR E4082
    i32[2] q = {[1] = 1, [1] = 2};          // ERROR E4085
    i32[2] r = {.x = 1};                    // ERROR E4087
    U s = {1, 2};                           // ERROR E4087
    U t = {.g = 1};                         // ERROR E4084
    auto w = {1};                           // ERROR E4087
    i32 x = {1};                            // ERROR E4087
    P y = {1.5};                            // ERROR E4001
    P z = {{1}};                            // ERROR E4087
}
