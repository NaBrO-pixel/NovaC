// Constructs of later phases are recognised and rejected, never ignored.
import core.mem;                    // ERROR E9001
impl Point { }                      // ERROR E9001
interface Ord { }                   // ERROR E9001
void f(i32 x) {
    defer f(1);                     // ERROR E9001
    asm volatile ("nop");           // ERROR E9001
}
