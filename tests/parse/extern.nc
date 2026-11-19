@mode(controlled) module sys;
extern "C" {
    c_int printf(const c_char* fmt, ...);
    @link_name("write") isize sys_write(c_int fd, const u8* buf, usize n);
}
extern "C" u32 crc(const u8* p, usize n);
@export("nova_hash") extern "C" u64 hash(const u8* p) { return 0; }
@export("nova_count") static u64 counter = 0;
void f() { unsafe { printf(c"%s\n", c"tab\t"); } }
