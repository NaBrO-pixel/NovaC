/* Module-level declarations: the module namespace, attributes, written
 * types, struct/union layout (NOVAC_ABI.md §4), enums, typedefs,
 * constexprs and statics.
 *
 * Everything is resolved lazily, on first use, so declarations may appear
 * in any order. Resolution marks a declaration BUSY while it runs; reaching
 * a BUSY declaration again is a cycle: a struct that contains itself by
 * value (E4080) or a typedef / constexpr defined in terms of itself
 * (E3005). */
#include "sema/sema_internal.h"
#include "target/target.h"

#include <string.h>

/* ---- attributes (NOVAC_SPEC.md §9) -------------------------------------------------- */

static const char *const known_attrs[] = {
    "mode", "cfg", "export", "link_name", "callconv", "interrupt", "naked", "section", "used", "align",
    "packed", "repr", "nocopy", "inline", "cold", "noreturn", "must_use", "deprecated", "thread_local",
    "overflow", "panic_handler", "asm_syntax", "unsafe_impl",
};

unsigned nc_sema_attrs(nc_sema *sm, const nc_attr *a, uint32_t n, unsigned allowed)
{
    unsigned flags = 0;
    for (uint32_t i = 0; i < n; i++) {
        const char *name = nc_sema_sym(sm, a[i].name);
        bool known = false;
        for (size_t k = 0; k < sizeof known_attrs / sizeof known_attrs[0]; k++)
            if (strcmp(known_attrs[k], name) == 0) known = true;
        if (!known) {
            nc_diag_error(NC_DIAG(sm), a[i].span, "E3040", "unknown attribute '@%s'", name);
            continue;
        }
        unsigned flag = strcmp(name, "packed") == 0  ? NC_ATTR_PACKED
                        : strcmp(name, "repr") == 0  ? NC_ATTR_REPR
                        : strcmp(name, "mode") == 0  ? NC_ATTR_MODE
                        : strcmp(name, "export") == 0  ? NC_ATTR_EXPORT
                        : strcmp(name, "link_name") == 0 ? NC_ATTR_LINK_NAME
                                                     : 0;
        if (!flag) {
            nc_diag_error(NC_DIAG(sm), a[i].span, "E9001", "attribute '@%s' is not implemented in this version", name);
            continue;
        }
        if (!(allowed & flag)) {
            nc_diag_error(NC_DIAG(sm), a[i].span, "E3041", "attribute '@%s' cannot be applied here", name);
            continue;
        }
        if (flag == NC_ATTR_PACKED && a[i].has_parens) {
            nc_diag_error(NC_DIAG(sm), a[i].span, "E3041", "'@packed' takes no arguments");
            continue;
        }
        if (flag == NC_ATTR_EXPORT || flag == NC_ATTR_LINK_NAME) {
            const nc_expr *arg = a[i].nargs == 1 ? a[i].args[0].value : NULL;
            if (!arg || arg->kind != NC_E_STRING || nc_sym_len(&sm->s->strtab, arg->u.str) == 0) {
                nc_diag_error(NC_DIAG(sm), a[i].span, "E3041", "'@%s' takes one argument, the symbol name as a string",
                              name);
                continue;
            }
        }
        if (flag == NC_ATTR_MODE) {
            const nc_expr *arg = a[i].nargs == 1 ? a[i].args[0].value : NULL;
            const char *m = arg && arg->kind == NC_E_NAME ? nc_sema_sym(sm, arg->u.name) : "";
            if (strcmp(m, "safe") == 0) sm->mode = NC_MODE_SAFE;
            else if (strcmp(m, "controlled") == 0) sm->mode = NC_MODE_CONTROLLED;
            else if (strcmp(m, "kernel") == 0) sm->mode = NC_MODE_KERNEL;
            else {
                nc_diag_error(NC_DIAG(sm), a[i].span, "E3041", "'@mode' takes one argument: `safe`, `controlled` or `kernel`");
                continue;
            }
        }
        if (flag == NC_ATTR_REPR) {
            /* @repr(C): C layout is already the default (D-017). */
            const nc_expr *arg = a[i].nargs == 1 ? a[i].args[0].value : NULL;
            if (!arg || arg->kind != NC_E_NAME || strcmp(nc_sema_sym(sm, arg->u.name), "C") != 0) {
                nc_diag_error(NC_DIAG(sm), a[i].span, "E3041", "'@repr' takes one argument, `C`");
                continue;
            }
        }
        flags |= flag;
    }
    return flags;
}

