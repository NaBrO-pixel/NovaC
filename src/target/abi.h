/* C calling-convention lowering (NOVAC_ABI.md §5).
 *
 * LLVM implements the register assignment of each target's C convention
 * for the LLVM types it is given, but not the mapping of C aggregates to
 * those types: the front end decides it, as Clang does. This module makes
 * that decision for `extern "C"` functions, per target, from NovaC types.
 * It knows nothing of LLVM: code generation turns the description into
 * LLVM types and attributes.
 *
 * The result was calibrated against Clang 18 and is verified by the
 * conformance tests in tests/abi/, which call between NovaC and C compiled
 * by Clang in both directions on every target. */
#ifndef NC_TARGET_ABI_H
#define NC_TARGET_ABI_H

#include "sema/types.h"
#include "target/target.h"

typedef enum nc_abi_kind {
    NC_ABI_DIRECT,   /* a scalar, as itself (with `ext`) */
    NC_ABI_COERCE,   /* an aggregate, as the `pieces` loaded from its memory */
    NC_ABI_INDIRECT, /* an aggregate in memory: by pointer (argument) or through `sret` (result) */
    NC_ABI_VOID,     /* a `void` result */
} nc_abi_kind;

typedef enum nc_abi_ext { NC_EXT_NONE, NC_EXT_SIGN, NC_EXT_ZERO } nc_abi_ext;

typedef enum nc_abi_piece_kind {
    NC_PIECE_INT,     /* an integer of `size` bytes */
    NC_PIECE_F32,
    NC_PIECE_F64,
    NC_PIECE_F32X2,   /* two floats in one SSE eightbyte (x86-64 `<2 x float>`) */
} nc_abi_piece_kind;

typedef struct nc_abi_piece {
    nc_abi_piece_kind kind;
    unsigned size;    /* bytes */
    unsigned offset;  /* in the aggregate */
} nc_abi_piece;

typedef struct nc_abi_arg {
    nc_abi_kind kind;
    nc_abi_ext ext;            /* DIRECT */
    nc_abi_piece pieces[4];    /* COERCE */
    unsigned npieces;
    /* COERCE: the pieces form one LLVM array value `[n x T]` (AArch64),
     * instead of separate arguments / a literal struct result. */
    bool as_array;
    unsigned align_stack;      /* COERCE on AArch64 HFAs: `alignstack(8)` */
    /* INDIRECT argument: a copy on the stack (x86-64 `byval`), else a
     * pointer to a copy the caller makes. */
    bool byval;
} nc_abi_arg;

typedef struct nc_abi_fn {
    nc_abi_arg ret;            /* INDIRECT: an `sret` pointer is the first argument */
    nc_abi_arg *params;        /* nparams entries, caller-owned */
    unsigned nparams;
} nc_abi_fn;

/* Classify a C function. `params` has nparams entries; `out->params` must
 * point to space for as many. Variadic arguments are scalars and are
 * classified with nc_abi_classify_variadic. */
void nc_abi_classify_fn(const nc_target *t, const nc_type *ret, const nc_type *const *params, unsigned nparams,
                        nc_abi_fn *out);
/* A scalar passed in the variadic part of a call. */
nc_abi_arg nc_abi_classify_variadic(const nc_target *t, const nc_type *arg);

#endif
