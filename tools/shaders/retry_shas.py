"""Re-translate the given census sha1s and print each outcome, and update their rows of build/spirv/failed.tsv (the
pre-commit wants it no older than any result: without this, a commit of a few retranslated programs needed a whole
translate.py pass, 12/11/2026). Usage: retry_shas.py SHA1..."""
import csv
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/shaders"))
import translate as T

idx = {r["sha1"]: r["stage"] for r in csv.DictReader(open(ROOT / "build/shaders/index.csv"))}
jobs = [(s, idx[s], str(ROOT / "build/shaders"), str(ROOT / "build/spirv"), False, T.translator_hash()) for s in sys.argv[1:]]


def update_failed(outcomes: dict[str, str]) -> None:
    """failed.tsv (reason, programs; as translate.py writes it) with these programs' rows replaced."""
    path = ROOT / "build/spirv/failed.tsv"
    failed: dict[str, list[str]] = {}
    if path.exists():
        for line in path.read_text().splitlines():
            why, _, shas = line.partition("\t")
            failed[why] = [s for s in shas.split() if s not in outcomes]
    for sha, why in outcomes.items():
        if why != "ok":
            failed.setdefault(why, []).append(sha)
    path.write_text("".join(f"{w}\t{' '.join(s)}\n" for w, s in sorted(failed.items(), key=lambda x: -len(x[1])) if s))


if __name__ == "__main__":
    outcomes = {}
    with ProcessPoolExecutor(initializer=T.init) as pool:
        for s, r in pool.map(T.work, jobs):
            print(s[:12], r[:150])
            outcomes[s] = r
    update_failed(outcomes)