nc_sym nc_sema_attr_string(nc_sema *sm, const nc_attr *a, uint32_t n, const char *name)
{
    for (uint32_t i = 0; i < n; i++) {
        if (strcmp(nc_sema_sym(sm, a[i].name), name) != 0 || a[i].nargs != 1) continue;
        const nc_expr *arg = a[i].args[0].value;
        if (arg && arg->kind == NC_E_STRING) return arg->u.str;
    }
    return 0;
}

/* ---- core.ffi (D-045) ---------------------------------------------------------------- */

const nc_type *nc_sema_ffi_type(nc_sema *sm, const char *name)
{
    static const struct {
        const char *name;
        nc_prim prim;
    } ints[] = {
        {"c_schar", NC_PRIM_I8},      {"c_uchar", NC_PRIM_U8},    {"c_short", NC_PRIM_I16},
        {"c_ushort", NC_PRIM_U16},    {"c_int", NC_PRIM_I32},     {"c_uint", NC_PRIM_U32},
        {"c_long", NC_PRIM_I64},      {"c_ulong", NC_PRIM_U64},   {"c_longlong", NC_PRIM_I64},
        {"c_ulonglong", NC_PRIM_U64}, {"c_size_t", NC_PRIM_USIZE}, {"c_ssize_t", NC_PRIM_ISIZE},
    };
    if (strncmp(name, "c_", 2) != 0) return NULL;
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++)
        if (strcmp(ints[i].name, name) == 0) return nc_ty_prim(ints[i].prim); /* LP64: every version-1 target */
    if (strcmp(name, "c_char") == 0) {
        bool sgn = sm->s->target ? sm->s->target->char_signed : true;
        return nc_ty_prim(sgn ? NC_PRIM_I8 : NC_PRIM_U8);
    }
    if (strcmp(name, "c_void") == 0) {
        if (!sm->s->c_void) {
            /* An opaque struct: usable only behind a pointer (NOVAC_TYPE_SYSTEM.md §2.5). */
            nc_aggregate *agg = nc_arena_alloc(&sm->s->arena, sizeof *agg);
            agg->name = nc_intern_cstr(&sm->s->strtab, "c_void");
            agg->span = NC_NO_SPAN;
            agg->opaque = true;
            sm->s->c_void = nc_ty_new_aggregate(sm->s, agg);
        }
        return sm->s->c_void;
    }
    return NULL;
}

/* Why t cannot cross a C boundary, or NULL. `top`: a parameter or result
 * itself rather than a field. */
static const char *ffi_problem(nc_sema *sm, const nc_type *t, bool top, const nc_type **culprit)
{
    *culprit = t;
    switch (t->kind) {
    case NC_TY_PRIM:
    case NC_TY_PTR: return NULL;
    case NC_TY_ENUM:
        return t->en->decl->u.en.underlying ? NULL : "an enum needs an explicit underlying type, such as `: c_int`, to match C";
    case NC_TY_ARRAY:
        if (top) return "C passes arrays as pointers; pass a pointer to the first element";
        return ffi_problem(sm, t->elem, false, culprit);
    case NC_TY_STRUCT:
    case NC_TY_UNION:
        if (t->agg->packed) return "a @packed struct has no C calling convention; pass a pointer to it";
        if (t->agg->nfields == 0) return "an empty struct has no C equivalent";
        for (uint32_t i = 0; i < t->agg->nfields; i++) {
            const char *why = ffi_problem(sm, t->agg->fields[i].type, false, culprit);
            if (why) return why;
        }
        return NULL;
    default: return NULL;
    }
}

