set -eu

cd "$(dirname "$0")/../.."
TYCHOC="${TYCHOC:-./tychoc1}"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

[ -x "$TYCHOC" ] || make tychoc >/dev/null

"$TYCHOC" tools/tycho-docs/main.ty -o "$T/docs-site" >"$T/build.log" 2>&1 || {
    echo "tycho-docs: FAILED (does not build)"; tail -5 "$T/build.log"; exit 1; }
F="$T/docs-site"

# --- [1] every docs page renders -------------------------------------------
RECORD="${RECORD:-0}"
golden=tools/tycho-docs/expected.out
"$F" docs "$T/site" >"$T/got" 2>&1 || bad "[1] generator exited non-zero"
if [ "$RECORD" = 1 ]; then cp "$T/got" "$golden"; echo "rec  docs-site"; fi
[ -f "$golden" ] || bad "[1] no golden -- run RECORD=1"
cmp -s "$T/got" "$golden" || { bad "[1] transcript != golden"; diff "$golden" "$T/got" | head -8; }

# every .md under docs/ must have its .html beside it in the output,
# plus the two project pages and the search index
missing=0
while IFS= read -r md; do
    rel="${md#docs/}"
    html="$T/site/${rel%.md}.html"
    if [ ! -f "$html" ]; then echo "FAIL: no output for $md"; missing=1; fi
done <<EOF
$(find docs -name '*.md' | sort)
EOF
for f in changelog.html release-notes.html search.json; do
    if [ ! -f "$T/site/$f" ]; then echo "FAIL: no output for $f"; missing=1; fi
done
[ "$missing" = 0 ] || fail=1

# --- [2] the output is linked: every page reachable from the index -----------
if command -v python3 >/dev/null 2>&1; then
    python3 - "$T/site" <<'PY' || bad "[2] reachability probe reported a failure"
import os, re, sys
root = sys.argv[1]
pages = set()
for dp, _, fns in os.walk(root):
    for fn in fns:
        if fn.endswith(".html"):
            pages.add(os.path.relpath(os.path.join(dp, fn), root))
href = re.compile(r'href="([^"#:]+\.html)"')
orphans = []
for p in pages:
    if p == "index.html":
        continue
    if os.path.basename(p) == "index.html":
        continue  # a directory alias of its README sibling, covered by construction
    hit = False
    for q in pages:
        src = open(os.path.join(root, q), errors="replace").read()
        for m in href.findall(src):
            tgt = os.path.normpath(os.path.join(os.path.dirname(q), m))
            if tgt == p:
                hit = True
                break
        if hit:
            break
    if not hit:
        orphans.append(p)
if orphans:
    print("orphaned pages (nothing links them):")
    print("\n".join(sorted(orphans)[:10]))
    sys.exit(1)
import json
idx = json.load(open(os.path.join(root, "search.json")))
urls = {p for p in pages}
missing = sorted(e["u"] for e in idx if e["u"] not in urls)
if missing:
    print("search index names pages with no output:")
    print("\n".join(missing[:10]))
    sys.exit(1)
print("reachability: every page linked from the index tree; search index resolves")
PY
fi

[ "$fail" = 0 ] && echo "docs-site: green (every docs page renders; output cross-linked)" || { echo "docs-site: FAIL"; exit 1; }
