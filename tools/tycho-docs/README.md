# tycho-docs — the docs tree as a browsable site

A static-site generator for this repository's documentation. Every `.md` under
`docs/` becomes an HTML page at the same relative path, plus a documentation
home, per-directory aliases, and the changelog and release notes.

Run from the repository root:

```sh
make docs-site            # docs/ -> build/docs-site/
./build/tycho-docs docs build/docs-site
sh tools/tycho-docs/run.sh # renderer, links, shell, and paragraph regressions
```

## One website shell

`site.css`, `site.js`, `header.html`, and `footer.html` in this directory own the
shared website shell. Public copy calls Tycho's library **corelib** and its
modules **corelib packages**, not stdlib packages. The landing page must identify
Tycho as a programming language before introducing the memory model. The generator
copies CSS and JavaScript to its output and
renders the templates into each page. Both the landing page and docs use that
same stylesheet, header navigation, and footer. The homepage keeps its **76rem
outer width**. Documentation instead has an independently centered **44rem
reading column**: sidebars sit outside it when the viewport is wider than 80rem.
At narrower widths both sidebars fold into toggles above the reading column;
they never squeeze the text to make room.

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

Search lives in the shared header on the homepage and every docs page. Clicking
Search, Ctrl/Cmd+K, or `/` outside an editable field opens a native modal dialog.
It traps focus, makes the background inert, and restores focus on close. Escape,
the close button, or a backdrop click dismisses it; arrow keys move through
results and Enter opens a result. Without JavaScript the header link goes to
the documentation index.

Search uses the generated static `search.json`, with no backend. The index loads
only when the dialog opens and is reused on that page. It searches complete
document bodies, requires all query terms, and ranks exact titles and title
matches ahead of body matches. Results include paths, excerpts, and highlights.
Loading, no-results, and fetch-failure states are explicit, with retry and a
link to browse the docs. Results use safe text nodes and validated relative URLs.

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

## Homepage editorial contract

The root `index.html` on `gh-pages` is truthful marketing for newcomers, not a
language specification or a test dashboard. Read it as a visitor who has never
heard of Tycho: identify the language and its benefit, explain the approach,
introduce the everyday language features, show a few real programs, state the
fit and limitations, then offer a clear way to try it and continue learning.
Define unfamiliar terms before relying on them. Keep benchmark methodology,
optimization internals, test inventories, and contributor procedures in their
own documents; link to them rather than reproducing them. Do not imply mature
production adoption, blanket memory safety, or universal performance wins.
Use plain, grammatical sentences and concrete descriptions. Avoid brochure
abstractions, repeated motivational slogans, forced slang, and fake familiarity.
Review the wording throughout the page, not just its opening. The speaker is
Tycho’s author, addressing prospective users—not an outside reviewer. Use “I”
where personal work, decisions, or invitations matter; state language capabilities
directly. Do not refer to the creator as “Tycho’s author,” invent a team with
“we,” or present author-written examples as independent adoption.

Reference landing pages for positioning and project voice:
[Zig](https://ziglang.org/), [Odin](https://odin-lang.org/),
[Go](https://go.dev/), and [Rust](https://rust-lang.org/).
Their ecosystem, adoption, and maturity claims do not apply to Tycho.

## Browser verification

`browser-check.cjs` runs with Puppeteer against the generated website (including
its landing page). Set `SITE_URL` to its local URL and optionally `SHOTS` to an
output directory. `CHROMIUM` overrides the browser executable. For example,
with the installed Alpine Chrome/Puppeteer image, mount this script at
`/usr/src/app/browser-check.cjs` and run it with Node. It checks shared chrome,
the centered reading width and outside/folded sidebars at 320–1920px in both themes, every document's mobile overflow and active
navigation, disclosures, search ranking/excerpts/keyboard/retry, safe result
rendering, and navigation without JavaScript.

## Limits

This is a small renderer, not a complete CommonMark implementation (for example,
list nesting and arbitrary block content within a list are not supported). Cross-page
anchor verification is still owned by `make check-links` on the source files.
The home lists files by group; the tutorial and curated Markdown index provide
the recommended learning order. Maintainer records remain reachable for source
citations, but are not the starting point for readers.
