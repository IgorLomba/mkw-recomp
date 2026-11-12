// mkw --unpack PACKAGE DEST [PREFIX...]: files of a packed dump (.nup or .nx, pack.h) written out as loose files under
// DEST (DEST/exefs/..., DEST/romfs/...), only those whose path starts with one of the PREFIXes ("exefs",
// "romfs/Shader") when any is given. Why: the setup (tools/prepare/prepare.py) needs exefs/ as files and the RomFS's
// shader archives, and for a .nx it has no reader of its own (NXFormat's is C++, here in nx.cpp); a .nup it reads
// itself (tools/prepare/nup.py) but this works for both.
// Prints "unpack: N files, B bytes" first, then "progress DONE TOTAL" (bytes) about every 1 % for the setup's bar.
// DEST "-": that first line only (the setup's count of the RomFS).
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pack.h"

static int wanted(const char *path, int nprefix, char **prefix) {
    if (!nprefix) return 1;
    for (int k = 0; k < nprefix; k++) {
        size_t n = strlen(prefix[k]);
        if (!strncmp(path, prefix[k], n) && (path[n] == 0 || path[n] == '/')) return 1;
    }
    return 0;
}

int pack_unpack_main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: mkw --unpack PACKAGE DEST [PREFIX...]\n");
        return 2;
    }
    Pack *p = pack_open(argv[0]);
    if (!p) {
        fprintf(stderr, "unpack: %s is not a readable .nup or .nx package (see the log)\n", argv[0]);
        return 1;
    }
    static const char *parts[2] = {"exefs", "romfs"};
    uint64_t files = 0, total = 0, done = 0, next = 0;
    char path[4200];
    for (int pass = 0; pass < 2; pass++) {   // pass 0 counts, pass 1 writes
        if (pass) printf("unpack: %" PRIu64 " files, %" PRIu64 " bytes\n", files, total), fflush(stdout);
        if (pass && !strcmp(argv[1], "-")) return 0;
        for (int part = 0; part < 2; part++) {
            const PackEntry *e;
            size_t n = pack_entries(p, part, &e);
            for (size_t i = 0; i < n; i++) {
                snprintf(path, sizeof path, "%s/%s", parts[part], e[i].name);
                if (!wanted(path, argc - 2, argv + 2)) continue;
                if (!pass) {
                    files++, total += e[i].size;
                    continue;
                }
                if (snprintf(path, sizeof path, "%s/%s/%s", argv[1], parts[part], e[i].name) >= (int)sizeof path) {
                    fprintf(stderr, "unpack: path too long: %s\n", e[i].name);
                    return 1;
                }
                char *slash = strrchr(path, '/');
                *slash = 0;
                plat_file_mkdir_p(path);
                *slash = '/';
                uint8_t *data = pack_read_whole(p, &e[i]);
                if (!data || !plat_file_write(path, data, e[i].size)) {
                    fprintf(stderr, "unpack: %s: %s\n", e[i].name, data ? "cannot write" : "cannot read");
                    free(data);
                    return 1;
                }
                free(data);
                done += e[i].size;
                if (done >= next || done == total) {
                    printf("progress %" PRIu64 " %" PRIu64 "\n", done, total);
                    fflush(stdout);
                    next = done + total / 100;
                }
            }
        }
    }
    return 0;
}
