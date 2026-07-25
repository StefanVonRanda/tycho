# Make Tycho able to write a real web server

Follows the front-door-defects plan (archived: `docs/internals/plan-front-door-DONE.md`,
42 phases, head `894a767`). That plan hardened the compiler. This one is the opposite
posture: **the deliverable is a program, and the language work exists only to unblock it.**

Direction set by the user, 2026-07-25: *"we should stop working on tychoc0, its done its
job … we start building useful software and validate the language that way. My biggest
concern about tycho is its ergonomics, the syntax, expressiveness, readability and
general UX."*

## Goal

An honest-to-goodness web server written in Tycho — concurrent, serving a real directory
of real files (HTML, CSS, JS, **images and fonts**), with correct content types,
persistent connections, and sane behaviour under malformed input. Done = it serves a real
site to a real browser, and we have an honest written account of what writing it felt
like.

## Anti-scope — read this before adding a phase

The previous plan grew from 6 phases to 44 because every phase filed more. That is the
failure mode to avoid, not a template. Rules for this plan:

- **A phase belongs here only if the web server cannot be written without it.** Not "is
  wrong", not "is inconsistent" — *blocks the program*.
- **No conformance audits, no citation sweeps, no gate-building** unless a gate is the
  cheapest way to keep the server working.
- Discovered defects that do NOT block the server get one line in `FRICTION.md` and are
  left alone. That file is a deliverable; fixing everything in it is not.
- **Stop condition: the server serves the site.** Not "the language is good."

## What the probes already established (2026-07-25, measured not assumed)

Written while trying to stand up the simplest possible concurrent server.

| # | Finding | Evidence | Blocks the server? |
|---|---|---|---|
| 1 | `spawn` gives **real parallelism**, and a task can own a socket fd | two 600ms handlers: **1214ms sequential vs 609ms concurrent** | no — this is the good news |
| 2 | **Task handles are affine**: cannot be stored in any container or aggregate | `push(ts, spawn work(1))` → `a task handle cannot be stored in a container or aggregate -- wait(t) first`; `[]Task(int)` → `unknown type 'Task'` | **shapes the architecture** |
| 3 | **`httpd` bodies are `string`-only** — `Request.body`, `Response.body`, and `render()` builds the whole response as a string before `to_bytes` | `corelib/httpd/httpd.ty:30,:37`, `:208`; its own header admits an interior `0x00` truncates | **YES — cannot serve a favicon** |
| 4 | **No content-type mapping** — defaults to `text/plain` | `httpd.ty` `render()`; grep of `corelib/*/*.ty` finds no MIME table | **YES — browsers show HTML as source** |
| 5 | **No keep-alive** — the connection is closed after one request | no `Connection`/`keep-alive` handling anywhere in `httpd.ty` | **YES for a real site** (dozens of assets per page) |
| 6 | **No `sleep` anywhere user-facing** | exhaustive grep of `corelib/`, `runtime/`, `docs/spec/`: `nanosleep` exists at `runtime/tycho_rt.c:822` for `select` only | no, but no backoff/shutdown either |
| 7 | **`handle` is a reserved keyword** — cannot name a function `handle` | `fn handle(...)` → `error: expected a procedure name`, which never says why | no — pure UX |
| 8 | `package main` makes the compiler compile **the whole directory** | stray probe files in the same dir were pulled into the build | no — Go-like, by design |

## Pre-flight

- **Phase 1 changes a published corelib API.** `Request.body`/`Response.body` going from
  `string` to `bytes` breaks every existing consumer: `examples/webserver/main.ty`,
  `examples/site`, `examples/weblog`, `corelib/test/httpd`. They must land in the same
  commit or `make corelib`/`make test` go red. This is the one phase with real blast
  radius; the rest are additive.
- **Unknown, resolved by Phase 1:** whether Tycho's `bytes` is ergonomic enough to build a
  response with. If concatenating `bytes` is painful, the server's whole IO path is
  painful, and that is itself a headline ergonomics finding — record it either way.
- Reversibility: git; one commit per phase; no user data.
- The gate set still applies (`make test`, `corelib`, `conc`, `fixpoint`, `ilp32`,
  `asan-self`, `frontparity`, `spec-check`, `check-links`) — these are corelib changes and
  the corelib is gated. Run them; do not add to them.
- **ENVIRONMENT:** run every gate as `env -u LD_PRELOAD make …` (a foreign preload in the
  dev shell breaks ASan link order; not a code bug).

## Phases

