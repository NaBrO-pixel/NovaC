// Constructs of later phases are recognised and rejected, never ignored.
import core.mem;                    // ERROR E9001
struct Point { i32 x; }             // ERROR E9001
union U { i32 a; }                  // ERROR E9001
enum Color : u8 { Red }             // ERROR E9001
typedef u64 PhysAddr;               // ERROR E9001
static i32 counter = 0;             // ERROR E9001
constexpr usize N = 4;              // ERROR E9001
impl Point { }                      // ERROR E9001
interface Ord { }                   // ERROR E9001
extern "C" { i32 puts(); }          // ERROR E9001
void f(i32 x) {
    switch (x) { case 1: break; }   // ERROR E9001
    defer f(1);                     // ERROR E9001
    unsafe { f(2); }                // ERROR E9001
    asm volatile ("nop");           // ERROR E9001
    fallthrough;                    // ERROR E9001
}
