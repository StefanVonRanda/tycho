#!/bin/sh
# What is true about this tree, right now, measured rather than read.
#
# WHY THIS EXISTS. On 2026-09-02 five separate documents were found describing a
# tree that no longer existed: a perf phase ranked by a 100-400x memory gap that
# had closed, a compile-speed ladder that had reached parity unnoticed, a roadmap
# three quarters closed history, release notes claiming core:net has no polling
# when it has four kinds, and a release that nearly shipped a compiler unable to
# compile anything. None was a code defect. Every one was the written record
# drifting from the tree, and the only way to learn anything was to run something.
#
# So this prints NOTHING it read from a document. Every number below is counted,
# executed, or asked of git at the moment you run it. If a fact cannot be
# measured cheaply it says so rather than guessing -- an unknown is honest, a
# stale number is not.
#
# THE ONE RECORD IT DOES READ is the CI record, and the rule there is that a
# record must not be able to outlive the tree it describes. A sha alone cannot
# carry that: it says nothing about uncommitted changes, it reads as "no drift"
# when HEAD is an ANCESTOR of it, and an empty one compares equal to everything.
# So the record names a FINGERPRINT of the tested content -- commit plus every
# uncommitted change -- and only an exact match prints GREEN. Every other
# outcome, including a record this script cannot parse, is unverified.
#
#   sh scripts/status.sh          # the tree          (make status)
#   sh scripts/status.sh --net    # also ask GitHub   (make status-net)
#   sh scripts/status.sh --fingerprint   # the id scripts/ci.sh records
#   sh scripts/status.sh --selfcheck     # this script, against a scratch repo
set -u
cd "$(dirname "$0")/.." || exit 2

net=0
mode=report
case "${1:-}" in
    "")            ;;
    --net)         net=1 ;;
    --fingerprint) mode=fingerprint ;;
    --selfcheck)   mode=selfcheck ;;
    *) printf 'status.sh: usage: status.sh [--net|--fingerprint|--selfcheck]\n' >&2; exit 2 ;;
esac

hr() { printf '%s\n' "----------------------------------------------------------------"; }
row() { printf '  %-26s %s\n' "$1" "$2"; }

# ---- the primitives, kept separate so --selfcheck can drive them -----------

# The content this tree IS: the commit, plus every uncommitted change, in one
# id. --porcelain names untracked files and `diff HEAD` does not carry their
# bytes, so the third line hashes each one's CONTENT: editing an untracked,
# non-ignored file between a sweep and a status check moved nothing before it.
# Measured 2026-09-03: 0 such files here, 0.01 s -- the gap was never the cost.
tree_fingerprint() {
    { git rev-parse HEAD 2>/dev/null || echo unborn
      git status --porcelain 2>/dev/null
      git ls-files --others --exclude-standard 2>/dev/null \
          | git hash-object --stdin-paths 2>/dev/null
      git diff HEAD 2>/dev/null
    } | git hash-object --stdin 2>/dev/null || echo unknown
}

# Where $1 sits relative to HEAD, in words. `rev-list --count X..HEAD` alone
# answers 0 for three different situations -- same commit, HEAD an ancestor of
# X, and X empty -- and printing GREEN for the last two is the defect this
# replaces.
commit_desc() {
    _c="${1:-}"
    [ -n "$_c" ] || { printf 'no commit (the record names none)'; return; }
    _c="$(git rev-parse --verify --quiet "$_c^{commit}" 2>/dev/null)" \
        || { printf 'a commit this clone does not have'; return; }
    if [ "$_c" = "$(git rev-parse HEAD 2>/dev/null)" ]; then
        printf 'the same commit'
    elif git merge-base --is-ancestor "$_c" HEAD 2>/dev/null; then
        printf '%s commit(s) behind HEAD' "$(git rev-list --count "$_c"..HEAD)"
    elif git merge-base --is-ancestor HEAD "$_c" 2>/dev/null; then
        printf '%s commit(s) AHEAD of HEAD' "$(git rev-list --count "HEAD..$_c")"
    else
        printf 'a diverged commit'
    fi
}

