@mode(kernel) module drivers.uart;

struct Port { u16 base; const u8* name; Port* next; }

unsafe u8 read(const Port* p, isize i) {
    return p->name[i];
}

void poke(u8* p, usize a) {
    unsafe {
        *p = 1;
        u8* q = ptr_from_addr::<u8>(a);
        *(q + 1) = *p;
        usize back = addr(q);
    }
    const u8* c = &*p;
    Port* none_yet = null;
}
