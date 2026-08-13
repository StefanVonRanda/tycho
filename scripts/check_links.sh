#!/bin/sh
# Check that every relative Markdown link in the repo points at a file that exists,
# and that no tracked Markdown file carries a raw C0 control byte.
# Catches the dead file links that creep in when docs are moved or renamed.
#
#   scripts/check_links.sh
#
# Only real prose links are checked: fenced code blocks and inline `code` spans are
# ignored (so `[text](url)` shown as syntax, or Tycho code like `ops[1](5)`, don't
# count), http(s)/#anchor/mailto targets are skipped, and the webserver example's
# served content (examples/*/content, whose links are runtime routes) is excluded.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail="$root/.linkcheck_fail"
rm -f "$fail"

git ls-files '*.md' | grep -vE 'examples/[^/]*/content/' | while IFS= read -r md; do
    dir="$(dirname "$md")"
    # emit one link target per line, skipping code fences and inline code spans
    awk '
        /^[[:space:]]*```/ { fence = !fence; next }
        fence { next }
        {
            line = $0
            gsub(/`[^`]*`/, "", line)                 # strip inline code
            while (match(line, /\]\([^)]+\)/)) {
                t = substr(line, RSTART + 2, RLENGTH - 3)
                print t
                line = substr(line, RSTART + RLENGTH)
            }
        }
    ' "$md" | while IFS= read -r target; do
        case "$target" in
            http://*|https://*|'#'*|mailto:*|'') continue ;;
        esac
        path="${target%%#*}"
        [ -z "$path" ] && continue
        if [ ! -e "$dir/$path" ]; then
            echo "DEAD  $md  ->  $target"
            touch "$fail"
        fi
    done
done

# Raw control bytes. A NUL in a tracked .md is invisible in every renderer and
# silently turns line-oriented tools binary; two were committed here and BOTH
# doc gates passed over them, because neither looks at bytes. Legal: TAB, LF,
# CR. Rejected: the rest of C0 -- 00-08 0B 0C 0E-1F. `tr -d` of every legal byte
# leaves exactly that set, one cheap pass per file; `od` runs only for a file
# that has already failed, so the green path never pays for it. Markdown only:
# tracked binaries (the PNG fixtures) are full of NULs by construction, and
# excluding them would cost more machinery than this failure is worth.
git ls-files '*.md' | while IFS= read -r md; do
    if [ "$(LC_ALL=C tr -d '\11\12\15\40-\377' < "$md" | wc -c)" -eq 0 ]; then continue; fi
    LC_ALL=C od -An -v -tu1 "$md" | awk -v f="$md" '
        { for (i = 1; i <= NF; i++) {
              b = $i + 0
              if (b < 32 && b != 9 && b != 10 && b != 13) {
                  printf "CTRL  %s  byte offset %d (0x%02X)\n", f, off, b
                  if (++n >= 5) { print "      (further hits suppressed)"; exit }
              }
              off++ } }'
    touch "$fail"
done

# Reachability. A dead link is loud; a document nobody points at is silent, and
# this script said nothing about the second class until 2026-08-13 (open-list
# item 13). Run last so its verdict lands under this script's own.
python3 "$root/scripts/check_reachable.py" || touch "$fail"

if [ -f "$fail" ]; then
    rm -f "$fail"
    echo "link check: FAILED (dead links, raw control bytes, or unreachable docs above)"
    exit 1
fi
echo "link check: ok ($(git ls-files '*.md' | grep -vcE 'examples/[^/]*/content/') markdown files, no dead relative links; $(git ls-files '*.md' | wc -l) free of raw control bytes)"