bool nc_sema_check_ffi(nc_sema *sm, const nc_type *t, nc_span span, const char *what)
{
    if (nc_ty_is_error(t) || nc_ty_is_void(t)) return true;
    const nc_type *culprit;
    const char *why = ffi_problem(sm, t, true, &culprit);
    if (why) {
        nc_diag_error(NC_DIAG(sm), span, "E7030", "%s has type '%s', which cannot cross a C boundary", what, nc_type_name(t));
        if (culprit != t) nc_diag_note(NC_DIAG(sm), span, "because it contains '%s'", nc_type_name(culprit));
        nc_diag_help(NC_DIAG(sm), "%s", why);
        return false;
    }
    const nc_target *tg = sm->s->target;
    if (nc_ty_is_aggregate(t) && tg && !tg->abi_verified) {
        nc_diag_error(NC_DIAG(sm), span, "E7032",
                      "%s passes '%s' by value, but the C ABI lowering for %s is not verified yet", what, nc_type_name(t),
                      tg->name);
        nc_diag_help(NC_DIAG(sm), "pass a pointer instead (NOVAC_ABI.md §5)");
        return false;
    }
    return true;
}

/* ---- the module namespace ---------------------------------------------------------- */

nc_module_sym *nc_sema_find_module_sym(nc_sema *sm, nc_sym name)
{
    for (size_t i = 0; i < sm->msyms.len; i++)
        if (sm->msyms.data[i].name == name) return &sm->msyms.data[i];
    return NULL;
}

static nc_sym decl_name(const nc_decl *d, nc_span *span)
{
    switch (d->kind) {
    case NC_D_FUNCTION: *span = d->u.fn.name_span; return d->u.fn.name;
    case NC_D_STRUCT:
    case NC_D_UNION: *span = d->u.agg.name_span; return d->u.agg.name;
    case NC_D_ENUM: *span = d->u.en.name_span; return d->u.en.name;
    case NC_D_TYPEDEF:
    case NC_D_STATIC:
    case NC_D_CONSTEXPR: *span = d->u.var.name_span; return d->u.var.name;
    case NC_D_UNSUPPORTED: break;
    }
    return 0;
}

/* Enter every module-level declaration into the namespace (E3002 on a
 * duplicate; the duplicate is then ignored). Types get their nominal type
 * object now; everything else is resolved on first use. */
void nc_sema_collect(nc_sema *sm)
{
    for (uint32_t i = 0; i < sm->m->ndecls; i++) {
        nc_decl *d = sm->m->decls[i];
        nc_span span;
        nc_sym name = decl_name(d, &span);
        if (!name) continue;
        nc_module_sym *prev = nc_sema_find_module_sym(sm, name);
        if (prev) {
            nc_span pspan;
            decl_name(prev->decl, &pspan);
            nc_diag_error(NC_DIAG(sm), span, "E3002", "redefinition of '%s'", nc_sema_sym(sm, name));
            nc_diag_note(NC_DIAG(sm), pspan, "previous definition is here");
            continue;
        }
        nc_module_sym ms = {name, NC_MSYM_FN, d, NULL};
        switch (d->kind) {
        case NC_D_FUNCTION: ms.kind = NC_MSYM_FN; break;
        case NC_D_STRUCT:
        case NC_D_UNION: {
            nc_aggregate *agg = nc_arena_alloc(&sm->s->arena, sizeof *agg);
            agg->name = name;
            agg->span = span;
            agg->decl = d;
            agg->is_union = d->kind == NC_D_UNION;
            agg->opaque = d->u.agg.opaque;
            nc_ty_new_aggregate(sm->s, agg);
            ms.kind = NC_MSYM_AGGREGATE;
            ms.info = agg;
            break;
        }
        case NC_D_ENUM: {
            nc_enum_type *en = nc_arena_alloc(&sm->s->arena, sizeof *en);
            en->name = name;
            en->span = span;
            en->decl = d;
            nc_ty_new_enum(sm->s, en);
            ms.kind = NC_MSYM_ENUM;
            ms.info = en;
            break;
        }
        case NC_D_TYPEDEF: {
            nc_typedef_info *td = nc_arena_alloc(&sm->s->arena, sizeof *td);
            td->decl = d;
            ms.kind = NC_MSYM_TYPEDEF;
            ms.info = td;
            break;
        }
        case NC_D_STATIC: {
            nc_static_info *st = nc_arena_alloc(&sm->s->arena, sizeof *st);
            st->decl = d;
            st->name = name;
            st->span = span;
            st->is_public = d->is_public;
            ms.kind = NC_MSYM_STATIC;
            ms.info = st;
            break;
        }
        case NC_D_CONSTEXPR: {
            nc_const_info *c = nc_arena_alloc(&sm->s->arena, sizeof *c);
            c->decl = d;
            ms.kind = NC_MSYM_CONST;
            ms.info = c;
            break;
        }
        case NC_D_UNSUPPORTED: continue;
        }
        d->sema = ms.info;
        nc_vec_push(&sm->msyms, ms);
    }
}

