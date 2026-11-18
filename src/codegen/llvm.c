/* NIR -> LLVM IR, optimisation and emission (NOVAC_COMPILER_ARCHITECTURE.md
 * §9.4, §10).
 *
 * Every NIR local becomes an `alloca` in the entry block; LLVM's mem2reg
 * promotes them at -O1 and above. Integer arithmetic that NovaC checks uses
 * the llvm.*.with.overflow intrinsics; failed asserts branch to a call of
 * the noreturn `__nova_panic` with a static description of the site
 * (NOVAC_ABI.md §9.1). All functions are `nounwind` (D-015). */
#define _POSIX_C_SOURCE 200809L /* strdup, basename, dirname */
#include "codegen/codegen.h"

#include "sema/consteval.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <libgen.h>
#include <stdlib.h>
#include <string.h>

typedef struct cg {
    nc_session *s;
    const nc_nir_module *m;
    const nc_codegen_options *o;
    LLVMContextRef ctx;
    LLVMModuleRef mod;
    LLVMBuilderRef b;
    LLVMTypeRef ptr_ty, i32_ty, i64_ty;
    LLVMValueRef *fn_vals;
    LLVMTypeRef *fn_tys;
    LLVMValueRef panic_fn;
    LLVMTypeRef panic_ty, info_ty;
    LLVMValueRef file_str; /* source file name for panic info */
    size_t file_len;
    /* debug info */
    LLVMDIBuilderRef di;
    LLVMMetadataRef di_file, di_cu, di_scope;
    /* current function */
    const nc_nir_fn *f;
    LLVMValueRef fv;
    LLVMValueRef *allocas;
    LLVMBasicBlockRef *bbs;
    LLVMBuilderRef entry_b; /* adds allocas to the entry block, so loops do not grow the stack */
    /* aggregates and statics */
    LLVMTargetDataRef td;
    const nc_aggregate **agg_keys;
    LLVMTypeRef *agg_types;
    uint32_t naggs, cap_aggs;
    LLVMValueRef *static_vals;
} cg;

void nc_codegen_init(void)
{
    static bool done; /* process-wide LLVM state; initialisation is idempotent */
    if (done) return;
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmPrinters();
    LLVMInitializeAllAsmParsers();
    done = true;
}

void nc_codegen_llvm_version(char *buf, size_t n)
{
    unsigned major, minor, patch;
    LLVMGetVersion(&major, &minor, &patch);
    snprintf(buf, n, "%u.%u.%u", major, minor, patch);
}

/* ---- types and names --------------------------------------------------------------- */

static LLVMTypeRef llvm_type(cg *g, const nc_type *t);

/* A struct's or union's named LLVM type. A union is `{ [size/align x iA] }`
 * with A its alignment in bits, so LLVM gives it the computed size and
 * alignment; fields are accessed through the union's address. The layout
 * LLVM computes must equal the one NovaC computed (NOVAC_ABI.md §4). */
static LLVMTypeRef aggregate_type(cg *g, const nc_aggregate *agg)
{
    for (uint32_t i = 0; i < g->naggs; i++)
        if (g->agg_keys[i] == agg) return g->agg_types[i];
    char name[300];
    snprintf(name, sizeof name, "%s.%s", agg->is_union ? "union" : "struct", nc_sym_str(&g->s->strtab, agg->name));
    LLVMTypeRef ty = LLVMStructCreateNamed(g->ctx, name);
    if (g->naggs == g->cap_aggs) {
        g->cap_aggs = g->cap_aggs ? g->cap_aggs * 2 : 8;
        g->agg_keys = nc_xrealloc(g->agg_keys, g->cap_aggs * sizeof *g->agg_keys);
        g->agg_types = nc_xrealloc(g->agg_types, g->cap_aggs * sizeof *g->agg_types);
    }
    g->agg_keys[g->naggs] = agg;
    g->agg_types[g->naggs++] = ty;
    if (agg->is_union) {
        LLVMTypeRef unit = LLVMIntTypeInContext(g->ctx, (unsigned)(agg->align * 8));
        LLVMTypeRef body = LLVMArrayType2(unit, agg->size / agg->align);
        LLVMStructSetBody(ty, &body, 1, 0);
    } else {
        LLVMTypeRef *fields = nc_xcalloc(agg->nfields ? agg->nfields : 1, sizeof *fields);
        for (uint32_t i = 0; i < agg->nfields; i++) fields[i] = llvm_type(g, agg->fields[i].type);
        LLVMStructSetBody(ty, fields, agg->nfields, agg->packed);
        free(fields);
    }
    bool ok = LLVMABISizeOfType(g->td, ty) == agg->size && LLVMABIAlignmentOfType(g->td, ty) == agg->align;
    for (uint32_t i = 0; ok && !agg->is_union && i < agg->nfields; i++)
        ok = LLVMOffsetOfElement(g->td, ty, i) == agg->fields[i].offset;
    if (!ok) nc_ice("codegen", "the LLVM layout of an aggregate differs from the computed layout");
    return ty;
}

static LLVMTypeRef llvm_type(cg *g, const nc_type *t)
{
    switch (t->kind) {
    case NC_TY_ARRAY: return LLVMArrayType2(llvm_type(g, t->elem), t->len);
    case NC_TY_STRUCT:
    case NC_TY_UNION: return aggregate_type(g, t->agg);
    case NC_TY_ENUM: return llvm_type(g, t->en->underlying);
    case NC_TY_PTR: return g->ptr_ty;
    case NC_TY_PRIM: break;
    default: nc_ice("codegen", "unexpected type");
    }
    switch (t->prim) {
    case NC_PRIM_I8: case NC_PRIM_U8: return LLVMInt8TypeInContext(g->ctx);
    case NC_PRIM_I16: case NC_PRIM_U16: return LLVMInt16TypeInContext(g->ctx);
    case NC_PRIM_I32: case NC_PRIM_U32: return LLVMInt32TypeInContext(g->ctx);
    case NC_PRIM_I64: case NC_PRIM_U64: case NC_PRIM_ISIZE: case NC_PRIM_USIZE: return LLVMInt64TypeInContext(g->ctx);
    case NC_PRIM_F32: return LLVMFloatTypeInContext(g->ctx);
    case NC_PRIM_F64: return LLVMDoubleTypeInContext(g->ctx);
    case NC_PRIM_BOOL: return LLVMInt1TypeInContext(g->ctx);
    case NC_PRIM_VOID: return LLVMVoidTypeInContext(g->ctx);
    default: nc_ice("codegen", "unexpected type");
    }
}