- [x] **Phase 1 — decide the concurrency shape (probe only, no library change)**
  - Task handles cannot be stored, so thread-per-connection-with-tracking is out. Probe
    what IS expressible for N long-lived workers, and pick one:
    (a) N `spawn`s into N named locals, each running its own `accept` loop on the shared
    listening fd (fd is an `int`, copies freely);
    (b) `parallel for i in range(N):` with an accept loop per iteration — but that is a
    *data-parallel reduction* construct chunked across `tycho_ncpu()`, so verify it
    actually yields N independent long-lived loops rather than chunked batches;
    (c) accept on the main task, `spawn` per connection fire-and-forget — needs an answer
    to "who waits, and does the program exit while tasks are live?"
  - Done when: one shape is chosen with a measurement behind it (N concurrent clients
    served in ~1 unit of time, not N), and the losing options have a recorded reason.
    This is the server's architecture; get it right before writing the server.

  **DONE 2026-07-25 — chose (a'), recursive fan-out of shape (a).**

  Method: four probe programs, each its own directory (`package main` compiles the
  whole directory). Handler does a fixed 500ms spin on `time.elapsed_ms` — a wall-clock
  wait `-O2` cannot fold away. Client is N parallel `curl`s, wall clock measured around
  the batch (`scratchpad/bench.sh`). Build: `env -u LD_PRELOAD ./tychoc <dir>/main.ty -o
  <dir>/prog`. Host: 16 CPUs. One unit = ~500ms.

  | Shape | Workers | Clients | Wall | Verdict |
  |---|---|---|---|---|
  | (a) N spawns into N named locals | 4 | 1 | 513ms | baseline = 1 unit |
  | (a) same | 4 | **4** | **511ms** | **1 unit — real concurrency** |
  | (a) same — control | 4 | 8 | 1011ms | 2 units, as it must be |
  | (a) 8 named locals | 8 | 8 | 510ms | 1 unit |
  | (a') recursive fan-out, N=8 runtime | 8 | 8 | **509ms** | **1 unit — chosen** |
  | (a') same — control | 8 | 16 | 1012ms | 2 units, so exactly 8 loops are live |
  | (b) `parallel for i in range(4)` | 4 | 4 | 510ms | 1 unit *on a 16-CPU host only* |
  | (b) same, `TYCHO_THREADS=2` | **2** | 4 | 1012ms | **2 units — chunking collapsed it** |
  | (b) same, `TYCHO_THREADS=1` | **1** | 2 | 1010ms | 2 units |
  | (c) accept on main, spawn per conn | — | 1 | 510ms | 1 unit |
  | (c) same | — | **4** | **2011ms** | **4 units — fully serial** |

  **(c) is disqualified, and the source says why.** `spawn` must bind a handle: a bare
  `spawn work(1)` statement is rejected — `error: a statement must be a declaration,
  assignment, or call -- a bare expression has no effect`. Once bound, the compiler
  emits an implicit join at the handle's scope exit — `src/tychoc.c:9148`
  (`taskvar_push(sfmt("tycho_task_finish(h_%s)", s->name))`) and `compiler/tychoc0.ty:9100,:9116`
  — which runs `pthread_join` for any un-waited task (`runtime/tycho_rt.c:602-606`). In an
  accept loop the handle is a loop-body local, so every iteration joins before the next
  `accept`: 4 clients cost 4 units. Answering the phase's question directly: an un-waited
  task does **not** leak, abort, or get reaped asynchronously — the parent blocks for it.
  Measured: `main` reached its last statement at 0ms, the process exited at 505ms.
  Fire-and-forget is not expressible in Tycho.

  **(b) is disqualified as unsound, not as slow.** `parallel for` fans out
  `tycho_ncpu()` chunk threads (`runtime/tycho_rt.c:843-852`), so `range(N)` yields
  `min(N, ncpu)` *live* iterations. Iterations chunked behind a non-returning one never
  start at all — an accept loop never returns. It read as the prettiest shape and passed
  at N=4 purely because the host has 16 CPUs; `TYCHO_THREADS=2` silently cut the server
  to 2 workers with no diagnostic. A worker count that depends on the machine, not the
  program, is not an architecture.

  **Chosen: (a') recursive fan-out.** Source:

  ```tycho
  fn accept_loop(srv: int, id: int) -> int:
      served := 0
      for served < 1000:
          conn := net.accept(srv)
          if conn < 0:
              return served
          req := httpd.read_request(conn)
          spin_ms(500)
          httpd.write_response(conn, httpd.response(200, "worker " + str(id) + "\n"))
          net.close_fd(conn)
          served = served + 1
      return served

  fn worker(srv: int, id: int, remaining: int) -> int:
      if remaining > 1:
          peer := spawn worker(srv, id + 1, remaining - 1)
          n := accept_loop(srv, id)
          return n + wait(peer)
      return accept_loop(srv, id)

  fn main():
      srv := net.listen("127.0.0.1", 8105)
      print(str(worker(srv, 1, 8)))
  ```

  **Readability, honestly.** Plain (a) at N=8 is literally eight near-identical
  `w1 := spawn worker(srv, 1)` lines followed by eight `wait(w1)` lines — sixteen lines
  of copy-paste where every other language writes one loop, and the worker count is
  frozen at *write* time, not run time. That is a real ergonomic finding about Tycho and
  it belongs on the record even though (a) won on measurement.

  (a') gets the count back to a runtime value with one function and no repetition, and
  it is the shape the server will use. But it is a workaround wearing a nice coat: the
  recursion exists solely because a task handle is affine and cannot be stored, so the
  only place to put N handles is N stack frames. `peer := spawn worker(...)` is doing
  the job of `for _ in range(n): spawn worker(...)`, and a reader has to already know
  the affinity rule to see why. It also builds an N-deep join chain, so shutdown unwinds
  through every frame — fine at N=8, and something to remember if N ever gets large.
  The honest summary: Tycho can express a concurrent server cleanly enough to write one,
  but "start N workers" — the most ordinary thing a server does — has no direct spelling.

- [ ] **Phase 2 — BLOCKER: `httpd` carries `bytes` bodies**
  - `Request.body` and `Response.body` become `bytes`; `render()` stops building the
    response as one string. Headers stay `string` (they are ASCII by spec).
  - Update every consumer in the same commit: `examples/webserver`, `examples/site`,
    `examples/weblog`, `corelib/test/httpd`, plus any golden they own.
  - Record honestly how `bytes` felt to work with — concatenation, slicing, converting
    to/from `string`. That answer decides whether Tycho can do IO comfortably at all.
  - Done when: a PNG round-trips through `read_request`/`write_response` byte-identically,
    fixture-locked, and the full gate set is green.

- [ ] **Phase 3 — BLOCKER: content type by file extension**
  - A MIME table (`.html .css .js .json .svg .png .jpg .gif .woff2 .ico .txt .wasm` at
    minimum) and a `content_type(path) -> string`. Put it in `httpd` unless there is a
    reason to prefer `path`.
  - Unknown extension → `application/octet-stream`, never `text/plain` (guessing wrong is
    how browsers execute things they should download).
  - Done when: serving a directory gives each file the right type, verified with `curl -I`.

- [ ] **Phase 4 — BLOCKER: persistent connections**
  - HTTP/1.1 defaults to keep-alive; `httpd` closes after every request, so a page with 30
    assets pays 30 handshakes. Implement: read `Connection:`, honour `close`, keep the
    socket open otherwise, loop reading requests off one connection, and time out an idle
    peer so a slow-loris cannot pin a worker forever.
  - The idle timeout needs Phase 5, or a non-blocking read with a deadline — establish
    which before starting.
  - Done when: `curl -v` shows connection reuse across two requests, and an idle
    connection is dropped rather than holding a worker.

- [ ] **Phase 5 — FRICTION: `sleep` in `core:time`**
  - `runtime/tycho_rt.c:822` already calls `nanosleep` for `select`; expose `sleep_ms(ms)`
    and `sleep_ns(ns)`. Small, additive, and needed for backoff, retry and shutdown.
  - Done when: it sleeps for the requested duration (measured, not assumed) and is spec'd
    in `16-builtins.md`/the `core:time` docs.

- [ ] **Phase 6 — FRICTION (cheap): say why `handle` is rejected**
  - `fn handle(...)` gives `expected a procedure name`. `handle` is a real keyword
    (`docs/spec/01-lexical.md`), so the fix is the *message*, not the grammar: name the
    keyword. Check the sibling keywords give the same treatment.
  - Done when: the diagnostic names the reserved word, in both compilers.

- [ ] **Phase 7 — THE POINT: write the server**
  - Worker pool per Phase 1. Serves a real content directory: static files with correct
    types, binary-safe, keep-alive, index resolution, 404/405/400, path-traversal refusal,
    request logging.
  - Not a fixture, not a demo: something to point a browser at and actually use. Decide
    with the user where it lives (`examples/` is for demos; this may want its own home).
  - **Keep `FRICTION.md` as you go** — one line per moment the language got in the way.
    That file is the real output of this plan.
  - Done when: it serves a real site to a real browser, survives `curl` abuse (malformed
    requests, huge headers, early disconnect), and `FRICTION.md` is an honest account.

## Out of scope

- TLS (`corelib/tls` exists; HTTP first, and a reverse proxy is the normal answer anyway).
- HTTP/2, ranges, compression, caching headers, virtual hosts — none block a real server.
- The two items left open by the previous plan (`Phase 36`'s foreign-type-parameter hole,
  `Phase 44`'s citation corpus). The generics hole may surface here naturally; if it
  blocks the server it earns a phase, otherwise it stays where it is.
