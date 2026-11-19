#define _POSIX_C_SOURCE 200809L /* posix_spawnp, waitpid */
#include "driver/link.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

extern char **environ;

bool nc_link(nc_session *s, const nc_target *t, const char *linker, const char *const *objects, int nobjects,
             const char *const *extra, int nextra, const char *output)
{
    char cmd[128];
    if (linker) snprintf(cmd, sizeof cmd, "%s", linker);
    else if (nc_target_is_host(t)) snprintf(cmd, sizeof cmd, "cc");
    else snprintf(cmd, sizeof cmd, "%sgcc", t->cross_prefix);

    char **argv = nc_xcalloc((size_t)nobjects + (size_t)nextra + 4, sizeof *argv);
    int n = 0;
    argv[n++] = cmd;
    for (int i = 0; i < nobjects; i++) argv[n++] = (char *)objects[i];
    for (int i = 0; i < nextra; i++) argv[n++] = (char *)extra[i];
    argv[n++] = "-o";
    argv[n++] = (char *)output;
    argv[n] = NULL;

    pid_t pid;
    int rc = posix_spawnp(&pid, cmd, NULL, NULL, argv, environ);
    free(argv);
    if (rc != 0) {
        nc_diag_error(&s->diag, NC_NO_SPAN, "E8003", "cannot run the linker '%s': %s", cmd, strerror(rc));
        if (!nc_target_is_host(t) && !linker)
            nc_diag_help(&s->diag, "install a cross C toolchain for %s (e.g. the package gcc-%.*s) or use --linker=",
                         t->name, (int)(strlen(t->cross_prefix) - 1), t->cross_prefix);
        return false;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            nc_diag_error(&s->diag, NC_NO_SPAN, "E8002", "linking failed: cannot wait for '%s'", cmd);
            return false;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        nc_diag_error(&s->diag, NC_NO_SPAN, "E8002", "linking failed: '%s' exited with status %d", cmd,
                      WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return false;
    }
    return true;
}