/* NOVAC_ABI.md §7: _NC <module path> M <item>, each component length-prefixed. */
static void mangle(cg *g, nc_sym name, char *buf, size_t n)
{
    size_t k = (size_t)snprintf(buf, n, "_NC");
    for (uint32_t i = 0; i < g->m->name.count && k < n; i++) {
        const char *p = nc_sym_str(&g->s->strtab, g->m->name.parts[i]);
        k += (size_t)snprintf(buf + k, n - k, "%zu%s", strlen(p), p);
    }
    if (k < n) {
        const char *p = nc_sym_str(&g->s->strtab, name);
        snprintf(buf + k, n - k, "M%zu%s", strlen(p), p);
    }
}

static void add_attr(cg *g, LLVMValueRef fn, const char *name)
{
    unsigned kind = LLVMGetEnumAttributeKindForName(name, strlen(name));
    LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, kind, 0));
}

static void common_fn_attrs(cg *g, LLVMValueRef fn)
{
    add_attr(g, fn, "nounwind"); /* panics never unwind (D-015) */
    if (strncmp(g->o->target->name, "aarch64", 7) == 0) {
        /* Keep AArch64 frame records for interoperable stack walking (NOVAC_ABI.md §3). */
        LLVMAttributeRef a = LLVMCreateStringAttribute(g->ctx, "frame-pointer", 13, "non-leaf", 8);
        LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, a);
    }
}

/* ---- constants and panic support ---------------------------------------------------- */

static LLVMValueRef global_string(cg *g, const char *s, size_t len, const char *name)
{
    LLVMValueRef init = LLVMConstStringInContext(g->ctx, s, (unsigned)len, 0); /* NUL-terminated */
    LLVMValueRef gv = LLVMAddGlobal(g->mod, LLVMTypeOf(init), name);
    LLVMSetInitializer(gv, init);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetUnnamedAddress(gv, LLVMGlobalUnnamedAddr);
    return gv;
}

/* Hosted default panic handler (D-039): weak, so a program may replace it.
 *   void __nova_panic(const __nova_panic_info *info) {
 *       dprintf(2, "%.*s:%u:%u: panic: %.*s\n", file_len, file, line, column, msg_len, msg);
 *       abort();
 *   } */
static void define_default_panic(cg *g)
{
    LLVMValueRef fn = g->panic_fn;
    LLVMSetLinkage(fn, LLVMWeakAnyLinkage);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->b, bb);
    LLVMSetCurrentDebugLocation2(g->b, NULL);
    LLVMValueRef info = LLVMGetParam(fn, 0);
    LLVMValueRef field[6];
    LLVMTypeRef ftypes[6] = {g->ptr_ty, g->i64_ty, g->ptr_ty, g->i64_ty, g->i32_ty, g->i32_ty};
    for (unsigned i = 0; i < 6; i++) {
        LLVMValueRef p = LLVMBuildStructGEP2(g->b, g->info_ty, info, i, "");
        field[i] = LLVMBuildLoad2(g->b, ftypes[i], p, "");
    }
    LLVMTypeRef dprintf_params[2] = {g->i32_ty, g->ptr_ty};
    LLVMTypeRef dprintf_ty = LLVMFunctionType(g->i32_ty, dprintf_params, 2, 1);
    LLVMValueRef dprintf_fn = LLVMGetNamedFunction(g->mod, "dprintf");
    if (!dprintf_fn) dprintf_fn = LLVMAddFunction(g->mod, "dprintf", dprintf_ty);
    LLVMTypeRef abort_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    LLVMValueRef abort_fn = LLVMGetNamedFunction(g->mod, "abort");
    if (!abort_fn) abort_fn = LLVMAddFunction(g->mod, "abort", abort_ty);
    add_attr(g, abort_fn, "noreturn");

    const char *fmt = "%.*s:%u:%u: panic: %.*s\n";
    LLVMValueRef fmt_g = global_string(g, fmt, strlen(fmt), "nova.panic.fmt");
    LLVMValueRef args[8] = {
        LLVMConstInt(g->i32_ty, 2, 0),
        fmt_g,
        LLVMBuildTrunc(g->b, field[3], g->i32_ty, ""), field[2], /* file */
        field[4], field[5],                                     /* line, column */
        LLVMBuildTrunc(g->b, field[1], g->i32_ty, ""), field[0], /* message */
    };
    LLVMBuildCall2(g->b, dprintf_ty, dprintf_fn, args, 8, "");
    LLVMBuildCall2(g->b, abort_ty, abort_fn, NULL, 0, "");
    LLVMBuildUnreachable(g->b);
}

static void declare_panic(cg *g)
{
    LLVMTypeRef fields[6] = {g->ptr_ty, g->i64_ty, g->ptr_ty, g->i64_ty, g->i32_ty, g->i32_ty};
    g->info_ty = LLVMStructTypeInContext(g->ctx, fields, 6, 0);
    g->panic_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &g->ptr_ty, 1, 0);
    g->panic_fn = LLVMAddFunction(g->mod, "__nova_panic", g->panic_ty);
    add_attr(g, g->panic_fn, "noreturn");
    add_attr(g, g->panic_fn, "cold");
    add_attr(g, g->panic_fn, "nounwind");
    define_default_panic(g); /* every Phase 3 target is hosted */

    const char *file = nc_source_get(&g->s->sources, g->m->file)->name;
    g->file_len = strlen(file);
    g->file_str = global_string(g, file, g->file_len, "nova.panic.file");
}

