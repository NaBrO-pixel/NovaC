// Arrays: value semantics, indexing with checked indices, nested arrays,
// arrays of structs, designated elements and zero fill.
struct Cell { u8 kind; i32 value; }

i32 sum(i32[8] a) {
    i32 s = 0;
    for (usize i = 0; i < 8; i++) s += a[i];
    return s;
}

i32[8] squares() {
    i32[8] out = {};
    for (usize i = 0; i < 8; i++) out[i] = i32(i * i);
    return out;
}

int main(void) {
    i32[8] sq = squares();
    if (sum(sq) != 140) return 1;
    i32[8] copy = sq;
    copy[0] = 100;
    if (sq[0] != 0 || copy[0] != 100) return 2;
    i32[5] d = {1, [3] = 4, 5};
    if (d[0] != 1 || d[1] != 0 || d[2] != 0 || d[3] != 4 || d[4] != 5) return 3;
    i32[3][2] grid = {{1, 2, 3}, {4, 5, 6}};
    i32 t = 0;
    for (usize r = 0; r < 2; r++)
        for (usize k = 0; k < 3; k++) t += grid[r][k] * i32(r + 1);
    if (t != 36) return 4;
    Cell[4] cells = {[2] = {.kind = 1, .value = -7}};
    if (cells[2].value != -7 || cells[0].kind != 0) return 5;
    cells[1].value += 3;
    if (cells[1].value != 3) return 6;
    u8 small = 3;
    if (sq[small] != 9) return 7;         // u8 index widens to usize
    usize i = 7;
    sq[i] -= 49;
    return sq[i] == 0 ? 0 : 8;
}