# GNU date takes a path with -r; BSD date takes an epoch and refuses one, so
# macOS falls through to stat -f. Neither is portable alone.
file_mtime() {
    date -r "$1" '+%Y-%m-%d %H:%M' 2>/dev/null && return 0
    stat -f '%Sm' -t '%Y-%m-%d %H:%M' "$1" 2>/dev/null && return 0
    printf '?\n'
}

# Same whitespace class as scripts/check_version_status.py's VERDEF, which is
# the reader that gates this constant; a single literal space is not the rule.
read_cver() {
    sed -n 's/^#define[[:space:]][[:space:]]*TYCHO_VERSION[[:space:]][[:space:]]*"\([^"]*\)".*/\1/p' "$1" 2>/dev/null | head -1
}
# Anchored to the VERSION() body. "the first digit-led string return in the
# file" is not a version reader, it is a coincidence that has held so far.
read_tver() {
    sed -n '/^fn VERSION()/,/^[^ ]/s/^[[:space:]][[:space:]]*return[[:space:]][[:space:]]*"\([^"]*\)".*/\1/p' "$1" 2>/dev/null | head -1
}

# grep -c already prints 0 and exits 1 when nothing matches, so `|| echo 0`
# printed the zero twice. Masked while plan.md had open items.
plan_open() { grep -c '^- \[ \]' "$1" 2>/dev/null; }

# tests/run.sh's own globs (tests/run.sh@run_fixture and the loops below it),
# not a tests/** sweep no gate uses. These are fixture FILES and package DIRS;
# make test's "passed:" line counts CASES and is a different number by design.
fixture_count() {
    ls -d examples/*.ty tests/*.ty tests/reject/*.ty tests/abort/*.ty \
          tests/diag/*.ty tests/warn/*.ty \
          tests/pkg/*/ tests/reject/pkg/*/ tests/warn/pkg/*/ 2>/dev/null | wc -l | tr -d ' '
}
reject_count() { ls -d tests/reject/*.ty tests/reject/pkg/*/ 2>/dev/null | wc -l | tr -d ' '; }

# ---- the two rows that read a record --------------------------------------

ci_row() {
    _f=build/ci-status
    if [ ! -f "$_f" ]; then
        row "make ci" "never recorded here -- run: make ci"; return
    fi
    if [ "$(head -1 "$_f" 2>/dev/null)" != "tycho-ci-record v2" ]; then
        row "make ci" "record predates the fingerprinted format -- run: make ci"; return
    fi
    _sha="$(sed -n 's/^sha //p'         "$_f" | head -1)"
    _fp="$( sed -n 's/^fingerprint //p' "$_f" | head -1)"
    _when="$(sed -n 's/^date //p'       "$_f" | head -1)"
    _dur="$(sed -n 's/^dur //p'         "$_f" | head -1)"
    _fuzz="$(sed -n 's/^fuzz //p'       "$_f" | head -1)"
    _skip="$(sed -n 's/^skipped //p'    "$_f" | head -1)"
    # Scope is part of the verdict: `make ci N=0` runs no fuzz lane at all, and
    # a record that does not say so is indistinguishable from a full sweep.
    _scope="fuzz N=${_fuzz:-?}"
    [ "${_skip:-none}" = "none" ] || _scope="$_scope, SKIPPED: $_skip"
    if [ -n "$_fp" ] && [ "$_fp" = "$(tree_fingerprint)" ]; then
        row "make ci" "GREEN for this exact tree ($_when, ${_dur}s, $_scope)"
    elif [ -n "$_sha" ] && [ "$_sha" = "$(git rev-parse HEAD 2>/dev/null)" ]; then
        row "make ci" "green at this commit but the tree has changed since -- UNVERIFIED ($_when, $_scope)"
    else
        row "make ci" "recorded at $(commit_desc "$_sha") -- UNVERIFIED ($_when, $_scope)"
    fi
}

tag_row() {
    _tag="$(git tag --list 'v*' --sort=-v:refname 2>/dev/null | head -1)"
    if [ -z "$_tag" ]; then
        row "latest tag" "none in this clone"; return
    fi
    _d="$(commit_desc "$_tag")"
    if [ "$_d" = "the same commit" ]; then
        row "latest tag" "$_tag (HEAD is the tagged commit)"
    else
        row "latest tag" "$_tag, $_d"
    fi
}

