// Guest memory in a capture (cap.h): a snapshot of every populated page, then before each recorded call the pages whose
// bytes changed. A 64-bit hash per page (in 2 MiB chunks made on first use) says what changed; hashing runs on several
// threads, so a delta over the ~5 GiB a race holds costs a fraction of a second, and the game thread that submits waits
// for it (the capture is a diagnosis: its frames run slower).
#include <stdlib.h>
#include <string.h>

#include "cap.h"
#include "gamelib.h"
#include "host.h"
#include "platform/platform.h"

#define PAGE 4096ull
#define CHUNK (2ull << 20)
#define CPAGES (CHUNK / PAGE)
#define THREADS 8
#define MAX_RUNS 65536

typedef struct {
    uint64_t base, size;   // region, CHUNK aligned outwards
    uint64_t **hash;       // per chunk: CPAGES page hashes (0: the page reads zero), NULL: never populated
} Region;
static Region regions[8];
static int nregions;

static uint64_t align_dn(uint64_t v, uint64_t a) { return v & ~(a - 1); }

static void regions_init(void) {
    if (nregions) return;
    const RtGuestLayout *L = mem_layout();
    const RtGame *g = rt_game();
    RtRange r[8];
    int n = 0;
    r[n++] = (RtRange){g->modules[0].base,
                       g->modules[g->nmodules - 1].base + g->modules[g->nmodules - 1].size - g->modules[0].base};
    r[n++] = L->stack;
    r[n++] = L->heap;
    r[n++] = L->alias;
    for (int i = 0; i < n; i++) {
        Region *x = &regions[nregions++];
        x->base = align_dn(r[i].base, CHUNK);
        x->size = align_dn(r[i].base + r[i].size + CHUNK - 1, CHUNK) - x->base;
        x->hash = calloc(x->size / CHUNK, sizeof *x->hash);
        if (!x->hash) rt_fatal("cap: out of memory for the page hashes");
    }
}

// 0 for a page of zeros; otherwise never 0.
static uint64_t page_hash(const uint64_t *p) {
    uint64_t a = 0x9e3779b97f4a7c15ull, b = 0xc2b2ae3d27d4eb4full, c = 0x165667b19e3779f9ull, d = 0x27d4eb2f165667c5ull,
             any = 0;
    for (int i = 0; i < 512; i += 4) {
        any |= p[i] | p[i + 1] | p[i + 2] | p[i + 3];
        a = ((a ^ p[i]) * 0xff51afd7ed558ccdull) ^ (a >> 29);
        b = ((b ^ p[i + 1]) * 0xc4ceb9fe1a85ec53ull) ^ (b >> 31);
        c = ((c ^ p[i + 2]) * 0xff51afd7ed558ccdull) ^ (c >> 27);
        d = ((d ^ p[i + 3]) * 0xc4ceb9fe1a85ec53ull) ^ (d >> 33);
    }
    if (!any) return 0;
    uint64_t h = a ^ (b << 1) ^ (c << 2) ^ (d << 3);
    h = (h ^ (h >> 33)) * 0xff51afd7ed558ccdull;
    return h ? h : 1;
}

// One chunk of work: its populated pages (bit set) hashed into now[] by a worker; kept pages keep their last hash.
typedef struct {
    Region *r;
    uint64_t ci;
    uint64_t pop[CPAGES / 64], keep[CPAGES / 64];
    uint64_t now[CPAGES];
} Work;
typedef struct { Work *w; uint64_t n; volatile uint64_t *next; } Job;

static void hash_worker(void *arg) {
    Job *j = arg;
    for (;;) {
        uint64_t k = __atomic_fetch_add(j->next, 1, __ATOMIC_RELAXED);
        if (k >= j->n) return;
        Work *w = &j->w[k];
        uint64_t base = w->r->base + w->ci * CHUNK;
        const uint64_t *was = w->r->hash[w->ci];
        for (uint64_t i = 0; i < CPAGES; i++)
            w->now[i] = w->keep[i / 64] >> (i % 64) & 1 ? (was ? was[i] : 0)
                        : w->pop[i / 64] >> (i % 64) & 1 ? page_hash((const uint64_t *)(uintptr_t)(base + i * PAGE)) : 0;
    }
}

