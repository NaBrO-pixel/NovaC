#!/usr/bin/env python3
"""Generates the C ABI conformance tests (NOVAC_ABI.md §12).

For every type below, NovaC and C (compiled by Clang) call each other in
both directions: C makes a value and NovaC checks it, NovaC makes one and C
checks it, values are echoed through both, and a call with eight copies
exhausts the argument registers. Scalars, sign/zero extension and
variadic calls have their own pair of files. Both sides compute the same
checksum of every field; any disagreement prints a FAIL line.

    python3 tests/abi/gen.py           rewrite the generated files
    python3 tests/abi/gen.py --check   fail if they are out of date

The tests run in the `abi` suite of tests/run_tests.py on every target.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# NovaC scalar -> (C type, kind) ; kind: s signed, u unsigned, f float, b bool, p pointer
SCALARS = {
    "i8": ("signed char", "s"), "u8": ("unsigned char", "u"), "i16": ("short", "s"), "u16": ("unsigned short", "u"),
    "i32": ("int", "s"), "u32": ("unsigned", "u"), "i64": ("long", "s"), "u64": ("unsigned long", "u"),
    "f32": ("float", "f"), "f64": ("double", "f"), "bool": ("_Bool", "b"), "u8*": ("unsigned char *", "p"),
    "E": ("enum E", "e"),
}

# name -> (is_union, [(type, field name, array length or 0)])
def S(*fields, union=False):
    out = []
    for i, f in enumerate(fields):
        if isinstance(f, tuple):
            out.append((f[0], f"f{i}", f[1]))
        else:
            out.append((f, f"f{i}", 0))
    return (union, out)

TYPES = {
    "I1": S("i32"), "I2": S("i32", "i32"), "I3": S("i32", "i32", "i32"), "I4": S("i32", "i32", "i32", "i32"),
    "L1": S("i64"), "L2": S("i64", "i64"), "L3": S("i64", "i64", "i64"),
    "C1": S("u8"), "C3": S("u8", "u8", "u8"), "C5": S(("i8", 5)), "C9": S(("u8", 9)), "S3": S("i16", "i16", "i16"),
    "B2": S("bool", "bool"),
    "F1": S("f32"), "F2": S("f32", "f32"), "F3": S("f32", "f32", "f32"), "F4": S(("f32", 4)), "F5": S(("f32", 5)),
    "D1": S("f64"), "D2": S("f64", "f64"), "D3": S("f64", "f64", "f64"), "D4": S(("f64", 4)), "D5": S(("f64", 5)),
    "FI": S("f32", "i32"), "IF": S("i32", "f32"), "FL": S("f32", "i64"), "LF": S("i64", "f32"),
    "DL": S("f64", "i64"), "LD": S("i64", "f64"), "FD": S("f32", "f64"), "DF": S("f64", "f32"),
    "IFI": S("i32", "f32", "i32"), "CD": S("u8", "f64"), "FFI": S("f32", "f32", "i32"), "SF": S("i16", "f32"),
    "AF": S(("f32", 2)), "AF3": S(("f32", 3)),
    "NF": S("F2", "f32"), "ND": S("D1", "D1"), "NM": S("I1", "F1"), "NFD": S("F1", "D1"),
    "P1": S("u8*"), "P2": S("u8*", "i32"), "EN": S("E", "i32"),
    "U1": S("f32", "i32", union=True), "U2": S("f64", "i64", union=True), "UF": S("f32", "f32", union=True),
    "UD": S("D2", "f64", union=True), "UBig": S(("i64", 3), "i8", union=True), "UFI": S("FI", "i64", union=True),
    "BigMix": S("i32", "f64", "u8", "i64"), "Align": S("u8", "i64"), "Pad3": S("u8", "i16", "u8"),
    "MixArr": S(("i16", 3), "f32"),
}

MANY = 8  # copies in the register-pressure test


def leaves(tname, path):
    """(path, scalar) for every field that is set and checked; for a union, its first member."""
    if tname in SCALARS:
        return [(path, tname)]
    union, fields = TYPES[tname]
    out = []
    for ftype, fname, n in (fields[:1] if union else fields):
        if n:
            for k in range(n):
                out += leaves(ftype, f"{path}.{fname}[{k}]")
        else:
            out += leaves(ftype, f"{path}.{fname}")
    return out


# ---- values: the same on both sides --------------------------------------------------------

def c_value(kind, ctype, j):
    x = f"((s * 7 + {j * 13}) % 50 - 20)"
    if kind == "s": return f"({ctype})({x} * {1000000007 if ctype == 'long' else 1})"
    if kind == "u": return f"({ctype})((s * 7 + {j * 13}) % 200 + 1)"
    if kind == "f": return f"({ctype})((s + {j}) + 0.5)"
    if kind == "b": return f"(_Bool)((s + {j}) % 2)"
    if kind == "p": return f"(unsigned char *)(unsigned long)(4096 + 16 * (s + {j}))"
    if kind == "e": return f"((s + {j}) % 2 ? E_B : E_A)"


def nc_value(kind, ntype, j):
    x = f"((s * 7 + {j * 13}) % 50 - 20)"
    if kind == "s": return f"{ntype}({x} * {1000000007 if ntype == 'i64' else 1})"
    if kind == "u": return f"{ntype}((s * 7 + {j * 13}) % 200 + 1)"
    if kind == "f": return f"{ntype}(f64(s + {j}) + 0.5)"
    if kind == "b": return f"(s + {j}) % 2 == 1"
    if kind == "p": return f"ptr_from_addr::<u8>(usize(4096 + 16 * (s + {j})))"
    if kind == "e": return f"(s + {j}) % 2 == 1 ? E.B : E.A"


def c_enc(kind, e):
    if kind == "s": return f"(unsigned long)((long){e} + (1L << 40))"
    if kind in "ub": return f"(unsigned long){e}"
    if kind == "f": return f"(unsigned long)((double){e} * 4.0)"
    if kind == "p": return f"(unsigned long){e}"
    if kind == "e": return f"(unsigned long)(int){e}"


def nc_enc(kind, ntype, e):
    if kind == "s": return f"u64(i64({e}) + 1099511627776)"
    if kind == "u": return f"u64({e})"
    if kind == "b": return f"u64({e})"
    if kind == "f": return f"u64(f64({e}) * 4.0)"
    if kind == "p": return f"u64(addr({e}))"
    if kind == "e": return f"u64(c_int({e}))"


# ---- declarations ---------------------------------------------------------------------------

def c_type(t):
    if t in SCALARS: return SCALARS[t][0]
    return ("union " if TYPES[t][0] else "struct ") + t


def c_decls():
    out = ["enum E { E_Z = 0, E_A = 1, E_B = 5 };"]
    for name, (union, fields) in TYPES.items():
        body = " ".join(f"{c_type(ft)} {fn}{f'[{n}]' if n else ''};" for ft, fn, n in fields)
        out.append(f"{'union' if union else 'struct'} {name} {{ {body} }};")
    return "\n".join(out)


def nc_decls():
    out = ["enum E : c_int { Z = 0, A = 1, B = 5 }"]
    for name, (union, fields) in TYPES.items():
        body = " ".join(f"{ft}{f'[{n}]' if n else ''} {fn};" for ft, fn, n in fields)
        out.append(f"{'union' if union else 'struct'} {name} {{ {body} }}")
    return "\n".join(out)


# ---- the struct tests -------------------------------------------------------------------------

def gen_structs():
    c = [f"/* Generated by tests/abi/gen.py: do not edit. C half of abi_structs.nc. */",
         "void *memset(void *, int, unsigned long);", c_decls(), ""]
    n = [f"// Generated by tests/abi/gen.py: do not edit. NovaC half of the C ABI",
         "// conformance tests for aggregates (NOVAC_ABI.md §5, §12).",
         "// C-SOURCE: abi_structs.c",
         "@mode(controlled) module abi_structs;", "",
         'extern "C" c_int printf(const c_char* fmt, ...);', nc_decls(), ""]
    for t in TYPES:
        ls = leaves(t, "v")
        ct = c_type(t)
        # C: make, sum, echo, many, check NovaC's exports
        c.append(f"{ct} c_make_{t}(long s) {{ {ct} v; memset(&v, 0, sizeof v);")
        for j, (path, st) in enumerate(ls):
            c.append(f"    {path} = {c_value(SCALARS[st][1], SCALARS[st][0], j)};")
        c.append("    return v; }")
        c.append(f"unsigned long c_sum_{t}({ct} v) {{ unsigned long h = 17;")
        for path, st in ls:
            c.append(f"    h = h * 31 + {c_enc(SCALARS[st][1], path)};")
        c.append("    return h; }")
        c.append(f"{ct} c_echo_{t}({ct} v) {{ return v; }}")
        params = ", ".join(f"{ct} v{k}" for k in range(MANY))
        c.append(f"unsigned long c_many_{t}(long a, double f, {params}, int tail) {{")
        c.append("    unsigned long h = (unsigned long)(a + 1000) + (unsigned long)(f * 2.0) + (unsigned long)tail;")
        for k in range(MANY):
            c.append(f"    h = h * 3 + c_sum_{t}(v{k});")
        c.append("    return h; }")
        c.append(f"{ct} nv_make_{t}(long s); unsigned long nv_sum_{t}({ct} v); {ct} nv_echo_{t}({ct} v);")
        c.append(f"unsigned long nv_many_{t}(long a, double f, {params}, int tail);")
        args = ", ".join(f"c_make_{t}(s + {k})" for k in range(MANY))
        c.append(f"int c_check_{t}(long s) {{ int bad = 0; {ct} v = c_make_{t}(s);")
        c.append(f"    if (c_sum_{t}(nv_make_{t}(s)) != c_sum_{t}(v)) bad |= 1;")
        c.append(f"    if (nv_sum_{t}(v) != c_sum_{t}(v)) bad |= 2;")
        c.append(f"    if (c_sum_{t}(nv_echo_{t}(v)) != c_sum_{t}(v)) bad |= 4;")
        c.append(f"    if (nv_many_{t}(-3, 1.5, {args}, 9) != c_many_{t}(-3, 1.5, {args}, 9)) bad |= 8;")
        c.append("    return bad; }")
        c.append("")

        # NovaC: imports, a native checksum, exports
        n.append(f'extern "C" {{ {t} c_make_{t}(i64 s); u64 c_sum_{t}({t} v); {t} c_echo_{t}({t} v); '
                 f'c_int c_check_{t}(i64 s);')
        nparams = ", ".join(f"{t} v{k}" for k in range(MANY))
        n.append(f"    u64 c_many_{t}(i64 a, f64 f, {nparams}, c_int tail); }}")
        n.append(f"unsafe {t} make_{t}(i64 s) {{ {t} v = {{}};")
        for j, (path, st) in enumerate(ls):
            n.append(f"    {path} = {nc_value(SCALARS[st][1], st, j)};")
        n.append("    return v; }")
        n.append(f"unsafe u64 sum_{t}({t} v) {{ u64 h = 17;")
        for path, st in ls:
            n.append(f"    h = h *% 31 +% {nc_enc(SCALARS[st][1], st, path)};")
        n.append("    return h; }")
        n.append(f'extern "C" unsafe {t} nv_make_{t}(i64 s) {{ return make_{t}(s); }}')
        n.append(f'extern "C" unsafe u64 nv_sum_{t}({t} v) {{ return sum_{t}(v); }}')
        n.append(f'extern "C" {t} nv_echo_{t}({t} v) {{ return v; }}')
        n.append(f'extern "C" unsafe u64 nv_many_{t}(i64 a, f64 f, {nparams}, c_int tail) {{')
        n.append("    u64 h = u64(a + 1000) +% u64(f * 2.0) +% u64(tail);")
        for k in range(MANY):
            n.append(f"    h = h *% 3 +% sum_{t}(v{k});")
        n.append("    return h; }")
        nargs = ", ".join(f"make_{t}(s + {k})" for k in range(MANY))
        n.append(f"unsafe c_int test_{t}(i64 s) {{ c_int bad = 0; {t} v = make_{t}(s);")
        n.append(f"    if (sum_{t}(c_make_{t}(s)) != sum_{t}(v)) bad |= 16;")
        n.append(f"    if (c_sum_{t}(v) != sum_{t}(v)) bad |= 32;")
        n.append(f"    if (sum_{t}(c_echo_{t}(v)) != sum_{t}(v)) bad |= 64;")
        n.append(f"    if (c_many_{t}(-3, 1.5, {nargs}, 9) != nv_many_{t}(-3, 1.5, {nargs}, 9)) bad |= 128;")
        n.append(f"    return bad | c_check_{t}(s); }}")
        n.append("")
    n.append("int main(void) {")
    n.append("    c_int failures = 0;")
    n.append("    unsafe {")
    n.append("        for (i64 s = 1; s <= 3; s++) {")
    for t in TYPES:
        n.append(f"            c_int r_{t} = test_{t}(s);")
        n.append(f'            if (r_{t} != 0) {{ printf(c"FAIL {t} seed %ld: mask %d\\n", s, r_{t}); failures++; }}')
    n.append("        }")
    n.append(f'        printf(c"abi structs: %d failures\\n", failures);')
    n.append("    }")
    n.append("    return failures == 0 ? 0 : 1;")
    n.append("}")
    n.append("// STDOUT: abi structs: 0 failures")
    return "\n".join(c) + "\n", "\n".join(n) + "\n"


# ---- scalars, extension, variadic calls ------------------------------------------------------------

SC = ["i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64", "bool"]


def gen_scalars():
    c = ["/* Generated by tests/abi/gen.py: do not edit. C half of abi_scalars.nc. */",
         "#include <stdarg.h>", ""]
    n = ["// Generated by tests/abi/gen.py: do not edit. NovaC half of the C ABI",
         "// conformance tests for scalars, sign/zero extension and variadic calls.",
         "// C-SOURCE: abi_scalars.c", "@mode(controlled) module abi_scalars;", "",
         'extern "C" c_int printf(const c_char* fmt, ...);', ""]
    # every scalar type as argument and result, twice (registers, then the stack)
    cparams = ", ".join(f"{SCALARS[t][0]} a{i}" for i, t in enumerate(SC * 2))
    nparams = ", ".join(f"{t} a{i}" for i, t in enumerate(SC * 2))
    c.append(f"unsigned long c_all({cparams}) {{ unsigned long h = 7;")
    for i, t in enumerate(SC * 2):
        c.append(f"    h = h * 31 + {c_enc(SCALARS[t][1], f'a{i}')};")
    c.append("    return h; }")
    n.append(f'extern "C" u64 c_all({nparams});')
    n.append(f'extern "C" unsafe u64 nv_all({nparams}) {{ u64 h = 7;')
    for i, t in enumerate(SC * 2):
        n.append(f"    h = h *% 31 +% {nc_enc(SCALARS[t][1], t, f'a{i}')};")
    n.append("    return h; }")
    for t in SC:
        ct, kind = SCALARS[t]
        c.append(f"{ct} c_ret_{t}(long s) {{ return {c_value(kind, ct, 3)}; }}")
        c.append(f"{ct} nv_ret_{t}(long s);")
        c.append(f"int c_check_ret_{t}(long s) {{ return nv_ret_{t}(s) == c_ret_{t}(s); }}")
        n.append(f'extern "C" {{ {t} c_ret_{t}(i64 s); c_int c_check_ret_{t}(i64 s); }}')
        n.append(f'extern "C" unsafe {t} nv_ret_{t}(i64 s) {{ return {nc_value(kind, t, 3)}; }}')
    # C calls nv_all with values it computes, compares with its own checksum
    vals = ", ".join(c_value(SCALARS[t][1], SCALARS[t][0], i) for i, t in enumerate(SC * 2))
    c.append(f"unsigned long nv_all({cparams});")
    c.append(f"int c_check_all(long s) {{ return nv_all({vals}) == c_all({vals}); }}")
    n.append('extern "C" c_int c_check_all(i64 s);')
    # variadic: alternating long and double
    c.append("double c_vsum(int n, ...) { va_list ap; va_start(ap, n); double t = 0;")
    c.append("    for (int i = 0; i < n; i++) t += i % 2 ? va_arg(ap, double) : (double)va_arg(ap, long);")
    c.append("    va_end(ap); return t; }")
    n.append('extern "C" f64 c_vsum(c_int n, ...);')
    n.append("unsafe c_int run(i64 s) {")
    n.append("    c_int bad = 0;")
    nvals = ", ".join(nc_value(SCALARS[t][1], t, i) for i, t in enumerate(SC * 2))
    n.append(f"    if (c_all({nvals}) != nv_all({nvals})) bad |= 1;")
    n.append("    if (c_check_all(s) == 0) bad |= 2;")
    for k, t in enumerate(SC):
        n.append(f"    if (c_ret_{t}(s) != ({nc_value(SCALARS[t][1], t, 3)})) bad |= 4;")
        n.append(f"    if (c_check_ret_{t}(s) == 0) bad |= 8;")
    n.append("    if (c_vsum(12, 1i64, 0.5, 2i64, 0.25, 3i64, 1.5, 4i64, 2.0, 5i64, 0.125, 6i64, 8.0) != 33.375) bad |= 16;")
    n.append("    // Values computed in wider types and narrowed must arrive extended.")
    n.append("    i32 wide = i32(s) * 1000 - 3000;")
    n.append("    i8 narrow = i8(wide / 1000 - 1);")
    n.append(f"    if (c_all({', '.join(['narrow' if t == 'i8' else nc_value(SCALARS[t][1], t, i) for i, t in enumerate(SC * 2)])})"
             f" != nv_all({', '.join(['narrow' if t == 'i8' else nc_value(SCALARS[t][1], t, i) for i, t in enumerate(SC * 2)])})) bad |= 32;")
    n.append("    return bad;")
    n.append("}")
    n.append("")
    n.append("int main(void) {")
    n.append("    c_int failures = 0;")
    n.append("    unsafe {")
    n.append("        for (i64 s = 1; s <= 3; s++) {")
    n.append("            c_int r = run(s);")
    n.append('            if (r != 0) { printf(c"FAIL scalars seed %ld: mask %d\\n", s, r); failures++; }')
    n.append("        }")
    n.append('        printf(c"abi scalars: %d failures\\n", failures);')
    n.append("    }")
    n.append("    return failures == 0 ? 0 : 1;")
    n.append("}")
    n.append("// STDOUT: abi scalars: 0 failures")
    return "\n".join(c) + "\n", "\n".join(n) + "\n"


def main():
    files = {}
    files["abi_structs.c"], files["abi_structs.nc"] = gen_structs()
    files["abi_scalars.c"], files["abi_scalars.nc"] = gen_scalars()
    check = "--check" in sys.argv[1:]
    stale = []
    for name, text in files.items():
        path = os.path.join(HERE, name)
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if old != text:
            if check:
                stale.append(name)
            else:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
    if stale:
        print("tests/abi: out of date (run python3 tests/abi/gen.py): " + ", ".join(stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
