# Layout/SIMD probe, 2026-09-07 — four new features, no findings

The first probe aimed at the surface the freeze was broken for. `packed`,
`align(N)`, `vector[N]T` and groups/swizzling shipped 2026-09-04 and 2026-09-05;
every probe record before this one is dated 2026-08-19 or 2026-08-20, so nothing
outside this repo had driven any of them.

The agent wrote a 57-line pixel kernel using all four, and it worked on the first
pass. **Zero doc gaps, zero defects.** That is the result, and it is worth
recording precisely because a probe that finds nothing is the one most likely to
go unwritten.

## What it exercised

`packed struct` over four `u8` fields with a `to_bytes` / `from_bytes` round
trip; `align(8) struct` over three `f32`; `vector[4]f32` multiplied by a
broadcast scalar; and `bright.(r, b) = bright.(b, r)`, the swizzle form of
simultaneous assignment. It read the emitted C and confirmed the four attributes
landed — `packed`, `aligned(8)`, `vector_size`, and a swizzle lowered as
read-both-then-write-both.

## The five things it reported, and why none is a finding

Each was checked against `main` before being dismissed, per
`docs/internals/probe-procedure.md`'s "check each finding against main" step.

| reported | verdict |
|---|---|
| no ternary; `if` is an expression only in tail position | documented, and the agent quotes the section that says so |
| `vector[N]T` elements limited to `int`/`float`/`f32` | documented with its rationale; the agent wanted `vector[4]u8` for byte work, which is a design objection |
| `to_f32(u8)` "worked via implicit widening" against a doc that says `float` | **the agent misread the doc.** `docs/reference/builtins.md:121` says `numeric -> f32`, and `docs/spec/06-conversions.md:40` documents the conversion as **total** over any sized int/float. Re-run here: `to_f32` on a `u8` field gives `200.0`. Nothing undocumented occurred |
| no `range` in `for` | documented |
| `println` is string-only | documented |

The third is the one worth keeping. An agent reporting a doc as wrong, when the
doc is right and says so two lines from where it looked, is a failure mode a
probe report will always be able to produce — so a report's own citations have to
be opened, not trusted.

## Run under the sanitizers, separately

`docs/internals/probe-procedure.md` says to build the agent's code yourself
because the 2026-08-19 run's best finding was never in its log. Done here: the
emitted C at `-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -fwrapv`
with `detect_leaks=1` exits 0 and prints output byte-identical to the plain
build. No equivalent hidden finding this time.

## A defect in the procedure itself

`docs/internals/probe-procedure.md` states its goal as the agent being unable to
read compiler source, then lists deletions that leave `compiler/` in place — 19
files including `compiler/parse/parse.ty` and the typechecker, which is a syntax
oracle for exactly the features under test. It was deleted by hand for this run.

**Every round since `compiler/` entered the tree had this leak.** That weakens
those records rather than voiding them — an agent that can read a parser is still
not the author — but it is unmeasured, and the delete list should name
`compiler`.

## What this does not establish

One program, one surface, 361 seconds. The agent's own "did not check" list is
the honest boundary: no non-x86 host, the `packed` pad byte never read back, and
`align(8)`'s trailing padding confirmed in the emitted C but never measured at
run time. The program was thrown away; this record is the artifact.
