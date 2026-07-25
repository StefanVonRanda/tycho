# FRICTION

One line per moment the language got in the way while writing a real web server.
Non-blocking by construction: anything that blocks the server earns a phase in
`plan.md` instead. This file is a deliverable; fixing everything in it is not.

- **Phase 1** — `spawn f(x)` as a bare statement is rejected with `a statement must be a declaration, assignment, or call -- a bare expression has no effect`, which never says the real rule: a task handle must be bound so the compiler can hang the implicit join on it.
- **Phase 1** — `parallel for i in range(N)` runs only `min(N, tycho_ncpu())` iterations concurrently (`runtime/tycho_rt.c:843-852`); iterations chunked behind one that never returns never start, and nothing warns. `TYCHO_THREADS=2` silently cut a 4-worker server to 2.
- **Phase 1** — starting N workers has no direct spelling: task handles are affine and unstorable, so it is either N hand-written `spawn` lines or a recursive fan-out where each frame holds one handle.
- **Phase 0** — six non-gated runners still build tychoc0 and compare against it (`examples/fetch`, `examples/sqlite`, `examples/webserver`, `examples/weblog`, `bench/run.sh`, `tools/prof/profile.sh`); none is in `make ci`, so none can redden, but each will drift as tychoc0 does.
- **Phase 0** — the harness scripts of the removed gates are still on disk unreferenced (`compiler/run.sh`, `compiler/fixpoint.sh`, `compiler/pkg-split.sh`, `scripts/frontparity.sh`, `tests/rtparity/`, `fuzz/run_pkg.py`, `fuzz/run_typeparity.py`, `run_parforparity.py`, `run_eqparity.py`, `run_unaryparity.py`); kept deliberately so the method behind the recorded self-hosting result stays readable.
- **Phase 0** — the 15 `tests/diag/*.h0err` tychoc0-diagnostic goldens are now orphaned; kept because three archived internals docs cite them.
- **Phase 0** — prepending a 50-line banner to `compiler/tychoc0.ty` invalidated every `:N` self-citation in its own comments (the citation gate only checks docs→source, not source→source), so the file now carries a "+50" correction note instead.
- **Phase 0** — `docs/bootstrap.md` is cited by `compiler/tychoc0.ty`'s original header and by `Makefile`'s old `bootstrap` comment, but the file does not exist anywhere in the tree; the citation gate never caught it because it only validates Markdown-to-Markdown links.
