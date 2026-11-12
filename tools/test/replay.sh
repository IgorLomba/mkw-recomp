#!/usr/bin/env bash
# Replay test without the game: a capture (MKW_CAPTURE, runtime/gfx/cap) replayed serially and with MKW_REPLAY_PAR,
# each present saved as PNG and the two compared with tools/imagen/dircmp.py. Seconds per run instead of a run to the
# race (CONTRIBUTING.md, back end changes). A capture with the game's images (CAPTURE_DIR/ref) is compared with them too.
# Extra MKW_* pass through to both replays; MODES chooses what is replayed; REPEAT=N replays chunked N more times.
# Usage: tools/test/replay.sh CAPTURE_DIR [NAME]   (CAPTURE_DIR holds capture.zst; output in CAPTURE_DIR/NAME/)
set -euo pipefail
cd "$(dirname "$0")/../.."
cap=${1:?usage: replay.sh CAPTURE_DIR [NAME]} name=${2:-t}
out=$cap/$name
if [ -n "$(find runtime -newer build/mkw \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) -print -quit)" ]; then
  echo "${0##*/}: runtime/ is newer than build/mkw: build first" >&2; exit 1
fi
# Each replay holds the whole capture and its GPU memory (UMA: system RAM): ~8.2 GB at peak, serial or in chunks since
# released textures go with the parts sent (pt-ser/pt-par, 07/11/2026; before, chunked runs reached ~12 GB and one
# started with 8 GB free took the system out of memory). 9500 leaves ~1.3 GB.
tools/run/gpu_ok.sh || exit 1   # no run on a GPU left where the next fault can freeze the desktop
# The pre-submit checks stop the run before a fault reaches the GPU (vk_guard.c); MKW_GPU_GUARD=0 to measure without.
export MKW_GPU_GUARD=${MKW_GPU_GUARD:-1}
avail=$(tools/run/avail.sh --wait "${MIN_AVAIL:-9500}")   # WAIT_MEM=<min>: wait for it instead of refusing
[ "$avail" -ge "${MIN_AVAIL:-9500}" ] || { echo "replay.sh: only $avail MB available (need ${MIN_AVAIL:-9500}): not starting" >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
status=0
for mode in ${MODES:-serial par}; do
  par=0; [ "$mode" = par ] && par=1
  if ! JOB_MEM=${JOB_MEM:-12G} MKW_REPLAY_PAR=$par tools/run/job.sh run "replay-$mode" \
      build/mkw --replay "$cap" --out "$out/$mode" --tag f > "$out/$mode.log" 2>&1; then
    echo "$mode: FAILED ($out/$mode.log)"; grep -E "FATAL|fatal|validation" "$out/$mode.log" | head -5; status=1
  fi
  grep -E "^replay:|replay in chunks:" "$out/$mode.log" | cut -c1-260 || true
done
# Against the game's own image of each frame (CAPTURE_DIR/ref, saved by the capture), then serial against chunked.
for mode in ${MODES:-serial par}; do
  if [ -d "$cap/ref" ] && [ -d "$out/$mode" ]; then
    echo "== $mode against the game"; uv run tools/imagen/dircmp.py "$cap/ref" "$out/$mode" --worst 3 --out "$out/cmp-ref-$mode"
  fi
done
if [ -d "$out/serial" ] && [ -d "$out/par" ]; then
  echo "== serial against par"; uv run tools/imagen/dircmp.py "$out/serial" "$out/par" --worst 3 --out "$out/cmp"
fi
# REPEAT=N: N more chunked replays, each against the serial one: a race between the recording threads shows in some
# of them only (the red frame: 1 of 60 race frames, img-par3).
for i in $(seq 1 "${REPEAT:-0}"); do
  JOB_MEM=${JOB_MEM:-12G} MKW_REPLAY_PAR=1 tools/run/job.sh run "replay-par$i" \
    build/mkw --replay "$cap" --out "$out/par$i" --tag f > "$out/par$i.log" 2>&1 || { echo "par$i: FAILED"; status=1; }
  echo "== serial against par$i: $(uv run tools/imagen/dircmp.py "$out/serial" "$out/par$i" --worst 1 | grep median)"
done
exit $status
