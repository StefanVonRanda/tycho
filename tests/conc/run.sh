#!/bin/sh
# Concurrency (spawn/wait/parallel-for/channels) test runner — BOTH compilers.
#
# Per positive fixture: tychoc builds native (-O3), ASan+UBSan (leaks on), and
# TSan binaries — all three must produce the golden tests/conc/<name>.out with
# silent sanitizers — AND the tychoc-built tychoc0 compiles the same fixture,
# whose output must match the same golden (the concurrency parity
# differential). Reject fixtures: tychoc must FAIL (repo precedent: negative
# paths gate the C compiler only). Abort fixtures: compile, then die with the
# message in the sibling .err file.
set -u
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
TYCHOC=./tychoc
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# LeakSanitizer is absent from Apple's ASan; detect_leaks=1 aborts every
# sanitizer binary at exit there. Gate by OS (see tests/run.sh).
case "$(uname -s)" in Darwin) TYCHO_LSAN=0 ;; *) TYCHO_LSAN=1 ;; esac
export ASAN_OPTIONS=detect_leaks=$TYCHO_LSAN
pass=0; fail=0
note() { echo "FAIL $1 ($2)"; }

# the self-hosted compiler, for the parity differential
if ! $TYCHOC compiler/tychoc0.ty -o "$TMP/tychoc0" >/dev/null 2>&1; then
    echo "FAIL tychoc0 (build)"; exit 1
fi

for f in tests/conc/*.ty; do
    name=$(basename "$f" .ty)
    gold="tests/conc/$name.out"
    c="$TMP/$name.c"
    if ! $TYCHOC "$f" --emit-c >/dev/null 2>"$TMP/$name.log" || ! mv "${f%.ty}.c" "$c" 2>/dev/null; then
        note "$name" "tychoc"; sed 's/^/      /' "$TMP/$name.log"; fail=$((fail+1)); continue
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
    if [ $ok -eq 1 ]; then
        # parity differential: the SELF-HOSTED compiler must agree on the golden
        if ! "$TMP/tychoc0" < "$f" > "$TMP/$name.h0.c" 2>"$TMP/$name.h0e" \
           || ! $CC -O2 -pthread -o "$TMP/$name.h0" "$TMP/$name.h0.c" -lm 2>"$TMP/$name.h0e" \
           || ! "$TMP/$name.h0" > "$TMP/$name.h0out" 2>&1 \
           || ! cmp -s "$TMP/$name.h0out" "$gold"; then
            note "$name" "tychoc0 parity"; sed 's/^/      /' "$TMP/$name.h0e" 2>/dev/null | head -3; ok=0
        fi
    fi
    if [ $ok -eq 1 ]; then pass=$((pass+1)); else fail=$((fail+1)); fi
done

# abort fixtures: must compile, then DIE at runtime with the message in the
# sibling .err file (the CC-2 double-wait and CC-4 closed-channel backstops --
# defined loud failures, never UB).
for f in tests/conc/abort/*.ty; do
    name=abort/$(basename "$f" .ty)
    want=$(cat "${f%.ty}.err")
    if ! $TYCHOC "$f" -o "$TMP/ab" >/dev/null 2>&1; then
        note "$name" "tychoc"; fail=$((fail+1)); continue
    fi
    "$TMP/ab" >/dev/null 2>"$TMP/ab.err"
    if [ $? -eq 0 ] || ! grep -q "$want" "$TMP/ab.err"; then
        note "$name" "expected runtime die '$want'"; fail=$((fail+1))
    else
        pass=$((pass+1))
    fi
done

for f in tests/conc/reject/*.ty; do
    name=reject/$(basename "$f" .ty)
    if $TYCHOC "$f" -o "$TMP/rej" >/dev/null 2>&1; then
        note "$name" "compiled but must be rejected"; fail=$((fail+1))
    else
        pass=$((pass+1))
    fi
done

echo "conc: passed $pass   failed $fail"
[ $fail -eq 0 ] || exit 1
