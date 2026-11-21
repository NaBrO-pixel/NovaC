/* Linking (NOVAC_ABI.md §8). Nova Linker does not exist; hosted
 * executables are linked by the C compiler driver for the target (`cc` on
 * the host, `<triple>-gcc` when cross-compiling) so the C library's start
 * files are used. */
#ifndef NC_DRIVER_LINK_H
#define NC_DRIVER_LINK_H

#include "session/session.h"
#include "target/target.h"

/* Reports E8002 (linker failed) or E8003 (linker not found). */
/* Links `objects` and `extra` (-l, -L, .o, .a inputs) into `output`: with
 * the C compiler driver for the Linux targets (D-042), or with `ld`
 * against LibNovaC in `libnovac` for x86_64-novaris (NOVAC_ABI.md §10.1). */
bool nc_link(nc_session *s, const nc_target *t, const char *linker, const char *libnovac, const char *const *objects,
             int nobjects, const char *const *extra, int nextra, const char *output);

#endif