/* ---- written types ------------------------------------------------------------------- */

static const nc_type *resolve_typedef(nc_sema *sm, nc_typedef_info *td)
{
    if (td->state == NC_LAYOUT_DONE) return td->type;
    if (td->state == NC_LAYOUT_FAILED) return nc_ty_error();
    if (td->state == NC_LAYOUT_BUSY) {
        nc_diag_error(NC_DIAG(sm), td->decl->u.var.name_span, "E3005", "type alias '%s' is defined in terms of itself",
                      nc_sema_sym(sm, td->decl->u.var.name));
        td->state = NC_LAYOUT_FAILED;
        return nc_ty_error();
    }
    td->state = NC_LAYOUT_BUSY;
    nc_sema_attrs(sm, td->decl->attrs, td->decl->nattrs, 0);
    bool c;
    const nc_type *t = nc_sema_resolve_type(sm, td->decl->u.var.type, &c);
    if (td->state == NC_LAYOUT_FAILED) return nc_ty_error();
    td->type = t;
    td->state = nc_ty_is_error(t) ? NC_LAYOUT_FAILED : NC_LAYOUT_DONE;
    return t;
}

static const nc_type *array_type(nc_sema *sm, const nc_type_expr *te, bool *is_const)
{
    bool c;
    const nc_type *elem = nc_sema_value_type(sm, te->u.array.elem, &c, false, "an array element");
    *is_const = *is_const || c; /* `const i32[4]`: the elements, hence the array, are immutable */
    nc_expr *len = te->u.array.len;
    const nc_type *usize = nc_ty_prim(NC_PRIM_USIZE);
    nc_check_value(sm, &len, usize);
    bool ok = nc_coerce(sm, &len, usize, "an array length");
    if (nc_ty_is_error(elem) || !ok || nc_ty_is_error(len->type)) return nc_ty_error();
    if (len->cval.kind != NC_CV_INT) {
        nc_diag_error(NC_DIAG(sm), len->span, "E4089", "an array length must be a constant expression");
        return nc_ty_error();
    }
    if (len->cval.mag == 0) {
        nc_diag_error(NC_DIAG(sm), len->span, "E4089", "an array length must be at least 1");
        return nc_ty_error();
    }
    uint64_t esize = nc_ty_size(elem);
    if (esize && len->cval.mag > NC_MAX_OBJECT_SIZE / esize) {
        nc_diag_error(NC_DIAG(sm), te->span, "E4089", "array type is too large (more than 2^47 bytes)");
        return nc_ty_error();
    }
    return nc_ty_array(sm->s, elem, len->cval.mag);
}

