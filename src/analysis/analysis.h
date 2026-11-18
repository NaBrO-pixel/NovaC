/* Analyses on NIR (NOVAC_COMPILER_ARCHITECTURE.md §9.2). */
#ifndef NC_ANALYSIS_ANALYSIS_H
#define NC_ANALYSIS_ANALYSIS_H

#include "nir/nir.h"

/* Definite initialisation (NOVAC_MEMORY_MODEL.md §5.2): every read of a
 * declared variable must be preceded, on every path, by an assignment.
 * Reports E5031 once per variable. Returns false if anything was reported. */
bool nc_check_init(nc_session *s, const nc_nir_module *m);

#endif
