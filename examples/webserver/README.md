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
(`sh examples/webserver/run.sh`).

Routes: `/` (landing), `/blog` (index, newest first), `/blog/<slug>`,
`/wiki/<page>`, else `404`. A URL segment that becomes a filename is sanitized to
`[a-z0-9-]` only, so `/blog/../secret` is a `404`, not a path traversal (Rule 5).

Composes `core:httpd`, `core:net`, `core:io`, `core:strings`, `core:sort`, and
`core:markdown` — zero manual memory management.

## Dogfood findings

### Gotchas (not bugs)