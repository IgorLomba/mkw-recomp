// Read-only access to a Switch 2 .nup package (the dump format that packs a title's exefs/ and romfs/ into one file),
// so the RomFS can be read in place instead of from a 23 GB extracted folder (romfs.c).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "platform/platform.h"

enum { NUP_EXEFS = 0, NUP_ROMFS = 1 };

typedef struct {
    char *name;               // path under the part's root, '/'-separated
    uint64_t offset;          // absolute offset of the stored bytes in the package
    uint64_t size, stored;    // file size, and bytes stored in the package (differs when compressed)
    int zstd;                 // stored as one zstd frame (window up to 2 GiB), else raw
} NupEntry;

typedef struct Nup Nup;

int nup_is(const char *path);                             // a regular file that starts with the NUP magic
// The package at path, parsed once and kept until exit (NULL and a log line when it is not a valid NUP).
// Its newest title (the last in the table, as an update's comes after the base game's) is the one read.
Nup *nup_open(const char *path);
// The files of a part (NUP_EXEFS, NUP_ROMFS), sorted by name; the array belongs to the Nup.
size_t nup_entries(Nup *n, int part, const NupEntry **out);
PlatFile *nup_file(Nup *n);                               // the package, shared; positional reads only
// A whole file of the package, decompressed when stored compressed; malloc'd (free() it), NULL on a read or format error.
uint8_t *nup_read(Nup *n, const NupEntry *e);
