// The .nx backend of pack.h: NXFormat archives (fab918/nxformat), read with its own C++17 reader
// (third_party/nxformat/nx_reader.hpp @ 208b815, MIT). An archive holds snapshots (base game, updates, DLC), each a
// complete tree with exefs/ and romfs/ at its root; files are split into blocks of at most 1 MiB, each raw or one zstd
// frame, so any range of a file is read by decoding only the blocks it touches (PACK_READ).
// Snapshot read: the executable one (base, update or standalone update, never DLC) with the highest version that has
// exefs/main, as the newest complete game in the archive.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include <zstd.h>

#include "pack.h"
#include "third_party/nxformat/nx_reader.hpp"

extern "C" void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

namespace {

// Positional reads on the shared handle: the reader calls this from every thread that reads a file.
struct Source final : nx::RandomAccessSource {
    PlatFile *f;
    uint64_t n;
    std::uint64_t size() const noexcept override { return n; }
    bool read_at(std::uint64_t off, std::uint8_t *dst, std::size_t len) override {
        for (std::size_t done = 0; done < len;) {
            uint32_t want = len - done > (8u << 20) ? (8u << 20) : (uint32_t)(len - done);
            int64_t got = plat_file_pread(f, dst + done, want, off + done);
            if (got <= 0) return false;
            done += (std::size_t)got;
        }
        return true;
    }
};

// One standard frame, exactly consumed, its declared size the expected output (the reader's contract, READER.md);
// one decoder context per thread, as reads run in parallel.
bool zstd_block(void *, const std::uint8_t *frame, std::size_t size, std::uint8_t *out, std::size_t cap,
                std::size_t *written, std::size_t *consumed) {
    thread_local ZSTD_DCtx *z = ZSTD_createDCtx();
    if (!z || ZSTD_findFrameCompressedSize(frame, size) != size) return false;
    unsigned long long declared = ZSTD_getFrameContentSize(frame, size);
    if (declared == ZSTD_CONTENTSIZE_UNKNOWN || declared == ZSTD_CONTENTSIZE_ERROR || declared > cap) return false;
    std::size_t r = ZSTD_decompressDCtx(z, out, cap, frame, size);
    if (ZSTD_isError(r) || r != declared) return false;
    *written = r;
    *consumed = size;
    return true;
}

}  // namespace

struct NxPack {
    Source src;
    nx::Archive archive;
    std::uint32_t snapshot = 0;
    std::vector<PackEntry> part[2];
};

namespace {

void walk(NxPack *p, nx::DirectoryHandle dir, const std::string &prefix, std::vector<PackEntry> &out) {
    p->archive.enumerate(dir, [&](const nx::DirectoryEntryView &v) {
        std::string path = prefix.empty() ? std::string(v.name) : prefix + "/" + std::string(v.name);
        if (v.type == nx::EntryType::directory) {
            walk(p, v.directory, path, out);
        } else {
            PackEntry e{};
            e.name = strdup(path.c_str());
            e.size = e.stored = v.size;
            e.kind = PACK_READ;
            e.id = v.file.value;
            out.push_back(e);
        }
        return true;
    });
}

}  // namespace

extern "C" int nx_is(const char *path) {
    PlatFile *f = path && plat_file_exists(path) ? plat_file_open(path) : nullptr;
    char m[4] = {0};
    int ok = f && plat_file_pread(f, m, 4, 0) == 4 && !memcmp(m, "NXAR", 4);
    if (f) plat_file_close(f);
    return ok;
}

extern "C" NxPack *nx_open(const char *path) {
    PlatFile *f = plat_file_open(path);
    PlatFileId id;
    if (!f || !plat_file_id(f, &id)) {
        rt_log("NX %s: cannot open", path);
        if (f) plat_file_close(f);
        return nullptr;
    }
    NxPack *p = new (std::nothrow) NxPack{{}, {}, 0, {}};
    if (!p) return nullptr;
    p->src.f = f;
    p->src.n = id.size;
    nx::OpenOptions o;
    o.zstd = zstd_block;
    auto opened = nx::Archive::open(p->src, o);
    const char *err = nullptr;
    std::string why;
    if (!opened) err = "open", why = opened.error.message;
    else {
        p->archive = std::move(*opened.value);
        nx::Status st = p->archive.mount();
        if (!st) err = "mount", why = st.error.message;
    }
    if (!err) {
        const auto &snaps = p->archive.snapshots();
        int best = -1;
        for (std::uint32_t i = 0; i < snaps.size(); i++) {
            if (snaps[i].type == 2 || !p->archive.resolve_file(i, "exefs/main")) continue;   // 2: DLC
            if (best < 0 || snaps[i].numeric_version >= snaps[(std::uint32_t)best].numeric_version) best = (int)i;
        }
        if (best < 0) err = "snapshot", why = "no game snapshot with exefs/main";
        else p->snapshot = (std::uint32_t)best;
    }
    for (int part = 0; !err && part < 2; part++) {
        auto root = p->archive.root_directory(p->snapshot);
        auto dir = root ? p->archive.lookup(*root.value, part ? "romfs" : "exefs") : decltype(p->archive.lookup({}, "")){};
        if (!dir || dir.value->type != nx::EntryType::directory) continue;   // a part may be absent (an exefs-only dump)
        walk(p, dir.value->directory, "", p->part[part]);
        std::sort(p->part[part].begin(), p->part[part].end(),
                  [](const PackEntry &a, const PackEntry &b) { return strcmp(a.name, b.name) < 0; });
    }
    if (err) {
        rt_log("NX %s: %s failed: %s", path, err, why.c_str());
        plat_file_close(f);
        delete p;
        return nullptr;
    }
    const auto &s = p->archive.snapshots()[p->snapshot];
    rt_log("NX %s: snapshot %u of %zu (type %u, version %u), %zu exefs and %zu romfs files", path, p->snapshot,
           p->archive.snapshots().size(), s.type, s.numeric_version, p->part[0].size(), p->part[1].size());
    return p;
}

extern "C" size_t nx_entries(NxPack *p, int part, const PackEntry **out) {
    *out = p->part[part].data();
    return p->part[part].size();
}

extern "C" int nx_read_at(NxPack *p, const PackEntry *e, uint64_t off, void *buf, uint64_t len) {
    // the reader caps one request (16 MiB by default): longer reads go in pieces
    for (uint64_t done = 0; done < len;) {
        std::size_t want = len - done > (8u << 20) ? (8u << 20) : (std::size_t)(len - done);
        auto r = p->archive.read_file(nx::FileId{e->id}, off + done, want, (std::uint8_t *)buf + done, want);
        if (!r || r.bytes != want) {
            rt_log("NX: %s at %llu: %s", e->name, (unsigned long long)(off + done), r.error.message.c_str());
            return 0;
        }
        done += want;
    }
    return 1;
}
