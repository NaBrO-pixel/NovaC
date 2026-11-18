#include "target/target.h"

#include <string.h>

static const nc_target targets[] = {
    /* Hosted Linux (Phase 3). Baselines from D-023. */
    {"x86_64-linux-gnu", "x86_64-unknown-linux-gnu", "x86-64", "", "", "x86_64-linux-gnu-", "qemu-x86_64",
     "/usr/x86_64-linux-gnu", true, 3},
    {"aarch64-linux-gnu", "aarch64-unknown-linux-gnu", "generic", "+v8a", "", "aarch64-linux-gnu-", "qemu-aarch64",
     "/usr/aarch64-linux-gnu", true, 3},
    {"riscv64-linux-gnu", "riscv64-unknown-linux-gnu", "generic-rv64", "+m,+a,+f,+d,+c", "lp64d",
     "riscv64-linux-gnu-", "qemu-riscv64", "/usr/riscv64-linux-gnu", true, 3},
    /* Recognised, not yet implemented. */
    {"x86_64-novaris", "x86_64-unknown-linux-gnu", "x86-64", "", "", "", NULL, NULL, false, 4},
    {"x86_64-unknown-none", "x86_64-unknown-none-elf", "x86-64", "", "", "", NULL, NULL, false, 7},
    {"x86_64-novaris-kernel", "x86_64-unknown-none-elf", "x86-64", "", "", "", NULL, NULL, false, 7},
    {"aarch64-unknown-none", "aarch64-unknown-none-elf", "generic", "", "", "", NULL, NULL, false, 7},
    {"riscv64-unknown-none", "riscv64-unknown-none-elf", "generic-rv64", "", "", "", NULL, NULL, false, 7},
};

const nc_target *nc_target_find(const char *name)
{
    for (size_t i = 0; i < sizeof targets / sizeof targets[0]; i++)
        if (strcmp(targets[i].name, name) == 0) return &targets[i];
    return NULL;
}

const nc_target *nc_target_host(void)
{
#if defined(__x86_64__)
    return &targets[0];
#elif defined(__aarch64__)
    return &targets[1];
#elif defined(__riscv) && __riscv_xlen == 64
    return &targets[2];
#else
    return &targets[0]; /* not a supported host; cross-compile with --target */
#endif
}

bool nc_target_is_host(const nc_target *t)
{
    return t == nc_target_host();
}

const char *nc_target_list(void)
{
    return "x86_64-linux-gnu, aarch64-linux-gnu, riscv64-linux-gnu";
}