# ---- --fingerprint ---------------------------------------------------------
if [ "$mode" = fingerprint ]; then
    tree_fingerprint
    exit 0
fi

# ---- --selfcheck -----------------------------------------------------------
# Every branch above, driven against a scratch repo. Each case is a situation
# that printed GREEN, or printed a wrong number, before 2026-09-03.
if [ "$mode" = selfcheck ]; then
    sc_fail=0
    ck()  { case "$3" in *"$2"*) printf 'ok    %s\n' "$1" ;;
            *) printf 'FAIL  %s\n        want substring: %s\n        got:            %s\n' "$1" "$2" "$3"; sc_fail=$((sc_fail+1)) ;; esac; }
    ckn() { case "$3" in *"$2"*) printf 'FAIL  %s\n        must NOT contain: %s\n        got:              %s\n' "$1" "$2" "$3"; sc_fail=$((sc_fail+1)) ;;
            *) printf 'ok    %s\n' "$1" ;; esac; }
    cke() { if [ "$3" = "$2" ]; then printf 'ok    %s\n' "$1"
            else printf 'FAIL  %s\n        want exactly: %s\n        got:          %s\n' "$1" "$2" "$3"; sc_fail=$((sc_fail+1)); fi; }

    _root="$(pwd)"
    _d="$(mktemp -d)" || exit 2
    trap 'cd /; rm -rf "$_d"' EXIT INT TERM
    cd "$_d" || exit 2
    git init -q .
    git config user.email selfcheck@localhost
    git config user.name selfcheck
    # /build/ and the ignore rule mirror the real repo: the ci record itself is
    # ignored, so writing it must not move the fingerprint it stores.
    printf '/build/\ni.txt\n' > .gitignore
    echo one > a.txt; git add a.txt .gitignore; git commit -q -m one
    C1="$(git rev-parse HEAD)"
    mkdir -p build

    write_rec() { # sha fingerprint fuzz skipped
        { echo 'tycho-ci-record v2'; echo "sha $1"; echo "fingerprint $2"
          echo 'date 2026-09-03'; echo 'dur 900'; echo "fuzz $3"; echo "skipped $4"
        } > build/ci-status
    }

    printf '\nstatus.sh --selfcheck\n\n'

    # [1] no record at all
    rm -f build/ci-status
    ck 'no record says so' 'never recorded here' "$(ci_row)"

    # [2] the pre-fingerprint format must not be trusted, not silently re-read
    printf '%s 2026-09-02 889\n' "$C1" > build/ci-status
    ck 'legacy record refused' 'predates the fingerprinted format' "$(ci_row)"

    # [3] an exact match is the ONLY thing that prints GREEN
    write_rec "$C1" "$(tree_fingerprint)" 200 none
    ck 'exact match is green' 'GREEN for this exact tree' "$(ci_row)"

    # [4] defect 1: a dirty tree at the same commit is not what was tested
    echo dirt >> a.txt
    ckn 'dirty tree is not green' 'GREEN' "$(ci_row)"
    ck  'dirty tree says why'    'tree has changed since' "$(ci_row)"
    git checkout -q -- a.txt

    # [5] defect 2: an empty sha compared equal to everything
    write_rec '' '' 200 none
    ckn 'empty record is not green' 'GREEN' "$(ci_row)"
    ck  'empty record says why'     'no commit (the record names none)' "$(ci_row)"

    # [6] defect 2: HEAD an ANCESTOR of the recorded commit -- an older checkout
    echo two > b.txt; git add b.txt; git commit -q -m two
    C2="$(git rev-parse HEAD)"
    write_rec "$C2" 'stale-fingerprint' 200 none
    git checkout -q "$C1"
    ckn 'older checkout is not green' 'GREEN' "$(ci_row)"
    ck  'older checkout says why'     'commit(s) AHEAD of HEAD' "$(ci_row)"
    git checkout -q "$C2"
    ck  'newer HEAD reports drift'    'commit(s) behind HEAD' \
        "$(write_rec "$C1" x 200 none; git checkout -q "$C2"; ci_row)"

    # [7] defect 3: N=0 skipped the fuzz lanes and the record must say so
    write_rec "$C2" "$(tree_fingerprint)" 0 fuzz
    ck 'skip is in the verdict' 'SKIPPED: fuzz' "$(ci_row)"
    ck 'fuzz N is in the verdict' 'fuzz N=0' "$(ci_row)"

    # [8] defect 4: no tags at all, and HEAD older than the tag
    ck 'no tag says so' 'none in this clone' "$(tag_row)"
    git tag v1.0.0
    ck 'tag at HEAD' '(HEAD is the tagged commit)' "$(tag_row)"
    git checkout -q "$C1"
    ckn 'older HEAD is not the tag' 'HEAD is the tagged commit' "$(tag_row)"
    git checkout -q "$C2"

    # [9] defect 5: grep -c prints 0 itself; `|| echo 0` printed it twice
    printf 'nothing open here\n' > p.md
    cke 'no open items prints one 0' '0' "$(plan_open p.md)"
    printf -- '- [ ] a\n- [x] b\n- [ ] c\n' > p.md
    cke 'open items counted' '2' "$(plan_open p.md)"

    # [10] defect 6: a tab after #define, and a decoy return before VERSION()
    printf '#define\tTYCHO_VERSION\t"9.9.9"\n' > v.c
    cke 'cver reads a tab' '9.9.9' "$(read_cver v.c)"
    printf 'fn other() -> string:\n    return "0.0.1"\n\nfn VERSION() -> string:\n    return "9.9.9"\n' > v.ty
    cke 'tver ignores a decoy return' '9.9.9' "$(read_tver v.ty)"

    # [11] defect 7: a timestamp, on either date(1)
    ck 'file_mtime is a date' '-' "$(file_mtime a.txt)"
    ckn 'file_mtime is not unknown' '?' "$(file_mtime a.txt)"

    # [12] the fingerprint moves for an uncommitted change, which is the whole
    #      property; a sha does not.
    F1="$(tree_fingerprint)"
    echo more >> a.txt
    cke 'sha is blind to a dirty tree' "$C2" "$(git rev-parse HEAD)"
    if [ "$F1" = "$(tree_fingerprint)" ]; then
        printf 'FAIL  fingerprint moves for a dirty tree\n'; sc_fail=$((sc_fail+1))
    else
        printf 'ok    fingerprint moves for a dirty tree\n'
    fi

    # [13] the hole R14 named: an untracked, non-ignored file's BYTES. Its name
    #      is in --porcelain, so [12]'s property held for creation and deletion
    #      but not for an EDIT.
    : > u.txt
    F2="$(tree_fingerprint)"
    printf 'edited\n' > u.txt
    if [ "$F2" = "$(tree_fingerprint)" ]; then
        printf 'FAIL  fingerprint moves for an edited untracked file\n'; sc_fail=$((sc_fail+1))
    else
        printf 'ok    fingerprint moves for an edited untracked file\n'
    fi
    F3="$(tree_fingerprint)"
    printf 'edited\n' > i.txt
    cke 'an ignored file is still invisible' "$F3" "$(tree_fingerprint)"

    cd "$_root" || exit 2
    printf '\n'
    if [ "$sc_fail" -eq 0 ]; then
        printf 'status.sh --selfcheck: all green\n\n'; exit 0
    fi
    printf 'status.sh --selfcheck: %d FAILED\n\n' "$sc_fail"; exit 1
