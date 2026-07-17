# webserver — a web server written in Tycho

A small HTTP server that serves a site — a landing page, a Markdown **blog**
(index + posts) and a Markdown **wiki** — over `core:httpd` + `core:net`, rendering
content through `core:markdown` inside a shared HTML layout. It's the website
dogfood: the same machinery a real public Tycho site (landing / blog / wiki) needs.

```
make
./tychoc examples/webserver/main.ty -o server
./server --serve                 # listen on 127.0.0.1:8080
PORT=8137 ./server --serve       # …or another port
./server                         # self-test: dispatch fixed routes, print them
```

Then open `http://127.0.0.1:8080/`.

## Design

The interesting logic is a **pure** `route(req) -> Response` — given a parsed
request, it produces a response (reads the `.md` from disk, renders it, wraps it in
the layout). `--serve` wraps that in a `core:net` accept loop; the default self-test
wraps it in a fixed-path loop and prints each response — so routing is testable
without a live socket, and the self-test output is golden-locked
(`sh examples/webserver/run.sh`, both compilers).

Routes: `/` (landing), `/blog` (index, newest first), `/blog/<slug>`,
`/wiki/<page>`, else `404`. A URL segment that becomes a filename is sanitized to
`[a-z0-9-]` only, so `/blog/../secret` is a `404`, not a path traversal (Rule 5).

Composes `core:httpd`, `core:net`, `core:io`, `core:strings`, `core:sort`, and
`core:markdown` — zero manual memory management.

## Dogfood findings

- **A real `tychoc` soundness bug — found here and fixed (the headline).** Serving a
  binary asset returns a struct holding the file `bytes`; the body came back as
  *reused arena memory* (fragments of the response headers). Root cause: `copy_into`
  (which re-homes a heap value into the caller's arena) had no `T_BYTES` case, so a
  `bytes` field of a returned struct wasn't deep-copied — it dangled into the callee's
  freed scope, and a later allocation reused the block. A use-after-free that **ASan
  can't catch** (valid memory, wrong data), and a `tychoc`/`tychoc0` *divergence* the
  output-only fixpoint missed (tychoc0 re-homed bytes; tychoc didn't). Fixed in
  `copy_into`; regression-locked by `tools_check.sh`'s `bytes-rehome` assertion.
- **Binary/static serving now works.** With `io.read_bytes` (added for this) and the
  compiler fix, the server serves CSS and a favicon PNG (20 NUL bytes) **byte-identical
  over the socket** — the string-model `0x00` limit is sidestepped by keeping the body
  as `bytes` and writing it via `net.write`. The write stays in this package (`net`
  directly) rather than a cross-package `httpd` helper; a `httpd.write_bytes` is a
  reasonable follow-up now that the bytes-lifetime bug is gone.
- **`core:markdown`'s second consumer.** The renderer (built for this) works
  end-to-end — the blog index is even *built as Markdown and rendered*.
- **No router in `core:httpd`.** You hand-write the path matching. A small pattern
  router (`/blog/:slug`) would be a natural corelib addition — demand-gated.

### Gotchas (not bugs)

- A cross-package type must be qualified: `httpd.Response`, `httpd.Request`.
- `handle` is a reserved keyword (typed handles), so the router is named `route`.
- An HTML entity written in Markdown source (`&mdash;`) is escaped to
  `&amp;mdash;` — correct behavior; use the literal character (`—`).
- Every shim-backed import must be named on the `tychoc0` link line — here
  `net_shim.c` **and** `io_shim.c` (see `run.sh`); `tychoc` auto-links them.
