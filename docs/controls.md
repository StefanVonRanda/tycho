# Every gate must be able to fail

A green test suite is evidence of nothing until you know each test can go red.
That sounds obvious and it is routinely untrue: a check that passes because it
is broken looks exactly like a check that passes because the code is right.
Tycho has no cloud CI, 102 entries in a defect log, and a rule that fell out of
both — **every gate here has to demonstrate it can redden, and the demonstration
is part of the gate.**

This page is four times that rule earned its keep. Every number below is from
[`internals/FRICTION.md`](internals/FRICTION.md), which records defects in this
project as they were found, including the ones that were embarrassing.

## 1. Sixteen attacks refused, and the gate was measuring nothing

The server was probed with sixteen hostile requests — raw `../`, `%2e%2e%2f`,
double-encoded `%252e`, `....//`, overlong UTF-8 `%c0%af`, absolute-form, a NUL
in the target, an 8000-byte target. All refused. Nothing leaked. The server kept
serving afterwards.

Then the control was run: one of the two defence layers was replaced with naive
string concatenation, to check the probe would notice.

It still returned 403.

The defence is two independent layers, and **either one alone refuses every
payload in the suite**. So deleting one changed no observable behaviour, and
sixteen green results were compatible with the defence being half gone. Two
commits, neither of which looks wrong in review, would have taken it to zero
with every gate still green.

Nothing was broken. The finding was that nothing *could tell* — which is a
different and worse problem, because it has no symptom. (`FRICTION.md` entry 72.)

## 2. The flag that quietly emptied the corpus

`make ci N=0` is documented as "skip the slow fuzz lanes for a quick check".
`scripts/format_diff.sh` read its corpus size from a bare `N=${N:-400}`. Make
exports its variables, so the differential picked up the same `N`:

```
N=0 sh scripts/format_diff.sh   ->  20 paths,   9 refused
sh scripts/format_diff.sh       -> 420 paths, 129 refused
```

The quick-check flag cut that lane to **4.8% of its corpus** and said nothing.
It was caught by luck: the lane happens to exit non-zero if fewer than ten paths
are refused — a guard written for an unrelated reason — and with `N=0` the
corpus fell just past it. Without that accident, `make ci N=0` would have been a
green sweep over a twentieth of the evidence. (Entry 84.)

## 3. A control that was optimised into its own negative

Tycho's crypto gate proves a hex decoder does not branch on secret key material.
Proving an *absence* needs an instrument known to detect the presence, so the
control is a patched copy of the decoder with the branchless select rewritten as
an `if/else` chain. It must redden. If it does not, the leg proves nothing.

Under gcc at `-O1` that chain survives and the control reports four
secret-dependent branches. Under clang at `-O1` it is if-converted straight back
into a branchless select — so the control emitted no branch, reported zero, and
the gate refused to certify.

**The control had been optimised into the thing it was the negative of.** The
fix is to build the control at `-O0`, where its branch survives any compiler,
while the subject stays at `-O1`, because "the shipped decode has no
secret-dependent branch" is a claim about optimised code and is worth nothing at
`-O0`. (Entry 92.)

Two sibling entries the same day were the same mistake wearing different
clothes: a packed-instruction count and an unused-symbol suppression, each a
property of *gcc* that a gate had recorded as a property of *C*. All three
failed closed, which is the gates working. What none of them could do was say
"this is a property of the compiler I happen to be." (Entries 89, 90, 92.)

## 4. The class no sanitizer sees

Four sanitizer lanes run here: addresses, undefined behaviour, leaks, races.
None of them sees a **read of memory that was never written**.

That gap lands squarely on this design. `arena_alloc` is a bump pointer over a
`malloc`'d block, so it hands back memory that is not zeroed and that ASan
considers perfectly valid, because the enclosing block is live. A codegen path
reading a field before writing it is invisible to every lane — and it tends to
*pass a golden*, because a fresh page reads as zeros until the allocation
pattern shifts.

So the arena was instrumented under valgrind, every handout marked undefined,
and the whole corpus swept: the fixtures, every corelib package, every example,
and all 27 tools driven through their own gates with their real inputs.

**972 programs, 0 uninitialised arena reads.**

The number that makes that worth printing is not 972. It is the control, run in
both directions: a read of an unwritten arena slot fires `Conditional jump or
move depends on uninitialised value(s)` and exits 42, and the same slot written
first is silent and exits 0. Without the first, a clean sweep is indistinguishable
from a dead detector. Without the second, it is indistinguishable from a detector
that flags everything. (`internals/probe-uninit-arena-2026-09-11.md`.)

## The practice

- **Every closed entry names a command that would catch its regression.** 93
  closed, 92 pinned by 66 distinct commands, 1 excused with a stated reason, 0
  unpinned — and `make friction-check` *runs the pins*, so the log is executable
  rather than decorative. An entry claiming a fix that no longer holds turns the
  gate red.
- **Skips are enumerated, never silent.** A lane that cannot run on this host
  names itself and the reason; an unenumerated skip is a failure.
- **A gate that cannot discriminate refuses to certify** rather than passing
  vacuously. That is why entries 89 and 92 above surfaced as red builds on a
  tree where nothing was wrong.
- **The log records the wrong turns too** — theories that were disproved, a
  commit message that was wrong on a point of fact and the entry that corrects
  it, recommendations made without reading the comment above the code.

None of this makes the language good. It makes the *evidence* about the language
worth reading, which is the part a stranger cannot take on trust.

See also: [architecture](architecture.md) · [thesis](thesis.md) ·
[the friction log itself](internals/FRICTION.md)
