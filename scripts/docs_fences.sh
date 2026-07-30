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
#              scripts/check_citations.py:316@ARCHIVED.
#   FRAGMENT   a fence containing no `fn` declaration at all. The spec is
#              written mostly in fragments (a type, an expression, three lines of
#              a body) and wrapping them in a synthetic `main` would typecheck a
#              program the document does not contain. (A fence that DOES declare
#              whole `fn`s but no `main` is a different case and is checked --
#              see the no-main retry in the loop below.)
#   MARKED     a fence preceded by `<!-- fence-skip: <reason> -->`. For the
#              fences that DO declare an `fn` but still cannot compile alone:
#              deliberate error cases, proposed syntax, `extern` blocks against a
#              real C library, or a body that calls a helper the prose defined
#              earlier. The reason is printed on every run, so the skip list is
#              visible rather than a quiet exclusion that grows.
#
# THE POPULATION, as of 2026-07-30 (phase 43 tagged the reader-facing tree,
# phase 61 the last 64, phase 62 taught it `extern fn`): 252 fences in docs/, of
# which 122 are ```tycho -- 45 CHECKed (12 of them via the no-main retry),
# 53 FRAGMENT, 19 MARKED, 5 FROZEN. It was 40 tycho fences and 10 CHECKed before
# phase 43. Tagging is what opts a fence in, so the number grows by review,
# never by a heuristic guessing at a language.
#
# EVERY FENCE IN docs/ NOW CARRIES A TAG (plan.md phase 61, 2026-07-30). The 64
# that did not -- 56 in docs/internals/, 4 in docs/rfc/, 4 in docs/ -- were read
# one at a time and tagged `text` (73 total), `sh`, or `tycho`. Only three became
# `tycho`, and the rule that produced that number is the one to keep: a fence in a
# DATED design record or an archived plan documents syntax as it was, so tagging
# it `tycho` would opt a historical snippet into a gate that checks today's
# grammar. `text` is the honest tag for those, and for command output, commit
# lists, symbol tables and deliberately-broken illustrations. `tycho` went only to
# snippets still meant to be current, and each was confirmed by this gate
# compiling it, not by inspection.
#
# WHAT IT DOES NOT CHECK -- read this before trusting it:
#
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
          # The name of the carved file must be unique across the WHOLE run. awk is
          # re-invoked per document, so `n` restarts at 1 in each and NR is only
          # a line number: two different documents both closing their first
          # fence on line 47 used to produce the SAME f_47_1.ty, and the second
          # silently overwrote the first -- the gate then compiled the fence of
          # one document while reporting the path of the other. Harmless at the
          # 40 fences of batch 3, a real mis-report at 119. The path is part of
          # the name, so a collision needs the same file at the same line.
          safe = F; gsub(/[^A-Za-z0-9]/, "_", safe)
          file = OUT "/f_" safe "_" NR "_" n ".ty"
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
          # `extern fn NAME(...)` is a declaration too -- the bare /fn[ \t]/ test
          # missed it and filed three compilable FFI fences as FRAGMENT
          # "no fn declaration" (plan.md phase 62).
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
        # NO-MAIN RETRY. A fence may declare complete `fn`s and no `main` --
        # a reference page showing two generic functions, say. `--emit-c` needs
        # an entry point, so the whole fence used to be un-checkable and the only
        # honest option was a fence-skip. Appending an EMPTY `main` typechecks
        # exactly the declarations the document DOES contain and adds nothing
        # else, which is why it is not the synthetic-main wrapper phase 33
        # rejected: that one would have invented a body for loose statements.
        # Negative control, run before this shipped: a fence whose non-main fn
        # returns a string from an `-> int` still fails here.
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
