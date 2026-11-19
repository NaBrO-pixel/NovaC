// Functions in an extern block are declarations; the block takes no attributes.
extern "C" {
    void body() { }                             // ERROR E2001
    c_int fine(c_int x);
}
@cold extern "C" { void g(); }                  // ERROR E2001