const nc_type *nc_sema_resolve_type(nc_sema *sm, const nc_type_expr *te, bool *is_const)
{
    *is_const = te->is_const;
    const char *what = NULL;
    int phase = 0;
    switch (te->kind) {
    case NC_TE_PRIM: return nc_ty_prim(te->u.prim);
    case NC_TE_ERROR: return nc_ty_error();
    case NC_TE_ARRAY: return array_type(sm, te, is_const);
    case NC_TE_SELF: what = "`Self` is"; phase = 5; break;
    case NC_TE_PTR: {
        /* The pointee need not be complete: `struct Node { Node* next; }`. */
        bool pointee_const;
        const nc_type *inner = nc_sema_resolve_type(sm, te->u.inner, &pointee_const);
        if (nc_ty_is_error(inner)) return inner;
        return nc_ty_ptr(sm->s, inner, pointee_const);
    }
    case NC_TE_REF: what = "reference types are"; phase = 5; break;
    case NC_TE_FN: what = "function pointer types are"; phase = 6; break; /* with optional<> (D-046) */
    case NC_TE_PATH: {
        const nc_path *p = &te->u.path.path;
        const char *first = nc_sema_sym(sm, p->parts[0]);
        if (p->count > 1) {
            what = "types from other modules are";
            phase = 6;
            break;
        }
        if (strcmp(first, "slice") == 0 || strcmp(first, "optional") == 0 || strcmp(first, "result") == 0 ||
            strcmp(first, "str") == 0) {
            nc_diag_error(NC_DIAG(sm), te->span, "E9001", "'%s' is not implemented in this version (Phase 6)", first);
            return nc_ty_error();
        }
        if (strcmp(first, "atomic") == 0) {
            nc_diag_error(NC_DIAG(sm), te->span, "E9001", "'atomic' is not implemented in this version (Phase 8)");
            return nc_ty_error();
        }
        if (strcmp(first, "mmio") == 0) {
            nc_diag_error(NC_DIAG(sm), te->span, "E9001", "'mmio' is not implemented in this version (Phase 7)");
            return nc_ty_error();
        }
        if (te->u.path.nargs) {
            what = "generic types are";
            phase = 6;
            break;
        }
        nc_module_sym *ms = nc_sema_find_module_sym(sm, p->parts[0]);
        if (ms) {
            const nc_type *t = nc_sema_msym_type(sm, ms);
            if (t) return t;
            nc_diag_error(NC_DIAG(sm), te->span, "E3006", "'%s' is not a type", first);
            return nc_ty_error();
        }
        const nc_type *ffi = nc_sema_ffi_type(sm, first);
        if (ffi) return ffi;
        nc_diag_error(NC_DIAG(sm), te->span, "E3001", "unknown type '%s'", first);
        return nc_ty_error();
    }
    }
    nc_diag_error(NC_DIAG(sm), te->span, "E9001", "%s not implemented in this version (Phase %d)", what, phase);
    return nc_ty_error();
}

const nc_type *nc_sema_value_type(nc_sema *sm, const nc_type_expr *te, bool *is_const, bool allow_void, const char *what)
{
    const nc_type *t = nc_sema_resolve_type(sm, te, is_const);
    if (nc_ty_is_error(t)) return t;
    if (nc_ty_is_void(t)) {
        if (allow_void) return t;
        nc_diag_error(NC_DIAG(sm), te->span, "E4005", "%s cannot have type 'void'", what);
        return nc_ty_error();
    }
    return nc_sema_complete(sm, t, te->span) ? t : nc_ty_error();
}

/* ---- aggregates and enums ------------------------------------------------------------ */

static uint64_t align_up(uint64_t n, uint64_t a)
{
    return (n + a - 1) / a * a;
}

