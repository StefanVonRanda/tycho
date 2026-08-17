set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "docs-fences: no ./tychoc -- run 'make' first" >&2; exit 2; }

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

{ git ls-files 'docs/*.md'; git ls-files 'README.md' 'CONTRIBUTING.md'; } | while read -r f; do
    awk -v OUT="$TMP" -v F="$f" '
      # remember the most recent skip marker; any non-blank non-marker line clears it
      /^[ \t]*<!--[ \t]*fence-skip:/ {
          mark = $0
          sub(/^[ \t]*<!--[ \t]*fence-skip:[ \t]*/, "", mark)
          sub(/[ \t]*-->[ \t]*$/, "", mark)
          next
      }
      /^```tycho[ \t]*$/ { mode=1; buf=""; start=NR; hasfn=0; next }
      mode==1 && /^```[ \t]*$/ {
          n++
          safe = F; gsub(/[^A-Za-z0-9]/, "_", safe)
          file = OUT "/f_" safe "_" NR "_" n ".ty"
          printf "%s", buf > file
          close(file)
          verdict = "CHECK"; reason = ""
          if (mark != "") { verdict = "MARKED";   reason = mark }
          else if (!hasfn)     { verdict = "FRAGMENT"; reason = "no fn declaration" }
          printf "%s\t%s\t%s\t%d\t%s\n", file, verdict, F, start, reason
          mode=0; mark=""; next
      }
      mode==1 {
          # `extern fn NAME(...)` is a declaration too -- the bare /fn[ \t]/ test
          # missed it and filed three compilable FFI fences as FRAGMENT
          # "no fn declaration" (the loops-cleanup plan).
          if ($0 ~ /^[ \t]*(extern[ \t]+)?fn[ \t]/) hasfn=1
          # `extern "lib" fn NAME(...)` -- the library name sits between the two
          # keywords, so the anchored form above cannot reach it.
          if ($0 ~ /^[ \t]*extern[ \t]+"[^"]*"[ \t]+fn[ \t]/) hasfn=1
          buf = buf $0 "\n"
          next
      }
      # a blank line does NOT clear the marker (a marker may be separated from
      # its fence by one); any other prose line does.
      /^[ \t]*$/ { next }
      { mark="" }
    ' "$f"
done > "$TMP/index"

nchk=0; nskip=0; nfail=0; nmain=0
while IFS='	' read -r src verdict f line reason; do
    [ -n "${src:-}" ] || continue
    if [ "$verdict" != "CHECK" ]; then
        nskip=$((nskip+1))
        echo "    skip  $f:$line  [$verdict] $reason"
        continue
    fi
    nchk=$((nchk+1))
    if "$TYCHOC" "$src" --emit-c -o "$src.out" >"$src.log" 2>&1; then
        echo "    ok    $f:$line"
    elif grep -q "no 'main' procedure" "$src.log" &&
         { cp "$src" "$src.m"; printf 'fn main():\n    return\n' >> "$src.m"; } &&
         "$TYCHOC" "$src.m" --emit-c -o "$src.out" >"$src.log" 2>&1; then
        nmain=$((nmain+1))
        echo "    ok    $f:$line  (+ an empty main; the fence declares none)"
    else
        nfail=$((nfail+1))
        echo "docs-fences: FAIL $f:$line -- does not compile" >&2
        sed -e "s|$TMP/[^ :]*\.ty|<fence>|g" -e 's/^/      /' "$src.log" >&2
    fi
done < "$TMP/index"

echo "docs-fences: $nchk fence(s) compiled ($nmain of them with an appended empty main), $nskip skipped (reasons above), $nfail failure(s)"
[ "$nfail" -eq 0 ] || exit 1
exit 0