/* The static description of one panic site (NOVAC_ABI.md §9.1). */
static LLVMValueRef panic_info(cg *g, const char *msg, nc_span span)
{
    uint32_t line = 0, col = 0;
    nc_source_line_col(&g->s->sources, span.file, span.lo, &line, &col);
    LLVMValueRef null = LLVMConstNull(g->ptr_ty);
    LLVMValueRef vals[6];
    if (g->o->panic_minimal) {
        vals[0] = null;
        vals[1] = LLVMConstInt(g->i64_ty, 0, 0);
        vals[2] = null;
        vals[3] = LLVMConstInt(g->i64_ty, 0, 0);
    } else {
        size_t len = strlen(msg);
        vals[0] = global_string(g, msg, len, "nova.panic.msg");
        vals[1] = LLVMConstInt(g->i64_ty, len, 0);
        vals[2] = g->file_str;
        vals[3] = LLVMConstInt(g->i64_ty, g->file_len, 0);
    }
    vals[4] = LLVMConstInt(g->i32_ty, line, 0);
    vals[5] = LLVMConstInt(g->i32_ty, col, 0);
    LLVMValueRef init = LLVMConstNamedStruct(g->info_ty, vals, 6);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, g->info_ty, "nova.panic.site");
    LLVMSetInitializer(gv, init);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    return gv;
}

/* ---- debug info -------------------------------------------------------------------- */

static void set_loc(cg *g, nc_span span)
{
    if (!g->di || !g->di_scope) return;
    uint32_t line = 0, col = 0;
    if (span.file != NC_NO_FILE) nc_source_line_col(&g->s->sources, span.file, span.lo, &line, &col);
    LLVMMetadataRef loc = LLVMDIBuilderCreateDebugLocation(g->ctx, line, col, g->di_scope, NULL);
    LLVMSetCurrentDebugLocation2(g->b, loc);
}

static void init_debug(cg *g)
{
    const char *path = nc_source_get(&g->s->sources, g->m->file)->name;
    char *a = strdup(path), *b2 = strdup(path);
    if (!a || !b2) nc_ice("codegen", "out of memory");
    const char *base = basename(a), *dir = dirname(b2);
    g->di = LLVMCreateDIBuilder(g->mod);
    g->di_file = LLVMDIBuilderCreateFile(g->di, base, strlen(base), dir, strlen(dir));
    const char *producer = "ncc (NovaC)";
    /* Until DWARF has a language code for NovaC, C11 lets debuggers show values (D-035). */
    g->di_cu = LLVMDIBuilderCreateCompileUnit(g->di, LLVMDWARFSourceLanguageC11, g->di_file, producer, strlen(producer),
                                              g->o->opt != '0', "", 0, 0, "", 0, LLVMDWARFEmissionLineTablesOnly, 0, 0, 0,
                                              "", 0, "", 0);
    LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorWarning, "Dwarf Version", 13,
                      LLVMValueAsMetadata(LLVMConstInt(g->i32_ty, 5, 0)));
    LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorWarning, "Debug Info Version", 18,
                      LLVMValueAsMetadata(LLVMConstInt(g->i32_ty, LLVMDebugMetadataVersion(), 0)));
    free(a);
    free(b2);
}

/* ---- function bodies ---------------------------------------------------------------- */

static LLVMValueRef const_value(cg *g, nc_cval v, const nc_type *t)
{
    LLVMTypeRef ty = llvm_type(g, t);
    if (nc_ty_is_bool(t)) return LLVMConstInt(ty, v.kind == NC_CV_BOOL ? v.b : v.mag != 0, 0);
    if (nc_ty_is_float(t)) return LLVMConstReal(ty, nc_cv_to_double(v));
    uint64_t bits = v.neg ? (uint64_t)0 - v.mag : v.mag;
    return LLVMConstInt(ty, bits, 0);
}

/* The address of a place (NIR projections become GEPs). Sets *misaligned
 * if the place lies inside a @packed struct, where its natural alignment
 * cannot be assumed. */
static LLVMValueRef place_addr(cg *g, const nc_place *p, bool *misaligned)
{
    *misaligned = false;
    LLVMValueRef addr = p->base == NC_PLACE_LOCAL ? g->allocas[p->id] : g->static_vals[p->id];
    const nc_type *t = nc_nir_place_base_type(g->m, g->f, p);
    for (uint32_t i = 0; i < p->nprojs; i++) {
        const nc_proj *pr = &p->projs[i];
        if (pr->kind == NC_PROJ_FIELD) {
            if (nc_ty_is_struct(t) && t->agg->packed) *misaligned = true;
            if (nc_ty_is_struct(t)) addr = LLVMBuildStructGEP2(g->b, llvm_type(g, t), addr, pr->field, "");
            /* a union's fields all start at its address */
        } else {
            LLVMValueRef idx[2] = {LLVMConstInt(g->i64_ty, 0, 0),
                                   pr->index_is_const ? LLVMConstInt(g->i64_ty, pr->index, 0)
                                                      : LLVMBuildLoad2(g->b, g->i64_ty, g->allocas[pr->index_local], "")};
            /* inbounds: the index was checked against the length */
            addr = LLVMBuildInBoundsGEP2(g->b, llvm_type(g, t), addr, idx, 2, "");
        }
        t = pr->type;
    }
    return addr;
}

static LLVMValueRef operand(cg *g, const nc_operand *o)
{
    switch (o->kind) {
    case NC_OP_CONST: return const_value(g, o->cval, o->type);
    case NC_OP_ZERO: return LLVMConstNull(llvm_type(g, o->type));
    case NC_OP_COPY: break;
    }
    bool misaligned;
    LLVMValueRef addr = place_addr(g, &o->place, &misaligned);
    LLVMValueRef v = LLVMBuildLoad2(g->b, llvm_type(g, o->type), addr, "");
    if (misaligned) LLVMSetAlignment(v, 1);
    return v;
}

/* NovaC's internal calling convention passes struct, union and array
 * values indirectly (NOVAC_ABI.md §6): an argument is the address of the
 * value, which the callee copies on entry, and a result is written through
 * a hidden first parameter. First-class aggregate arguments would make
 * LLVM expand large arrays element by element. */
static bool indirect(const nc_type *t)
{
    return nc_ty_is_aggregate(t);
}

/* The address of operand `o` (an aggregate): its place, or a temporary. */
static LLVMValueRef operand_addr(cg *g, const nc_operand *o);

/* Store operand `o` of type t at addr. Aggregates are copied with memcpy
 * and zeroed with memset: LLVM expands first-class aggregate loads and
 * stores element by element, which is slow for large arrays. */
