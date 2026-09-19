# Where Tycho is

One page, written 2026-09-19. `make status` answers *"is the tree healthy right
now"*. This answers *"where is this project, and what is actually left"* — the
question that is otherwise smeared across a 7,144-line friction log, a roadmap
and `surface.lock`.

Where a number comes from a gate, the gate is named, so any line here can be
checked by running one command.

**Provenance, because this page is only worth what its weakest number is.** The
counts, gate results, timings and sweeps below were measured on 2026-09-19 by
running the command named beside them, and every benchmark column was reproduced
the same day (three of them in containers, because `rustc`, `go` and `koka` are
not installed on this machine). The platform and 1.0 sections are read from
`ROADMAP.md` and not independently checked. Nothing here audits whether the
fixture corpus has coverage holes.

---

## The thesis is proven

The project was started to answer one question: can implicit lexical arenas
under value semantics replace reference counting and garbage collection?

Peak resident memory (MB), same checksum computed by every binary. **Every column
below was reproduced on this machine on 2026-09-19** — `tycho` and `C` by
`make bench-prongB` on the host, and `Rust`, `Go` and `Koka` in containers,
each compiling the harness's own `bench/peakrss.c` and using the harness's own
flags (`rustc -C opt-level=3`, `go build`, `koka -O2`). The harness also reports
*"all outputs identical within each workload"*, so the programs agree before the
memory is compared.

| workload | tycho | C | Rust | Go (GC) | Koka (Perceus) |
|---|--:|--:|--:|--:|--:|
| binary-trees | **13** | 33 | 34 | 32 | 16 |
| tree-rewrite | **6** | 13 | 8 | 23 | 8 |
| array-pipeline | 6 | 3 | 1 | 7 | 16 |
| string-pipeline | 1 | 1 | 0 | 4 | 4 |

**On both allocation-heavy workloads Tycho uses the least memory of the five** —
0.39x C's on binary-trees and 0.46x on tree-rewrite — with no GC, no reference
counting, and no manual management written by the programmer. Wall time on the
same run: binary-trees 144 ms against C's 778.

Two caveats, because the measurement is only worth its method. **The libc
differs**: `tycho` and `C` ran on the host (glibc), Rust and Go in Alpine
(musl), Koka in Debian (glibc). That shifts the *small*-RSS rows — Rust's
string-pipeline reading 0 rather than the recorded 2 is a musl baseline, not a
Rust improvement — while leaving the tree workloads, where the numbers are tens
of megabytes, essentially unaffected. And **wall times are not comparable across
host and container** the way the kernel-measured RSS is.

These figures land within 1-2 MB of the values recorded in
[`bench/prongB/RESULTS.md`](bench/prongB/RESULTS.md) on every row, which is the
useful result: the table in this repository is reproducible by someone who did
not write it.

**That question is answered.** Everything below is about the second project:
whether it is a language someone would choose to use.

---

## What works, and how that is known

| | | checked by |
|---|---|---|
| **Self-hosting** | the compiler compiles itself and reaches a fixpoint: `gen2.c == gen3.c` | `make fixpoint-check` |
| **Fixture corpus** | 1057 fixtures, **645 of them refusals** — what must *not* compile is the larger half | `make test` |
| **Corelib** | 45 packages shipped, each golden-locked (46 directories — `corelib/test/` is the test tree, not a package) | `make corelib` |
| **Real programs** | 29 tools in Tycho, 18.5k lines — **5 over 1000 lines** (`db` 2063, `scheme` 1609, `sheet` 1523, `q` 1359, `chess` 1166), 8 mid-size, 16 small utilities. Not 29 substantial programs; `tycho-db`'s gate does assert crash recovery from a real `kill -9`, torn-record discard and a TCP server | their own `*-check` lanes |
| **Normative spec** | 28 files, and the implementation is gated against them | `make spec-check` |
| **The whole gate** | 77 lanes, ~407s, green under **gcc and clang** | `make ci` |
| **Memory safety** | 4 sanitizer lanes (ASan/UBSan/LSan/TSan) + 3 fuzz lanes at 200 seeds | `make ci` |
| **Uninitialised reads** | 972 programs swept under an instrumented arena, 0 findings — the class no sanitizer sees | `docs/internals/probe-uninit-arena-2026-09-11.md` |
| **Defect log** | **every closed entry is pinned by a command that is executed** — 1 excused with its reason, **0 unpinned**, which is the number that matters and the only one quoted here, because the totals move with every entry added. `make ci` scores the *fact* pins as lane `[1d2/13]` in 0.16s and names how many suite pins it defers; the deferred ones are asserted only by the full command (FRICTION 104) | `make friction-check` |
| **Docs** | 88 reachable pages, no dead links, every `path:line` citation resolves to the line it names, and **every public corelib function is named in the catalogue** (431/431) | `make check-links` |
| **Diagnostics** | 325 of the compiler's 335 distinct messages have their wording pinned; the residue is matcher offsets, not gaps | `make test` |
| **Surface freeze** | 115 keywords, 41 builtins, **431** corelib functions, locked; broken deliberately 3 times, each measured first. Was 559 until 2026-09-19, when 125 internal helpers became package-private (FRICTION 96) — the freeze now covers what it meant to | `make surface-check` |

