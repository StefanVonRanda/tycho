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

# Shared shell and navigation must resolve from every directory depth.
css = open(os.path.join(root, 'site.css')).read()
js = open(os.path.join(root, 'site.js')).read()
assert '.page{max-width:76rem;' in css
assert 'color:var(--paper);-webkit-text-stroke:2px var(--line);paint-order:stroke fill' in css
assert 'grid-template-columns:minmax(0,1fr) minmax(0,44rem) minmax(0,1fr)' in css
assert 'max-width:calc(44rem + 40px)' in css
assert 'pre code{color:inherit;font-size:inherit;' in css
assert 'table{display:block;max-width:100%;overflow-x:auto;' in css
assert 'Switch to ' in js and 'Retry search' in js
for p in pages:
    src = open(os.path.join(root, p), errors="replace").read()
    depth = p.count("/")
    prefix = "../" * depth
    assert 'href="' + prefix + 'site.css"' in src, p
    assert 'src="' + prefix + 'site.js"' in src, p
    assert 'class="wordmark" href="' + prefix + '../"' in src, p
    assert 'id="tbtn"' in src and 'class="site-footer page"' in src, p
    assert 'class="page docs-layout"' in src and 'class="docs-sidebar"' in src, p
    assert 'aria-label="Documentation"' in src and src.count('id="q"') == 1, p
    assert src.count('id="search-dialog"') == 1 and 'id="search-open"' in src, p
    assert 'data-root="' + prefix + '"' in src, p
    assert src.index('id="search-dialog"') < src.index('class="page docs-layout"'), p
    assert 'href="' + prefix + '../favicon.svg"' in src, p
    if p != 'index.html':
        assert 'aria-current="page"' in src, p
index = open(os.path.join(root, "index.html")).read()
assert 'New to Tycho?' in index and 'href="tutorial.html"' in index
tutorial = open(os.path.join(root, "tutorial.html")).read()
assert 'href="https://github.com/StefanVonRanda/tycho/blob/main/README.md#trying-it"' in tutorial
for p in pages:
    src = open(os.path.join(root, p)).read()
    for u in href.findall(src):
        target = os.path.normpath(os.path.join(os.path.dirname(p), u))
        assert target in pages, (p, u)
print("links: repository-only targets routed to source; all local HTML links resolve")
assert 'aria-label="On this page"' in tutorial
assert all('Switch to ' not in e['x'] and '--paper:' not in e['x'] for e in idx)
print("presentation: shared chrome, independent 44rem reading column, folding sidebars and shared global search")
PY
fi

# --- [3] hard-wrapped Markdown is one paragraph, including inline markup ---
mkdir -p "$T/paragraph-input" "$T/tools/tycho-docs"
cp tools/tycho-docs/site.css tools/tycho-docs/site.js tools/tycho-docs/header.html tools/tycho-docs/footer.html "$T/tools/tycho-docs/"
printf '# Paragraphs\n\nA **wrapped\nphrase** with a [link](reference.md).\n\nNext paragraph.\n\n```text\nfirst\nsecond\n```\n\n> A **wrapped\n> quotation**.\n>\n> Second quote paragraph.\n\n- A **wrapped\n  list item**.\n- Another item.\n\n2. A numbered item\n   with continuation.\n3. The next item.\n\nFinal paragraph\nwithout a trailing blank.\n' > "$T/paragraph-input/example.md"
"$F" "$T/paragraph-input" "$T/paragraph-site" > "$T/paragraph.log" 2>&1 || bad "[3] paragraph fixture did not render"
if command -v python3 >/dev/null 2>&1; then
    python3 - "$T/paragraph-site/example.html" <<'PY' || bad "[3] paragraph formatting regressed"
import sys
src = open(sys.argv[1]).read()
assert '<p>A <strong>wrapped phrase</strong> with a <a href="reference.html">link</a>.</p>' in src
assert '<p>Next paragraph.</p>' in src
assert '<code class="language-text">first\nsecond\n</code>' in src
assert '<blockquote>\n<p>A <strong>wrapped quotation</strong>.</p>\n<p>Second quote paragraph.</p>\n</blockquote>' in src
assert '<ul>\n<li>A <strong>wrapped list item</strong>.</li>\n<li>Another item.</li>\n</ul>' in src
assert '<ol start="2">\n<li>A numbered item with continuation.</li>\n<li>The next item.</li>\n</ol>' in src
assert '<p>Final paragraph without a trailing blank.</p>' in src
print("Markdown: soft-wrapped paragraphs/quotes/lists, ordered lists, fences and EOF intact")
PY
fi

[ "$fail" = 0 ] && echo "docs-site: green (every docs page renders; output cross-linked)" || { echo "docs-site: FAIL"; exit 1; }
