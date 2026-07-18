#!/usr/bin/env python3
"""Generate the GitHub wiki's reader-doc pages from the repo's /docs.

The repo's /docs is the canonical source; the wiki is a *generated mirror* of
the outward-facing reader documentation, so nothing has to be maintained twice.

Ported:      docs/tutorial, thesis, architecture, from-c-to-arenas,
             docs/reference/*, docs/guides/*
Not ported:  docs/spec/ (formal, large), docs/internals/, docs/rfc/  — these are
             left as links into the repo.

Every relative link is rewritten: to the matching wiki page when the target is
ported, otherwise to a full github.com/.../blob|tree/main/ URL. Fenced code is
left untouched. Each mirrored page gets a banner noting the repo is canonical.
The hand-written hub pages (Home, FAQ, Installing-Tycho, Documentation-Map,
Contributing, _Sidebar, _Footer) are regenerated or link-unified in place.

Usage:
    scripts/sync-wiki.py [WIKI_DIR]      # default: <repo>/.wiki

The wiki is a separate git repo (github.com/StefanVonRanda/tycho.wiki.git,
branch `master`). Clone it into WIKI_DIR first, or use `make wiki`, which clones
or pulls it for you, runs this script, and prints the review/commit/push steps.
Re-runnable and idempotent: it overwrites the generated pages in place.
"""
import os, re, sys, posixpath, glob

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIKI = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, ".wiki")
BLOB = "https://github.com/StefanVonRanda/tycho/blob/main/"
TREE = "https://github.com/StefanVonRanda/tycho/tree/main/"

if not os.path.isdir(os.path.join(REPO, "docs")):
    sys.exit("error: no docs/ under %s — run from the tycho repo." % REPO)
if not os.path.isdir(WIKI):
    sys.exit("error: wiki dir %r does not exist. Clone the wiki repo there first,\n"
             "       or run `make wiki` which clones/pulls it for you." % WIKI)

# ── ported map: repo-relative path -> wiki slug, plus H1 titles ──
pmap, title = {}, {}
def add(relpath, slug): pmap[relpath] = slug
add("docs/tutorial.md", "Tutorial")
add("docs/thesis.md", "Thesis")
add("docs/architecture.md", "Architecture")
add("docs/from-c-to-arenas.md", "From-C-to-Arenas")
for p in sorted(glob.glob(os.path.join(REPO, "docs/reference/*.md"))):
    add("docs/reference/" + os.path.basename(p), "Reference-" + os.path.splitext(os.path.basename(p))[0])
for p in sorted(glob.glob(os.path.join(REPO, "docs/guides/*.md"))):
    add("docs/guides/" + os.path.basename(p), "Guide-" + os.path.splitext(os.path.basename(p))[0])

def h1_of(path):
    with open(path) as f:
        for line in f:
            if line.startswith("# "):
                return line[2:].strip()
    return os.path.splitext(os.path.basename(path))[0]
for relpath in pmap:
    title[relpath] = h1_of(os.path.join(REPO, relpath))

# ── link rewriting ──
def convert_full(url):
    if url.startswith(BLOB):
        path, sep, anc = url[len(BLOB):].partition("#")
        if path in pmap:
            return pmap[path] + (("#" + anc) if anc else "")
    return url

def rewrite(url, reldir):
    if url.startswith(("http://", "https://")):
        return convert_full(url)
    if url.startswith(("mailto:", "tel:", "//")) or url.startswith("#"):
        return url
    path, sep, anc = url.partition("#")
    if path == "":
        return url
    norm = posixpath.normpath(posixpath.join(reldir, path))
    if norm in pmap:
        return pmap[norm] + (("#" + anc) if anc else "")
    base = TREE if (path.endswith("/") or "." not in posixpath.basename(norm)) else BLOB
    return base + norm + (("#" + anc) if anc else "")

LINK = re.compile(r'\]\(\s*<?([^)\s>]+)>?((?:\s+"[^"]*")?)\s*\)')
DEF  = re.compile(r'^(\s*\[[^\]]+\]:\s+)(\S+)(.*)$')