// Readable committed runs of the region, then their populated pages, as bits per chunk. Read-trapped pages (no access
// until filled, memory.c) are read by the first record, the snapshot (the access fills them); after it they are kept:
// they hold the GPU's bytes of a fence the replay reaches itself. Read at every delta, they went into the capture as
// CPU writes, and the replay's shadow sync copied the game's GPU results over the replay's own: the culling dispatch's
// instance records of one run with the draw arguments of another, black cacti in 2 of 9 serial replays of
// build/cap/qrace that the game did not have (ind1-ind4).
static int snapped;
static Work *collect(Region *r, uint64_t *nwork, Work *w, uint64_t *cap) {
    static uint64_t runs[MAX_RUNS][2];
    for (uint64_t a = r->base, end; a < r->base + r->size; a = end) {
        int st = plat_mem_state(a, r->base + r->size, &end);
        if (st < PLAT_PROT_NONE || (st == PLAT_PROT_NONE && !mem_trapped(a))) continue;
        for (uint64_t from = a; from < end;) {
            uint64_t n = st == PLAT_PROT_NONE ? 0 : plat_mem_populated(from, end - from, runs, MAX_RUNS);
            if (n == UINT64_MAX) rt_fatal("cap: populated pages cannot be listed on this platform");
            if (st == PLAT_PROT_NONE) runs[0][0] = from, runs[0][1] = end, n = 1;
            for (uint64_t k = 0; k < n; k++)
                for (uint64_t p = runs[k][0]; p < runs[k][1]; p += PAGE) {
                    uint64_t ci = (p - r->base) / CHUNK, pi = (p - r->base) % CHUNK / PAGE;
                    if (!*nwork || w[*nwork - 1].ci != ci || w[*nwork - 1].r != r) {
                        if (*nwork == *cap) {
                            *cap = *cap ? *cap * 2 : 256;
                            if (!(w = realloc(w, sizeof *w * *cap))) rt_fatal("cap: out of memory for the page scan");
                        }
                        Work *x = &w[(*nwork)++];
                        x->r = r, x->ci = ci;
                        memset(x->pop, 0, sizeof x->pop), memset(x->keep, 0, sizeof x->keep);
                    }
                    uint64_t *bits = snapped && st == PLAT_PROT_NONE ? w[*nwork - 1].keep : w[*nwork - 1].pop;
                    bits[pi / 64] |= 1ull << (pi % 64);
                }
            from = n == MAX_RUNS ? runs[n - 1][1] : end;
        }
    }
    // Chunks that held data before and none now still need a pass: their pages went back to zero.
    for (uint64_t ci = 0; ci < r->size / CHUNK; ci++) {
        if (!r->hash[ci]) continue;
        int listed = 0;
        for (uint64_t k = *nwork; k-- > 0 && w[k].r == r && w[k].ci >= ci;)
            if (w[k].ci == ci) { listed = 1; break; }
        if (listed) continue;
        if (*nwork == *cap) {
            *cap = *cap ? *cap * 2 : 256;
            if (!(w = realloc(w, sizeof *w * *cap))) rt_fatal("cap: out of memory for the page scan");
        }
        Work *x = &w[(*nwork)++];
        x->r = r, x->ci = ci;
        memset(x->pop, 0, sizeof x->pop), memset(x->keep, 0, sizeof x->keep);
    }
    return w;
}

static uint64_t run_lo, run_hi;
static int run_zero;
static void flush_run(CapOut *o) {
    if (run_hi == run_lo) return;
    uint64_t hdr[2] = {run_lo, run_hi - run_lo};
    if (run_zero) cap_out(o, CAP_ZERO, hdr, sizeof hdr, NULL, 0);
    else cap_out(o, CAP_MEM, hdr, sizeof hdr, (const void *)(uintptr_t)run_lo, run_hi - run_lo);
    run_lo = run_hi = 0;
}
static void emit(CapOut *o, uint64_t page, int zero) {
    if (run_hi != run_lo && (page != run_hi || zero != run_zero || run_hi - run_lo >= (64ull << 20))) flush_run(o);
    if (run_hi == run_lo) run_lo = page, run_zero = zero;
    run_hi = page + PAGE;
}