static void store_operand(cg *g, const nc_operand *o, LLVMValueRef addr, bool misaligned, const nc_type *t)
{
    if (nc_ty_is_aggregate(t) && o->kind != NC_OP_CONST) {
        unsigned align = misaligned ? 1 : (unsigned)nc_ty_align(t);
        LLVMValueRef size = LLVMConstInt(g->i64_ty, nc_ty_size(t), 0);
        if (o->kind == NC_OP_ZERO) {
            LLVMBuildMemSet(g->b, addr, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), size, align);
        } else {
            /* Places of one type are equal or disjoint, as llvm.memcpy requires. */
            bool src_misaligned;
            LLVMValueRef src = place_addr(g, &o->place, &src_misaligned);
            LLVMBuildMemCpy(g->b, addr, align, src, src_misaligned ? 1 : (unsigned)nc_ty_align(t), size);
        }
        return;
    }
    LLVMValueRef st = LLVMBuildStore(g->b, operand(g, o), addr);
    if (misaligned) LLVMSetAlignment(st, 1);
}

static LLVMValueRef operand_addr(cg *g, const nc_operand *o)
{
    if (o->kind == NC_OP_COPY) {
        bool misaligned;
        return place_addr(g, &o->place, &misaligned);
    }
    LLVMValueRef tmp = LLVMBuildAlloca(g->entry_b, llvm_type(g, o->type), "");
    store_operand(g, o, tmp, false, o->type);
    return tmp;
}

/* An aggregate rvalue: zero the destination, then store each member that
 * is not zero. */
static void store_aggregate(cg *g, const nc_rvalue *rv, LLVMValueRef addr, bool misaligned, const nc_type *t)
{
    nc_operand zero = {0};
    zero.kind = NC_OP_ZERO;
    zero.type = t;
    store_operand(g, &zero, addr, misaligned, t);
    for (uint32_t i = 0; i < rv->nelems; i++) {
        const nc_operand *m = &rv->elems[i];
        if (m->kind == NC_OP_ZERO) continue;
        LLVMValueRef ma = addr;
        bool mm = misaligned;
        if (nc_ty_is_array(t)) {
            LLVMValueRef idx[2] = {LLVMConstInt(g->i64_ty, 0, 0), LLVMConstInt(g->i64_ty, i, 0)};
            ma = LLVMBuildInBoundsGEP2(g->b, llvm_type(g, t), addr, idx, 2, "");
        } else if (nc_ty_is_struct(t)) {
            ma = LLVMBuildStructGEP2(g->b, llvm_type(g, t), addr, i, "");
            mm = mm || t->agg->packed;
        }
        store_operand(g, m, ma, mm, m->type);
    }
}

/* -Ofast (D-040): flags that may change floating-point results but never
 * create poison. `nnan` and `ninf` are excluded: under them an operation
 * producing NaN or infinity yields poison, and a check that branches on it
 * is undefined behaviour, which safe code must never reach (D-016). */
static LLVMValueRef fast(cg *g, LLVMValueRef v)
{
    if (g->o->fast_math && LLVMCanValueUseFastMathFlags(v))
        LLVMSetFastMathFlags(v, LLVMFastMathAllowReassoc | LLVMFastMathNoSignedZeros | LLVMFastMathAllowReciprocal |
                                    LLVMFastMathAllowContract | LLVMFastMathApproxFunc);
    return v;
}

static LLVMValueRef binary(cg *g, nc_nir_binop op, const nc_operand *oa, const nc_operand *ob)
{
    LLVMValueRef a = operand(g, oa), b = operand(g, ob);
    const nc_type *t = oa->type;
    bool flt = nc_ty_is_float(t), sgn = nc_ty_is_signed(t);
    LLVMBuilderRef B = g->b;
    if (flt) {
        switch (op) {
        case NC_NB_ADD: return fast(g, LLVMBuildFAdd(B, a, b, ""));
        case NC_NB_SUB: return fast(g, LLVMBuildFSub(B, a, b, ""));
        case NC_NB_MUL: return fast(g, LLVMBuildFMul(B, a, b, ""));
        case NC_NB_DIV: return fast(g, LLVMBuildFDiv(B, a, b, ""));
        /* Ordered comparisons are false when either operand is NaN; `!=` is true. */
        case NC_NB_EQ: return fast(g, LLVMBuildFCmp(B, LLVMRealOEQ, a, b, ""));
        case NC_NB_NE: return fast(g, LLVMBuildFCmp(B, LLVMRealUNE, a, b, ""));
        case NC_NB_LT: return fast(g, LLVMBuildFCmp(B, LLVMRealOLT, a, b, ""));
        case NC_NB_LE: return fast(g, LLVMBuildFCmp(B, LLVMRealOLE, a, b, ""));
        case NC_NB_GT: return fast(g, LLVMBuildFCmp(B, LLVMRealOGT, a, b, ""));
        case NC_NB_GE: return fast(g, LLVMBuildFCmp(B, LLVMRealOGE, a, b, ""));
        default: nc_ice("codegen", "invalid floating-point operation");
        }
    }
    switch (op) {
    case NC_NB_ADD: return LLVMBuildAdd(B, a, b, "");
    case NC_NB_SUB: return LLVMBuildSub(B, a, b, "");
    case NC_NB_MUL: return LLVMBuildMul(B, a, b, "");
    case NC_NB_DIV: return sgn ? LLVMBuildSDiv(B, a, b, "") : LLVMBuildUDiv(B, a, b, "");
    case NC_NB_REM: return sgn ? LLVMBuildSRem(B, a, b, "") : LLVMBuildURem(B, a, b, "");
    case NC_NB_SHL: return LLVMBuildShl(B, a, b, "");
    case NC_NB_SHR: return sgn ? LLVMBuildAShr(B, a, b, "") : LLVMBuildLShr(B, a, b, "");
    case NC_NB_AND: return LLVMBuildAnd(B, a, b, "");
    case NC_NB_OR: return LLVMBuildOr(B, a, b, "");
    case NC_NB_XOR: return LLVMBuildXor(B, a, b, "");
    case NC_NB_EQ: return LLVMBuildICmp(B, LLVMIntEQ, a, b, "");
    case NC_NB_NE: return LLVMBuildICmp(B, LLVMIntNE, a, b, "");
    case NC_NB_LT: return LLVMBuildICmp(B, sgn ? LLVMIntSLT : LLVMIntULT, a, b, "");
    case NC_NB_LE: return LLVMBuildICmp(B, sgn ? LLVMIntSLE : LLVMIntULE, a, b, "");
    case NC_NB_GT: return LLVMBuildICmp(B, sgn ? LLVMIntSGT : LLVMIntUGT, a, b, "");
    case NC_NB_GE: return LLVMBuildICmp(B, sgn ? LLVMIntSGE : LLVMIntUGE, a, b, "");
    }
    nc_ice("codegen", "invalid integer operation");
}

