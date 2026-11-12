// A packed dump: one file that holds a title's exefs/ and romfs/, read in place instead of from loose folders.
// Two formats: .nup (nup.c, a55uka/nup) and .nx (nx.cpp, fab918/nxformat). romfs.c and --unpack see only this.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "platform/platform.h"

enum { PACK_EXEFS = 0, PACK_ROMFS = 1 };
// How a file's bytes are reached: in place at its offset in the package (pack_file), only as a whole (one .nup zstd
// frame: no way to start in the middle), or at any offset through pack_read_at (.nx blocks, each its own frame).
enum { PACK_RAW = 0, PACK_WHOLE = 1, PACK_READ = 2 };

typedef struct {
    char *name;               // path under the part's root, '/'-separated
    uint64_t offset;          // PACK_RAW, PACK_WHOLE: absolute offset of the stored bytes in the package
    uint64_t size, stored;    // file size, and bytes stored in the package (differs when compressed)
    int kind;                 // PACK_RAW, PACK_WHOLE or PACK_READ
    uint32_t id;              // PACK_READ: the format's own file handle
} PackEntry;

typedef struct Pack Pack;

int pack_is(const char *path);   // a regular file that starts with the magic of one of the formats
// The package at path, parsed once and kept until exit (NULL and a log line when it is not valid).
Pack *pack_open(const char *path);
const char *pack_format(Pack *p);   // "nup" or "nx", for the log
// The files of a part (PACK_EXEFS, PACK_ROMFS), sorted by name; the array belongs to the Pack.
size_t pack_entries(Pack *p, int part, const PackEntry **out);
PlatFile *pack_file(Pack *p);       // the package, shared, for PACK_RAW rows; positional reads only
// A whole file, decompressed when stored compressed; malloc'd (free() it), NULL on a read or format error.
uint8_t *pack_read_whole(Pack *p, const PackEntry *e);
// len bytes at off of a PACK_RAW or PACK_READ file into buf; 0 on a read or format error. Thread-safe.
int pack_read_at(Pack *p, const PackEntry *e, uint64_t off, void *buf, uint64_t len);

int pack_unpack_main(int argc, char **argv);   // mkw --unpack PACKAGE DEST [PREFIX...] (pack_unpack.c)

// The .nx backend (nx.cpp), behind pack.c.
typedef struct NxPack NxPack;
int nx_is(const char *path);
NxPack *nx_open(const char *path);
size_t nx_entries(NxPack *n, int part, const PackEntry **out);
int nx_read_at(NxPack *n, const PackEntry *e, uint64_t off, void *buf, uint64_t len);

#ifdef __cplusplus
}
#endif
