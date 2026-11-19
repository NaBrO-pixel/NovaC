// C calls NovaC: exported functions (one renamed with @export), small
// integer arguments that need sign and zero extension, and an exported
// static. The C half is c_callbacks.c.
// C-SOURCE: c_callbacks.c
@mode(controlled) module c_callbacks;

extern "C" c_int c_drive(c_int n);

extern "C" c_int nova_add(c_int a, c_int b) { return a + b; }
@export("nova_mix") extern "C" c_long mix(i8 a, u16 b, f64 c) { return c_long(a) + c_long(b) + c_long(c); }
@export("nova_magic") static const c_int MAGIC = 1000;

int main(void) {
    c_int r = 0;
    unsafe { r = c_drive(10); }
    return r == 1045 ? 0 : 1;
}
