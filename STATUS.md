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
| **Real programs** | **33 programs in Tycho, 19,006 lines** — every `.ty` under `tools/`, which is 30 `tools/tycho-*/` directories plus `tycho.ty`, `tychofmt.ty` and `lsp.ty`. **6 over 1000 lines** (`db` 2063, `scheme` 1609, `sheet` 1483, `lsp` 1403, `q` 1359, `chess` 1166), 10 between 300 and 999, 17 under 300. One of the 33 was not written by the author: `tycho-du` came in from the 2026-09-21 `handle` probe ([record](docs/internals/probe-handle-2026-09-21.md)), which is ROADMAP §1's instrument rather than its answer. Not 32 substantial programs; `tycho-db`'s gate does assert crash recovery from a real `kill -9`, torn-record discard and a TCP server | `find tools -name '*.ty' \| xargs wc -l` |
| **Normative spec** | 28 files, and the implementation is gated against them | `make spec-check` |
| **The whole gate** | **81 lanes** (`grep -c '^step "' scripts/ci.sh`), green under **gcc and clang** on x86-64 Linux and under **clang on darwin-arm64**. Wall clock is deliberately not quoted as one number — repeated runs here span 263-390s, which is FRICTION 91 | `make ci` |
| **Memory safety** | 4 sanitizer lanes (ASan/UBSan/LSan/TSan) + 3 fuzz lanes at 200 seeds; **re-run at 2000 seeds on 2026-09-19: `ok=2000 skip=0 timeout=0 FAIL=0`** in 7m14s. Plus 3 differential parity fuzzers (FRICTION 119) | `make ci`, `make fuzz N=2000` |
| **Float determinism** | the emitted C is compiled `-ffp-contract=off`, so `a*b+c` rounds twice on every host — without it ARM fuses and x86-64 cannot, and `0.1+0.2` rendered differently on the two (FRICTION 112) | `make sheet-check` |
| **Uninitialised reads** | 972 programs swept under an instrumented arena, 0 findings — the class no sanitizer sees | `docs/internals/probe-uninit-arena-2026-09-11.md` |
| **Defect log** | **every closed entry is pinned by a command that is executed** — 1 excused with its reason, **0 unpinned**, which is the number that matters and the only one quoted here, because the totals move with every entry added. `make ci` scores the *fact* pins as lane `[1d2/13]` in 0.16s and names how many suite pins it defers; the deferred ones are asserted only by the full command (FRICTION 104) | `make friction-check` |
| **Docs** | 91 reachable pages, no dead links, every `path:line` citation resolves to the line it names, and **every public corelib function is named in the catalogue** (423/423, matched as a word — it was 431/431 by substring until FRICTION 118) | `make check-links` |
| **Diagnostics** | 325 of the compiler's 335 distinct messages have their wording pinned; the residue is matcher offsets, not gaps | `make test` |
| **Surface freeze** | 115 keywords, 41 builtins, **423** corelib functions, locked; broken deliberately 3 times, each measured first. Was 559 until 2026-09-19: 125 internal helpers became package-private (FRICTION 96), then 8 more the catalogue gate had never actually checked (FRICTION 118) — the freeze now covers what it meant to | `make surface-check` |

**`make parse-check` was RED for a day and is now GREEN**, which is worth a
paragraph because of how it got there. It runs in neither `make ci` nor the
pre-push hook, so between `f0078c52` and the end of 2026-09-19 five commits
recorded `Verified: make ci GREEN` truthfully while that gate was failing. Seven
drifted count literals, three stale censuses and one real defect were sitting in
it: `strings.len(...)` answered *"package 'strings' has no symbol 'len'"* in the
reference compiler and named the builtin and the cure in the self-hosted one —
FRICTION 87's shape exactly, and the reference was the wrong one. All cleared,
and the cheap predictors are now lanes: `corpus-check` `[1d3/13]` at 0.04s and
`builtin-qualified` `[1d4/13]`, which reads the builtin set out of `surface.lock`
so a builtin the compiler forgets to list cannot hide. `make tychoc1-check`
passes. **Both parse-check and tychoc1-check remain uncalled by any automatic
run** — +223s on a ~290s sweep is an open cost question. See FRICTION 106-108;
the row below is `make ci`'s scope, not the project's.

The unusual thing in that table is the last two rows. The defect log is
*executable* — a closed entry that stops being true makes `make friction-check`
red, and since 2026-09-19 it turns **`make ci`** red too for every pin that
asserts a specific fact, which `ci` scores in 0.16s (FRICTION 104; before that
the target existed and nothing called it). And every
surface change so far was justified by counting sites in real programs, not by
taste: `packed` shipped because 141 sites hand-assembled bytes a byte at a time.

---

## What is not done

**Platforms.** Artifacts exist for `linux-x86_64`, `mingw64-x86_64` and, since
2026-09-19, **`darwin-arm64`** — built by the unchanged `scripts/release.sh`,
byte-identical across two builds (`make release-check`), and content-gated at 31
legs (`make release-content`). It is **built, not published**: `gh release
create` is still the owner's to run, and it is verified on the build box only.
Getting there found that the content gate did nothing at all on this platform —
it exited on a missing *Windows* tool before it ever checked the native archive
(FRICTION 109). The gate was run on `darwin-arm64` for
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
been tuned for the experience of using it daily. One measured example: the
four-line "bind on ok, leave on failure" `match`. The trivial `return Err(e)`
half went to `or_return`; `or_else` (§14.6.1, shipped 2026-09-25) took 79 more,
45 mapping the error and 34 returning a plain value. 13 remain on purpose: test
fixtures and benchmarks (11) and two that would bind a payload only to drop it.
Nothing more of the backlog has been mined yet.

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
- **Publishing the macOS/ARM64 build**, for "runs where developers are".
  The artifact exists as of 2026-09-19 and the gate is green on
  `darwin-arm64`; what is left is `gh release create`, which is the
  owner's to run, and a verification anywhere but the build box.
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
