# Probe: file-backed social graph (relational data as values) — 2026-10-18

Model: Muse Spark 1.3 Contributor (same family as the maintainer's assistant;
the external-model route was unavailable — the `opencode` API key is expired
and `openai-codex` is rate-limited — so the probe ran as a fresh agent with no
compiler, internals, RFCs, or prior findings in context, in a stripped tree).
Host: Linux x86_64. Compiler: `tychoc 0.8.7`.

Program: `graph.ty` (432 lines, single file, no imports; thrown away after
this record). A social network as one owned `Graph` value (`[int: Person]` +
`next_id`): add/rename/remove people, friendships, friends-of-friends and BFS
shortest-path queries, save/load with a byte-identity round-trip check, clean
diagnoses for a missing file and three corrupt-file shapes. MUST-use coverage:
int-keyed maps, arrays, `Option`/`Result` with both `or_return` and both
`or_else` spellings, a generic `find_pos` over `[$T]`, a custom
`subscript person` (read + in-place rename).

## What went right

Most of the documented language worked on first use: struct value semantics,
`inout` through a map place (`push(g.people[a].friends, b)`), insertion-ordered
`keys()`, exhaustive `match` on `Option`/`Result`, `or_return` chains in the
loader, generic inference with `==` and no `where` clause, f-strings with call
holes, `Result(void, string)` with bare `Ok()`, the `subscript` one-use rule,
`Option`/`None` typing from context. The tutorial's "everything is a value"
framing made the index-pool design feel like the intended shape rather than a
workaround. First full-program compile needed exactly one fix.

## Findings (adjudicated against `main`, all reproduced)

1. **Missing composite map read segfaults (exit 139, silent), both compilers.**
   `m[99].name` on `[int: Person]` with no key 99. `maps.md:39` promises a deep
   copy of the value type's zero. Scalar misses correctly return zero. UBSan:
   non-zero offset applied to a null pointer. Minimized to 6 lines; filed and
   fixed as FRICTION 204 (NULL guard in `tycho_str_copy`, shared runtime).
   The probe's `in`-guards and present-keys-only subscript rule were load-bearing
   against this bug.
2. `or_else` refused in void `main`, with a good message pointing at `or_return`
   or `match`. Documented (`spec/10-statements.md:219`). Shaping friction only:
   unwraps moved into helpers, `main` a thin `match`/`die` wrapper.
3. `split("", ",")` returns `[""]`. Follows from the spec's "`n` separators
   yield `n+1` fields" (`spec/16-builtins.md:136`) but is easy to misread; the
   loader special-cases empty fields. Minor.
4. Unused functions are warnings; the brief's zero-warning rule made every
   helper earn its place. Working as designed.
5. `read_file` fails open (`""` for missing/unreadable/empty alike,
   `builtins.md:99`); the fail-closed `core:io.read_text` needs an import.
   Documented tradeoff, genuine friction for single-file programs.
6. `0 - 1` cargo-culting from `examples/` and `corelib/` instead of unary
   minus. Stale style post-0.8.7 (negative literals now adapt); examples lag
   the language.

## Where the language reorganized the problem

No references ⇒ the graph is an index pool by construction; removal is "delete
the entry, sweep every friend list." No pop-front/deque ⇒ BFS uses an array
queue with a head index. Copies on return ⇒ queries hand out deep copies,
mutation only via `inout` or the subscript place. Strict parsing is manual
labor (no exceptions; lax parse fails open per `quiet-results.md`) ⇒ the
loader validates magic header, count agreement, id positivity/uniqueness, name
legality, and edge dangling, each with its own message.

## Verification

Rebuilt clean from a scratch directory under `./tychoc` and `./tychoc1`:
byte-identical output (33 lines); ASan+UBSan build identical, no findings.
`m[99]` on a missing scalar key returns `0`, exit 0, on both compilers.