static void complete_aggregate(nc_sema *sm, nc_aggregate *agg)
{
    agg->state = NC_LAYOUT_BUSY;
    nc_decl *d = agg->decl;
    unsigned flags = nc_sema_attrs(sm, d->attrs, d->nattrs, agg->is_union ? NC_ATTR_REPR : NC_ATTR_PACKED | NC_ATTR_REPR);
    agg->packed = flags & NC_ATTR_PACKED;
    bool failed = false;
    if (d->u.agg.ngeneric) {
        nc_diag_error(NC_DIAG(sm), d->u.agg.generic_span, "E9001", "generic types are not implemented in this version (Phase 6)");
        failed = true;
    }
    agg->nfields = d->u.agg.nfields;
    agg->fields = nc_arena_alloc(&sm->s->arena, (agg->nfields ? agg->nfields : 1) * sizeof *agg->fields);
    for (uint32_t i = 0; i < agg->nfields; i++) {
        nc_field_decl *fd = &d->u.agg.fields[i];
        nc_field *f = &agg->fields[i];
        f->name = fd->name;
        f->span = fd->span;
        f->is_public = fd->is_public;
        for (uint32_t k = 0; k < i; k++)
            if (agg->fields[k].name == fd->name) {
                nc_diag_error(NC_DIAG(sm), fd->span, "E3002", "duplicate field '%s'", nc_sema_sym(sm, fd->name));
                nc_diag_note(NC_DIAG(sm), agg->fields[k].span, "previous field is here");
            }
        nc_sema_attrs(sm, fd->attrs, fd->nattrs, 0);
        bool c;
        const nc_type *t = nc_sema_resolve_type(sm, fd->type, &c);
        if (!nc_ty_is_error(t) && nc_ty_is_void(t)) {
            nc_diag_error(NC_DIAG(sm), fd->span, "E4005", "field '%s' cannot have type 'void'", nc_sema_sym(sm, fd->name));
            t = nc_ty_error();
        }
        if (!nc_ty_is_error(t) && !nc_sema_complete(sm, t, fd->span)) t = nc_ty_error();
        if (nc_ty_is_error(t)) failed = true;
        f->type = t;
    }
    if (failed) {
        agg->state = NC_LAYOUT_FAILED;
        return;
    }
    uint64_t offset = 0, align = 1;
    for (uint32_t i = 0; i < agg->nfields; i++) {
        nc_field *f = &agg->fields[i];
        uint64_t a = agg->packed ? 1 : nc_ty_align(f->type);
        uint64_t sz = nc_ty_size(f->type);
        if (a > align) align = a;
        if (agg->is_union) {
            f->offset = 0;
            if (sz > offset) offset = sz;
        } else {
            offset = align_up(offset, a);
            f->offset = offset;
            offset += sz;
        }
    }
    if (offset > NC_MAX_OBJECT_SIZE) {
        nc_diag_error(NC_DIAG(sm), agg->span, "E4089", "'%s' is too large (more than 2^47 bytes)", nc_sema_sym(sm, agg->name));
        agg->state = NC_LAYOUT_FAILED;
        return;
    }
    agg->align = align;
    agg->size = align_up(offset, align);
    agg->state = NC_LAYOUT_DONE;
}

static void complete_enum(nc_sema *sm, nc_enum_type *en)
{
    en->state = NC_LAYOUT_BUSY;
    nc_decl *d = en->decl;
    nc_sema_attrs(sm, d->attrs, d->nattrs, NC_ATTR_REPR);
    const nc_type *u = nc_ty_prim(NC_PRIM_I32); /* matches C on every version-1 target */
    if (d->u.en.underlying) {
        bool c;
        u = nc_sema_resolve_type(sm, d->u.en.underlying, &c);
        if (nc_ty_is_error(u)) {
            en->state = NC_LAYOUT_FAILED;
            return;
        }
        if (!nc_ty_is_integer(u)) {
            nc_diag_error(NC_DIAG(sm), d->u.en.underlying->span, "E4090",
                          "the underlying type of an enum must be an integer type, not '%s'", nc_type_name(u));
            en->state = NC_LAYOUT_FAILED;
            return;
        }
    }
    en->underlying = u;
    en->nitems = d->u.en.nitems;
    en->items = nc_arena_alloc(&sm->s->arena, (en->nitems ? en->nitems : 1) * sizeof *en->items);
    nc_cval next = nc_cv_int(false, 0);
    bool next_ok = true;
    bool failed = false;
    for (uint32_t i = 0; i < en->nitems; i++) {
        nc_enumerator_decl *it = &d->u.en.items[i];
        nc_enumerator *e = &en->items[i];
        e->name = it->name;
        e->span = it->span;
        for (uint32_t k = 0; k < i; k++)
            if (en->items[k].name == it->name) {
                nc_diag_error(NC_DIAG(sm), it->span, "E3002", "duplicate enumerator '%s'", nc_sema_sym(sm, it->name));
                nc_diag_note(NC_DIAG(sm), en->items[k].span, "previous enumerator is here");
            }
        if (it->value) {
            nc_expr *v = it->value;
            nc_check_value(sm, &v, u);
            if (!nc_coerce(sm, &v, u, "an enumerator value") || nc_ty_is_error(v->type)) {
                failed = true;
                continue;
            }
            if (v->cval.kind != NC_CV_INT) {
                nc_diag_error(NC_DIAG(sm), v->span, "E4082", "an enumerator value must be a constant expression");
                failed = true;
                continue;
            }
            e->value = v->cval;
        } else {
            if (!next_ok || !nc_cv_fits(next, u)) {
                nc_diag_error(NC_DIAG(sm), it->span, "E4040", "enumerator '%s' would have a value that does not fit '%s'",
                              nc_sema_sym(sm, it->name), nc_type_name(u));
                failed = true;
                continue;
            }
            e->value = next;
        }
        next_ok = nc_cv_add(e->value, nc_cv_int(false, 1), &next);
    }
    en->state = failed ? NC_LAYOUT_FAILED : NC_LAYOUT_DONE;
}

