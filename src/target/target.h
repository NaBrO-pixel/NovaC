/* Compilation targets (NOVAC_ABI.md §2). Phase 3 implements the three
 * hosted Linux targets; the NovarisOS and freestanding targets are
 * recognised and rejected until Phases 4 and 7. */
#ifndef NC_TARGET_TARGET_H
#define NC_TARGET_TARGET_H

#include <stdbool.h>

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
