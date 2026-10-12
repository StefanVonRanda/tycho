# Editor churn: rebuild-per-operation on a long-lived value (2026-08-12)

Full measurement journal for [thesis §4c](../thesis.md). That section keeps a
one-paragraph summary; everything below is the complete record, kept here so the
user page stays readable.

## The shape: replacement, which self-append does not cover

Self-append is one way a loop abandons its accumulator. There is another, and a
text editor is the everyday case of it. Its line buffer does

```
b.lines[ln] = s[0:c] + t + s[c:len(s)]
```

on every keystroke. That is not `acc = acc + e` — the old line is not a prefix
of the new one, it is *replaced* — so the self-append shape does not match and
nothing grows in place. Each edit allocates a whole new line and abandons the
previous copy inside a scope that never ends: an editing session is one arena
that lives as long as the program.

*Measured* on a 16-core box, 2026-08-12, `tychoc tools/tycho-ed/main.ty` then
`--stress=N` (see `tools/tycho-ed/main.ty@stress`; N edits in ten equal
buckets, one character each with Enter every 60 so the *line* stays bounded
while the *document* grows):

| N edits | first bucket | last bucket | last/first | document | peak RSS |
|---|---|---|---|---|---|
| 10 000 | 133 ns/edit | 119 ns/edit | 89% | 15 KB | 5.8 MB |
| 100 000 | 133 ns/edit | 298 ns/edit | 224% | 147 KB | 11.8 MB |
| 1 000 000 | 214 ns/edit | 1413 ns/edit | 660% | 1.46 MB | 95.7 MB |

Flat through 10 000 edits, already drifting at 100 000, and about 7× slower per
edit by a million — with 65× more memory resident than the document it is
holding.

**It is not the arena's allocation cost, and that is what makes the case worth
recording.** Four controls, each 1 000 000 steps in the same ten-bucket shape:
pushing a two-field struct onto an array, bounded string concatenation (reset
every 60 steps, so it measures churn and not quadratic growth), pushing into an
array field of an `inout` struct, and pushing an enum variant carrying a string.
All four land between 5 and 35 ns/step and none degrades meaningfully across the
range — the worst, the enum carrying a string, goes 17 → 35 ns and is still
*forty times* under what the editor costs at the same point. No single
ingredient of an edit is slow. What the editor adds is **volume**: a million
abandoned line copies that nothing reclaims.

The mitigation is the same one thesis §4b already names, in the shape this workload
needs: **mutate in place instead of rebuilding**. For an editor that means a gap
buffer — a line held as text-before-cursor, a gap, and text-after-cursor, where
inserting a character writes one byte into the gap and allocates nothing.

## The gap buffer, built and measured

`tools/tycho-ed/` now has both. `--backend=gap` keeps the line being edited in a
mutable `[int]` with a hole in it (`bytes` is immutable, so it cannot be the
store); a keystroke writes bytes into the hole and allocates nothing, and the
line is rebuilt as a string once per *focus change* — an Enter, an arrow key —
instead of once per keystroke. The journal, the API and every other operation
are the same code in both modes, and `make ed-check` leg [6] re-runs the demo
transcript and the six-edit undo/redo roundtrip under the gap backend and
requires them **byte-identical** to the string backend, so this is a change of
representation and of nothing else.

*Measured* the same day on the same box, three runs of every cell — last bucket
in ns/edit, and `last/first` as the drift:

| N edits | string, last | gap, last | string last/first | gap last/first |
|---|---|---|---|---|
| 10 000 | 145 / 121 / 124 | 114 / 177 / 129 | 96 / 50 / 89 % | 85 / 119 / 96 % |
| 100 000 | 281 / 267 / 279 | 119 / 120 / 119 | 181 / 161 / 187 % | 69 / 91 / 67 % |
| 1 000 000 | 1490 / 1443 / 1947 | 141 / 155 / 147 | 764 / 747 / 959 % | **97 / 113 / 103 %** |

**The curve flattens completely.** At a million edits the string backend's last
bucket costs about 8× its first; the gap backend's costs what its first did, and
the millionth keystroke is ~10× cheaper in absolute terms. The mitigation §4b
names is the right one, and the diagnosis it rested on — abandoned line copies,
not arena allocation cost — is confirmed by the fix working.

**Peak RSS did not move at all**, and that is the part worth keeping. 95.7 MB
for the string backend at a million edits, 95.7 MB for the gap backend, with a
million fewer abandoned line copies between them. So the resident set was never
the churn. A fifth control settles where it does come from: a million pushes of
the editor's exact journal entry — the `Op` enum plus a `Cursor`, with no buffer
and no line behind it — reaches **94.0 MB on its own**, at 42–56 ns/step. The
memory is the **undo journal**, one retained entry per keystroke, which is the
feature doing its job rather than a leak; and because it is flat in time, it
never contributed to the drift. Removing the churn was always going to move one
number and not the other.

The correction matters more than the confirmation. The paragraph above this one
used to read "a resident set that grows with the session rather than with the
document" as if it were the same phenomenon as the slowdown. It is not: one is
churn the arena cannot reclaim and a gap buffer removes, the other is live data
the program is deliberately holding. Two costs that rose together are not one
cost, and only building the fix separated them.

Written down here rather than filed as a defect, because it is not one: it is a
measured instance of a limit this project already concedes. The memory-model
guide says the same thing in general terms — allocation-churn workloads are
where the arena is at its weakest, and the honest loss
([`docs/memory-model.md:109-111`](../memory-model.md)). What the
controls add is the reason to believe the diagnosis: without them, "1 M edits
got 7× slower" is equally well explained by the arena, by string
concatenation, or by `inout`, and the fix would be aimed at the wrong one.

## What it costs the thesis

This is the one place in the thesis where the abstraction does not hold, and
the honest statement of it is narrow but real — narrower, in fact, than the
memory framing this section reached for first. The retained bytes turned out to
be the undo journal doing its job, and the overwritten lines are recycled. What
the model actually charged here was a **full copy of the line on every
keystroke**, and it charged it because the source said to rebuild the line. The
compiler is not wrong: it has no shape to recognize, and under thesis §3 its only
freedom is one it may take safely. And the fix works completely — the gap buffer
flattens the curve to ~100% drift at a million edits.

But the fix was a **representation change made by the programmer**, not an
optimization applied by the compiler. For this class of program — a long-lived
mutable structure edited in place over a session — the model asks you to know
the arena is there and to choose a shape that mutates rather than rebuilds. That
is a smaller ask than `malloc`/`free`, it is the same ask a data-oriented C
programmer answers anyway, and it is not zero. "Zero memory cognition" (thesis §6) is
earned for build-and-return programs and is *not* earned for this one, and the
two should not be claimed in the same breath.

The boundary is crisp enough to state as a rule of thumb: **if a mutable value
outlives every scope that would reclaim it, rebuilding it per operation is the
expensive shape, and mutating it in place is the supported one.** Thesis §5 carries
this as a wall; [the limits note](value-semantics-limits.md) carries
the representations that answer it.