**One thing this table cannot show: `make parse-check` is RED**, and it is in
neither `make ci` nor the pre-push hook — so between `0b385a24` and the end of
2026-09-19, five commits recorded `Verified: make ci GREEN` truthfully while that
gate was failing. Seven drifted count literals and three stale censuses have
since been cleared, and `corpus-check` — the sub-second predictor of exactly the
drift that caused it — is now lane `[1d3/13]`. **What remains red is one genuine
`tychoc`/`tychoc1` diagnostic divergence**, of the class FRICTION 87 exists for,
left standing because which compiler is right is an owner's decision and not a
cleanup. `make tychoc1-check` — the other gate ROADMAP calls load-bearing for the
self-hosted compiler — passes, and both remain uncalled by any automatic run. See
FRICTION 106; the row below is `make ci`'s scope, not the project's.

The unusual thing in that table is the last two rows. The defect log is
*executable* — a closed entry that stops being true makes `make friction-check`
red, and since 2026-09-19 it turns **`make ci`** red too for every pin that
asserts a specific fact, which `ci` scores in 0.16s (FRICTION 104; before that
the target existed and nothing called it). And every
surface change so far was justified by counting sites in real programs, not by
taste: `packed` shipped because 141 sites hand-assembled bytes a byte at a time.

---

## What is not done

**Platforms.** Artifacts exist for `linux-x86_64` and `mingw64-x86_64`. There is
still **no macOS or ARM64 release artifact** — but as of 2026-09-19 that is a
packaging gap and no longer an unknown. The gate was run on `darwin-arm64` for
the first time that day and is **green**, repeatedly measured between **267s
and 390s** — that spread is FRICTION 91, not a regression, which is why no single
number is quoted: both compilers build native
Mach-O arm64 with no source change (`make tychoc` 2.0s, `make tychoc1` 34.1s)
and `make test` is 1065/1065.

**Read that green with its scope**, the way the Windows green is read. It
carries **12 skips over 7 causes**, each printing its reason, and one of them is
a real coverage loss rather than an inapplicable lane: **Apple's ASan ships no
LeakSanitizer**, so `fuzz-leak` and the raytrace and mandelbrot leak legs do not
run on macOS at all, so the LSan lane named in the memory-safety row above is
Linux's alone. The rest are
lanes with nothing to test here: gdb (`debug-check` and the gdb transcript in
`docs/debugging.md` — whose lldb counterpart runs *only* on macOS), the glibc
symbol floor, `ilp32`, `resource.prlimit`, `vector-check` leg [7]
(`--target x86-64-v3` on an arm64 host), and the `crypto_hygiene` probe, which
needs `-Wl,--wrap` that ld64 does not implement.

Getting to green cost three fixes, none Darwin-specific in cause: FRICTION
[100](docs/internals/FRICTION.md), 101 and 102. Two were latent defects in
shipped code — a listen backlog that made any Tycho server refuse peers under a
burst on any BSD-derived kernel, and a single-read site that aborts the process
— which is the return on running the gate somewhere it had never run.

**1.0 is blocked on two things, and neither is code:**

1. **A real program written by someone who is not the author.** A fourth program
   by the same person does not advance this.
2. **An external security review.** `docs/internals/audit-brief.md` is what that
   reviewer gets handed.

The roadmap is explicit that neither is a task the roadmap can complete. Both
need a person. No amount of further work alone moves either one.

**Ergonomics.** The language is correct and the surface is frozen, but it has not
been tuned for the experience of using it daily. One measured example: 76 sites
across 17 files are a four-line `match` whose entire content is "bind on ok,
return a domain error on failure" — 34 of them the trivial `return Err(e)`. One
construct removes all 76. That is a design backlog derived from real code, and
nothing more of it has been mined yet.

**A structural limit, already decided.** Pointer-shaped and structurally-shared
data (tries, graphs, DAGs) cost more here than in C or Go, because value
semantics store children by value and there are no references to share. The
index-pool idiom is the answer, and `docs/rfc/limited-references-spike.md`
records *why* references are not: the feature that would lift the limit would
dismantle the invariants the project exists to demonstrate. Closed as a decision,
not left open.

---

## What "good enough to see the light of day" would take

Less than it feels like from inside, which is the point of writing it down:

- **Nothing for correctness.** The gates are green under two compilers, the
  fuzzer finds nothing at 200 seeds, and the last sweep for the one memory-error
  class no sanitizer catches came back clean over 972 programs.
- **A macOS/ARM64 build**, for "runs where developers are".
- **One other person writing one real program**, which is the actual 1.0 gate.
- **Ergonomics work**, ranked by counting sites the way the surface changes
  already were.

---

## What this page is not

It is not a promise, and it is not an argument that the project should continue.
It is the overview that did not exist, so that the decision — whatever it is —
gets made against what this thing measurably is, rather than against how the
last stretch of work happened to feel.

See also: [ROADMAP](ROADMAP.md) · [how it is tested](docs/controls.md) ·
[the friction log](docs/internals/FRICTION.md) · `make status`