static LLVMValueRef cast(cg *g, nc_cast_kind k, const nc_operand *oa, const nc_type *to)
{
    LLVMValueRef a = operand(g, oa);
    const nc_type *from = oa->type;
    LLVMTypeRef ty = llvm_type(g, to);
    switch (k) {
    case NC_CAST_INT_RESIZE: {
        unsigned fb = nc_ty_bits(from), tb = nc_ty_bits(to);
        if (fb == tb) return a;
        if (tb < fb) return LLVMBuildTrunc(g->b, a, ty, "");
        return nc_ty_is_signed(from) ? LLVMBuildSExt(g->b, a, ty, "") : LLVMBuildZExt(g->b, a, ty, "");
    }
    case NC_CAST_INT_TO_FLOAT:
        return nc_ty_is_signed(from) ? LLVMBuildSIToFP(g->b, a, ty, "") : LLVMBuildUIToFP(g->b, a, ty, "");
    case NC_CAST_FLOAT_TO_INT: /* the range was checked: no poison */
        return nc_ty_is_signed(to) ? LLVMBuildFPToSI(g->b, a, ty, "") : LLVMBuildFPToUI(g->b, a, ty, "");
    case NC_CAST_FLOAT_RESIZE:
        if (from == to) return a;
        return to->prim == NC_PRIM_F64 ? LLVMBuildFPExt(g->b, a, ty, "") : LLVMBuildFPTrunc(g->b, a, ty, "");
    case NC_CAST_BOOL_TO_INT: return LLVMBuildZExt(g->b, a, ty, "");
    case NC_CAST_NOP: return a;
    }
    nc_ice("codegen", "invalid cast");
}

static LLVMValueRef overflow_intrinsic(cg *g, nc_nir_binop op, const nc_type *t, LLVMTypeRef *fty)
{
    const char *name = nc_ty_is_signed(t)
                           ? (op == NC_NB_ADD ? "llvm.sadd.with.overflow" : op == NC_NB_SUB ? "llvm.ssub.with.overflow" : "llvm.smul.with.overflow")
                           : (op == NC_NB_ADD ? "llvm.uadd.with.overflow" : op == NC_NB_SUB ? "llvm.usub.with.overflow" : "llvm.umul.with.overflow");
    unsigned id = LLVMLookupIntrinsicID(name, strlen(name));
    LLVMTypeRef it = llvm_type(g, t);
    *fty = LLVMIntrinsicGetType(g->ctx, id, &it, 1);
    return LLVMGetIntrinsicDeclaration(g->mod, id, &it, 1);
}

static void gen_stmt(cg *g, const nc_nir_stmt *st)
{
    set_loc(g, st->span);
    const nc_type *dt = st->dest.type;
    LLVMValueRef v;
    if (st->kind == NC_ST_CHECKED) {
        LLVMTypeRef fty;
        LLVMValueRef fn = overflow_intrinsic(g, st->op, dt, &fty);
        LLVMValueRef args[2] = {operand(g, &st->rv.a), operand(g, &st->rv.b)};
        LLVMValueRef pair = LLVMBuildCall2(g->b, fty, fn, args, 2, "");
        LLVMBuildStore(g->b, LLVMBuildExtractValue(g->b, pair, 1, ""), g->allocas[st->flag]);
        v = LLVMBuildExtractValue(g->b, pair, 0, "");
    } else {
        switch (st->rv.kind) {
        case NC_RV_USE:
            if (nc_ty_is_aggregate(dt)) {
                bool misaligned;
                LLVMValueRef addr = place_addr(g, &st->dest, &misaligned);
                store_operand(g, &st->rv.a, addr, misaligned, dt);
                return;
            }
            v = operand(g, &st->rv.a);
            break;
        case NC_RV_BINARY: v = binary(g, st->rv.binop, &st->rv.a, &st->rv.b); break;
        case NC_RV_UNARY: {
            LLVMValueRef a = operand(g, &st->rv.a);
            if (st->rv.unop == NC_NU_NEG_FLOAT) v = fast(g, LLVMBuildFNeg(g->b, a, ""));
            else v = LLVMBuildXor(g->b, a, LLVMConstAllOnes(LLVMTypeOf(a)), ""); /* ! on i1, ~ on iN */
            break;
        }
        case NC_RV_CAST: v = cast(g, st->rv.cast, &st->rv.a, dt); break;
        case NC_RV_AGGREGATE: {
            bool misaligned;
            LLVMValueRef addr = place_addr(g, &st->dest, &misaligned);
            store_aggregate(g, &st->rv, addr, misaligned, dt);
            return;
        }
        default: nc_ice("codegen", "invalid rvalue");
        }
    }
    bool misaligned;
    LLVMValueRef st_i = LLVMBuildStore(g->b, v, place_addr(g, &st->dest, &misaligned));
    if (misaligned) LLVMSetAlignment(st_i, 1);
}

