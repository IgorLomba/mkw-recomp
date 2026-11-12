# /// script
# requires-python = ">=3.12"
# ///
"""The game's own texts, one language at a time, as "label<TAB>text" lines: the vocabulary a menu translation should
reuse (Time Trials, Free Roam, the cup and course names...). Reads romfs/Mals/<region><lang>/*.msarc (SARC archives of
MSBT files) from the game copy; control tags (0x0E...0x0F sequences) are dropped.

Usage:
  uv run tools/i18n/game_texts.py EUde                 every text of that folder
  uv run tools/i18n/game_texts.py JPja -o build/i18n/  one file per folder given, in that directory
  uv run tools/i18n/game_texts.py --list               the folders the game has
"""

import argparse
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def game_dir() -> Path:
    return Path(os.environ.get("MKW_GAME", ROOT / "game"))


def sarc_files(data: bytes) -> dict[str, bytes]:
    """The files of a little-endian SARC archive, by name."""
    if data[:4] != b"SARC" or data[6:8] != b"\xff\xfe":
        raise ValueError("not a little-endian SARC")
    (hdr_len,) = struct.unpack_from("<H", data, 4)
    (data_start,) = struct.unpack_from("<I", data, 12)
    sfat = hdr_len
    if data[sfat : sfat + 4] != b"SFAT":
        raise ValueError("no SFAT")
    sfat_len, count = struct.unpack_from("<HH", data, sfat + 4)
    sfnt = sfat + sfat_len + count * 16
    if data[sfnt : sfnt + 4] != b"SFNT":
        raise ValueError("no SFNT")
    names = sfnt + 8
    out = {}
    for i in range(count):
        _hash, attr, start, end = struct.unpack_from("<IIII", data, sfat + sfat_len + i * 16)
        name = f"#{i}"
        if attr & 0x01000000:
            off = names + (attr & 0xFFFF) * 4
            name = data[off : data.index(b"\0", off)].decode()
        out[name] = data[data_start + start : data_start + end]
    return out


def msbt_texts(data: bytes) -> list[tuple[str, str]]:
    """(label, text) of an MSBT in UTF-16LE, in label order."""
    if data[:8] != b"MsgStdBn" or data[8:10] != b"\xff\xfe":
        raise ValueError("not a little-endian MSBT")
    (sections,) = struct.unpack_from("<H", data, 0x0E)
    off = 0x20
    labels: dict[int, str] = {}
    texts: list[str] = []
    for _ in range(sections):
        magic = data[off : off + 4]
        (size,) = struct.unpack_from("<I", data, off + 4)
        body = off + 16
        if magic == b"LBL1":
            (groups,) = struct.unpack_from("<I", data, body)
            for g in range(groups):
                n, p = struct.unpack_from("<II", data, body + 4 + g * 8)
                q = body + p
                for _ in range(n):
                    ln = data[q]
                    name = data[q + 1 : q + 1 + ln].decode()
                    (idx,) = struct.unpack_from("<I", data, q + 1 + ln)
                    labels[idx] = name
                    q += 1 + ln + 4
        elif magic == b"TXT2":
            (n,) = struct.unpack_from("<I", data, body)
            offs = [struct.unpack_from("<I", data, body + 4 + i * 4)[0] for i in range(n)] + [size]
            for i in range(n):
                texts.append(clean(data[body + offs[i] : body + offs[i + 1]]))
        off = body + ((size + 15) & ~15)
    return [(labels.get(i, f"#{i}"), t) for i, t in enumerate(texts)]


def clean(raw: bytes) -> str:
    """UTF-16LE text without its control tags (0x0E group type len args, 0x0F group type) or the final NUL."""
    units = struct.unpack(f"<{len(raw) // 2}H", raw[: len(raw) // 2 * 2])
    out, i = [], 0
    while i < len(units):
        u = units[i]
        if u == 0x0E:
            i += 4 + units[i + 3] // 2 if i + 3 < len(units) else len(units)
            continue
        if u == 0x0F:
            i += 3
            continue
        if u == 0:
            break
        out.append(u)
        i += 1
    return struct.pack(f"<{len(out)}H", *out).decode("utf-16-le", "replace").replace("\n", "\\n")


def folder_lines(folder: Path) -> list[str]:
    lines = []
    for arc in sorted(folder.glob("*.msarc")):
        for name, blob in sorted(sarc_files(arc.read_bytes()).items()):
            if not name.endswith(".msbt"):
                continue
            for label, text in msbt_texts(blob):
                if text:
                    lines.append(f"{arc.stem}/{name.removesuffix('.msbt')}/{label}\t{text}")
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("folders", nargs="*", help="romfs/Mals folders, e.g. EUde JPja")
    ap.add_argument("-o", "--out", type=Path, help="write <folder>.tsv files here instead of stdout")
    ap.add_argument("--list", action="store_true", help="the folders the game has")
    args = ap.parse_args()
    mals = game_dir() / "romfs" / "Mals"
    if args.list or not args.folders:
        print(" ".join(sorted(p.name for p in mals.iterdir() if p.is_dir())))
        return 0
    for f in args.folders:
        lines = folder_lines(mals / f)
        if args.out:
            args.out.mkdir(parents=True, exist_ok=True)
            (args.out / f"{f}.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"{f}: {len(lines)} texts", file=sys.stderr)
        else:
            sys.stdout.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
