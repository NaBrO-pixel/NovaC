// IEEE 754 behaviour; NaN comparisons.
f64 div(f64 a, f64 b) { return a / b; }
int main(void) {
    f64 x = 0.1 + 0.2;
    if (x == 0.3) return 1;                  // not exactly 0.3
    f64 nan = div(0.0, 0.0);                 // floats never panic
    if (nan == nan) return 2;
    if (!(nan != nan)) return 3;
    if (nan < 1.0 || nan > 1.0 || nan <= 1.0 || nan >= 1.0) return 4;
    f64 inf = div(1.0, 0.0);
    if (!(inf > 1e308)) return 5;
    f32 f = 16777216.0f32;
    if (f + 1.0f32 != f) return 6;           // f32 rounding
    f64 g = -0.0;
    if (g != 0.0) return 7;
    f64 h = 2.5;
    h *= 2.0; h -= 1.0; h /= 2.0; h++;
    if (h != 3.0) return 8;
    return 0;
}