static void gen_term(cg *g, const nc_terminator *t)
{
    set_loc(g, t->span);
    LLVMBuilderRef B = g->b;
    switch (t->kind) {
    case NC_TERM_GOTO: LLVMBuildBr(B, g->bbs[t->target]); break;
    case NC_TERM_BRANCH: LLVMBuildCondBr(B, operand(g, &t->cond), g->bbs[t->target], g->bbs[t->other]); break;
    case NC_TERM_RETURN:
        if (t->has_value && indirect(t->value.type)) {
            store_operand(g, &t->value, LLVMGetParam(g->fv, 0), false, t->value.type);
            LLVMBuildRetVoid(B);
        } else if (t->has_value) {
            LLVMBuildRet(B, operand(g, &t->value));
        } else {
            LLVMBuildRetVoid(B);
        }
        break;
    case NC_TERM_UNREACHABLE: LLVMBuildUnreachable(B); break;
    case NC_TERM_SWITCH: {
        LLVMValueRef sw = LLVMBuildSwitch(B, operand(g, &t->cond), g->bbs[t->other], t->ncases);
        for (uint32_t c = 0; c < t->ncases; c++)
            LLVMAddCase(sw, const_value(g, t->case_values[c], t->cond.type), g->bbs[t->case_targets[c]]);
        break;
    }
    case NC_TERM_CALL: {
        uint32_t idx = 0;
        while (idx < g->m->nfns && g->m->fns[idx] != t->callee) idx++;
        bool sret = indirect(t->callee->ret);
        uint32_t n = t->nargs + sret;
        LLVMValueRef *args = nc_xcalloc(n ? n : 1, sizeof *args);
        if (sret) args[0] = g->allocas[t->dest];
        for (uint32_t i = 0; i < t->nargs; i++)
            args[i + sret] = indirect(t->args[i].type) ? operand_addr(g, &t->args[i]) : operand(g, &t->args[i]);
        LLVMValueRef r = LLVMBuildCall2(B, g->fn_tys[idx], g->fn_vals[idx], args, n, "");
        free(args);
        if (t->dest != UINT32_MAX && !sret) LLVMBuildStore(B, r, g->allocas[t->dest]);
        LLVMBuildBr(B, g->bbs[t->target]);
        break;
    }
    case NC_TERM_ASSERT: {
        LLVMValueRef c = operand(g, &t->cond);
        LLVMBasicBlockRef fail = LLVMAppendBasicBlockInContext(g->ctx, g->fv, "panic");
        if (t->expected) LLVMBuildCondBr(B, c, g->bbs[t->target], fail);
        else LLVMBuildCondBr(B, c, fail, g->bbs[t->target]);
        LLVMPositionBuilderAtEnd(B, fail);
        LLVMValueRef info = panic_info(g, t->message, t->span);
        LLVMValueRef call = LLVMBuildCall2(B, g->panic_ty, g->panic_fn, &info, 1, "");
        LLVMSetInstructionCallConv(call, LLVMCCallConv);
        LLVMBuildUnreachable(B);
        break;
    }
    case NC_TERM_NONE: nc_ice("codegen", "unterminated block");
    }
}

static void gen_function(cg *g, uint32_t idx)
{
    const nc_nir_fn *f = g->m->fns[idx];
    g->f = f;
    g->fv = g->fn_vals[idx];
    g->di_scope = NULL;
    if (g->di) {
        uint32_t line = 0, col = 0;
        nc_source_line_col(&g->s->sources, f->span.file, f->span.lo, &line, &col);
        LLVMMetadataRef sub_ty = LLVMDIBuilderCreateSubroutineType(g->di, g->di_file, NULL, 0, LLVMDIFlagZero);
        const char *name = nc_sym_str(&g->s->strtab, f->name);
        size_t link_len;
        const char *link = LLVMGetValueName2(g->fv, &link_len);
        g->di_scope = LLVMDIBuilderCreateFunction(g->di, g->di_file, name, strlen(name), link, link_len, g->di_file, line,
                                                  sub_ty, !f->is_public, 1, line, LLVMDIFlagZero, g->o->opt != '0');
        LLVMSetSubprogram(g->fv, g->di_scope);
    }
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->fv, "entry");
    g->bbs = nc_xcalloc(f->nblocks, sizeof *g->bbs);
    for (uint32_t b = 0; b < f->nblocks; b++) {
        char name[32];
        snprintf(name, sizeof name, "bb%u", b);
        g->bbs[b] = LLVMAppendBasicBlockInContext(g->ctx, g->fv, name);
    }
    LLVMPositionBuilderAtEnd(g->b, entry);
    set_loc(g, f->span);
    g->allocas = nc_xcalloc(f->nlocals ? f->nlocals : 1, sizeof *g->allocas);
    for (uint32_t l = 0; l < f->nlocals; l++) {
        char name[64];
        if (f->locals[l].name) snprintf(name, sizeof name, "%s", nc_sym_str(&g->s->strtab, f->locals[l].name));
        else snprintf(name, sizeof name, "_%u", l);
        g->allocas[l] = LLVMBuildAlloca(g->b, llvm_type(g, f->locals[l].type), name);
    }
    bool sret = indirect(f->ret);
    for (uint32_t p = 0; p < f->nparams; p++) {
        const nc_type *pt = f->locals[p].type;
        LLVMValueRef arg = LLVMGetParam(g->fv, p + sret);
        if (indirect(pt))
            LLVMBuildMemCpy(g->b, g->allocas[p], (unsigned)nc_ty_align(pt), arg, (unsigned)nc_ty_align(pt),
                            LLVMConstInt(g->i64_ty, nc_ty_size(pt), 0));
        else
            LLVMBuildStore(g->b, arg, g->allocas[p]);
    }
    LLVMPositionBuilderBefore(g->entry_b, LLVMBuildBr(g->b, g->bbs[0]));
    for (uint32_t b = 0; b < f->nblocks; b++) {
        LLVMPositionBuilderAtEnd(g->b, g->bbs[b]);
        const nc_nir_block *blk = &f->blocks[b];
        for (uint32_t k = 0; k < blk->nstmts; k++) gen_stmt(g, &blk->stmts[k]);
        gen_term(g, &blk->term);
    }
    free(g->allocas);
    free(g->bbs);
    g->allocas = NULL;
    g->bbs = NULL;
}

/* ---- statics ------------------------------------------------------------------------- */

static bool has_union(const nc_type *t)
{
    if (nc_ty_is_union(t)) return true;
    if (nc_ty_is_array(t)) return has_union(t->elem);
    if (nc_ty_is_struct(t))
        for (uint32_t i = 0; i < t->agg->nfields; i++)
            if (has_union(t->agg->fields[i].type)) return true;
    return false;
}

