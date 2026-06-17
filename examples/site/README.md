# site — a static-site generator dogfood

A tiny static-site generator, and the second corelib composing dogfood (after
[`../fetch`](../fetch)). It reads a site directory and writes one HTML page per
source file plus a date-sorted index, composing **eight** corelib modules with no
FFI, no external dependencies, and zero manual memory management:

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

The Markdown subset is block-level: `# `/`## ` headings, `- ` list items
(grouped into `<ul>`), and blank-line-separated paragraphs, all HTML-escaped.

## Running

```
$ site <site_dir> <out_dir>
```

`make site` (or `sh examples/site/run.sh`) builds the program by all three
compilers (the C `hierc`, `hierc0` via `--bundle`, and standalone `hierc0`), runs
each against this fixture site, and asserts the build report (page list + per-page
content hashes) byte-identical against `expected.out`. The emitted C is also run
under ASan/UBSan — a heavy string-building / per-scope-arena workload that
exercises exactly what the thesis claims. Because it has no external dependency,
it is part of `make ci`.
