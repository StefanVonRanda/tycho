#!/bin/sh
# docs_fences.sh -- front-end pass over the ```tycho fences in docs/.
#
# WHY THIS EXISTS. scripts/spec_examples.sh compiles a ```tycho fence only when
# it is immediately followed by an ```output fence -- 9 such pairs in the whole
# spec. Every other tycho fence in docs/ was parsed by NOTHING. That is how
# docs/guides/arrays-structs.md came to show `1_000_000`, a digit separator the
# language does not have (docs/spec/01-lexical.md:192-194 says so), in a snippet
# that had therefore never compiled in either of its spellings.
#
# WHAT IT CHECKS. Every ```tycho fence in a tracked docs/*.md must survive
# `tychoc --emit-c`: lex, parse, typecheck, emit C. No cc, no link, no run --
# this is a front-end gate, so it is fast and it does not need a program to do
# anything useful, only to be a legal one.
#
# WHAT IT SKIPS, and why each skip is named rather than silent:
#
#   FROZEN     docs/internals/plan-*-DONE.md. Archived verification evidence;
#              its snippets record syntax as it was at the time and must not be
#              dragged forward. Same exemption, same reason, as ARCHIVED in
#              scripts/check_citations.py:258.
#   FRAGMENT   a fence containing no `fn` declaration at all. The spec is
#              written mostly in fragments (a type, an expression, three lines of
#              a body) and wrapping them in a synthetic `main` would typecheck a
#              program the document does not contain.
#
# As of 2026-07-30 the tree has 40 ```tycho fences: 10 CHECKed, 19 FRAGMENT,
# 6 MARKED, 5 FROZEN. Ten is a small number and it is the honest one -- the
# gate's value is that a NEW tycho fence is checked by default, which is what
# would have caught docs/guides/arrays-structs.md.
#   MARKED     a fence preceded by `<!-- fence-skip: <reason> -->`. For the
#              fences that DO declare an `fn` but still cannot compile alone:
#              deliberate error cases, proposed syntax, `extern` blocks against a
#              real C library, or a body that calls a helper the prose defined
#              earlier. The reason is printed on every run, so the skip list is
#              visible rather than a quiet exclusion that grows.
#
# WHAT IT DOES NOT CHECK -- read this before trusting it:
#
#   * ~155 fences in docs/ opened with a BARE ``` and no language tag. Some are
#     shell, C, or output; some are Tycho. Nothing here can tell them apart, so
#     none is checked. Tagging one `tycho` opts it in -- that is the intended
#     way to grow coverage, one reviewed fence at a time.
#   * The MARKED and FRAGMENT sets are not compiled. A fragment can still be
#     wrong; this gate only proves that what claims to be a whole program is one.
#   * Nothing is RUN. Output correctness for the 9 runnable spec examples is
#     scripts/spec_examples.sh's job and stays there.
#
# Exit 0 = every checked fence compiles; non-zero names the ones that did not.
set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "docs-fences: no ./tychoc -- run 'make' first" >&2; exit 2; }

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# Pass 1: carve every ```tycho fence out of every tracked docs/*.md into
# $TMP/f_<n>.ty, and emit one classification record per fence on stdout:
#   <n> TAB <verdict> TAB <file> TAB <line> TAB <reason>
# verdict is CHECK, FROZEN, FRAGMENT or MARKED.
git ls-files 'docs/*.md' | while read -r f; do
    case "$f" in
        docs/internals/plan-*-DONE.md) frozen=1 ;;
        *) frozen=0 ;;
    esac
    awk -v OUT="$TMP" -v F="$f" -v FROZEN="$frozen" '
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
          id = FILENUM "_" n
          file = OUT "/f_" NR "_" n ".ty"
          printf "%s", buf > file
          close(file)
          verdict = "CHECK"; reason = ""
          if (FROZEN == "1")   { verdict = "FROZEN";   reason = "archived verification evidence" }
          else if (mark != "") { verdict = "MARKED";   reason = mark }
          else if (!hasfn)     { verdict = "FRAGMENT"; reason = "no fn declaration" }
          printf "%s\t%s\t%s\t%d\t%s\n", file, verdict, F, start, reason
          mode=0; mark=""; next
      }
      mode==1 {
          if ($0 ~ /^[ \t]*fn[ \t]/) hasfn=1
          buf = buf $0 "\n"
          next
      }
      # a blank line does NOT clear the marker (a marker may be separated from
      # its fence by one); any other prose line does.
      /^[ \t]*$/ { next }
      { mark="" }
    ' "$f"
done > "$TMP/index"

nchk=0; nskip=0; nfail=0
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
    else
        nfail=$((nfail+1))
        echo "docs-fences: FAIL $f:$line -- does not compile" >&2
        sed -e "s|$TMP/[^ :]*\.ty|<fence>|g" -e 's/^/      /' "$src.log" >&2
    fi
done < "$TMP/index"

echo "docs-fences: $nchk fence(s) compiled, $nskip skipped (reasons above), $nfail failure(s)"
[ "$nfail" -eq 0 ] || exit 1
exit 0
