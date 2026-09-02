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
#   sh scripts/status.sh          # the tree
#   sh scripts/status.sh --net    # also ask GitHub about releases and issues
set -u
cd "$(dirname "$0")/.." || exit 2

net=0
[ "${1:-}" = "--net" ] && net=1

hr() { printf '%s\n' "----------------------------------------------------------------"; }
row() { printf '  %-26s %s\n' "$1" "$2"; }

printf '\nTYCHO -- measured %s\n' "$(date '+%Y-%m-%d %H:%M')"
hr

# ---- version -------------------------------------------------------------
# Read from the two constants themselves, because they are kept in step BY HAND
# and release.sh validates only the first. A disagreement here is a real defect
# and is printed as one.
cver="$(sed -n 's/^#define TYCHO_VERSION "\([^"]*\)".*/\1/p' src/tychoc.c | head -1)"
tver="$(sed -n 's/^ *return "\([0-9][^"]*\)".*/\1/p' compiler/main.ty | head -1)"
if [ "$cver" = "$tver" ]; then
    row "version" "$cver"
else
    row "version" "MISMATCH  src/tychoc.c=$cver  compiler/main.ty=$tver"
fi

tag="$(git tag --list 'v*' --sort=-v:refname | head -1)"
since="$(git rev-list --count "$tag"..HEAD 2>/dev/null || echo '?')"
if [ "$since" = "0" ]; then
    row "latest tag" "$tag (HEAD is the tagged commit)"
else
    row "latest tag" "$tag, $since commit(s) behind HEAD"
fi

dirty="$(git status --porcelain | wc -l | tr -d ' ')"
[ "$dirty" = "0" ] && row "working tree" "clean" || row "working tree" "$dirty uncommitted change(s)"
ahead="$(git rev-list --count @{u}..HEAD 2>/dev/null || echo '?')"
[ "$ahead" = "0" ] && row "vs origin" "pushed" || row "vs origin" "$ahead commit(s) unpushed"

# ---- verification --------------------------------------------------------
hr
# The single most useful line here, and the one no document can carry: is the
# tree VERIFIED as it stands? scripts/ci.sh writes build/ci-status on a green
# sweep; anything else means the answer is unknown, which is not the same as bad.
if [ -f build/ci-status ]; then
    csha="$(cut -d' ' -f1 build/ci-status)"
    cwhen="$(cut -d' ' -f2 build/ci-status)"
    cdur="$(cut -d' ' -f3 build/ci-status)"
    drift="$(git rev-list --count "$csha"..HEAD 2>/dev/null || echo '?')"
    if [ "$drift" = "0" ]; then
        row "make ci" "GREEN at HEAD ($cwhen, ${cdur}s)"
    else
        row "make ci" "green $drift commit(s) ago ($cwhen, ${cdur}s) -- HEAD unverified"
    fi
else
    row "make ci" "never recorded here -- run: make ci"
fi

# Cheap lanes, run for real. Together well under a second, so there is no reason
# to report them from memory.
for lane in version-check surface-check goldens-check; do
    if make -s "$lane" >/dev/null 2>&1; then row "$lane" "ok"; else row "$lane" "FAILING -- run: make $lane"; fi
done

# ---- size ----------------------------------------------------------------
hr
row "fixtures" "$(git ls-files 'tests/**/*.ty' 'tests/*.ty' | wc -l | tr -d ' ') .ty ($(git ls-files 'tests/reject/**/*.ty' 'tests/reject/*.ty' | wc -l | tr -d ' ') of them refusals)"
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
        row "$c" "$("$c" --version 2>&1 | head -1) (built $(date -r "$c" '+%Y-%m-%d %H:%M'))"
    else
        row "$c" "not built -- run: make ${c#./}"
    fi
done

# ---- open work -----------------------------------------------------------
hr
if [ -f plan.md ]; then
    open_n="$(grep -c '^- \[ \]' plan.md 2>/dev/null || echo 0)"
    row "plan.md" "$open_n open item(s), $(wc -l < plan.md | tr -d ' ') lines"
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
    row "adoption" "not asked (pass --net)"
fi

hr
printf '\n'
