# tycho-docs — the docs tree as a browsable site

A static-site generator for this repository's documentation. Every `.md` under
`docs/` becomes an HTML page at the same relative path, plus a documentation
home, per-directory aliases, and the changelog and release notes.

Run from the repository root:

```sh
make docs-site            # docs/ -> build/docs-site/
./tycho-docs docs build/docs-site
sh tools/tycho-docs/run.sh # renderer, links, shell, and paragraph regressions
```

## One website shell

`site.css`, `site.js`, `header.html`, and `footer.html` in this directory own the
shared website shell. Public copy calls Tycho's library **corelib** and its
modules **corelib packages**, not stdlib packages. The generator copies CSS and JavaScript to its output and
renders the templates into each page. Both the landing page and docs use that
same stylesheet, header navigation, footer, and **76rem outer width**. The docs
reading column stays narrower within that shell, between two sidebars.

When publishing to `gh-pages`:

- Put generated output under `docs/`; the landing page loads the emitted
  stylesheet and JavaScript from that directory too.
- Keep vendored `fonts/` and `favicon.svg` at the website root. Fonts fall back
  to system fonts if these optional hosted assets are absent.
- Keep the landing page's header/footer markup synchronized with these templates:
  `{{root}}` is `./` there, and `{{docs}}` is `./docs/`. Generated docs use
  depth-correct relative paths instead.

The global sidebar groups guides, reference, specification, maintainer records,
and project pages. It opens the current group and marks the current document.
On mobile it becomes a keyboard-accessible disclosure. An on-page contents
sidebar links to level-two and level-three headings; it also becomes a disclosure
on narrower screens. Guides begin with the tutorial and introductory material,
not an alphabetical list.

Every docs page has search. The index is fetched only when search is focused,
so ordinary reading does not download it. It searches the complete document body, rather than
page chrome or a truncated prefix, requires all query terms, and ranks exact
titles and title matches ahead of body matches. Results include document paths,
matching excerpts, and highlights. `/` or Ctrl/Cmd+K focuses search; arrow keys
move through results and Escape clears it. Loading, no-results, and fetch-failure
states are explicit, with a retry button after failure. Results are constructed
with text nodes, and index URLs are validated before being made into links.

## Markdown and links

- The title is the first `# ` heading.
- Headings, bullet/numbered lists, quotes, tables, fences, rules, paragraphs,
  and inline code/bold/links use the repository's Markdown subset.
- Soft-wrapped prose and quotations join into paragraphs; indented list
  continuations stay inside their item. Blank lines separate paragraphs.
- HTML comment marker lines are dropped.
- Relative `.md` links become `.html`; directory `README.html` pages also get
  `index.html` aliases. Links outside the docs tree point to repository source,
  except changelog/release-note links which use their local published pages.
- Absolute URLs and same-page anchors pass through unchanged.

## Browser verification

`browser-check.cjs` runs with Puppeteer against the generated website (including
its landing page). Set `SITE_URL` to its local URL and optionally `SHOTS` to an
output directory. `CHROMIUM` overrides the browser executable. For example,
with the installed Alpine Chrome/Puppeteer image, mount this script at
`/usr/src/app/browser-check.cjs` and run it with Node. It checks shared chrome
and width, both themes at 320–1440px, every document's mobile overflow and active
navigation, disclosures, search ranking/excerpts/keyboard/retry, safe result
rendering, and navigation without JavaScript.

## Limits

This is a small renderer, not a complete CommonMark implementation (for example,
list nesting and arbitrary block content within a list are not supported). Cross-page
anchor verification is still owned by `make check-links` on the source files.
The home lists files by group; the tutorial and curated Markdown index provide
the recommended learning order. Maintainer records remain reachable for source
citations, but are not the starting point for readers.
