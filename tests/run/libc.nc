// Calling the C library: puts, printf with variadic arguments of every
// promoted kind, strlen, memcmp; a C string static; an exported function.
@mode(controlled) module libc;

extern "C" {
    c_int puts(const c_char* s);
    c_int printf(const c_char* fmt, ...);
    usize strlen(const c_char* s);
    c_int memcmp(const c_void* a, const c_void* b, usize n);
    @link_name("labs") c_long absolute(c_long x);
}

extern "C" i32 twice(i32 v) { return v * 2; }
static const c_char* GREETING = c"static greeting";

int main(void) {
    unsafe {
        puts(c"hello from NovaC");
        printf(c"%d %s %.2f %ld %u %c\n", 42, GREETING, 2.5, absolute(-7), 3000000000u32, c_int('x'));
        printf(c"len=%zu twice=%d\n", strlen(c"abcd"), twice(21));
        i32[3] a = {1, 2, 3};
        i32[3] b = {1, 2, 4};
        const c_void* pa = ptr_cast::<const c_void*>(&a);
        const c_void* pb = ptr_cast::<const c_void*>(&b);
        if (memcmp(pa, pb, 8) != 0 || memcmp(pa, pb, 12) >= 0) return 1;
    }
    return 0;
}
// STDOUT: hello from NovaC
// STDOUT: 42 static greeting 2.50 7 3000000000 x
// STDOUT: len=4 twice=42
