// pack.h: one packed dump, .nup (nup.c) or .nx (nx.cpp), behind one interface for romfs.c and --unpack.
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "nup.h"
#include "pack.h"

struct Pack {
    char *path;
    Nup *nup;
    NxPack *nx;
    Pack *next;
};

static PlatMutex lock = PLAT_MUTEX_INIT;
static Pack *opened;

int pack_is(const char *path) { return nup_is(path) || nx_is(path); }

Pack *pack_open(const char *path) {
    plat_mutex_lock(&lock);
    Pack *p = opened;
    while (p && strcmp(p->path, path)) p = p->next;
    if (!p) {
        Nup *nup = nup_is(path) ? nup_open(path) : NULL;
        NxPack *nx = !nup && nx_is(path) ? nx_open(path) : NULL;
        if ((nup || nx) && (p = calloc(1, sizeof *p)) && (p->path = strdup(path))) {
            p->nup = nup, p->nx = nx, p->next = opened;
            opened = p;
        } else if (p) {
            free(p);
            p = NULL;
        }
        if (!nup && !nx) rt_log("pack %s: neither a .nup nor a .nx package", path);
    }
    plat_mutex_unlock(&lock);
    return p;
}

const char *pack_format(Pack *p) { return p->nup ? "nup" : "nx"; }

size_t pack_entries(Pack *p, int part, const PackEntry **out) {
    return p->nup ? nup_entries(p->nup, part, out) : nx_entries(p->nx, part, out);
}

PlatFile *pack_file(Pack *p) { return p->nup ? nup_file(p->nup) : NULL; }

uint8_t *pack_read_whole(Pack *p, const PackEntry *e) {
    if (e->kind == PACK_WHOLE) return nup_read(p->nup, e);
    uint8_t *dst = malloc(e->size ? e->size : 1);
    if (dst && pack_read_at(p, e, 0, dst, e->size)) return dst;
    free(dst);
    return NULL;
}

int pack_read_at(Pack *p, const PackEntry *e, uint64_t off, void *buf, uint64_t len) {
    if (off > e->size || len > e->size - off) return 0;
    if (e->kind == PACK_READ) return nx_read_at(p->nx, e, off, buf, len);
    if (e->kind != PACK_RAW) return 0;   // PACK_WHOLE: pack_read_whole only
    for (uint64_t done = 0; done < len;) {
        uint32_t want = len - done > (8u << 20) ? (8u << 20) : (uint32_t)(len - done);
        int64_t got = plat_file_pread(nup_file(p->nup), (uint8_t *)buf + done, want, e->offset + off + done);
        if (got <= 0) return 0;
        done += (uint64_t)got;
    }
    return 1;
}
