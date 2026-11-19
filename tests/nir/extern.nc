// Imported C functions have no body; calls may pass variadic arguments;
// C strings are constant operands.
@mode(controlled) module ext;
extern "C" {
    c_int printf(const c_char* fmt, ...);
    @link_name("labs") c_long absolute(c_long x);
}
extern "C" c_int exported(c_int x) { return x; }
c_long use(c_long v) {
    unsafe { printf(c"%ld\n", v, 2.5); return absolute(v); }
}
