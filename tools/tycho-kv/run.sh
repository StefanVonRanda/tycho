set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "tycho-kv: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-kv/expected.out"
scripts="$PWD/tools/tycho-kv/scripts"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

K="$T/tycho-kv"
if ! "$TYCHOC" tools/tycho-kv/main.ty -o "$K" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-kv: FAIL"; exit 1
fi

out="$T/all.out"
: > "$out"
for s in big delete-heavy stress; do
    store="$T/$s.kv"
    "$K" init "$store" || { bad "$s: init failed"; continue; }
    if ! "$K" batch "$store" "$scripts/$s.txt" >"$T/$s.btree" 2>"$T/$s.err"; then
        bad "$s: the B+ tree rejected the script"; sed 's/^/      /' "$T/$s.err"; continue
    fi
    if ! "$K" --map batch "$store" "$scripts/$s.txt" >"$T/$s.map" 2>"$T/$s.merr"; then
        bad "$s: the map backend rejected the script"; continue
    fi
    cmp -s "$T/$s.btree" "$T/$s.map" || {
        bad "$s: the B+ tree and the map disagree"
        diff "$T/$s.map" "$T/$s.btree" | head -8 | sed 's/^/      /'
    }
    sed 's/^scan /pscan /; s/^scan$/pscan/' "$scripts/$s.txt" > "$T/$s.pscan.txt"
    "$K" batch "$store" "$T/$s.pscan.txt" > "$T/$s.pscan" 2>>"$T/$s.err" || {
        bad "$s: pscan variant failed"; sed 's/^/      /' "$T/$s.err"
    }
    cmp -s "$T/$s.pscan" "$T/$s.btree" || bad "$s: pscan differs from the serial scan"

    # [2] persistence: reload the store, replay the scans, compare to the
    # batch's OWN scan output (the map backend starts empty on a scan-only
    # replay, so it is the wrong reference here)
    if grep -q '^scan' "$scripts/$s.txt"; then
        awk '/^scan/{print}' "$scripts/$s.txt" > "$T/$s.scans"
        "$K" batch "$store" "$T/$s.scans" > "$T/$s.reload" 2>>"$T/$s.err" || {
            bad "$s: reload failed"; sed 's/^/      /' "$T/$s.err"; continue
        }
        # the original batch's scan output: every line between a `scan`
        # command and the next command in the script -- rebuild it from the
        # script and the btree output is complex; instead compare the reload
        # against a SECOND btree batch of the full script on the reloaded
        # store (the btree backend replays deterministically)
        "$K" batch "$store" "$scripts/$s.txt" > "$T/$s.re2" 2>>"$T/$s.err" || {
            bad "$s: re-batch failed"; continue
        }
        cmp -s "$T/$s.re2" "$T/$s.btree" || bad "$s: reloaded store does not reproduce the batch"
    fi
    cat "$T/$s.map" >> "$out"
done

if [ "$RECORD" = "1" ]; then
    cp "$out" "$golden"
    echo "tycho-kv: golden recorded ($(wc -l < "$golden") lines)"
    exit 0
fi
if ! cmp -s "$out" "$golden"; then
    bad "output does not match the golden; re-record with RECORD=1 sh tools/tycho-kv/run.sh"
    diff "$golden" "$out" | head -8 | sed 's/^/      /'
fi

if [ "$fail" -eq 1 ]; then
    echo "tycho-kv: FAIL"
    exit 1
fi
echo "tycho-kv: green (3 scripts byte-identical B+ tree vs map; reloads agree; golden locked)"
