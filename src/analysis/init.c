/* Definite initialisation: a forward "must" dataflow analysis over the NIR
 * control-flow graph. The state of a block is the set of locals that are
 * initialised on *every* path reaching it (intersection at joins). Blocks
 * not reachable from the entry keep the full set, so dead code is not
 * reported. Only declared variables are checked: temporaries are assigned
 * before use by construction. */
#include "analysis/analysis.h"

#include <stdlib.h>
#include <string.h>

typedef uint64_t word;
#define WBITS 64u

typedef struct bitset {
    word *w;
    uint32_t nwords;
} bitset;

static bool get(const word *s, uint32_t i) { return (s[i / WBITS] >> (i % WBITS)) & 1; }
static void set(word *s, uint32_t i) { s[i / WBITS] |= (word)1 << (i % WBITS); }

static void mark_assigned(word *s, const nc_nir_stmt *st)
{
    set(s, st->dest);
    if (st->kind == NC_ST_CHECKED) set(s, st->flag);
}

static void successors(const nc_terminator *t, nc_block_id out[2], int *n)
{
    *n = 0;
    switch (t->kind) {
    case NC_TERM_GOTO:
    case NC_TERM_CALL:
    case NC_TERM_ASSERT:
        out[(*n)++] = t->target;
        break;
    case NC_TERM_BRANCH:
        /* A constant condition (`while (true)`) has one successor, as in the
         * type checker's return-path analysis. */
        if (t->cond.kind == NC_OP_CONST && t->cond.cval.kind == NC_CV_BOOL) {
            out[(*n)++] = t->cond.cval.b ? t->target : t->other;
            break;
        }
        out[(*n)++] = t->target;
        out[(*n)++] = t->other;
        break;
    default:
        break;
    }
}

typedef struct checker {
    nc_session *s;
    const nc_nir_fn *f;
    bool *reported; /* per local */
    bool ok;
} checker;

static void use(checker *c, const word *state, const nc_operand *o)
{
    if (o->kind != NC_OP_COPY) return;
    const nc_nir_local *l = &c->f->locals[o->local];
    if (!l->is_user || get(state, o->local) || c->reported[o->local]) return;
    c->reported[o->local] = true;
    c->ok = false;
    nc_diag_error(&c->s->diag, o->span, "E5031", "use of possibly-uninitialised variable '%s'",
                  nc_sym_str(&c->s->strtab, l->name));
    nc_diag_note(&c->s->diag, l->span, "'%s' is declared here without an initialiser", nc_sym_str(&c->s->strtab, l->name));
}

static void check_fn(checker *c, const nc_nir_fn *f)
{
    uint32_t nw = (f->nlocals + WBITS - 1) / WBITS;
    if (nw == 0) nw = 1;
    word *in = nc_xcalloc((size_t)f->nblocks * nw, sizeof *in);
    word *tmp = nc_xcalloc(nw, sizeof *tmp);
    bool *reachable = nc_xcalloc(f->nblocks, sizeof *reachable);

    /* Reachability, so that unreachable blocks do not weaken the result. */
    uint32_t *stack = nc_xcalloc(f->nblocks, sizeof *stack);
    uint32_t sp = 0;
    reachable[0] = true;
    stack[sp++] = 0;
    while (sp) {
        uint32_t b = stack[--sp];
        nc_block_id succ[2];
        int ns;
        successors(&f->blocks[b].term, succ, &ns);
        for (int k = 0; k < ns; k++)
            if (!reachable[succ[k]]) {
                reachable[succ[k]] = true;
                stack[sp++] = succ[k];
            }
    }

    /* Initial states: entry = parameters only; others = everything (top). */
    for (uint32_t b = 0; b < f->nblocks; b++) memset(&in[(size_t)b * nw], b == 0 ? 0 : 0xFF, nw * sizeof(word));
    for (uint32_t p = 0; p < f->nparams; p++) set(&in[0], p);

    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t b = 0; b < f->nblocks; b++) {
            if (!reachable[b]) continue;
            memcpy(tmp, &in[(size_t)b * nw], nw * sizeof(word));
            const nc_nir_block *blk = &f->blocks[b];
            for (uint32_t k = 0; k < blk->nstmts; k++) mark_assigned(tmp, &blk->stmts[k]);
            if (blk->term.kind == NC_TERM_CALL && blk->term.dest != UINT32_MAX) set(tmp, blk->term.dest);
            nc_block_id succ[2];
            int ns;
            successors(&blk->term, succ, &ns);
            for (int k = 0; k < ns; k++) {
                word *dst = &in[(size_t)succ[k] * nw];
                for (uint32_t i = 0; i < nw; i++) {
                    word w = dst[i] & tmp[i];
                    if (w != dst[i]) {
                        dst[i] = w;
                        changed = true;
                    }
                }
            }
        }
    }

    /* Report reads of locals not initialised on every path. */
    c->f = f;
    c->reported = nc_xcalloc(f->nlocals ? f->nlocals : 1, sizeof *c->reported);
    for (uint32_t b = 0; b < f->nblocks; b++) {
        if (!reachable[b]) continue;
        memcpy(tmp, &in[(size_t)b * nw], nw * sizeof(word));
        const nc_nir_block *blk = &f->blocks[b];
        for (uint32_t k = 0; k < blk->nstmts; k++) {
            const nc_nir_stmt *st = &blk->stmts[k];
            use(c, tmp, &st->rv.a);
            if (st->kind == NC_ST_CHECKED || st->rv.kind == NC_RV_BINARY) use(c, tmp, &st->rv.b);
            mark_assigned(tmp, st);
        }
        const nc_terminator *t = &blk->term;
        if (t->kind == NC_TERM_BRANCH || t->kind == NC_TERM_ASSERT) use(c, tmp, &t->cond);
        if (t->kind == NC_TERM_RETURN && t->has_value) use(c, tmp, &t->value);
        if (t->kind == NC_TERM_CALL)
            for (uint32_t a = 0; a < t->nargs; a++) use(c, tmp, &t->args[a]);
    }
    free(c->reported);
    free(stack);
    free(reachable);
    free(tmp);
    free(in);
}

bool nc_check_init(nc_session *s, const nc_nir_module *m)
{
    checker c;
    memset(&c, 0, sizeof c);
    c.s = s;
    c.ok = true;
    for (uint32_t i = 0; i < m->nfns; i++) check_fn(&c, m->fns[i]);
    return c.ok;
}