fi

# ---- report ----------------------------------------------------------------
printf '\nTYCHO -- measured %s\n' "$(date '+%Y-%m-%d %H:%M')"
hr

# ---- version -------------------------------------------------------------
# Read from the two constants themselves, because they are kept in step BY HAND
# and release.sh validates only the first. A disagreement here is a real defect
# and is printed as one.
cver="$(read_cver src/tychoc.c)"
tver="$(read_tver compiler/main.ty)"
if [ -z "$cver" ] || [ -z "$tver" ]; then
    row "version" "UNREADABLE  src/tychoc.c='$cver'  compiler/main.ty='$tver'"
elif [ "$cver" = "$tver" ]; then
    row "version" "$cver"
else
    row "version" "MISMATCH  src/tychoc.c=$cver  compiler/main.ty=$tver"
fi

tag_row

dirty="$(git status --porcelain | wc -l | tr -d ' ')"
[ "$dirty" = "0" ] && row "working tree" "clean" || row "working tree" "$dirty uncommitted change(s)"
ahead="$(git rev-list --count @{u}..HEAD 2>/dev/null || echo '?')"
[ "$ahead" = "0" ] && row "vs origin" "pushed" || row "vs origin" "$ahead commit(s) unpushed"

# ---- verification --------------------------------------------------------
hr
# The single most useful line here, and the one no document can carry: is the
# tree VERIFIED as it stands? scripts/ci.sh writes the record on a green sweep,
# and only for the exact content it swept; anything else means the answer is
# unknown, which is not the same as bad.
ci_row

