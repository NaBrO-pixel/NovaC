// Writing past the end of a nested array: the check precedes the store.
struct Grid { u8[3][3] cells; }
int main(void) {
    Grid g = {};
    usize row = 1;
    usize col = 3;
    g.cells[row][col] = 1; // PANIC: index out of bounds
    return 0;
}
