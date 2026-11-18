// ARGS: -g
// -g emits DWARF line tables with C11 as the language code (D-035).
module dbg;
public i32 f(i32 a) { return a + 1; }
// CHECK: define i32 @_NC3dbgM1f(i32 %0)
// CHECK: !dbg
// CHECK: DICompileUnit(language: DW_LANG_C11
// CHECK: emissionKind: LineTablesOnly
// CHECK: !"Dwarf Version", i32 5
// CHECK: DISubprogram(name: "f", linkageName: "_NC3dbgM1f"
// CHECK: DILocation(line: 4
