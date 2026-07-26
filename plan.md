# Make failure pleasant: adopt `Option`/`Result` across the corelib

Follows the web-server plan (archived: `docs/internals/plan-webserver-DONE.md`, 8 phases,
head `eb42c3e`). That plan built `server/` — `tycho-httpd`, ~500 lines serving a real site
to a real browser at 79,712 req/s — and its deliverable was `FRICTION.md`, an honest
account of writing it. This plan acts on that file's headline finding.

## The finding, in one line

**The language has a good answer for fallible calls and the standard library does not use
it.** `Option` and `Result` are built-in enums (`docs/spec/03-types.md` §5.3.6) and
`or_return` is a real postfix operator that unwraps or short-circuits
(`docs/spec/10-statements.md:75`). **Exactly 1 of the corelib's 386 functions returns an
`Option`** — `io.read_line` (`corelib/io/io.ty:69`). Everything else signals failure with
a sentinel, and the sentinel differs per call:

| call | failure is… | also means |
|---|---|---|
| `net.accept` | a negative int | — |
| `net.write`, `httpd.write_response` | `-1` | — |
| `net.read` | empty `bytes` | EOF **and** idle timeout |
| `io.read_bytes` | empty `bytes` | empty file **and** missing file **and** a directory |
| `path.safe_join` | `""` | — |
| `httpd.parse_request` | `method == ""` | EOF **and** timeout **and** malformed |
| `net.set_read_timeout_ms` | `false` | — |

Six spellings of "it went wrong", three indistinguishable from success. **This is not
theoretical: two of `server/`'s real bugs came from exactly these collisions.** It cannot
tell a malformed request from a hangup, and `resolve()` ships a documented wrong answer
(a 0-byte `200` for an empty directory) because "is this a directory" cannot be asked.

`FRICTION.md`'s verdict: *"the error model is worse by choice rather than by capability …
adopting them across the IO surface would remove more friction than every other item
combined."*

## Goal

A corelib where a fallible call says so in its type, and where handling failure at the
call site is shorter than ignoring it. Done = `server/main.ty` is rewritten against the
new surface and is **measurably better** — fewer lines of error plumbing, no
sentinel-collision bugs left, and the two known wrong answers fixed.

## Anti-scope

Inherited from the previous plan and still binding:

- **A phase belongs here only if it serves the goal above.** Not "is inconsistent", not
  "could be nicer".
- Discovered defects that do not block go to `FRICTION.md` as one line, unfixed.
- **Stop condition: `server/main.ty` is better against the new API.** Not "every corelib
  function has been converted."
- **This plan must not become a 386-function mechanical sweep.** The previous-previous
  plan grew 6 → 44 phases that way. Phase 1 exists specifically to find out how much
  conversion is actually worth doing.

### GATE CONSTRAINT — user directive, 2026-07-26, still binding

**`make ci` and `make test` run AT MOST ONCE PER DAY**, across all agents. Violating it
means the gates get removed entirely. Verification is *running the thing you built* —
here that means compiling and running `server/` and the corelib's own test programs
directly with `./tychoc`, which is not a gate.

## The design question Phase 1 must answer

Do NOT convert anything before this is settled. Three real unknowns, and the wrong answer
to any of them makes the change worse than the sentinels:

1. **`Option` or `Result`?** `Option` says *something failed*; `Result(T, E)` says *why*.
   The collisions that actually bit us — EOF vs timeout, missing vs empty vs directory —
   are exactly the ones `Option` cannot fix. That argues `Result`. But `Result` needs an
   error type, and what `E` should be is itself a decision: a `string`, a corelib-wide
   error enum, or per-package enums. A wrong choice here is worse than sentinels, because
   it is a breaking API that must then be broken again.
2. **Does `or_return` actually compose at IO call sites?** It short-circuits by returning
   from the enclosing function — which requires that function to return a compatible type.
   A web-server request handler returns a `Response`, not a `Result`. **If `or_return` is
   unusable in the exact place the friction lives, the whole premise collapses** and the
   answer might instead be better sentinels or a different construct. Verify by writing
   real code, not by reading the spec.
3. **What does the call site actually look like?** The current pain is `if x < 0` /
   `if len(x) == 0` / `if s == ""` scattered through every IO path. If the replacement is
   `match` on every call, that is not obviously better. Write both and compare.

## Phases

- [ ] **Phase 1 — PROBE: convert ONE package, rewrite the server against it, compare**
  - Pick `core:io` or `core:net` — whichever `server/main.ty` leans on hardest for
    fallible calls; read the server first and say which and why.
  - Answer the three design questions above **by writing code**: convert that one
    package's fallible surface, then rewrite the parts of `server/main.ty` that use it,
    and put the before and after side by side in the evidence.
  - **Measure, do not assert:** lines of error handling before vs after; whether the two
    known wrong answers (malformed-vs-hangup, empty-dir-vs-file) become expressible;
    whether `or_return` worked at the call sites or had to be abandoned.
  - **A legitimate outcome is "this is not better, do not roll it out."** If the converted
    call sites read worse, say so and stop — that finding is worth more than a
    386-function sweep done on a guess. The plan's premise is testable and this is the test.
  - Done when: one package converted, the server's corresponding code rewritten, a
    side-by-side comparison recorded, and a **recommendation with a reason** on whether to
    proceed, and in what form.

- [ ] **Phase 2 — CONTINGENT on Phase 1: roll out to the fallible IO surface**
  - Only if Phase 1 says it is better. Scope set by Phase 1's finding, not by this text.
  - Package order should follow what `server/` and the examples actually use — `io`,
    `net`, `httpd`, `path` first; a package no real program calls fallibly can wait.
  - Every consumer must land with it: `examples/*`, `corelib/test/*`, `server/`, goldens.
  - **Not a sweep for its own sake.** Convert what buys clarity; leave what does not and
    say which.

- [ ] **Phase 3 — CONTINGENT: fix the two wrong answers in `server/`**
  - `resolve()` reports an empty directory as a 0-byte `200` because `is_dir` cannot be
    asked (there is no `stat`; `io.exists` works by listing the parent). Needs a real
    `io.stat`/`io.is_dir`, which may be its own small addition regardless of Phase 1.
  - The read loop cannot distinguish a malformed request from a hangup, which is why
    `server/main.ty` reimplements `read_head` instead of using `httpd.read_request`.
  - Done when both are expressible and `server/main.ty` drops its workarounds.

## Out of scope

- The rest of `FRICTION.md`. It is a deliverable, not a queue: `\r` escapes, multi-line
  strings, `cli` argument spelling, `die()` always exiting 1, `getpeername`, `bytes`
  having no operators — all real, none of them this plan's subject. They stay recorded.
- `compiler/tychoc0.ty` — frozen 2026-07-26, diverging, unmaintained.
