#!/bin/sh
# TYCHO_ARENA_STATS counts per thread and publishes at task end (FRICTION 234):
# what one function allocated must not depend on how many threads ran it. (The
# grand totals do: every chunk task makes a few setup allocations of its own.)
set -eu
cd "$(dirname "$0")/.."
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM
cat > "$t/main.ty" <<'TY'
fn work(i: int) -> int:
    xs := []int
    for k := 0; k < 40; k += 1:
        push(xs, i + k)
    return len(xs)

fn main():
    t := 0
    parallel for i in 0..<20000:
        t += work(i)
    println(str(t))
TY
TYCHO_CORELIB="$PWD/corelib" ./tychoc "$t/main.ty" -o "$t/m" >/dev/null
for n in 1 8; do
    TYCHO_THREADS=$n TYCHO_ARENA_STATS=full "$t/m" >"$t/out$n" 2>"$t/err$n"
    grep -q '^800000$' "$t/out$n"
    grep -E '^    work ' "$t/err$n" | awk '{print $1, $5, $6, $8, $9}' >"$t/fn$n"
done
cmp "$t/fn1" "$t/fn8" || { echo "arena-stats: work's row differs between 1 and 8 threads" >&2; diff "$t/fn1" "$t/fn8" >&2; exit 1; }
[ -s "$t/fn1" ] || { echo "arena-stats: no row for work" >&2; exit 1; }
echo "arena-stats: work's bump and allocation count identical at 1 and 8 threads ($(cat "$t/fn1"))"
