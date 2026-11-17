void f(i32 x) {
    x + 1;                     // WARNING W4001
    x;                         // WARNING W4001
    i32 y = x / 0;             // WARNING W4002
    x = x;
    x++;
}