/* The bytes of a constant (all version-1 targets are little-endian). */
static void const_bytes(const nc_expr *e, const nc_type *t, uint8_t *buf)
{
    if (!e) return; /* zero */
    if (e->cval.kind != NC_CV_NONE) {
        uint64_t bits;
        nc_cval v = e->cval;
        if (nc_ty_is_float(t)) {
            if (t->prim == NC_PRIM_F32) {
                float f = (float)nc_cv_to_double(v);
                uint32_t u;
                memcpy(&u, &f, 4);
                bits = u;
            } else {
                double d = nc_cv_to_double(v);
                memcpy(&bits, &d, 8);
            }
        } else if (v.kind == NC_CV_BOOL) {
            bits = v.b;
        } else {
            bits = v.neg ? (uint64_t)0 - v.mag : v.mag;
        }
        for (uint64_t i = 0; i < nc_ty_size(t); i++) buf[i] = (uint8_t)(bits >> (8 * i));
        return;
    }
    if (e->kind == NC_E_IMPLICIT) {
        const_bytes(e->u.convert.operand, t, buf);
        return;
    }
    if (e->kind != NC_E_AGGREGATE) nc_ice("codegen", "static initialiser is not constant");
    for (uint32_t i = 0; i < e->u.agg.nelems; i++) {
        if (nc_ty_is_array(t)) const_bytes(e->u.agg.elems[i], t->elem, buf + i * nc_ty_size(t->elem));
        else if (nc_ty_is_union(t)) const_bytes(e->u.agg.elems[i], t->agg->fields[e->u.agg.union_field].type, buf);
        else const_bytes(e->u.agg.elems[i], t->agg->fields[i].type, buf + t->agg->fields[i].offset);
    }
}

static LLVMValueRef const_init(cg *g, const nc_expr *e, const nc_type *t)
{
    LLVMTypeRef ty = llvm_type(g, t);
    if (!e) return LLVMConstNull(ty);
    if (e->cval.kind != NC_CV_NONE) return const_value(g, e->cval, t);
    if (e->kind == NC_E_IMPLICIT) return const_init(g, e->u.convert.operand, t);
    if (e->kind != NC_E_AGGREGATE) nc_ice("codegen", "static initialiser is not constant");
    if (e->u.agg.nelems == 0) return LLVMConstNull(ty);
    uint32_t n = e->u.agg.nelems;
    LLVMValueRef *vals = nc_xcalloc(n, sizeof *vals);
    for (uint32_t i = 0; i < n; i++)
        vals[i] = const_init(g, e->u.agg.elems[i], nc_ty_is_array(t) ? t->elem : t->agg->fields[i].type);
    LLVMValueRef c = nc_ty_is_array(t) ? LLVMConstArray2(llvm_type(g, t->elem), vals, n) : LLVMConstNamedStruct(ty, vals, n);
    free(vals);
    return c;
}

static void gen_statics(cg *g)
{
    g->static_vals = nc_xcalloc(g->m->nstatics ? g->m->nstatics : 1, sizeof *g->static_vals);
    for (uint32_t i = 0; i < g->m->nstatics; i++) {
        const nc_nir_static *st = &g->m->statics[i];
        LLVMValueRef init;
        if (has_union(st->type)) {
            /* A union constant has no LLVM constant of the union's type;
             * emit its bytes. Accesses load and store typed values. */
            uint64_t size = nc_ty_size(st->type);
            uint8_t *buf = nc_xcalloc(size ? size : 1, 1);
            const_bytes(st->init, st->type, buf);
            init = LLVMConstStringInContext(g->ctx, (const char *)buf, (unsigned)size, 1);
            free(buf);
        } else {
            init = const_init(g, st->init, st->type);
        }
        char name[512];
        mangle(g, st->name, name, sizeof name);
        LLVMValueRef gv = LLVMAddGlobal(g->mod, LLVMTypeOf(init), name);
        LLVMSetInitializer(gv, init);
        LLVMSetGlobalConstant(gv, st->is_const);
        LLVMSetLinkage(gv, st->is_public ? LLVMExternalLinkage : LLVMInternalLinkage);
        LLVMSetAlignment(gv, (unsigned)nc_ty_align(st->type));
        g->static_vals[i] = gv;
    }
}

/* int main(int argc, char **argv, char **envp) { return <NovaC main>(); }
 * (NOVAC_ABI.md §7). */
static bool gen_main_wrapper(cg *g)
{
    for (uint32_t i = 0; i < g->m->nfns; i++) {
        const nc_nir_fn *f = g->m->fns[i];
        if (strcmp(nc_sym_str(&g->s->strtab, f->name), "main") != 0) continue;
        if (f->nparams != 0 || f->ret->kind != NC_TY_PRIM || f->ret->prim != NC_PRIM_I32) {
            nc_diag_error(&g->s->diag, f->span, "E8005", "`main` must be declared `i32 main()` (or `int main(void)`)");
            return false;
        }
        LLVMTypeRef params[3] = {g->i32_ty, g->ptr_ty, g->ptr_ty};
        LLVMTypeRef ty = LLVMFunctionType(g->i32_ty, params, 3, 0);
        LLVMValueRef w = LLVMAddFunction(g->mod, "main", ty);
        common_fn_attrs(g, w);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, w, "entry");
        LLVMPositionBuilderAtEnd(g->b, bb);
        LLVMSetCurrentDebugLocation2(g->b, NULL);
        LLVMValueRef r = LLVMBuildCall2(g->b, g->fn_tys[i], g->fn_vals[i], NULL, 0, "");
        LLVMBuildRet(g->b, r);
    }
    return true;
}

/* ---- target machine, passes, emission ------------------------------------------------ */

static LLVMTargetMachineRef make_target_machine(cg *g, char **err)
{
    LLVMTargetRef target;
    if (LLVMGetTargetFromTriple(g->o->target->triple, &target, err)) return NULL;
    LLVMTargetMachineOptionsRef opts = LLVMCreateTargetMachineOptions();
    LLVMTargetMachineOptionsSetCPU(opts, g->o->cpu ? g->o->cpu : g->o->target->cpu);
    LLVMTargetMachineOptionsSetFeatures(opts, g->o->features ? g->o->features : g->o->target->features);
    if (g->o->target->abi[0]) LLVMTargetMachineOptionsSetABI(opts, g->o->target->abi);
    LLVMCodeGenOptLevel lvl = g->o->opt == '0' ? LLVMCodeGenLevelNone
                              : g->o->opt == '1' ? LLVMCodeGenLevelLess
                              : g->o->opt == '3' ? LLVMCodeGenLevelAggressive
                                                 : LLVMCodeGenLevelDefault;
    LLVMTargetMachineOptionsSetCodeGenOptLevel(opts, lvl);
    LLVMTargetMachineOptionsSetRelocMode(opts, LLVMRelocPIC); /* hosted Linux links PIE by default */
    LLVMTargetMachineRef tm = LLVMCreateTargetMachineWithOptions(target, g->o->target->triple, opts);
    LLVMDisposeTargetMachineOptions(opts);
    if (!tm) *err = strdup("cannot create a target machine for this CPU and feature set");
    return tm;
}

