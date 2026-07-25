# FRICTION

One line per moment the language got in the way while writing a real web server.
Non-blocking by construction: anything that blocks the server earns a phase in
`plan.md` instead. This file is a deliverable; fixing everything in it is not.

- **Phase 1** — `spawn f(x)` as a bare statement is rejected with `a statement must be a declaration, assignment, or call -- a bare expression has no effect`, which never says the real rule: a task handle must be bound so the compiler can hang the implicit join on it.
- **Phase 1** — `parallel for i in range(N)` runs only `min(N, tycho_ncpu())` iterations concurrently (`runtime/tycho_rt.c:843-852`); iterations chunked behind one that never returns never start, and nothing warns. `TYCHO_THREADS=2` silently cut a 4-worker server to 2.
- **Phase 1** — starting N workers has no direct spelling: task handles are affine and unstorable, so it is either N hand-written `spawn` lines or a recursive fan-out where each frame holds one handle.
