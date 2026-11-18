// Size limits: objects of more than 2^47 bytes, and the expansion limit
// of array initialiser lists.
struct Big { u8[140737488355328] a; u8 b; }  // ERROR E4089
void f() {
    u64[17592186044417] x = {};              // ERROR E4089
    u8[4194304] ok = {1};
    u8[2] over = {1};                        // ERROR E9005
    u8[1099511627776] zeroed = {};
}