uint64_t cap_mem_delta(CapOut *o) {
    regions_init();
    static Work *w;
    static uint64_t cap;
    uint64_t n = 0;
    // Pages listed as committed stay so until their bytes are written out: the game decommits heap pages on its
    // threads meanwhile (a capture from present 2 read a page given back in page_hash, frames 86 and 114). Not for the
    // snapshot, which reads trapped pages: their fault takes the same lock.
    int held = snapped;
    if (held) mem_hold_layout();
    for (int i = 0; i < nregions; i++) w = collect(&regions[i], &n, w, &cap);
    volatile uint64_t next = 0;
    Job j = {w, n, &next};
    PlatThread *t[THREADS];
    for (int i = 0; i < THREADS; i++) t[i] = plat_thread_new(hash_worker, &j, "cap hash");
    for (int i = 0; i < THREADS; i++) plat_thread_join(t[i]);
    // Records in address order: the bytes are read again here, after hashing; a page the game changes in between
    // goes out with its newer bytes and is written again at the next delta.
    uint64_t written = 0;
    for (uint64_t k = 0; k < n; k++) {
        Work *x = &w[k];
        uint64_t **slot = &x->r->hash[x->ci], base = x->r->base + x->ci * CHUNK;
        for (uint64_t i = 0; i < CPAGES; i++) {
            uint64_t was = *slot ? (*slot)[i] : 0;
            if (x->now[i] == was) continue;
            if (!*slot && !(*slot = calloc(CPAGES, sizeof **slot))) rt_fatal("cap: out of memory for the page hashes");
            (*slot)[i] = x->now[i];
            emit(o, base + i * PAGE, !x->now[i]);
            written++;
        }
    }
    flush_run(o);
    if (held) mem_release_layout();
    snapped = 1;
    return written;
}

int cap_mem_snapshot(CapOut *o) {
    uint64_t probe[1][2];
    if (plat_mem_populated(mem_layout()->heap.base, PAGE, probe, 1) == UINT64_MAX) return 0;
    cap_mem_delta(o);
    return 1;
}

// Whole pages: a record may start inside one (cap_record.c: a pool object).
void cap_mem_commit(uint64_t addr, uint64_t len) {
    uint64_t lim = align_dn(addr + len + PAGE - 1, PAGE);
    addr = align_dn(addr, PAGE);
    for (uint64_t a = addr, end; a < lim; a = end) {
        int st = plat_mem_state(a, lim, &end);
        if (st == PLAT_MEM_FREE) rt_fatal("cap: guest page %#llx is not reserved here", (unsigned long long)a);
        if (st == PLAT_MEM_RESERVED && !plat_mem_commit(a, end - a))
            rt_fatal("cap: cannot commit %#llx+%#llx", (unsigned long long)a, (unsigned long long)(end - a));
        if (st == PLAT_PROT_NONE || st == PLAT_PROT_READ) plat_mem_protect(a, end - a, PLAT_PROT_RW);
    }
}

// A page under a read trap (memory.c: a GPU write-back still to fill it) or an MKW_EARLY_READ watch is released first,
// as a game write would: cap_mem_commit made it read-write behind the trap's back and the page kept its trapped mark,
// so the shadow sync and merge skipped the bytes restored into it (the black bushes of build/cap/race in about a third of
// the replays, depending on when the GPU finished: 0 of 6 with MKW_SHADOW_MERGE=0 or MKW_READ_TRAPS=0).
void cap_mem_restore(const void *data, uint64_t addr, uint64_t len) {
    mem_untrap(addr, len);
    cap_mem_commit(addr, len);
    memcpy((void *)(uintptr_t)addr, data, len);
}

void cap_mem_zero(uint64_t addr, uint64_t len) {
    mem_untrap(addr, len);
    cap_mem_commit(addr, len);
    memset((void *)(uintptr_t)addr, 0, len);
}
