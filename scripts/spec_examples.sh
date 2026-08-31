set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 2
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "spec-examples: no ./tychoc — run 'make' first" >&2; exit 2; }
CC="${CC:-cc}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# compile a C file, run it, diff stdout against the expected block.
# args: <cfile> <compiler-tag> <docfile> <docline> <expected.out>; returns 0/1.
check_c() {
    cf=$1; tag=$2; ff=$3; ll=$4; ex=$5; bin="$cf.bin"
    if ! $CC -O2 -fwrapv -std=c11 -o "$bin" "$cf" -lm >"$cf.cc.log" 2>&1; then
        echo "spec-examples: FAIL $ff:$ll [$tag] — C compile error" >&2; sed 's/^/    /' "$cf.cc.log" >&2; return 1
    fi
    "$bin" </dev/null >"$cf.got" 2>/dev/null; rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "spec-examples: FAIL $ff:$ll [$tag] — program exited $rc" >&2; return 1
    fi
    if ! diff -u "$ex" "$cf.got" >"$cf.diff" 2>&1; then
        echo "spec-examples: FAIL $ff:$ll [$tag] — output mismatch (--- expected / +++ got)" >&2
        sed 's/^/    /' "$cf.diff" >&2; return 1
    fi
    return 0
}

# Pass 1: carve out each tycho+output pair into $TMP/ex_N.ty / .out, and print
# "N<TAB>file<TAB>line" per pair on stdout.
index=$(
  fi=0
  for f in docs/spec/*.md; do
    fi=$((fi+1))
    # P (per-file prefix) keeps carved ids globally unique — a per-file counter
    # alone would collide (ex_1 from two files clobber each other).
    awk -v OUT="$TMP" -v F="$f" -v P="$fi" '
      /^```tycho[ \t]*$/     { mode="ty"; tybuf=""; tystart=NR; next }
      mode=="ty" && /^```[ \t]*$/ { mode="wait"; next }
      mode=="ty"             { tybuf=tybuf $0 "\n"; next }
      mode=="wait" && /^[ \t]*$/       { next }
      mode=="wait" && /^```output[ \t]*$/ { mode="out"; obuf=""; next }
      mode=="wait"           { mode="" }
      mode=="out" && /^```[ \t]*$/ {
        n++; id = P "_" n
        # each example gets its OWN directory: tychoc compiles a directory as a
        # package, so one example declaring `package main` (which any example
        # with an `import` must) invalidated every bare-`fn main()` sibling
        # carved from the same chapter. That made an import unshowable in the
        # chapter about imports.
        d = OUT "/ex_" id; system("mkdir -p \"" d "\"")
        printf "%s", tybuf > (d "/ex.ty")
        printf "%s", obuf  > (d "/ex.out")
        close(d "/ex.ty"); close(d "/ex.out")
        print id "\t" F "\t" tystart
        mode=""; next
      }
      mode=="out"            { obuf=obuf $0 "\n"; next }
    ' "$f"
  done
)

echo "$index" | while IFS='	' read -r id f line; do
  [ -n "${id:-}" ] || continue
  src="$TMP/ex_$id/ex.ty"; exp="$TMP/ex_$id/ex.out"; efail=0

  # tychoc (C reference): --emit-c -o writes ex_$id.c
  if "$TYCHOC" "$src" --emit-c -o "$TMP/ex_$id" >"$TMP/ex_$id.log" 2>&1; then
    check_c "$TMP/ex_$id.c" tychoc "$f" "$line" "$exp" || efail=1
  else
    echo "spec-examples: FAIL $f:$line [tychoc] — transpile error" >&2; sed 's/^/    /' "$TMP/ex_$id.log" >&2; efail=1
  fi

  if [ "$efail" -eq 0 ]; then
    echo "spec-examples: ok $f:$line (tychoc)"
  else
    : >"$TMP/failed"
  fi
done

runs=$(ls "$TMP"/ex_*/ex.ty 2>/dev/null | wc -l | tr -d ' ')
if [ -f "$TMP/failed" ]; then
  echo "spec-examples: $runs runnable example(s), FAILURES above" >&2
  exit 1
fi
echo "spec-examples: $runs runnable example(s), all pass"
exit 0