static bool fail(cg *g, const char *what, char *msg)
{
    nc_diag_error(&g->s->diag, NC_NO_SPAN, "E8001", "%s: %s", what, msg ? msg : "unknown error");
    if (msg) LLVMDisposeMessage(msg);
    return false;
}

bool nc_codegen(nc_session *s, const nc_nir_module *m, const nc_codegen_options *o, nc_output_kind kind, const char *path)
{
    nc_codegen_init();
    cg g;
    memset(&g, 0, sizeof g);
    g.s = s;
    g.m = m;
    g.o = o;
    g.ctx = LLVMContextCreate();
    char modname[256];
    mangle(&g, 0, modname, sizeof modname);
    g.mod = LLVMModuleCreateWithNameInContext(modname, g.ctx);
    const char *src = nc_source_get(&s->sources, m->file)->name;
    LLVMSetSourceFileName(g.mod, src, strlen(src));
    LLVMSetTarget(g.mod, o->target->triple);
    g.b = LLVMCreateBuilderInContext(g.ctx);
    g.entry_b = LLVMCreateBuilderInContext(g.ctx);
    g.ptr_ty = LLVMPointerTypeInContext(g.ctx, 0);
    g.i32_ty = LLVMInt32TypeInContext(g.ctx);
    g.i64_ty = LLVMInt64TypeInContext(g.ctx);

    bool ok = true;
    char *err = NULL;
    LLVMTargetMachineRef tm = make_target_machine(&g, &err);
    if (!tm) {
        ok = fail(&g, "cannot set up the target", err);
        goto done;
    }
    LLVMTargetDataRef td = LLVMCreateTargetDataLayout(tm);
    LLVMSetModuleDataLayout(g.mod, td);
    LLVMDisposeTargetData(td);
    g.td = LLVMGetModuleDataLayout(g.mod);
    if (o->target->abi[0]) {
        LLVMAddModuleFlag(g.mod, LLVMModuleFlagBehaviorError, "target-abi", 10,
                          LLVMMDStringInContext2(g.ctx, o->target->abi, strlen(o->target->abi)));
    }
    if (o->debug) init_debug(&g);

    declare_panic(&g);
    gen_statics(&g);
    g.fn_vals = nc_xcalloc(m->nfns ? m->nfns : 1, sizeof *g.fn_vals);
    g.fn_tys = nc_xcalloc(m->nfns ? m->nfns : 1, sizeof *g.fn_tys);
    for (uint32_t i = 0; i < m->nfns; i++) {
        const nc_nir_fn *f = m->fns[i];
        bool sret = indirect(f->ret);
        LLVMTypeRef *ps = nc_xcalloc(f->nparams + 1, sizeof *ps);
        if (sret) ps[0] = g.ptr_ty;
        for (uint32_t p = 0; p < f->nparams; p++)
            ps[p + sret] = indirect(f->locals[p].type) ? g.ptr_ty : llvm_type(&g, f->locals[p].type);
        g.fn_tys[i] = LLVMFunctionType(sret ? LLVMVoidTypeInContext(g.ctx) : llvm_type(&g, f->ret), ps,
                                       f->nparams + sret, 0);
        free(ps);
        char name[512];
        mangle(&g, f->name, name, sizeof name);
        g.fn_vals[i] = LLVMAddFunction(g.mod, name, g.fn_tys[i]);
        LLVMSetLinkage(g.fn_vals[i], f->is_public ? LLVMExternalLinkage : LLVMInternalLinkage);
        common_fn_attrs(&g, g.fn_vals[i]);
    }
    for (uint32_t i = 0; i < m->nfns; i++) gen_function(&g, i);
    if (!gen_main_wrapper(&g)) {
        ok = false;
        goto done;
    }
    if (g.di) LLVMDIBuilderFinalize(g.di);

    if (LLVMVerifyModule(g.mod, LLVMReturnStatusAction, &err)) {
        fprintf(stderr, "ncc: internal compiler error in codegen: invalid LLVM IR:\n%s\n", err);
        LLVMDisposeMessage(err);
        exit(3);
    }
    LLVMDisposeMessage(err);
    err = NULL;

    {
        char pipeline[32];
        snprintf(pipeline, sizeof pipeline, "default<O%c>", o->opt);
        LLVMPassBuilderOptionsRef pbo = LLVMCreatePassBuilderOptions();
        LLVMErrorRef e = LLVMRunPasses(g.mod, pipeline, tm, pbo);
        LLVMDisposePassBuilderOptions(pbo);
        if (e) {
            ok = fail(&g, "optimisation failed", LLVMGetErrorMessage(e));
            goto done;
        }
    }

    switch (kind) {
    case NC_OUT_LLVM_IR:
        if (!path) {
            char *text = LLVMPrintModuleToString(g.mod);
            fputs(text, stdout);
            LLVMDisposeMessage(text);
        } else if (LLVMPrintModuleToFile(g.mod, path, &err)) {
            ok = fail(&g, "cannot write LLVM IR", err);
        }
        break;
    case NC_OUT_LLVM_BC:
        if (LLVMWriteBitcodeToFile(g.mod, path)) {
            nc_diag_error(&s->diag, NC_NO_SPAN, "E8001", "cannot write bitcode to '%s'", path);
            ok = false;
        }
        break;
    case NC_OUT_ASM:
    case NC_OUT_OBJ: {
        char *p = strdup(path);
        if (LLVMTargetMachineEmitToFile(tm, g.mod, p, kind == NC_OUT_ASM ? LLVMAssemblyFile : LLVMObjectFile, &err))
            ok = fail(&g, "cannot emit machine code", err);
        free(p);
        break;
    }
    }

done:
    if (g.di) LLVMDisposeDIBuilder(g.di);
    free(g.fn_vals);
    free(g.fn_tys);
    free(g.static_vals);
    free(g.agg_keys);
    free(g.agg_types);
    LLVMDisposeBuilder(g.b);
    LLVMDisposeBuilder(g.entry_b);
    LLVMDisposeModule(g.mod);
    if (tm) LLVMDisposeTargetMachine(tm);
    LLVMContextDispose(g.ctx);
    return ok;
}
