#!/usr/bin/env bash
# Sends one command to a run started with tools/run/live.sh and prints its answer from the log; shot and dump also wait
# until the file or the dump is written (60 s at most); shot also writes PATH-s.png, 1280 wide, to look at.
# Usage: tools/run/ctl.sh NAME COMMAND...
#   frame | shot PATH | dump DIR [ubo|lut] | flight | pause | step [N] | resume | tex ADDR | mem ADDR SIZE PATH | press <ticks> <button> [hold] | press <ticks> LSTICK x y [hold] (a "p<N> " prefix: for player N) | stop
set -euo pipefail
cd "$(dirname "$0")/../.."
n=${1:?usage: ctl.sh NAME COMMAND...}; shift
log=build/run/$n.log fifo=build/run/$n.ctl
# A run is a service (job start) or a scope named job-NAME-PID (job run, as race.sh starts it): stop whichever is live.
if [ "${1:-}" = stop ]; then
  units=$(systemctl --user list-units --no-legend --plain --state=active "job-$n.service" "job-$n-*.scope" 2>/dev/null | awk '{print $1}')
  if [ -z "$units" ]; then
    # Without a job runner job.sh starts the run with nohup, no unit: the run is the mkw writing build/run/NAME.log
    # (11/11/2026: "not running" left each run up to its timeout, and the next one refused to start).
    pids=$(for p in $(pgrep -x mkw); do [ "$(readlink "/proc/$p/fd/1")" = "$PWD/$log" ] && echo "$p"; done)
    [ -n "$pids" ] || { echo "ctl: $n is not running" >&2; exit 1; }
    kill $pids; echo "ctl: $n stopped (pid $pids)"; exit 0
  fi
  systemctl --user stop $units; echo "ctl: $n stopped ($units)"; exit 0
fi
[ -p "$fifo" ] && pgrep -x mkw >/dev/null || { echo "ctl: $n is not running" >&2; exit 1; }
case "${1:-}" in
shot | dump) [[ "${2:-}" = /* ]] || set -- "$1" "$PWD/$2" "${@:3}" ;;
mem) [[ "${4:-}" = /* ]] || set -- "$1" "$2" "$3" "$PWD/$4" ;;
esac
before=$(wc -l < "$log")
printf '%s\n' "$*" > "$fifo"
for _ in $(seq 100); do
    ans=$(tail -n +"$((before + 1))" "$log" | grep -m1 -E '(ctl|freeze): ' || true)
    [ -n "$ans" ] && break
    sleep 0.1
done
echo "${ans:-ctl: no answer (is the game past its boot?)}"
case "$1" in
shot) for _ in $(seq 600); do
          [ -s "$2" ] || { sleep 0.1; continue; }
          # A 1280-wide copy to look at: the full shot is 2560x1440 (resized by hand ~6 times to 01/11/2026).
          small=${2%.png}-s.png
          ffmpeg -v error -y -i "$2" -vf scale=1280:-2 "$small" && echo "ctl: wrote $2 (to look at: $small)" || echo "ctl: wrote $2"
          exit 0
      done ;;
dump) for _ in $(seq 600); do
          done_line=$(tail -n +"$((before + 1))" "$log" | grep -m1 'vk_dump: frame [0-9]*: ' || true)
          [ -n "$done_line" ] && { echo "$done_line"; exit 0; }
          sleep 0.1
      done ;;
*) exit 0 ;;
esac
echo "ctl: timed out waiting for $1" >&2; exit 1
