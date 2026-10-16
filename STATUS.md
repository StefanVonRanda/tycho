# Where Tycho is

`make status` answers *"is the tree healthy right now"*. This answers *"where is this project, and what is actually left"*. See [ROADMAP](ROADMAP.md) for what is next, [CHANGELOG](CHANGELOG.md) for what changed.

Where a number comes from a gate, the gate is named, so any line here can be checked by running one command.

Terms used below, on first use: `surface.lock` records the frozen language surface (keywords, builtins, corelib signatures) and is gated by `make surface-check`. `tychoc` is the reference compiler (a single C file, `src/tychoc.c`); `tychoc1` is the self-hosted compiler (Tycho written in Tycho, in `compiler/`). A *lane* is one step of the local gate (`scripts/ci.sh`, run by `make ci`). `FRICTION n` is entry n in the defect log (`docs/internals/FRICTION.md`), which is executable: each closed entry names the command that checks it (`make friction-check`). A *probe* is a small program written against the docs alone to test them.

---

## Evidence for the arena model

The project was started to answer one question: can implicit lexical arenas
under value semantics replace reference counting and garbage collection?

Peak resident memory (MB), same checksum computed by every binary. Every column
below was reproduced on this machine on 2026-09-19 — `tycho` and `C` by
`make bench-prongB` on the host, and `Rust`, `Go` and `Koka` in containers,
each compiling the harness's own `bench/peakrss.c` and using the harness's own
flags. The harness also reports *"all outputs identical within each workload"*.

| workload | tycho | C | Rust | Go (GC) | Koka (Perceus) |
|---|--:|--:|--:|--:|--:|
| binary-trees | **13** | 33 | 34 | 32 | 16 |
| tree-rewrite | **6** | 13 | 8 | 23 | 8 |
| array-pipeline | 6 | 3 | 1 | 7 | 16 |
| string-pipeline | 1 | 1 | 0 | 4 | 4 |

Rounded per-round snapshot from the run above; the precise log, with wall
times and hardware, is [`bench/prongB/RESULTS.md`](bench/prongB/RESULTS.md).

**On both allocation-heavy workloads Tycho uses the least memory of the five** —
with no GC, no reference counting, and no manual management written by the programmer.

Method notes: `tycho` and `C` ran on the host (glibc), Rust and Go in Alpine
(musl), Koka in Debian (glibc). That shifts the small-RSS rows while leaving the
tree workloads essentially unaffected. Wall times are not comparable across host
and container the way kernel-measured RSS is. Full method and reproduction are in
[`bench/prongB/RESULTS.md`](bench/prongB/RESULTS.md).

**These workloads demonstrate that the approach is viable.** They do not prove
that arenas minimize memory for every workload, that scope exit lowers RSS, or
that execution is jitter-free. See [the memory model](docs/memory-model.md) and
[the latency benchmark's limits](bench/latency/RESULTS.md). Everything below is
about whether this is a language someone would choose to use.

---

## What works, and how that is known

| | | checked by |
|---|---|---|
| **Self-hosting** | the compiler compiles itself and reaches a fixpoint | `make fixpoint-check` |
| **Fixture corpus** | over 1,100 fixtures (see `sh tests/run.sh --count`), more than half of them refusals — what must *not* compile | `make test` |
| **Corelib** | 45 packages shipped, each golden-locked (`corelib/test/` is the test tree, not a package) | `make corelib` |
| **Real programs** | dozens of real programs in Tycho (~19,000 lines under `tools/`), 6 over 1,000 lines; `tycho-db` asserts crash recovery from a real `kill -9`, torn-record discard and a TCP server | `find tools -name '*.ty' \| xargs wc -l` |
| **Normative spec** | 28 files, and the implementation is gated against them | `make spec-check` |
| **The whole gate** | recorded passes under **gcc and clang** on x86-64 Linux and **clang on darwin-arm64**; rerun for the current tree/platform | `make ci` |
| **Memory safety** | sanitizer lanes (ASan/UBSan/LSan/TSan) plus fuzz lanes, plus differential parity fuzzers | `make ci`, `make fuzz` |
| **Float determinism** | the emitted C is compiled `-ffp-contract=off`, so `a*b+c` rounds the same on every host | `make sheet-check` |
| **Uninitialised reads** | swept under an instrumented arena, 0 findings — the class no sanitizer sees | `docs/internals/probe-uninit-arena-2026-09-11.md` |
| **Defect log** | every closed entry names the command that checks it; a closed entry that stops being true fails the gate | `make friction-check` |
| **Docs** | all reachable pages link-checked, every `path:line` citation resolves, every public corelib function is in the catalogue | `make check-links` |
| **Surface freeze** | keywords, builtins and corelib functions locked in `surface.lock`; deliberate exceptions are recorded in the same commit | `make surface-check` |

`make parse-check` and `make tychoc1-check` pass. Both run outside `make ci` for cost reasons; see FRICTION 106-108. The defect log is executable: a closed entry that stops being true fails `make friction-check`, and fact pins also fail `make ci`.

---

## What is not done

**Platforms.** Artifacts exist for `linux-x86_64`, `mingw64-x86_64` and
**`darwin-arm64`** — built by `scripts/release.sh`, byte-identical across two
builds (`make release-check`), and content-gated (`make release-content`). The
macOS archive is **built, not published**: `gh release create` is the owner's
to run, and it is verified on the build box only. The `darwin-arm64` gate is
green with enumerated skips, one of which is a real coverage loss (Apple's ASan
ships no LeakSanitizer, so leak legs run on Linux only). Running the gate on a
new platform found and fixed latent defects in shipped code. Details are in
[ROADMAP](ROADMAP.md#1-it-runs-where-developers-are).

**Two external requirements for 1.0 remain:**

1. **A real program written by someone who is not the author.**
2. **An external security review.** `docs/internals/audit-brief.md` is what that
   reviewer gets handed.

Both need a person. See [ROADMAP](ROADMAP.md#what-10-requires).

**Ergonomics.** The surface is frozen and the implementation has substantial test
coverage, but it has not been tuned for daily use. `or_else` (shipped 2026-09-25) removed one class of
four-line `match`; more of the backlog remains unmined.

**A structural limit, already decided.** Pointer-shaped and structurally-shared
data (tries, graphs, DAGs) cost more here than in C or Go, because value
semantics store children by value and there are no references to share. The
index-pool idiom is the answer; references are refused by decision
(`docs/rfc/limited-references-spike.md`).

---

## What "good enough to see the light of day" would take

- **Continue correctness and security verification.** Passing gates and bounded
  fuzz runs are evidence for the executed paths, not proof of a bug-free compiler
  or runtime. On 2026-10-15, the append changes passed 1,149 fixture checks with
  each compiler and the self-hosting fixpoint; that is not a full `make ci` run.
  Keep regression gates and external review as requirements, not conclusions.
- **Publishing the macOS/ARM64 build** (`gh release create`, owner's call).
- **One other person writing one real program**, which is the actual 1.0 gate.
- **Ergonomics work**, ranked by counting sites as surface changes were.

---

## What this page is not

It is not a promise. It is a snapshot of what this thing measurably is, so
decisions about it use numbers instead of impressions.

See also: [ROADMAP](ROADMAP.md) · [how it is tested](docs/controls.md) ·
[the friction log](docs/internals/FRICTION.md) · `make status`