bool nc_sema_complete(nc_sema *sm, const nc_type *t, nc_span span)
{
    if (nc_ty_is_error(t)) return false;
    if (t->kind == NC_TY_ARRAY) return nc_sema_complete(sm, t->elem, span);
    if (t->kind == NC_TY_ENUM) {
        nc_enum_type *en = t->en;
        if (en->state == NC_LAYOUT_NONE) complete_enum(sm, en);
        else if (en->state == NC_LAYOUT_BUSY) {
            nc_diag_error(NC_DIAG(sm), span, "E3005", "enum '%s' is used in its own definition", t->name);
            return false;
        }
        return en->state == NC_LAYOUT_DONE;
    }
    if (t->kind != NC_TY_STRUCT && t->kind != NC_TY_UNION) return true;
    nc_aggregate *agg = t->agg;
    if (agg->opaque) {
        nc_diag_error(NC_DIAG(sm), span, "E4088", "'%s' is an opaque type: its size is unknown, so it cannot be used by value",
                      t->name);
        return false;
    }
    switch (agg->state) {
    case NC_LAYOUT_DONE: return true;
    case NC_LAYOUT_FAILED: return false;
    case NC_LAYOUT_BUSY:
        nc_diag_error(NC_DIAG(sm), span, "E4080", "'%s' contains itself by value, so its size would be infinite", t->name);
        nc_diag_note(NC_DIAG(sm), agg->span, "'%s' is declared here", t->name);
        agg->state = NC_LAYOUT_FAILED;
        return false;
    case NC_LAYOUT_NONE:
        complete_aggregate(sm, agg);
        return agg->state == NC_LAYOUT_DONE;
    }
    return false;
}

/* ---- constexpr and static ------------------------------------------------------------- */

nc_const_info *nc_sema_const(nc_sema *sm, nc_module_sym *ms)
{
    nc_const_info *c = ms->info;
    if (c->state == NC_LAYOUT_DONE || c->state == NC_LAYOUT_FAILED) return c;
    nc_decl *d = c->decl;
    if (c->state == NC_LAYOUT_BUSY) {
        nc_diag_error(NC_DIAG(sm), d->u.var.name_span, "E3005", "constant '%s' is defined in terms of itself",
                      nc_sema_sym(sm, d->u.var.name));
        c->state = NC_LAYOUT_FAILED;
        return c;
    }
    c->state = NC_LAYOUT_BUSY;
    nc_sema_attrs(sm, d->attrs, d->nattrs, 0);
    bool k;
    const nc_type *t = nc_sema_resolve_type(sm, d->u.var.type, &k);
    if (!nc_ty_is_error(t) && !nc_ty_is_integer(t) && !nc_ty_is_float(t) && !nc_ty_is_bool(t) && !nc_ty_is_enum(t)) {
        nc_diag_error(NC_DIAG(sm), d->u.var.type->span, "E9001",
                      "constexpr values of type '%s' are not implemented in this version (Phase 6)", nc_type_name(t));
        t = nc_ty_error();
    }
    if (nc_ty_is_error(t) || (nc_ty_is_enum(t) && !nc_sema_complete(sm, t, d->u.var.type->span))) {
        c->state = NC_LAYOUT_FAILED;
        return c;
    }
    char what[96];
    snprintf(what, sizeof what, "the value of '%s'", nc_sema_sym(sm, d->u.var.name));
    nc_expr **slot = &d->u.var.init->expr;
    nc_check_value(sm, slot, t);
    if (c->state == NC_LAYOUT_FAILED) return c; /* a cycle was reported while checking */
    if (!nc_coerce(sm, slot, t, what) || nc_ty_is_error((*slot)->type)) {
        c->state = NC_LAYOUT_FAILED;
        return c;
    }
    if ((*slot)->cval.kind == NC_CV_NONE) {
        nc_diag_error(NC_DIAG(sm), (*slot)->span, "E4082", "the value of a constexpr must be a constant expression");
        c->state = NC_LAYOUT_FAILED;
        return c;
    }
    c->type = t;
    c->value = (*slot)->cval;
    c->state = NC_LAYOUT_DONE;
    return c;
}

