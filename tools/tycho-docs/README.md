# tycho-docs — the docs tree as a browsable site

Rule: every `.md` under `docs/` becomes one HTML page at the same relative
path, plus a top-level index, a search index, two project pages
(`CHANGELOG.md` → `changelog.html`, `RELEASE_NOTES.md` → `release-notes.html`),
and a per-directory alias.

A static-site generator for this repo's own documentation. It walks `docs/`,
renders each Markdown file with the shared subset (headings with id anchors,
lists, quotes, tables, fenced code, rules, paragraphs, inline code/bold/links),
rewrites relative `X.md` targets to `X.html` so the tree browses as-is, and
writes an index grouped by directory. A directory's `README.html` is also
published as that directory's `index.html`, so `[spec/](spec/)`-style links
resolve on a static host the way they do on GitHub.

```sh
make docs-site            # docs/ -> build/docs-site/
./tycho-docs docs build/docs-site
```

## Input rules

- One page per `.md`; the title is the first `# ` heading.
- `<!-- ... -->` marker lines are dropped (they are tooling, not prose).
- A `---` line is a rule. Tables need leading pipes and a `---` separator
  row for a header, the GitHub shape.
- Links: relative `X.md` (or `X.md#frag`) becomes `X.html`; absolute URLs
  and same-page `#frag` links pass through untouched.

## Output

Standalone pages, no assets: one embedded stylesheet (brutalist tokens, dark
code panels, `prefers-color-scheme` halves), a shared masthead and footer, a
breadcrumb home link relative to each page's depth, and a footer naming the
build as pre-1.0. The index groups pages — Guide, Reference, Specification,
then Maintainer records (internals, RFCs: written for maintainers, kept
rendered so reference links resolve), then Project — with a client-side
search box over a generated `search.json` (titles + capped body text).

## Limits

- No cross-page anchor checking: a `#frag` that names no heading renders
  anyway. `make check-links` owns that verdict on the sources.
- The index lists files; it does not follow `docs/README.md`'s curated
  order. Curating the landing order is presentation, and this tool is a
  renderer.
