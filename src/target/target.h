/* Compilation targets (NOVAC_ABI.md §2). Phase 3 implements the three
 * hosted Linux targets and Phase 4 the NovarisOS userspace target; the
 * freestanding and kernel targets are recognised and rejected until
 * Phase 7. */
#ifndef NC_TARGET_TARGET_H
#define NC_TARGET_TARGET_H

#include <stdbool.h>

/* The C calling convention (NOVAC_ABI.md §5). */
typedef enum nc_c_abi { NC_CABI_SYSV_X86_64, NC_CABI_AAPCS64, NC_CABI_LP64D } nc_c_abi;

typedef struct nc_target {
    const char *name;         /* --target value */
    const char *triple;       /* LLVM triple */
    const char *cpu;          /* baseline CPU (D-023) */
    const char *features;     /* baseline features */
    const char *abi;          /* LLVM target ABI name, or "" */
    const char *cross_prefix; /* GCC cross driver prefix when not the host, e.g. "aarch64-linux-gnu-" */
    const char *qemu;         /* user-mode emulator for running tests on other hosts */
    const char *sysroot;      /* emulator library prefix (QEMU_LD_PREFIX) */
    bool implemented;
    int phase;                /* when not implemented: the phase that adds it */
    nc_c_abi c_abi;
    bool char_signed;         /* C `char` (core.ffi.c_char): signed on x86-64, unsigned on AArch64 and RISC-V Linux */
    /* Aggregates may cross `extern "C"` by value only once tests/abi passes
     * for the target (E7032 otherwise). */
    bool abi_verified;
    /* Linked statically against LibNovaC, not by the C compiler driver
     * (NOVAC_ABI.md §10.1): non-PIC code, `ld`, crt0.o and libNovaC.a. */
    bool libnovac;
} nc_target;

/* NULL if unknown. */
const nc_target *nc_target_find(const char *name);
/* The target matching the machine ncc runs on (x86_64-linux-gnu in Phase 3). */
const nc_target *nc_target_host(void);
/* Is this the machine ncc runs on (so the host C compiler links for it)? */
bool nc_target_is_host(const nc_target *t);
/* Comma-separated list of implemented target names, for diagnostics. */
const char *nc_target_list(void);

#endif
