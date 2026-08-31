set -u
cd "$(dirname "$0")/.." || exit 2
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "entrypoints: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# The entry points with no gate of their own. Losing one of these to a rename is
# exactly the rot this lane exists to prevent, so they are named, not globbed.
MUST="examples/webserver/main.ty examples/weblog/main.ty examples/fetch/main.ty
      examples/sqlite/demo.ty server/main.ty tools/tycho-vm/main.ty
      bench/dijkstra/dijkstra.ty bench/trie/trie.ty bench/trie/trie_pool.ty"
missing=""
for m in $MUST; do [ -f "$m" ] || missing="$missing $m"; done
[ -z "$missing" ] || { echo "entrypoints: MUST-COVER FILE GONE:$missing -- this lane asserts LESS than it claims; fix the list or restore the file"; exit 1; }

list=""
for d in examples/*/; do
    case "$d" in examples/corelib/) continue ;; esac
    if [ -f "${d}main.ty" ]; then list="$list ${d}main.ty"
    else for f in "$d"*.ty; do [ -f "$f" ] && list="$list $f"; done
    fi
done
list="$list server/main.ty"
for d in tools/*/; do
    [ -f "${d}main.ty" ] && list="$list ${d}main.ty"
done

for f in $(find bench -name '*.ty' | sort); do
    case "$f" in bench/trie/*) continue ;; esac
    list="$list $f"
done

ISOLATE="bench/trie/trie.ty bench/trie/trie_pool.ty"

WARNBASE="${WARNBASE:-scripts/entrypoints.warn}"
: > "$T/warn"

n=0; fail=0
for e in $list; do
    n=$((n + 1))
    if "$TYCHOC" "$e" --emit-c -o "$T/e" >"$T/log" 2>&1; then
        grep -E ': warning: ' "$T/log" | sed "s|^|$e -> |" >> "$T/warn" || true
        echo "ok      $e"
    else
        echo "FAIL    $e"
        sed 's/^/        /' "$T/log" | head -6
        fail=$((fail + 1))
    fi
    rm -f "$T/e.c"
done

for e in $ISOLATE; do
    n=$((n + 1))
    rm -rf "$T/iso"; mkdir -p "$T/iso"; cp "$e" "$T/iso/"
    if "$TYCHOC" "$T/iso/$(basename "$e")" --emit-c -o "$T/e" >"$T/log" 2>&1; then
        grep -E ': warning: ' "$T/log" | sed "s|^|$e -> |" >> "$T/warn" || true
        echo "ok      $e (isolated)"
    else
        echo "FAIL    $e (isolated)"
        sed 's/^/        /' "$T/log" | head -6
        fail=$((fail + 1))
    fi
    rm -f "$T/e.c"
done

# A zero-length sweep is a broken gate, not a green one. The floor is a floor,
# not the count: it sits under examples+server+tools+bench so a find that stops
# matching bench/ cannot leave this lane silently green.
[ "$n" -ge 70 ] || { echo "entrypoints: only $n entry point(s) found -- the glob is broken, this lane asserts NOTHING"; exit 1; }
echo "-----------------------------------------"
[ "$fail" -eq 0 ] || { echo "entrypoints: FAILED ($fail of $n entry points do not compile)"; exit 1; }

sort "$T/warn" > "$T/warn.s"
if [ "${RECORD:-0}" = 1 ]; then
    cp "$T/warn.s" "$WARNBASE"; echo "rec     $WARNBASE ($(wc -l < "$WARNBASE") warning line(s))"
elif [ ! -f "$WARNBASE" ]; then
    echo "entrypoints: FAILED (no $WARNBASE -- record it with RECORD=1)"; exit 1
elif ! cmp -s "$T/warn.s" "$WARNBASE"; then
    echo "entrypoints: FAILED (the warnings these programs emit moved)"
    diff -u "$WARNBASE" "$T/warn.s" | sed -n '3,20p'
    echo "  If the change is intended: RECORD=1 sh scripts/entrypoints.sh"
    exit 1
fi
echo "entrypoints: ok ($n entry points compile with tychoc; $(wc -l < "$WARNBASE" | tr -d ' ') warning line(s), matching $WARNBASE)"
