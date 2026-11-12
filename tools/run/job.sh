#!/usr/bin/env bash
# Run a heavy command (a game run, a replay) through the host's job runner when there is one, else directly.
# Why: our machine runs them under ~/.agents/scripts/job (memory cap, one at a time, systemd unit); a fresh clone has
# no such runner and the scripts must still work. JOB_RUNNER=path picks another runner with the same interface.
# Usage: tools/run/job.sh run NAME CMD [ARGS...]     foreground, the command's exit status
#        tools/run/job.sh start NAME CMD [ARGS...]   background (the command redirects its own output)
set -euo pipefail

mode=$1 name=$2; shift 2
runner=${JOB_RUNNER:-$HOME/.agents/scripts/job}
if [[ -x $runner ]]; then
    exec "$runner" "$mode" "$name" "$@"
fi
# Without a runner, a systemd user unit when there is one, with the runner's cap: JOB_MEM (default 7G) and no swap,
# so a run that outgrows it is killed instead of pushing the desktop into swap. 11/11/2026: a run started with nohup,
# no cap, stalled the whole desktop (grey screen, forced reboot) at its boot; the unit also lets ctl.sh stop it.
# show-environment, not is-system-running: that one fails on a "degraded" session (any failed unit), common on desktops
if command -v systemd-run > /dev/null && systemctl --user show-environment > /dev/null 2>&1; then
    unit=(--user --quiet --collect -p MemoryMax="${JOB_MEM:-7G}" -p MemorySwapMax=0)
    for v in DISPLAY WAYLAND_DISPLAY XDG_RUNTIME_DIR XDG_SESSION_TYPE; do [[ -n ${!v:-} ]] && unit+=(-E "$v"); done
    case $mode in
        run) exec systemd-run "${unit[@]}" --scope --unit="job-$name-$$" "$@" ;;
        start) exec systemd-run "${unit[@]}" --same-dir --unit="job-$name" "$@" ;;
    esac
fi
case $mode in
    run) exec "$@" ;;
    start) nohup "$@" > /dev/null 2>&1 < /dev/null & ;;
    *) echo "job.sh: unknown mode $mode (run|start)" >&2; exit 2 ;;
esac
