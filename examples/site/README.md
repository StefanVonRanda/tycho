# site — a static-site generator dogfood

Rule: eight corelib modules, no FFI, no external dependencies, zero manual memory management.

A tiny static-site generator, and the second corelib-composing dogfood (after
[`../fetch`](../fetch)). It reads a site directory and writes one HTML page per
source file plus a date-sorted index, composing **eight** corelib modules:

| module          | role                                                        |
|-----------------|-------------------------------------------------------------|
| `core:io`       | read sources, list `content/`, write the HTML output        |
| `core:path`     | extensions (`.md` filter), slugs (`stem`), path joins        |
| `core:json`     | the `site.json` config                                      |
| `core:csv`      | the `authors.csv` handle → name table                       |
| `core:strings`  | frontmatter + Markdown parsing (`lines`/`split_once`/…)      |
| `core:sort`     | `argsort` the pages by date for the index                   |
| `core:datetime` | format each page's UNIX date as an ISO timestamp            |
| `core:sha256`   | a content hash of every rendered page                       |

## Input

```
examples/site/
  site.json            { "title": ..., "base_url": ... }
  authors.csv          handle,name
  content/*.md         `key: value` frontmatter, then a Markdown-subset body
```

The Markdown subset is block-level: `# `/`## `/`### ` headings, `- ` list items
(grouped into `<ul>`), `> ` quotes, `| ` tables (a `---` row makes a header),
triple-backtick fenced code (with an optional language for a CSS class), and
blank-line-separated paragraphs, all HTML-escaped. Inline: `` `code` ``,
**bold**, and `[text](url)`.

## Running

```
$ site <site_dir> <out_dir>
```

`make site` (or `sh examples/site/run.sh`) builds the program, runs it
against this fixture site, and asserts the build report (page list + per-page
content hashes) byte-identical against `expected.out`. The emitted C is also run
under ASan/UBSan — a heavy string-building / per-scope-arena workload. Since it has no external dependency,
it's part of `make ci`.