def transform(text, reldir):
    out, fence = [], None
    for line in text.split("\n"):
        st = line.lstrip()
        if fence:
            if st.startswith(fence): fence = None
            out.append(line); continue
        if st.startswith("```") or st.startswith("~~~"):
            fence = "```" if st.startswith("```") else "~~~"
            out.append(line); continue
        line = LINK.sub(lambda m: "](" + rewrite(m.group(1), reldir) + m.group(2) + ")", line)
        m = DEF.match(line)
        if m:
            line = m.group(1) + rewrite(m.group(2), reldir) + m.group(3)
        out.append(line)
    return "\n".join(out)

# ── mirror the reader docs ──
n = 0
for relpath, slug in pmap.items():
    reldir = posixpath.dirname(relpath)
    body = transform(open(os.path.join(REPO, relpath)).read(), reldir)
    banner = ("*Mirrored from [`%s`](%s%s) in the repo — that is the canonical source; "
              "edits to this wiki page are overwritten when the wiki is re-synced.*\n\n---\n\n"
              % (relpath, BLOB, relpath))
    open(os.path.join(WIKI, slug + ".md"), "w").write(banner + body + "\n")
    n += 1

# ── unify links in hand-written hub pages (full repo URL -> wiki slug) ──
for hub in ("FAQ.md", "Installing-Tycho.md", "Contributing.md"):
    fp = os.path.join(WIKI, hub)
    if os.path.exists(fp):
        open(fp, "w").write(transform(open(fp).read(), "___none___"))

# ── ordered nav ──
def label(relpath): return title.get(relpath, pmap[relpath])
REF_ORDER = ["index","basics","types","functions","structs-tuples","enums-options",
             "arrays-slices","subscripts","maps","generics","concurrency","ffi","packages","builtins"]
GUIDE_ORDER = ["memory-model","arrays-structs","map-values","map-mutation","generics",
               "concurrency","ffi","packages","corelib","perf","debugging"]
def ordered(prefix, order):
    keys = [k for k in pmap if k.startswith("docs/" + prefix + "/")]
    def rank(k):
        stem = os.path.splitext(os.path.basename(k))[0]
        return order.index(stem) if stem in order else len(order)
    return sorted(keys, key=lambda k: (rank(k), k))
REF, GUIDES = ordered("reference", REF_ORDER), ordered("guides", GUIDE_ORDER)
START = ["docs/tutorial.md","docs/thesis.md","docs/architecture.md","docs/from-c-to-arenas.md"]

# ── _Sidebar ──
sb = ["### Tycho Wiki\n", "- [Home](Home)", "- [Installing Tycho](Installing-Tycho)",
      "- [FAQ](FAQ)", "- [Documentation Map](Documentation-Map)", "- [Contributing](Contributing)\n",
      "### Start\n"]
for k in START: sb.append("- [%s](%s)" % (label(k), pmap[k]))
sb.append("\n### Reference\n")
for k in REF: sb.append("- [%s](%s)" % (label(k), pmap[k]))
sb.append("\n### Guides\n")
for k in GUIDES: sb.append("- [%s](%s)" % (label(k), pmap[k]))
sb += ["\n### Specification\n", "- [Language spec (in repo)](%sdocs/spec)\n" % TREE, "---",
       "- [Website](https://tycho-lang.com)", "- [Repository](https://github.com/StefanVonRanda/tycho)"]
open(os.path.join(WIKI, "_Sidebar.md"), "w").write("\n".join(sb) + "\n")

# ── Documentation-Map ──
blurb = {"docs/tutorial.md":"Hello-world to a real program, about an hour.",
         "docs/thesis.md":"The one idea, with measurements and the places it costs.",
         "docs/architecture.md":"How the two compilers fit together.",
         "docs/from-c-to-arenas.md":"The memory model, explained from C."}
dm = ["# Documentation Map\n",
      "The reader documentation is mirrored into this wiki (pages below); the canonical "
      "source stays in the repo under [`/docs`](%sdocs). The formal language specification "
      "is large and lives in the repo only.\n" % TREE,
      "## Start here\n", "| Page | What it is |", "| --- | --- |"]
