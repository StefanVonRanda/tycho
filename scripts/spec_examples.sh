#!/bin/sh
# spec_examples.sh — tier 2 of the spec conformance gate.
#
# Extracts every runnable example from docs/spec/ and asserts it compiles and
# runs with the documented output. An example is "runnable" iff a ```tycho
# block is immediately followed (blank lines allowed) by a ```output block; the
# tycho source is the program, the output block is its exact expected stdout.
# A ```tycho block with no following ```output is an illustrative fragment and
# is skipped — the spec is written mostly in fragments and grammar, by design.
#
# Build path mirrors tests/run.sh's native lane: tychoc --emit-c, then cc -O2.
# (Reference compiler only; running the self-hosted tychoc0 too is a documented
# tier-2b follow-up.) Exit 0 = all runnable examples pass; non-zero on failure.
set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "spec-examples: no ./tychoc — run 'make' first" >&2; exit 2; }
CC="${CC:-cc}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Pass 1: carve out each tycho+output pair into $TMP/ex_N.ty / .out, and print
# "N<TAB>file<TAB>line" per pair on stdout.
index=$(
  for f in docs/spec/*.md; do
    awk -v OUT="$TMP" -v F="$f" '
      /^```tycho[ \t]*$/     { mode="ty"; tybuf=""; tystart=NR; next }
      mode=="ty" && /^```[ \t]*$/ { mode="wait"; next }
      mode=="ty"             { tybuf=tybuf $0 "\n"; next }
      mode=="wait" && /^[ \t]*$/       { next }
      mode=="wait" && /^```output[ \t]*$/ { mode="out"; obuf=""; next }
      mode=="wait"           { mode="" }
      mode=="out" && /^```[ \t]*$/ {
        n++
        printf "%s", tybuf > (OUT "/ex_" n ".ty")
        printf "%s", obuf  > (OUT "/ex_" n ".out")
        close(OUT "/ex_" n ".ty"); close(OUT "/ex_" n ".out")
        print n "\t" F "\t" tystart
        mode=""; next
      }
      mode=="out"            { obuf=obuf $0 "\n"; next }
    ' "$f"
  done
)

# Pass 2: build + run + compare each carved example. The while loop runs in a
# subshell (it reads from a pipe), so failures are recorded by touching a marker
# file rather than a shell variable.
echo "$index" | while IFS='	' read -r n f line; do
  [ -n "${n:-}" ] || continue
  src="$TMP/ex_$n.ty"; exp="$TMP/ex_$n.out"
  if ! "$TYCHOC" "$src" --emit-c -o "$TMP/ex_$n" >"$TMP/ex_$n.log" 2>&1; then
    echo "spec-examples: FAIL $f:$line — transpile error" >&2; sed 's/^/    /' "$TMP/ex_$n.log" >&2; : >"$TMP/failed"; continue
  fi
  if ! $CC -O2 -fwrapv -std=c11 -o "$TMP/ex_$n.bin" "$TMP/ex_$n.c" -lm >"$TMP/ex_$n.log" 2>&1; then
    echo "spec-examples: FAIL $f:$line — C compile error" >&2; sed 's/^/    /' "$TMP/ex_$n.log" >&2; : >"$TMP/failed"; continue
  fi
  "$TMP/ex_$n.bin" </dev/null >"$TMP/ex_$n.got" 2>/dev/null; rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "spec-examples: FAIL $f:$line — program exited $rc" >&2; : >"$TMP/failed"; continue
  fi
  if ! diff -u "$exp" "$TMP/ex_$n.got" >"$TMP/ex_$n.diff" 2>&1; then
    echo "spec-examples: FAIL $f:$line — output mismatch (--- expected / +++ got)" >&2
    sed 's/^/    /' "$TMP/ex_$n.diff" >&2; : >"$TMP/failed"; continue
  fi
  echo "spec-examples: ok $f:$line"
done

runs=$(ls "$TMP"/ex_*.ty 2>/dev/null | wc -l | tr -d ' ')
if [ -f "$TMP/failed" ]; then
  echo "spec-examples: $runs runnable example(s), FAILURES above" >&2
  exit 1
fi
echo "spec-examples: $runs runnable example(s), all pass"
exit 0
