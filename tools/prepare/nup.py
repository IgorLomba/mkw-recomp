"""Read a Switch 2 .nup package: the dump format that packs a title's exefs/ and romfs/ into one file.

Why: players' dumps come out as .nup; the setup (prepare.py) and the shader extractor (tools/shaders/extract.py) read
them in place, and the runtime reads the RomFS from one too (runtime/sdk/fs/nup.c, which documents the layout).

Usage: uv run tools/prepare/nup.py list GAME.nup
       uv run tools/prepare/nup.py extract GAME.nup DIR [--part exefs|romfs]   (DIR/exefs, DIR/romfs)
As a module: Nup(path).files("romfs") -> {path: Entry}, .read(entry) -> bytes, .extract("exefs", dir).
"""

import argparse
import struct
import sys
from compression import zstd
from dataclasses import dataclass
from pathlib import Path

MAGIC = b"NUP\x01"
CHUNK = 8 << 20
ZSTD_OPTIONS = {zstd.DecompressionParameter.window_log_max: 31}  # frames written with zstd --long


@dataclass(frozen=True)
class Entry:
    offset: int  # of the stored bytes, in the package
    size: int
    stored: int
    zstd: bool


def is_nup(path: Path) -> bool:
    try:
        with open(path, "rb") as f:
            return f.read(4) == MAGIC
    except OSError:
        return False


class Nup:
    """The package's newest title (the last in its table: an update's comes after the base game's)."""

    def __init__(self, path: Path):
        self.path = Path(path)
        with open(self.path, "rb") as f:
            head = f.read(12)
            if head[:4] != MAGIC:
                raise ValueError(f"{path}: not a NUP package")
            toc = struct.unpack_from("<I", head, 8)[0]
            f.seek(toc)
            th = f.read(0x30)
            if th[:4] != b"TOC\x01":
                raise ValueError(f"{path}: no table of contents")
            nt = struct.unpack_from("<I", th, 4)[0]
            self._nd, nf, pool, pool_size = struct.unpack_from("<QQQQ", th, 8)
            f.seek(toc)
            self._t = f.read(pool + pool_size)
        self._pool = pool
        self._dirs = 0x30 + nt * 0x40
        self._files = self._dirs + self._nd * 24
        self._children = self._files + nf * 80
        tid, _, self._exefs, self._romfs, self.version = struct.unpack_from("<QIIII", self._t, 0x30 + (nt - 1) * 0x40)
        self.title_id = f"{tid:016X}"
        self._parts: dict[str, dict[str, Entry]] = {}

    def _name(self, rec: int) -> str:
        off, n = struct.unpack_from("<II", self._t, rec)
        return self._t[self._pool + off : self._pool + off + n].decode("utf-8")

    def files(self, part: str) -> dict[str, Entry]:
        """{path under the part: Entry} of "exefs" or "romfs", sorted by path."""
        if part not in self._parts:
            out: dict[str, Entry] = {}
            stack = [(self._exefs if part == "exefs" else self._romfs, "")]
            while stack:
                d, prefix = stack.pop()
                _, _, first, count = struct.unpack_from("<4I", self._t, self._dirs + d * 24)
                for k in range(first, first + count):
                    c = struct.unpack_from("<I", self._t, self._children + k * 4)[0]
                    if c < self._nd:
                        stack.append((c, prefix + self._name(self._dirs + c * 24) + "/"))
                        continue
                    rec = self._files + (c - self._nd) * 80
                    size, off, stored = struct.unpack_from("<QQQ", self._t, rec + 8)
                    codec = self._t[rec + 0x40]
                    if codec > 1:
                        raise ValueError(f"{self.path}: unknown codec {codec} for {prefix + self._name(rec)}")
                    out[prefix + self._name(rec)] = Entry(off, size, stored, codec == 1)
            self._parts[part] = dict(sorted(out.items()))
        return self._parts[part]

    def read(self, e: Entry) -> bytes:
        with open(self.path, "rb") as f:
            f.seek(e.offset)
            data = f.read(e.stored)
        if len(data) != e.stored:
            raise ValueError(f"{self.path}: short read at {e.offset:#x}")
        if e.zstd:
            data = zstd.decompress(data, options=ZSTD_OPTIONS)
        if len(data) != e.size:
            raise ValueError(f"{self.path}: a file at {e.offset:#x} has {len(data)} bytes, its table says {e.size}")
        return data

    def extract(self, part: str, dst: Path, progress=None) -> None:
        """The part's files into dst; files already there with the right size are kept (resume). progress(done, total)."""
        files = self.files(part)
        with open(self.path, "rb") as f:
            for n, (name, e) in enumerate(files.items(), 1):
                out = Path(dst, name)
                if not (out.is_file() and out.stat().st_size == e.size):
                    out.parent.mkdir(parents=True, exist_ok=True)
                    tmp = out.with_name(out.name + ".part")
                    with open(tmp, "wb") as o:
                        f.seek(e.offset)
                        left = e.stored
                        dec = zstd.ZstdDecompressor(options=ZSTD_OPTIONS) if e.zstd else None
                        while left:
                            buf = f.read(min(left, CHUNK))
                            if not buf:
                                raise ValueError(f"{self.path}: short read in {name}")
                            left -= len(buf)
                            o.write(dec.decompress(buf) if dec else buf)
                    if tmp.stat().st_size != e.size:
                        raise ValueError(f"{self.path}: {name} has {tmp.stat().st_size} bytes, its table says {e.size}")
                    tmp.replace(out)
                if progress and (n % 500 == 0 or n == len(files)):
                    progress(n, len(files))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list").add_argument("nup", type=Path)
    ex = sub.add_parser("extract")
    ex.add_argument("nup", type=Path)
    ex.add_argument("out", type=Path)
    ex.add_argument("--part", choices=("exefs", "romfs"), action="append")
    a = ap.parse_args()
    n = Nup(a.nup)
    print(f"title {n.title_id} v{n.version}: {len(n.files('exefs'))} exefs and {len(n.files('romfs'))} romfs files")
    if a.cmd == "extract":
        for part in a.part or ("exefs", "romfs"):
            n.extract(part, a.out / part, lambda d, t: print(f"  {part} {d}/{t}", end="\r", file=sys.stderr))
            print(f"{part}: done{' ' * 20}")


if __name__ == "__main__":
    main()
