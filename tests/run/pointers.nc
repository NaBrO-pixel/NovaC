// Raw pointers: linked structures, writes through pointers, pointer
// arithmetic and differences, comparisons, null, ptr_cast / addr /
// ptr_from_addr round trips, pointers to fields, elements and statics.
@mode(controlled) module pointers;

struct Node { i32 value; Node* next; }
struct Pair { i64 a; i64 b; }
static i32 hits = 0;

unsafe i32 sum(const Node* n) {
    i32 s = 0;
    while (n != null) {
        s += n->value;
        n = n->next;
    }
    return s;
}

void bump(i32* p) { unsafe { *p += 1; } }

void reverse(i32* first, usize n) {
    if (n < 2) return;
    unsafe {
        i32* lo = first;
        i32* hi = first + (n - 1);
        while (lo < hi) {
            i32 t = *lo;
            *lo = *hi;
            *hi = t;
            lo++;
            hi -= 1isize;
        }
    }
}

int main(void) {
    Node c = {30, null};
    Node b = {20, &mut c};
    Node a = {10, &mut b};
    i32 s = 0;
    unsafe { s = sum(&a); }
    if (s != 60) return 1;

    i32 x = 5;
    bump(&mut x);
    if (x != 6) return 2;

    i32[5] arr = {1, 2, 3, 4, 5};
    reverse(&mut arr[0], 5);
    if (arr[0] != 5 || arr[2] != 3 || arr[4] != 1) return 3;

    const i32* p0 = &arr[0];
    const i32* p4 = &arr[4];
    isize d = 0;
    unsafe { d = p4 - p0; }
    if (d != 4 || !(p0 < p4) || p0 == p4 || p0 != &arr[0]) return 4;

    Pair pr = {1, 2};
    i64* pb = &mut pr.b;
    unsafe { *pb = 99; }
    if (pr.b != 99) return 5;
    Pair* pp = &mut pr;
    unsafe { pp->a = pp->b + 1; }
    if (pr.a != 100) return 6;

    u8* bytes = ptr_cast::<u8*>(pp);
    usize at = addr(bytes);
    unsafe {
        Pair* back = ptr_from_addr::<Pair>(at);
        if (back->a != 100) return 7;
        u8 low = bytes[0];
        if (low != 100) return 8;      // little-endian on every version-1 target
    }

    i32* h = &mut hits;                // taking the address of a mutable static is safe
    unsafe {
        *h += 2;
        hits += 1;
        if (hits != 3) return 9;
    }
    Node* empty = null;
    if (empty != null) return 10;
    const Node* cn = &a;               // T* coerces to const T*
    unsafe { if (cn->next->next->value != 30) return 11; }
    return 0;
}
