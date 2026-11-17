i32 f() {
    i32 a = missing;           // ERROR E3001
    nowhere(1);                // ERROR E3001
    Point p = 0;               // ERROR E3001
    {
        i32 inner = 1;
    }
    return inner;              // ERROR E3001
}
