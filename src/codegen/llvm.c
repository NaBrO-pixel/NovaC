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

static LLVMTypeRef llvm_type(cg *g, const nc_type *t)
{
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

static LLVMValueRef operand(cg *g, const nc_operand *o)
{
    if (o->kind == NC_OP_CONST) return const_value(g, o->cval, o->type);
    return LLVMBuildLoad2(g->b, llvm_type(g, o->type), g->allocas[o->local], "");
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
    const nc_type *dt = g->f->locals[st->dest].type;
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
        case NC_RV_USE: v = operand(g, &st->rv.a); break;
        case NC_RV_BINARY: v = binary(g, st->rv.binop, &st->rv.a, &st->rv.b); break;
        case NC_RV_UNARY: {
            LLVMValueRef a = operand(g, &st->rv.a);
            if (st->rv.unop == NC_NU_NEG_FLOAT) v = fast(g, LLVMBuildFNeg(g->b, a, ""));
            else v = LLVMBuildXor(g->b, a, LLVMConstAllOnes(LLVMTypeOf(a)), ""); /* ! on i1, ~ on iN */
            break;
        }
        case NC_RV_CAST: v = cast(g, st->rv.cast, &st->rv.a, dt); break;
        default: nc_ice("codegen", "invalid rvalue");
        }
    }
    LLVMBuildStore(g->b, v, g->allocas[st->dest]);
}

static void gen_term(cg *g, const nc_terminator *t)
{
    set_loc(g, t->span);
    LLVMBuilderRef B = g->b;
    switch (t->kind) {
    case NC_TERM_GOTO: LLVMBuildBr(B, g->bbs[t->target]); break;
    case NC_TERM_BRANCH: LLVMBuildCondBr(B, operand(g, &t->cond), g->bbs[t->target], g->bbs[t->other]); break;
    case NC_TERM_RETURN:
        if (t->has_value) LLVMBuildRet(B, operand(g, &t->value));
        else LLVMBuildRetVoid(B);
        break;
    case NC_TERM_UNREACHABLE: LLVMBuildUnreachable(B); break;
    case NC_TERM_CALL: {
        uint32_t idx = 0;
        while (idx < g->m->nfns && g->m->fns[idx] != t->callee) idx++;
        LLVMValueRef *args = t->nargs ? nc_xcalloc(t->nargs, sizeof *args) : NULL;
        for (uint32_t i = 0; i < t->nargs; i++) args[i] = operand(g, &t->args[i]);
        LLVMValueRef r = LLVMBuildCall2(B, g->fn_tys[idx], g->fn_vals[idx], args, t->nargs, "");
        free(args);
        if (t->dest != UINT32_MAX) LLVMBuildStore(B, r, g->allocas[t->dest]);
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
    for (uint32_t p = 0; p < f->nparams; p++) LLVMBuildStore(g->b, LLVMGetParam(g->fv, p), g->allocas[p]);
    LLVMBuildBr(g->b, g->bbs[0]);
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

/* int main(int argc, char **argv, char **envp) { return <NovaC main>(); }
 * (NOVAC_ABI.md §7). */
static bool gen_main_wrapper(cg *g)
{
    for (uint32_t i = 0; i < g->m->nfns; i++) {
        const nc_nir_fn *f = g->m->fns[i];
        if (strcmp(nc_sym_str(&g->s->strtab, f->name), "main") != 0) continue;
        if (f->nparams != 0 || f->ret->prim != NC_PRIM_I32) {
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
    if (o->target->abi[0]) {
        LLVMAddModuleFlag(g.mod, LLVMModuleFlagBehaviorError, "target-abi", 10,
                          LLVMMDStringInContext2(g.ctx, o->target->abi, strlen(o->target->abi)));
    }
    if (o->debug) init_debug(&g);

    declare_panic(&g);
    g.fn_vals = nc_xcalloc(m->nfns ? m->nfns : 1, sizeof *g.fn_vals);
    g.fn_tys = nc_xcalloc(m->nfns ? m->nfns : 1, sizeof *g.fn_tys);
    for (uint32_t i = 0; i < m->nfns; i++) {
        const nc_nir_fn *f = m->fns[i];
        LLVMTypeRef *ps = nc_xcalloc(f->nparams ? f->nparams : 1, sizeof *ps);
        for (uint32_t p = 0; p < f->nparams; p++) ps[p] = llvm_type(&g, f->locals[p].type);
        g.fn_tys[i] = LLVMFunctionType(llvm_type(&g, f->ret), ps, f->nparams, 0);
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
    LLVMDisposeBuilder(g.b);
    LLVMDisposeModule(g.mod);
    if (tm) LLVMDisposeTargetMachine(tm);
    LLVMContextDispose(g.ctx);
    return ok;
}