for k in START: dm.append("| [%s](%s) | %s |" % (label(k), pmap[k], blurb[k]))
def two_col(keys, header):
    rows = ["\n## %s\n" % header, "| Page | | Page |", "| --- | --- | --- |"]
    half = (len(keys) + 1) // 2
    for i in range(half):
        l = "[%s](%s)" % (label(keys[i]), pmap[keys[i]])
        r = ("[%s](%s)" % (label(keys[i+half]), pmap[keys[i+half]])) if i+half < len(keys) else ""
        rows.append("| %s | | %s |" % (l, r))
    return rows
dm += two_col(REF, "Language reference")
dm += two_col(GUIDES, "Guides")
dm += ["\n## Specification\n",
       "The precise semantics live in the repo: [`docs/spec/`](%sdocs/spec) — eighteen "
       "numbered chapters plus appendices.\n" % TREE,
       "## Internals\n",
       "Design notes and rationale, repo only: [`docs/internals/`](%sdocs/internals).\n" % TREE]
open(os.path.join(WIKI, "Documentation-Map.md"), "w").write("\n".join(dm) + "\n")

# ── Home ──
open(os.path.join(WIKI, "Home.md"), "w").write("""# Tycho

**Tycho is an experimental systems language built to test one idea: implicit hierarchical arenas under value semantics.** Each scope owns a memory arena, freed when the scope exits; with no reference type in the language, the compiler sees every value's lifetime from the syntax alone and inserts every allocation and free itself. The result is automatic memory management — no garbage collector, no manual `free` — from lexical scope rather than a runtime. It transpiles to C and builds with `cc` and `make`.

It's a research project — an experiment testing that one idea — not a production language. But it's a heavily-checked one, and that's the part most experiments this young skip; see [Is it production-ready?](FAQ#is-tycho-production-ready) in the FAQ.

```
fn greet(name: string) -> string:
    return "hello " + name

fn main():
    name := input()
    println(greet(name))
```

## Start here

- **[Install Tycho](Installing-Tycho)** — a C compiler and `make`, nothing else to install.
- **[Tutorial](Tutorial)** — from hello-world to a real program in about an hour.
- **[The thesis](Thesis)** — the one idea, with the measurements and the places it costs.
- **[From `malloc` to arenas](From-C-to-Arenas)** — the memory model, explained from the C you already know.

## Find your way around

The reader documentation is mirrored into this wiki — see the **[Documentation Map](Documentation-Map)** for the full index of the reference and the how-to guides. The canonical source stays in the repo under [`/docs`](%sdocs), and the formal language specification lives there.

- **[FAQ](FAQ)** — garbage collector? references? production-ready? how fast?
- **[Documentation Map](Documentation-Map)** — reference, guides, spec.
- **[Contributing](Contributing)** — the two-compiler discipline and the local gate.

## Links

- Website — <https://tycho-lang.com>
- Source — <https://github.com/StefanVonRanda/tycho>
- License — MIT
""" % TREE)

# ── _Footer ──
open(os.path.join(WIKI, "_Footer.md"), "w").write(
    "The reader docs here are mirrored from [`/docs`](%sdocs) in the repo — the canonical "
    "source. · [tycho-lang.com](https://tycho-lang.com) · MIT licensed\n" % TREE)

# ── audit: every internal link must resolve to a real wiki page ──
pages = {os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(WIKI, "*.md"))}
bad, fence = [], None
for p in glob.glob(os.path.join(WIKI, "*.md")):
    fence = None
    for lineno, line in enumerate(open(p), 1):
        st = line.lstrip()
        if fence:
            if st.startswith(fence): fence = None
            continue
        if st.startswith("```"): fence = "```"; continue
        if st.startswith("~~~"): fence = "~~~"; continue
        for m in LINK.finditer(line):
            t = m.group(1)
            if t.startswith(("http://", "https://", "mailto:", "tel:", "//", "#")): continue
            if t.split("#", 1)[0] not in pages:
                bad.append((os.path.basename(p), lineno, t))

print("mirrored %d reader-doc pages (reference: %d, guides: %d) into %s"
      % (n, len(REF), len(GUIDES), WIKI))
if bad:
    print("FAIL: %d dangling internal link(s):" % len(bad))
    for b in bad[:40]: print("   %s:%d -> %s" % b)
    sys.exit(1)
print("link audit: OK (no dangling internal links)")