# Cheap lanes, run for real. Together well under a second, so there is no reason
# to report them from memory.
for lane in version-check surface-check goldens-check; do
    if make -s "$lane" >/dev/null 2>&1; then row "$lane" "ok"; else row "$lane" "FAILING -- run: make $lane"; fi
done

# ---- size ----------------------------------------------------------------
hr
row "fixtures" "$(fixture_count) by tests/run.sh's globs ($(reject_count) of them refusals)"
row "corelib packages" "$(ls -d corelib/*/ 2>/dev/null | grep -v '/test/' | wc -l | tr -d ' ') shipped, $(ls -d corelib/test/*/ 2>/dev/null | wc -l | tr -d ' ') with a test dir"
if [ -f surface.lock ]; then
    row "locked surface" "$(python3 - <<'PY' 2>/dev/null || echo '(unreadable)'
import json
d=json.load(open('surface.lock'))
print("%d keywords, %d builtins, %d corelib fns" % (len(d.get('keywords',[])), len(d.get('builtins',[])), len(d.get('corelib',[]))))
PY
)"
fi
row "tools" "$(ls -d tools/tycho-*/ 2>/dev/null | wc -l | tr -d ' ') programs"
row "docs" "$(git ls-files '*.md' | wc -l | tr -d ' ') markdown files"

# ---- compilers -----------------------------------------------------------
hr
# Both binaries answer for themselves. A stale ./tychoc1 on disk reporting an old
# version is exactly the kind of thing this whole file exists to surface.
for c in ./tychoc ./tychoc1; do
    if [ -x "$c" ]; then
        row "$c" "$("$c" --version 2>&1 | head -1) (built $(file_mtime "$c"))"
    else
        row "$c" "not built -- run: make ${c#./}"
    fi
done

# ---- open work -----------------------------------------------------------
hr
if [ -f plan.md ]; then
    row "plan.md" "$(plan_open plan.md) open item(s), $(wc -l < plan.md | tr -d ' ') lines"
    grep -n '^- \[ \]' plan.md 2>/dev/null | head -6 | sed 's/^/      /'
else
    row "plan.md" "absent (gitignored working file)"
fi

# ---- adoption ------------------------------------------------------------
# Off by default: it is the only thing here that needs the network, and a status
# command that hangs is a status command nobody runs.
if [ "$net" = "1" ]; then
    hr
    if command -v gh >/dev/null 2>&1; then
        row "releases" "$(gh release list --limit 3 --json tagName --jq '[.[].tagName] | join(", ")' 2>/dev/null || echo '(gh failed)')"
        row "downloads (all)" "$(gh api repos/:owner/:repo/releases --jq '[.[].assets[].download_count] | add // 0' 2>/dev/null || echo '?')"
        row "open issues" "$(gh issue list --state open --limit 100 2>/dev/null | wc -l | tr -d ' ')"
    else
        row "adoption" "gh not installed"
    fi
else
    hr
    row "adoption" "not asked (pass --net, or run: make status-net)"
fi

hr
printf '\n'
