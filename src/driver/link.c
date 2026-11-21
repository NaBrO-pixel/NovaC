#define _POSIX_C_SOURCE 200809L /* posix_spawnp, waitpid */
#include "driver/link.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static char *join(const char *dir, const char *file)
{
    char *p = nc_xmalloc(strlen(dir) + strlen(file) + 2);
    sprintf(p, "%s/%s", dir, file);
    return p;
}

bool nc_link(nc_session *s, const nc_target *t, const char *linker, const char *libnovac, const char *const *objects,
             int nobjects, const char *const *extra, int nextra, const char *output)
{
    char cmd[128];
    char *crt0 = NULL, *lib = NULL;
    if (t->libnovac) {
        if (!libnovac || !*libnovac) libnovac = getenv("NOVAC_LIBNOVAC");
        if (!libnovac || !*libnovac) {
            nc_diag_error(&s->diag, NC_NO_SPAN, "E8006", "the target %s links against LibNovaC, but no LibNovaC was given",
                          t->name);
            nc_diag_help(&s->diag, "build it (`make lib` in LibNovaC) and pass --libnovac=<build>/target, or set NOVAC_LIBNOVAC");
            return false;
        }
        crt0 = join(libnovac, "crt0.o");
        lib = join(libnovac, "libNovaC.a");
        if (access(crt0, R_OK) != 0 || access(lib, R_OK) != 0) {
            nc_diag_error(&s->diag, NC_NO_SPAN, "E8006", "'%s' does not contain crt0.o and libNovaC.a", libnovac);
            free(crt0);
            free(lib);
            return false;
        }
    }
    if (linker) snprintf(cmd, sizeof cmd, "%s", linker);
    else if (t->libnovac) snprintf(cmd, sizeof cmd, "ld");
    else if (nc_target_is_host(t)) snprintf(cmd, sizeof cmd, "cc");
    else snprintf(cmd, sizeof cmd, "%sgcc", t->cross_prefix);

    char **argv = nc_xcalloc((size_t)nobjects + (size_t)nextra + 16, sizeof *argv);
    int n = 0;
    argv[n++] = cmd;
    if (t->libnovac) {
        /* ld -m elf_x86_64 -static -nostdlib -e _start crt0.o <objects> libNovaC.a */
        static char *const head[] = {"-m", "elf_x86_64", "-static", "-nostdlib", "-e", "_start"};
        for (size_t i = 0; i < sizeof head / sizeof head[0]; i++) argv[n++] = head[i];
        argv[n++] = crt0;
    }
    for (int i = 0; i < nobjects; i++) argv[n++] = (char *)objects[i];
    for (int i = 0; i < nextra; i++) argv[n++] = (char *)extra[i];
    if (lib) argv[n++] = lib;
    argv[n++] = "-o";
    argv[n++] = (char *)output;
    argv[n] = NULL;

    pid_t pid;
    int rc = posix_spawnp(&pid, cmd, NULL, NULL, argv, environ);
    free(argv);
    free(crt0);
    free(lib);
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
