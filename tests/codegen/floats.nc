// NaN semantics: ordered comparisons, unordered `!=` (NOVAC_TYPE_SYSTEM.md §2.3).
module fl;
public bool lt(f64 a, f64 b) { return a < b; }
public bool ne(f64 a, f64 b) { return a != b; }
// CHECK: fcmp olt double
// CHECK: fcmp une double
