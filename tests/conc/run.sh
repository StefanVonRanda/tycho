#!/bin/sh
# Concurrency (spawn/wait) test runner — CC-1, hierc-only for now.
#
# Lives OUTSIDE tests/*.hi on purpose: hierc0 has no spawn yet, and the
# fixpoint differential runs every tests/*.hi fixture through BOTH compilers
# — a spawn fixture there would go red until hierc0 parity lands. Fold these
# into tests/ when it does.
#
# Per positive fixture: build native (-O3), ASan+UBSan (leaks on), and TSan
# binaries; all three must produce the golden tests/conc/<name>.out and the
# sanitizers must report nothing. Per reject fixture: hierc must FAIL.
set -u
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
HIERC=./hierc
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export ASAN_OPTIONS=detect_leaks=1
pass=0; fail=0
note() { echo "FAIL $1 ($2)"; }

for f in tests/conc/*.hi; do
    name=$(basename "$f" .hi)
    gold="tests/conc/$name.out"
    c="$TMP/$name.c"
    if ! $HIERC "$f" --emit-c >/dev/null 2>"$TMP/$name.log" || ! mv "${f%.hi}.c" "$c" 2>/dev/null; then
        note "$name" "hierc"; sed 's/^/      /' "$TMP/$name.log"; fail=$((fail+1)); continue
    fi
    ok=1
    for variant in "-O3:nat" "-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1:asan" "-fsanitize=thread -g -O1:tsan"; do
        flags=${variant%:*}; tag=${variant#*:}
        if ! $CC $flags -pthread -o "$TMP/$name.$tag" "$c" -lm 2>"$TMP/$name.cc"; then
            note "$name" "$tag cc"; sed 's/^/      /' "$TMP/$name.cc"; ok=0; break
        fi
        if ! "$TMP/$name.$tag" >"$TMP/$name.got" 2>"$TMP/$name.err"; then
            note "$name" "$tag run"; sed 's/^/      /' "$TMP/$name.err"; ok=0; break
        fi
        if [ -s "$TMP/$name.err" ]; then
            note "$name" "$tag stderr"; sed 's/^/      /' "$TMP/$name.err"; ok=0; break
        fi
        if ! cmp -s "$TMP/$name.got" "$gold"; then
            note "$name" "$tag output"; diff "$gold" "$TMP/$name.got" | sed 's/^/      /'; ok=0; break
        fi
    done
    if [ $ok -eq 1 ]; then pass=$((pass+1)); else fail=$((fail+1)); fi
done

# abort fixtures: must compile, then DIE at runtime with the expected message
# (the CC-2 double-wait backstop -- a defined loud failure, never UB).
for f in tests/conc/abort/*.hi; do
    name=abort/$(basename "$f" .hi)
    if ! $HIERC "$f" -o "$TMP/ab" >/dev/null 2>&1; then
        note "$name" "hierc"; fail=$((fail+1)); continue
    fi
    "$TMP/ab" >/dev/null 2>"$TMP/ab.err"
    if [ $? -eq 0 ] || ! grep -q "task already waited" "$TMP/ab.err"; then
        note "$name" "expected runtime die 'task already waited'"; fail=$((fail+1))
    else
        pass=$((pass+1))
    fi
done

for f in tests/conc/reject/*.hi; do
    name=reject/$(basename "$f" .hi)
    if $HIERC "$f" -o "$TMP/rej" >/dev/null 2>&1; then
        note "$name" "compiled but must be rejected"; fail=$((fail+1))
    else
        pass=$((pass+1))
    fi
done

echo "conc: passed $pass   failed $fail"
[ $fail -eq 0 ] || exit 1