nc_static_info *nc_sema_static(nc_sema *sm, nc_module_sym *ms)
{
    nc_static_info *st = ms->info;
    if (st->state != NC_LAYOUT_NONE) return st;
    st->state = NC_LAYOUT_BUSY;
    nc_decl *d = st->decl;
    if (nc_sema_attrs(sm, d->attrs, d->nattrs, NC_ATTR_EXPORT) & NC_ATTR_EXPORT)
        st->export_symbol = nc_sema_attr_string(sm, d->attrs, d->nattrs, "export");
    const nc_type *t = nc_sema_value_type(sm, d->u.var.type, &st->is_const, false, "a static");
    st->type = t;
    if (nc_ty_is_error(t)) {
        st->state = NC_LAYOUT_FAILED;
        return st;
    }
    if (!d->u.var.init) {
        nc_diag_error(NC_DIAG(sm), d->u.var.name_span, "E4086", "static '%s' needs an initialiser",
                      nc_sema_sym(sm, st->name));
        st->state = NC_LAYOUT_FAILED;
        return st;
    }
    char what[96];
    snprintf(what, sizeof what, "the initialiser of '%s'", nc_sema_sym(sm, st->name));
    if (!nc_sema_check_init(sm, d->u.var.init, t, what)) {
        st->state = NC_LAYOUT_FAILED;
        return st;
    }
    if (!nc_is_constant_expr(d->u.var.init->expr)) {
        nc_diag_error(NC_DIAG(sm), d->u.var.init->span, "E4082", "the initialiser of a static must be a constant expression");
        st->state = NC_LAYOUT_FAILED;
        return st;
    }
    st->init = d->u.var.init->expr;
    st->state = NC_LAYOUT_DONE;
    return st;
}

const nc_type *nc_sema_typedef(nc_sema *sm, nc_module_sym *ms)
{
    return resolve_typedef(sm, ms->info);
}

const nc_type *nc_sema_msym_type(nc_sema *sm, nc_module_sym *ms)
{
    switch (ms->kind) {
    case NC_MSYM_AGGREGATE: return ((nc_aggregate *)ms->info)->type;
    case NC_MSYM_ENUM: return ((nc_enum_type *)ms->info)->type;
    case NC_MSYM_TYPEDEF: return resolve_typedef(sm, ms->info);
    default: return NULL;
    }
}

/* ---- whole-module resolution ------------------------------------------------------------ */

void nc_sema_resolve_decls(nc_sema *sm)
{
    for (size_t i = 0; i < sm->msyms.len; i++) {
        nc_module_sym *ms = &sm->msyms.data[i];
        switch (ms->kind) {
        case NC_MSYM_FN: break;
        case NC_MSYM_AGGREGATE: {
            nc_aggregate *agg = ms->info;
            if (agg->opaque) nc_sema_attrs(sm, agg->decl->attrs, agg->decl->nattrs, 0);
            else nc_sema_complete(sm, agg->type, agg->span);
            break;
        }
        case NC_MSYM_ENUM: nc_sema_complete(sm, ((nc_enum_type *)ms->info)->type, ((nc_enum_type *)ms->info)->span); break;
        case NC_MSYM_TYPEDEF: resolve_typedef(sm, ms->info); break;
        case NC_MSYM_STATIC: nc_sema_static(sm, ms); break;
        case NC_MSYM_CONST: nc_sema_const(sm, ms); break;
        }
    }
}
