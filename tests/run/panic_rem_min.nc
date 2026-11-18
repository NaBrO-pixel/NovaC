// MIN % -1 panics like MIN / -1 (NOVAC_TYPE_SYSTEM.md §2.2).
i64 f(i64 a, i64 b) { return a % b; } // PANIC: integer overflow in '%'
int main(void) { return i32(f(-9223372036854775807 - 1, -1)); }
