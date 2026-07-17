#!/bin/sh
# Check that every relative Markdown link in the repo points at a file that exists.
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

if [ -f "$fail" ]; then
    rm -f "$fail"
    echo "link check: FAILED (dead links above)"
    exit 1
fi
echo "link check: ok ($(git ls-files '*.md' | grep -vcE 'examples/[^/]*/content/') markdown files, no dead relative links)"
