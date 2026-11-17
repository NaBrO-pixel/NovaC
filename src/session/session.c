#include "session/session.h"

void nc_session_init(nc_session *s, FILE *diag_out)
{
    nc_source_init(&s->sources);
    nc_strtab_init(&s->strtab);
    nc_arena_init(&s->arena);
    nc_diag_init(&s->diag, &s->sources, diag_out);
}

void nc_session_free(nc_session *s)
{
    nc_arena_free(&s->arena);
    nc_strtab_free(&s->strtab);
    nc_source_free(&s->sources);
}
