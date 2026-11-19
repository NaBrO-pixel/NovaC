/* Definite initialisation: a forward "must" dataflow analysis over the NIR
 * control-flow graph. The state of a block is the set of locals that are
 * initialised on *every* path reaching it (intersection at joins). Blocks
 * not reachable from the entry keep the full set, so dead code is not
 * reported. Only declared variables are checked: temporaries are assigned
 * before use by construction.
 *
 * Variables are tracked whole: reading a field or element needs the whole
 * variable initialised, and so does assigning one (NOVAC_MEMORY_MODEL.md
 * §5); initialise an aggregate with a list, where omitted members are
 * zero (D-043). */
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
    if (st->dest.base == NC_PLACE_LOCAL && st->dest.nprojs == 0) set(s, st->dest.id);
    if (st->kind == NC_ST_CHECKED) set(s, st->flag);
}

/* The successors of a block: nsucc() of them, the i-th given by succ(). */
static uint32_t nsucc(const nc_terminator *t)
{
    switch (t->kind) {
    case NC_TERM_GOTO:
    case NC_TERM_CALL:
    case NC_TERM_ASSERT: return 1;
    case NC_TERM_BRANCH:
        /* A constant condition (`while (true)`) has one successor, as in the
         * type checker's return-path analysis. */
        return t->cond.kind == NC_OP_CONST && t->cond.cval.kind == NC_CV_BOOL ? 1 : 2;
    case NC_TERM_SWITCH: return t->ncases + 1;
    default: return 0;
    }
}

static nc_block_id succ(const nc_terminator *t, uint32_t i)
{
    switch (t->kind) {
    case NC_TERM_BRANCH:
        if (t->cond.kind == NC_OP_CONST && t->cond.cval.kind == NC_CV_BOOL) return t->cond.cval.b ? t->target : t->other;
        return i == 0 ? t->target : t->other;
    case NC_TERM_SWITCH: return i < t->ncases ? t->case_targets[i] : t->other;
    default: return t->target;
    }
}

typedef struct checker {
    nc_session *s;
    const nc_nir_fn *f;
    bool *reported; /* per local */
    bool ok;
} checker;

static void need(checker *c, const word *state, const nc_place *p, nc_span span, bool partial_write)
{
    if (p->base != NC_PLACE_LOCAL) return; /* statics are always initialised */
    uint32_t id = p->id;
    const nc_nir_local *l = &c->f->locals[id];
    if (!l->is_user || get(state, id) || c->reported[id]) return;
    c->reported[id] = true;
    c->ok = false;
    const char *name = nc_sym_str(&c->s->strtab, l->name);
    if (partial_write)
        nc_diag_error(&c->s->diag, span, "E5031", "assignment to a part of possibly-uninitialised variable '%s'", name);
    else
        nc_diag_error(&c->s->diag, span, "E5031", "use of possibly-uninitialised variable '%s'", name);
    nc_diag_note(&c->s->diag, l->span, "'%s' is declared here without an initialiser", name);
    if (partial_write)
        nc_diag_help(&c->s->diag, "initialise '%s' first, e.g. with `= {}`, which sets every member to zero", name);
}

static void use(checker *c, const word *state, const nc_operand *o)
{
    if (o->kind == NC_OP_COPY) need(c, state, &o->place, o->span, false);
}

static void use_stmt(checker *c, const word *state, const nc_nir_stmt *st)
{
    use(c, state, &st->rv.a);
    if (st->kind == NC_ST_CHECKED || st->rv.kind == NC_RV_BINARY || st->rv.kind == NC_RV_PTR_OFFSET ||
        st->rv.kind == NC_RV_PTR_DIFF)
        use(c, state, &st->rv.b);
    if (st->kind == NC_ST_ASSIGN && st->rv.kind == NC_RV_AGGREGATE)
        for (uint32_t i = 0; i < st->rv.nelems; i++) use(c, state, &st->rv.elems[i]);
    /* Writing through `*p` reads p; writing a field or element needs the whole variable. */
    if (st->dest.nprojs) need(c, state, &st->dest, st->span, st->dest.projs[0].kind != NC_PROJ_DEREF);
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
        const nc_terminator *t = &f->blocks[b].term;
        for (uint32_t k = 0; k < nsucc(t); k++) {
            nc_block_id x = succ(t, k);
            if (!reachable[x]) {
                reachable[x] = true;
                stack[sp++] = x;
            }
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
            for (uint32_t k = 0; k < nsucc(&blk->term); k++) {
                word *dst = &in[(size_t)succ(&blk->term, k) * nw];
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
            use_stmt(c, tmp, st);
            mark_assigned(tmp, st);
        }
        const nc_terminator *t = &blk->term;
        if (t->kind == NC_TERM_BRANCH || t->kind == NC_TERM_ASSERT || t->kind == NC_TERM_SWITCH) use(c, tmp, &t->cond);
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
