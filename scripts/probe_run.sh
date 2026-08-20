#!/bin/sh
# Run an agent probe under a watchdog, and say WHY it ended.
#
# `pi -p` does not reliably exit: the 2026-08-20 core-surface run wrote its last
# file at 09:36, printed its closing summary, and was still resident at 10:01 at
# 0.0% CPU. Waiting on that is unbounded, and "still running" and "finished but
# will not exit" are the same observation from outside.
#
# They are NOT the same from inside. An agent thinking, or waiting on an API
# round trip, writes no files -- but its process still accrues CPU time as the
# stream is parsed. A finished-and-wedged one accrues NONE. So quiescence is
# scored on BOTH: zero CPU-time delta AND no write anywhere under the probe
# directory, sustained across the window. Either alone is a false positive
# waiting to happen -- see --selfcheck, which builds both kinds on purpose.
#
# Usage:  sh scripts/probe_run.sh <probe-dir> <prompt> [model]
#         QUIET=300 MAX=3600 sh scripts/probe_run.sh ...
# Exit:   0 the agent exited on its own | 0 killed after going quiet (work kept)
#         2 killed at the hard cap      | 3 bad usage
set -eu

QUIET="${QUIET:-300}"     # seconds of zero CPU AND zero writes before we call it done
MAX="${MAX:-3600}"        # hard cap, whatever it is doing
POLL="${POLL:-20}"

# CPU time (ticks) and newest mtime under $1 -- the two quiescence signals.
cputicks() { awk '{print $14 + $15}' "/proc/$1/stat" 2>/dev/null || echo -1; }
newest()   { find "$1" -type f -newermt "@0" -printf '%T@\n' 2>/dev/null | sort -rn | head -1; }

run_probe() {
    dir="$1"; prompt="$2"; model="$3"; log="$dir/pi-run.log"
    ( cd "$dir" && exec pi --model "$model" -p "$prompt" ) > "$log" 2>&1 &
    pid=$!
    start=$(date +%s); quiet_since=""; last_cpu=$(cputicks "$pid"); last_mt=$(newest "$dir")
    while kill -0 "$pid" 2>/dev/null; do
        sleep "$POLL"
        now=$(date +%s)
        if [ $((now - start)) -ge "$MAX" ]; then
            kill "$pid" 2>/dev/null || true
            echo "probe: KILLED at the hard cap (${MAX}s) -- it was still doing something"
            return 2
        fi
        cpu=$(cputicks "$pid"); mt=$(newest "$dir")
        if [ "$cpu" = "$last_cpu" ] && [ "$mt" = "$last_mt" ]; then
            [ -z "$quiet_since" ] && quiet_since="$now"
            if [ $((now - quiet_since)) -ge "$QUIET" ]; then
                kill "$pid" 2>/dev/null || true
                echo "probe: quiet for ${QUIET}s (no CPU, no writes) -- treating as finished"
                return 0
            fi
        else
            quiet_since=""; last_cpu="$cpu"; last_mt="$mt"
        fi
    done
    wait "$pid" 2>/dev/null || true
    echo "probe: the agent exited on its own after $(( $(date +%s) - start ))s"
    return 0
}

selfcheck() {
    # [c1] a process that finishes and WEDGES must be caught -- the observed failure.
    # [c2] a process still WORKING must NOT be, or the watchdog truncates real runs.
    d=$(mktemp -d); rc=0
    ( sleep 1; echo hi > "$d/out"; sleep 600 ) &
    wedged=$!
    quiet_since=""; lc=$(cputicks "$wedged"); lm=$(newest "$d"); caught=no
    i=0; while [ "$i" -lt 6 ] && kill -0 "$wedged" 2>/dev/null; do
        sleep 1; c=$(cputicks "$wedged"); m=$(newest "$d")
        if [ "$c" = "$lc" ] && [ "$m" = "$lm" ]; then
            [ -z "$quiet_since" ] && quiet_since=$i
            [ $((i - quiet_since)) -ge 2 ] && { caught=yes; break; }
        else quiet_since=""; lc="$c"; lm="$m"; fi
        i=$((i + 1))
    done
    kill "$wedged" 2>/dev/null || true
    echo "  [c1] wedged process detected as quiet: $caught"
    [ "$caught" = yes ] || rc=1

    ( i=0; while [ "$i" -lt 30 ]; do echo "$i" > "$d/w"; i=$((i+1)); sleep 0.2; done ) &
    busy=$!
    quiet_since=""; lc=$(cputicks "$busy"); lm=$(newest "$d"); flagged=no
    i=0; while [ "$i" -lt 5 ] && kill -0 "$busy" 2>/dev/null; do
        sleep 1; c=$(cputicks "$busy"); m=$(newest "$d")
        if [ "$c" = "$lc" ] && [ "$m" = "$lm" ]; then
            [ -z "$quiet_since" ] && quiet_since=$i
            [ $((i - quiet_since)) -ge 2 ] && { flagged=yes; break; }
        else quiet_since=""; lc="$c"; lm="$m"; fi
        i=$((i + 1))
    done
    kill "$busy" 2>/dev/null || true
    echo "  [c2] working process left alone: $([ "$flagged" = no ] && echo yes || echo 'NO -- would truncate a live run')"
    [ "$flagged" = no ] || rc=1

    rm -rf "$d"
    echo "selfcheck: $([ "$rc" -eq 0 ] && echo ok || echo FAILED)"
    return "$rc"
}

case "${1:-}" in
    --selfcheck) echo "probe_run selfcheck:"; selfcheck; exit $? ;;
    "") echo "usage: sh scripts/probe_run.sh <probe-dir> <prompt> [model]" >&2; exit 3 ;;
esac
[ -d "${1:-}" ] || { echo "probe_run: no such directory: ${1:-}" >&2; exit 3; }
[ -n "${2:-}" ] || { echo "probe_run: empty prompt" >&2; exit 3; }
run_probe "$1" "$2" "${3:-mimo-v2.5}"
