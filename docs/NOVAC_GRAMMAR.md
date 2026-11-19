# NovaC Grammar

Status: **Specification, Phase 1.** Nothing in this document is
implemented yet; see [FEATURE_STATUS.md](FEATURE_STATUS.md).

This is the normative syntax of NovaC. Semantics are in
[NOVAC_SPEC.md](NOVAC_SPEC.md), [NOVAC_TYPE_SYSTEM.md](NOVAC_TYPE_SYSTEM.md)
and [NOVAC_MEMORY_MODEL.md](NOVAC_MEMORY_MODEL.md).

## 1. Notation

ISO 14977-style EBNF:

| Notation | Meaning |
| --- | --- |
| `a b` | sequence |
| `a \| b` | alternative |
| `[ a ]` | optional |
| `{ a }` | zero or more |
| `"x"` | terminal (keyword or punctuator) |
| `UPPER` | token class from §2 |

Productions tagged **[P2]** form the Phase 2 subset: the first compiler
milestone must lex, parse, resolve and type-check exactly these (see
[NOVAC_ROADMAP.md §Phase 2](NOVAC_ROADMAP.md#phase-2--compiler-foundation)).
Untagged productions are parsed later; until then a construct the parser
recognises but the compiler does not implement is reported as
**E9001 "not implemented in this version"**, never silently accepted.

## 2. Lexical grammar

### 2.1 Source text

Source files are UTF-8 with extension `.nc`. A byte-order mark is
rejected. Line terminators are LF or CRLF. Non-ASCII characters are
permitted only inside comments and string literals (version 1).

### 2.2 Whitespace and comments

```ebnf
WHITESPACE    = " " | "\t" | "\r" | "\n" ;
LINE_COMMENT  = "//" { any-char-except-newline } ;
BLOCK_COMMENT = "/*" { any-char } "*/" ;        (* does not nest, as in C *)
```

An unterminated block comment is E1001. A `#` at the start of a line
(after optional whitespace) is **E1010** with a fix-it explaining the
replacement for the directive found (D-004).

### 2.3 Identifiers and keywords

```ebnf
IDENT = ( letter | "_" ) { letter | digit | "_" } ;   (* not a keyword *)
letter = "A".."Z" | "a".."z" ;
digit  = "0".."9" ;
```

Identifiers beginning with `__` are reserved for the implementation
(E3020 if declared by user code outside `core`).

Keywords (the authoritative list; `tools/check_docs.py` checks that
[NOVAC_SPEC.md](NOVAC_SPEC.md) lists the same set):

<!-- keywords:begin -->
```text
alignof as asm async auto await bool break case const constexpr continue
default defer do else enum extern f32 f64 false fallthrough fn for goto
i16 i32 i64 i8 if impl import in int interface is isize module mut none
null offsetof public return self Self sizeof some static struct switch
true try typedef u16 u32 u64 u8 uninit union unsafe usize void volatile
where while
```
<!-- keywords:end -->

Reserved but without meaning in version 1: `async`, `await`, `goto`,
`in`, `where`. `volatile` is meaningful only directly after `asm`
(D-027, D-028).

Contextual identifiers (ordinary identifiers with meaning in specific
positions): `C` after `extern`, and the names of the built-in generic
types `slice`, `optional`, `result`, `atomic`, `mmio`, and the type alias
`str`, which are predeclared in the prelude (they may be shadowed, with
warning W3001).

### 2.4 Literals

```ebnf
INT_LIT      = ( dec_int | hex_int | oct_int | bin_int ) [ int_suffix ] ;
dec_int      = "0" | nonzero-digit { [ "_" ] digit } ;
hex_int      = "0x" hex-digit { [ "_" ] hex-digit } ;
oct_int      = "0o" oct-digit { [ "_" ] oct-digit } ;
bin_int      = "0b" bin-digit { [ "_" ] bin-digit } ;
int_suffix   = "i8" | "i16" | "i32" | "i64" | "isize"
             | "u8" | "u16" | "u32" | "u64" | "usize" ;

FLOAT_LIT    = ( dec_float | hex_float ) [ float_suffix ] ;
dec_float    = digits "." digits [ exponent ] | digits exponent ;
exponent     = ( "e" | "E" ) [ "+" | "-" ] digits ;
hex_float    = "0x" hex-digits [ "." hex-digits ] ( "p" | "P" ) [ "+" | "-" ] digits ;
float_suffix = "f32" | "f64" ;
digits       = digit { [ "_" ] digit } ;

CHAR_LIT     = "'" ( ascii-char-except-quote-backslash | escape ) "'" ;
STRING_LIT   = "\"" { char-except-quote-backslash-newline | escape } "\"" ;
CSTRING_LIT  = "c" STRING_LIT ;
escape       = "\\" ( "n" | "r" | "t" | "0" | "\\" | "'" | "\"" )
             | "\\x" hex-digit hex-digit
             | "\\u{" hex-digit { hex-digit } "}" ;      (* STRING_LIT only *)
```

Lexical errors: a leading-zero decimal such as `0755` is **E1020** (use
`0o755`); `.5` and `5.` are **E1021** (use `0.5`, `5.0`); a decimal
float with a type suffix applied to a hex integer (`0x1f32`) lexes as the
integer `0x1f32` — hex literals never take float suffixes; an unknown
suffix is E1022; an unterminated string is E1030.

Lifetime tokens (`'a` not followed by a closing `'`) are reserved for
Phase 5b (Q-07).

### 2.5 Punctuators

```text
{ } ( ) [ ] ; , . -> ... :: : ? @
+ - * / % +% -% *% ++ --
& | ^ ~ ! << >> && ||
= += -= *= /= %= +%= -%= *%= <<= >>= &= |= ^=
== != < <= > >=
```

Maximal munch applies, except that `>>` and `>>=` are not single tokens:
the lexer emits `>` `>` and `>` `>=`, marking the first `>` as *joint*
(immediately followed by the next token). In expression context the
parser combines a joint `>` with a following `>` into the shift operator
`>>` and with a following `>=` into `>>=`; in a generic argument list each
`>` closes one list, so `optional<slice<u8>>` closes two.

## 3. Modules and top-level declarations

```ebnf
module_file   = [ module_decl ] { import_decl } { top_decl } EOF ;            (* [P2]: module_decl, top_decl *)
module_decl   = attribute_list "module" path ";" ;                            (* [P2] *)
path          = IDENT { "." IDENT } ;                                         (* [P2] *)
import_decl   = "import" path [ "as" IDENT ] ";"
              | "import" path "." "{" IDENT { "," IDENT } [ "," ] "}" ";" ;

top_decl      = attribute_list [ "public" ]
                ( function_decl | struct_decl | union_decl | enum_decl
                | typedef_decl | static_decl | constexpr_decl
                | impl_block | interface_decl | extern_block ) ;            (* [P2]: function_decl only *)

attribute_list = { attribute } ;
attribute      = "@" IDENT [ "(" attr_arg { "," attr_arg } ")" ] ;
attr_arg       = expr | IDENT "=" expr ;
```

If `module_decl` is absent the module is named after the file stem, which
must then be a valid identifier.

### 3.1 Functions

```ebnf
function_decl = fn_qualifiers type IDENT [ generic_params ]
                "(" param_list ")" ( block | ";" ) ;                          (* [P2] *)
fn_qualifiers = [ "extern" STRING_LIT ] [ "unsafe" ] ;
param_list    = [ "void" | param { "," param } [ "," "..." ] ] ;             (* [P2] without "..." *)
param         = type ( IDENT | "self" ) ;                                     (* [P2]: type IDENT *)
generic_params = "<" generic_param { "," generic_param } ">" ;
generic_param  = IDENT [ ":" path { "+" path } ]
               | "constexpr" type IDENT ;
```

A function without a body (`;`) is permitted only inside an
`extern_block`, an `interface_decl`, or with an `extern` qualifier.
`...` is permitted only in `extern "C"` declarations (calling a C
variadic function is an unsafe operation).

### 3.2 Aggregates, enums and aliases

```ebnf
struct_decl   = "struct" IDENT [ generic_params ] ( "{" { field } "}" | ";" ) ;
union_decl    = "union"  IDENT [ generic_params ] "{" { field } "}" ;
field         = attribute_list [ "public" ] type IDENT ";" ;
enum_decl     = "enum" IDENT [ ":" type ] "{" [ enumerator { "," enumerator } [ "," ] ] "}" ;
enumerator    = IDENT [ "=" expr ] ;
typedef_decl  = "typedef" type IDENT ";" ;
```

`struct Name;` with no body declares an opaque type (FFI handles).

### 3.3 Globals and constants

```ebnf
static_decl    = "static" type IDENT [ "=" initializer ] ";" ;
constexpr_decl = "constexpr" type IDENT "=" expr ";" ;
```

### 3.4 Implementations, interfaces, external blocks

```ebnf
impl_block     = "impl" [ generic_params ] type [ "for" type ]
                 "{" { attribute_list [ "public" ] function_decl } "}" ;
interface_decl = "interface" IDENT [ generic_params ]
                 "{" { attribute_list function_decl } "}" ;
extern_block   = "extern" STRING_LIT "{" { extern_item } "}" ;
extern_item    = attribute_list [ "public" ]
                 ( [ "unsafe" ] type IDENT "(" param_list ")" ";"
                 | "static" type IDENT ";" ) ;
```

In `impl T for I` the first `type` names the interface; the keyword
`for` is reused (it cannot be confused with the loop, which is a
statement).

## 4. Types

```ebnf
type          = [ "const" ] base_type { type_suffix } ;                       (* [P2]: primitive only *)
base_type     = primitive | path [ generic_args ] | "Self" ;
primitive     = "i8" | "i16" | "i32" | "i64" | "isize"
              | "u8" | "u16" | "u32" | "u64" | "usize"
              | "f32" | "f64" | "bool" | "void" | "int" ;                     (* [P2] *)
type_suffix   = "*" [ "const" ]
              | "&" [ "const" ]
              | "[" expr "]"
              | "fn" "(" [ type { "," type } [ "," "..." ] ] ")" ;
generic_args  = "<" generic_arg { "," generic_arg } ">" ;
generic_arg   = type | INT_LIT | "(" expr ")" ;
```

Reading rules (D-005, D-006):

| Written | Meaning |
| --- | --- |
| `u8*` | pointer to mutable `u8` |
| `const u8*` | pointer to immutable `u8` |
| `u8* const` | immutable binding of a pointer to mutable `u8` (top-level `const`) |
| `const u8* const*` | pointer to (immutable pointer to immutable `u8`) |
| `Node&` | exclusive borrow of `Node` |
| `const Node&` | shared borrow of `Node` |
| `i32[4]` | array of 4 `i32` |
| `i32[4][2]` | array of 2 (array of 4 `i32`) — suffixes apply left to right |
| `i32 fn(i32, i32)` | function pointer taking two `i32`, returning `i32` |
| `void fn()*` | pointer to a function pointer |

## 5. Statements

```ebnf
block          = "{" { statement } "}" ;                                       (* [P2] *)
statement      = block                                                         (* [P2] *)
               | decl_stmt                                                     (* [P2] *)
               | if_stmt | while_stmt | do_stmt | for_stmt                     (* [P2] *)
               | switch_stmt
               | "break" ";" | "continue" ";"                                  (* [P2] *)
               | "return" [ expr ] ";"                                         (* [P2] *)
               | "fallthrough" ";"
               | "defer" statement
               | "unsafe" block
               | asm_stmt
               | expr ";"                                                      (* [P2] *)
               | ";" ;                                                         (* [P2] *)

decl_stmt      = [ "static" ] decl_head declarator { "," declarator } ";" ;    (* [P2] without "static" *)
decl_head      = type | [ "const" ] "auto" ;                                   (* [P2] *)
declarator     = IDENT [ "=" initializer ] ;                                   (* [P2] *)
initializer    = expr | "{" [ init_item { "," init_item } [ "," ] ] "}" | "uninit" ;   (* [P2]: expr *)
init_item      = [ "." IDENT "=" | "[" expr "]" "=" ] initializer ;

if_stmt        = "if" "(" condition ")" statement [ "else" statement ] ;       (* [P2] *)
condition      = expr [ "is" pattern ] ;                                       (* [P2]: expr *)
pattern        = ( "some" | "none" | IDENT ) [ "(" ( IDENT | "_" ) ")" ] ;
while_stmt     = "while" "(" condition ")" statement ;                         (* [P2] *)
do_stmt        = "do" statement "while" "(" expr ")" ";" ;                     (* [P2] *)
for_stmt       = "for" "(" [ for_init ] ";" [ expr ] ";" [ expr_list ] ")" statement ;   (* [P2] *)
for_init       = decl_head declarator { "," declarator } | expr_list ;         (* [P2] *)
expr_list      = expr { "," expr } ;                                           (* [P2] *)
switch_stmt    = "switch" "(" expr ")" "{" { case_clause } "}" ;
case_clause    = ( "case" expr { "," expr } ":" | "default" ":" ) { statement } ;

asm_stmt       = "asm" [ "volatile" ] "(" STRING_LIT
                 [ ":" [ asm_operands ] [ ":" [ asm_operands ] [ ":" [ asm_clobbers ] ] ] ] ")" ";" ;
asm_operands   = asm_operand { "," asm_operand } ;
asm_operand    = [ "[" IDENT "]" ] STRING_LIT "(" expr ")" ;
asm_clobbers   = STRING_LIT { "," STRING_LIT } ;
```

**Declaration-statement disambiguation (D-005).** At statement start the
parser first attempts `decl_stmt`; if a `type` followed by an `IDENT`
parses, the statement is a declaration. Otherwise it is re-parsed as an
expression statement. Backtracking is bounded by the length of the type.

**Dangling `else`** binds to the nearest `if`, as in C.

**Struct literals.** In expression position, an identifier, a dotted path
or `Self`, optionally followed by generic arguments, that is immediately
followed by `{` begins a struct literal (`Point{...}`, `Vec<i32>{...}`).
No other valid expression has that shape, so the rule needs no symbol
table.

**Missing `;` at the end of a line** is reported (E2001) and parsing
continues as if it were present (error recovery only; the program is still
rejected).

## 6. Expressions

Precedence, highest first. All binary levels are left-associative unless
noted.

| Level | Operators | Associativity |
| --- | --- | --- |
| 1 Postfix | `f(...)` `a[i]` `a.b` `p->b` `x++` `x--` `name::<T>` | left |
| 2 Prefix | `+x` `-x` `!x` `~x` `*p` `&x` `&mut x` `++x` `--x` `try x` | right |
| 3 Multiplicative | `*` `/` `%` `*%` | left |
| 4 Additive | `+` `-` `+%` `-%` | left |
| 5 Shift | `<<` `>>` | left |
| 6 Relational | `<` `<=` `>` `>=` `is` | **none** |
| 7 Equality | `==` `!=` | **none** |
| 8 Bitwise AND | `&` | left |
| 9 Bitwise XOR | `^` | left |
| 10 Bitwise OR | `\|` | left |
| 11 Logical AND | `&&` | left |
| 12 Logical OR | `\|\|` | left |
| 13 Conditional | `c ? a : b` | right |
| 14 Assignment | `=` `+=` `-=` `*=` `/=` `%=` `+%=` `-%=` `*%=` `<<=` `>>=` `&=` `^=` `\|=` | right |

C's precedence is kept, including its well-known trap that `x & m == 0`
parses as `x & (m == 0)`; NovaC's type rules reject that expression
(no implicit `bool`↔integer conversion), so the trap becomes an error.

```ebnf
expr           = assign_expr ;                                                 (* [P2] *)
assign_expr    = cond_expr [ assign_op assign_expr ] ;                         (* [P2] *)
cond_expr      = or_expr [ "?" expr ":" cond_expr ] ;                          (* [P2] *)
or_expr        = and_expr { "||" and_expr } ;                                  (* [P2] *)
and_expr       = bitor_expr { "&&" bitor_expr } ;                              (* [P2] *)
bitor_expr     = bitxor_expr { "|" bitxor_expr } ;                             (* [P2] *)
bitxor_expr    = bitand_expr { "^" bitand_expr } ;                             (* [P2] *)
bitand_expr    = eq_expr { "&" eq_expr } ;                                     (* [P2] *)
eq_expr        = rel_expr [ ( "==" | "!=" ) rel_expr ] ;                       (* [P2] *)
rel_expr       = shift_expr [ ( "<" | "<=" | ">" | ">=" ) shift_expr
                            | "is" pattern ] ;                                 (* [P2]: without "is" *)
shift_expr     = add_expr { ( "<<" | ">>" ) add_expr } ;                       (* [P2] *)
add_expr       = mul_expr { ( "+" | "-" | "+%" | "-%" ) mul_expr } ;           (* [P2] *)
mul_expr       = prefix_expr { ( "*" | "/" | "%" | "*%" ) prefix_expr } ;      (* [P2] *)
prefix_expr    = ( "+" | "-" | "!" | "~" | "++" | "--" ) prefix_expr           (* [P2] *)
               | ( "*" | "&" | "&" "mut" | "try" ) prefix_expr
               | postfix_expr ;                                                (* [P2] *)
postfix_expr   = primary { postfix_op } ;                                      (* [P2] *)
postfix_op     = "(" [ expr { "," expr } ] ")"                                 (* [P2] *)
               | "[" expr "]" | "." IDENT | "->" IDENT
               | "::" generic_args
               | "++" | "--" ;                                                 (* [P2] *)
primary        = INT_LIT | FLOAT_LIT | "true" | "false"                        (* [P2] *)
               | CHAR_LIT | STRING_LIT | CSTRING_LIT
               | "null" | "none" | "self"
               | IDENT                                                         (* [P2] *)
               | "(" expr ")"                                                  (* [P2] *)
               | primitive "(" expr ")"                                        (* [P2] conversion *)
               | ( path | "Self" ) [ generic_args ] "{" [ init_item { "," init_item } [ "," ] ] "}"
               | "some" "(" expr ")"
               | "sizeof" "(" ( type | expr ) ")"
               | "alignof" "(" type ")"
               | "offsetof" "(" type "," IDENT ")" ;
assign_op      = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "+%=" | "-%=" | "*%="
               | "<<=" | ">>=" | "&=" | "^=" | "|=" ;                          (* [P2] *)
```

`sizeof(x)` where `x` is a single identifier is syntactically ambiguous
between a type and an expression; the parser produces an unresolved
operand and name resolution decides.

A C-style cast `(T)x` does not parse as a cast: when the parenthesised
contents are a primitive type the parser reports **E2031** with the fix-it
`T(x)` (D-010).

## 7. Grammar-level diagnostics (summary)

| Code | Condition |
| --- | --- |
| E1001 | unterminated block comment |
| E1002 | unexpected character (including non-ASCII outside comments and strings) |
| E1003 | source file begins with a byte-order mark |
| E1010 | preprocessor directive (`#include`, `#define`, …) |
| E1020 | leading-zero decimal literal |
| E1021 | float literal without digits on both sides of `.` |
| E1022 | unknown literal suffix |
| E1023 | integer literal does not fit in 64 bits |
| E1024 | malformed numeric literal (no digits after a base prefix, digit invalid for the base, misplaced `_`, hexadecimal float without exponent) |
| E1025 | floating-point literal out of range for `f64` |
| E1030 | unterminated string or character literal |
| E1031 | invalid escape sequence (including `\u{...}` in a character literal) |
| E1032 | invalid character literal (empty, more than one character, non-ASCII) |
| E1033 | NUL byte (`\0`, `\x00`) in a C string literal `c"..."` |
| E2001 | unexpected token (generic) |
| E2010 | chained relational/equality operator (`a < b < c`) |
| E2020 | C-style array declarator (`int a[4]`) — fix-it `i32[4] a` |
| E2031 | C-style cast — fix-it `T(x)` |
| E2050 | `switch` case falls through without `fallthrough;` |
| E2051 | `fallthrough;` that is not the last statement of a case followed by another case |
| E2060 | `goto` (not supported, D-034) |
| E9001 | construct recognised but not implemented in this version |
| E9004 | syntax tree deeper than the implementation limit of 1000 levels (nesting, operator chains, postfix chains and type suffixes all count) |

The full diagnostic code allocation is in
[NOVAC_COMPILER_ARCHITECTURE.md §6](NOVAC_COMPILER_ARCHITECTURE.md#6-diagnostics).
