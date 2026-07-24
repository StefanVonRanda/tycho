# Migrate `int` codegen to a fixed-width 64-bit `tycho_int` (punch-list #16 follow-up)

Follows the completed 1.0-gap plan (archived: `docs/internals/plan-1.0-freeze-DONE.md`;
commits `4e49442`, `972b9ae`, `3c0829a`, `eeb729b`, `755dd31`, `1b0e2b8`). That
plan RATIFIED `int` as required 64-bit two's-complement (`docs/spec/03-types.md`
§5.2.1) but deferred the codegen: both compilers still lower `int` to C `long`,
which is 64-bit on LP64 only and **32-bit on LLP64/ILP32**, so the reference impl
fails its own normative spec off-LP64. `appendix-f-impl-defined.md` F.3 records
this as the tracked follow-up. This plan closes it.

## Goal

Both compilers emit a fixed-width 64-bit integer for Tycho `int` (and the
`long`-carried `char`/`bool` reps and every internal `long` standing in for an
int-semantic value), so the reference implementation conforms on ILP32/LLP64 as
well as LP64. Done = a `tycho_int` typedef (`int64_t`) is the single width
authority, both compilers self-host byte-identically on it (`make fixpoint`
green), a new `make ilp32` gate compiles the emitted C `-m32` and runs the
fixtures green, `appendix-f-impl-defined.md` F.3 states the reference impl now
conforms on all three data models, and spec-plan #16's codegen follow-up is
struck.

User rulings (this session):
- **Repr** → `typedef int64_t tycho_int;` + `#define TY_PRId PRId64` in the
  emitted/runtime prelude; emit `tycho_int` everywhere `long` is emitted for an
  int-semantic value; `%ld` → `%" TY_PRId "`. One place owns the width.
- **Verify** → add a real `make ilp32` lane (emitted C rebuilt `-m32`, fixtures
  run, goldens compared). The dev box is LP64 (`long`==`int64_t`), so existing
  gates CANNOT prove the fix; the `-m32` lane is the only real proof. Keep an
  always-on `_Static_assert(sizeof(tycho_int)==8)` guard too.

## Pre-flight

- Worst case: an ABI change to emitted C. `long` is load-bearing — emitted type
  for `int`/`char`/`bool` (`src/tychoc.c:1181-1182`, `compiler/tychoc0.ty:4450-4454`),
  array/slice **length header** `((const long*)p)[-1]` (`tychoc.c:8969`,
  `tychoc0.ty:6444`), FFI crossing sigs `(const T*, long)` (`tychoc0.ty:2126`),
  map-key reps (`tychoc.c:10096`), parallel-loop bounds (`:8839-8841`), 384× in
  `runtime/tycho_rt.c`, plus hand-written shims (`corelib/net/net_shim.c`,
  `corelib/tls/tls_shim.c`). A partial migration that changes the int width but
  not a length header or an FFI sig is a silent ABI mismatch that only manifests
  off-LP64 — exactly where dev gates are blind. Hence audit (Phase 1) and the
  `-m32` gate (Phase 6) are not optional.
- **Self-hosting atomicity:** `make fixpoint` asserts tychoc and tychoc0 emit
  BYTE-IDENTICAL C. If tychoc emits `tycho_int` while tychoc0 still emits `long`,
  the text differs and fixpoint fails. So the two compilers' emission changes
  MUST land in ONE commit (Phase 4). Every other phase must also leave the tree
  fully green — no phase commits a red gate.
- Reversibility: git; one commit per phase. No user data, no persistent state.
- Assuming: `int64_t`/`PRId64` available (C99 `<stdint.h>`/`<inttypes.h>`); the
  emitted-C prelude is one shared location. Phase 1 VERIFIES the prelude location
  and the exact `long`-site inventory before any edit — the line numbers above
  are a grep snapshot, not trusted. Risk if wrong: a missed site is caught by the
  `-m32` gate (compile error or golden mismatch), not shipped.

## Phases

Strict order; one commit per phase; NO commit trailers (repo convention). Each
phase runs every relevant gate as its own foreground command and pastes the
summary line. A phase that fails a gate does NOT tick its box — it halts and
reports.

- [ ] **Phase 1 — audit every `long` site + probe the ILP32 toolchain (no code change)**
  - Scope: write `docs/internals/int64-migration-audit.md` — an exhaustive,
    classified inventory of every `long` in `src/tychoc.c`, `compiler/tychoc0.ty`,
    `runtime/*.c`/`*.h`, `corelib/**/*.c`, each tagged INT-SEMANTIC (→ `tycho_int`)
    vs NON-INT (genuine C `long`/`long long`/`unsigned long long` for `i64`/`u64`/
    FFI-to-C-`long`, leave alone). Name the single emitted-C prelude location both
    compilers share (typedef+guard+includes go there); quote it. Grep + classify
    every `%ld`/`%lu`. Probe: does `gcc -m32` (or clang) compile+link a trivial
    `int64_t` program? Record the exact command + result. Docs only — NO source.
  - Done when: audit lists each site file:line + classification + per-file totals;
    prelude location named with a quote; `-m32` probe result recorded (works /
    needs `gcc-multilib` / unavailable). If `-m32` is UNAVAILABLE, HALT here and
    report — the plan cannot prove its thesis without the proving gate.
  - Verify: `make test` green (no source touched); `git diff --stat` only `docs/`;
    paste the `-m32` probe command + output.

- [ ] **Phase 2 — emitted-C + runtime prelude: define `tycho_int`, migrate the runtime**
  - Scope: add to the shared prelude (from Phase 1): `#include <stdint.h>`,
    `#include <inttypes.h>`, `typedef int64_t tycho_int;`, `#define TY_PRId PRId64`,
    `_Static_assert(sizeof(tycho_int)==8, "tycho int must be 64 bits");`. Migrate
    `runtime/tycho_rt.c` (+ `.h`) INT-SEMANTIC `long`→`tycho_int`, `%ld`→
    `%" TY_PRId "`, per the audit — NOT the NON-INT/`i64`/`u64`/FFI sites. Do NOT
    change either compiler's emission yet (Phase 4). On LP64 `tycho_int`==`long`,
    so the still-`long` emitted C stays ABI-compatible with the migrated runtime
    and every gate stays green.
  - Done when: prelude defines typedef+guard; runtime int-semantic sites use it;
    tree builds + self-hosts.
  - Verify: `make test`, `make corelib`, `make rtparity`, `make conc`,
    `make fixpoint` — each its own command, paste each summary line. `fixpoint`
    MUST stay B==C (emission unchanged this phase).

- [ ] **Phase 3 — corelib FFI shims + any hand-written `long` ABI surface**
  - Scope: migrate INT-SEMANTIC `long` in `corelib/net/net_shim.c`,
    `corelib/tls/tls_shim.c`, and any other hand-written C the audit flags, to
    `tycho_int`, so the shim ABI matches what Phase 4 emits. Leave genuine
    C-library `long`/`socklen_t`/etc. alone. Precedes Phase 4 so the FFI boundary
    is ready before the compilers emit `tycho_int` across it.
  - Done when: shims use `tycho_int` on the Tycho-facing side; tree green.
  - Verify: `make test`, `make corelib`, `make ffi`, `make fixpoint` — each its
    own command, paste each summary line.

- [ ] **Phase 4 — BOTH compilers emit `tycho_int` (atomic, self-hosting-critical)**
  - Scope: in ONE commit, change every INT-SEMANTIC emission site in
    `src/tychoc.c` (`:1181-1182`, length headers `:8969`, map keys `:10096`,
    parallel bounds `:8839-8841`, range `:9673`, array-return sigs `:9775-9776`,
    struct len `:10210`, …) AND its mirror in `compiler/tychoc0.ty` (`:4450-4454`,
    `:6444`, slice guard `:5329`, FFI `:2126`, casts `:6480`/`:6484`,
    `:6618-6649`, …) from `long` to `tycho_int`, and every emitted `%ld` to
    `%" TY_PRId "`, per the audit. Keep the two compilers TEXTUALLY symmetric so
    `make fixpoint` stays byte-identical. Do NOT touch `i64`/`u64`/genuine-`long
    long` NON-INT sites. The one phase that changes the emitted ABI — lands whole.
  - Done when: both compilers emit `tycho_int`; a hand-run probe compiles under
    both and prints identical output; NO regression; `make fixpoint` B==C
    byte-identical (the backstop — a mismatched pair cannot pass).
  - Verify: build both, run a probe, `cmp` outputs (paste identical + exit 0);
    then `make test`, `make corelib`, `make rtparity`, `make conc`,
    `make fixpoint`, `make spec-check` — each its own command, paste each summary
    line. A missed INT-SEMANTIC site may still pass on LP64 (equal widths) —
    Phase 6's `-m32` gate is the real catch.

- [ ] **Phase 5 — regenerate the embedded/bootstrap C if the tree ships one**
  - Scope: Phase 1 determines whether a pre-generated `tychoc0` C artifact is
    committed (embed/bootstrap). If so, regenerate it from the Phase-4 compiler so
    the checked-in bootstrap also emits `tycho_int`, keeping `make bootstrap`
    consistent. If none is committed, this phase is a no-op — RECORD that finding
    and skip; do not invent work.
  - Done when: `make bootstrap` green from clean, or documented no-op.
  - Verify: `make bootstrap`, `make fixpoint` — paste summary lines.

- [ ] **Phase 6 — add the `make ilp32` gate (the real proof) + lock a fixture**
  - Scope: add an `ilp32` target to `Makefile` that emits the fixture suite's C
    with the Phase-4 compiler and compiles+runs it under `-m32` (ILP32), comparing
    against existing goldens — they must match bit-for-bit because `int` is now
    width-fixed. Add a fixture that MISBEHAVES under the old `long` lowering on
    ILP32 (a value above 2^31 that must not truncate) + its golden, so the gate
    has teeth. Use `gcc-multilib` if Phase 1 found it needed+present; the target
    MUST fail loudly (not skip silently) if the multilib toolchain is absent
    (RULE 4 visibility).
  - Done when: `make ilp32` builds the suite `-m32` and every golden matches; the
    new fixture passes on LP64 and ILP32; a reverted int width WOULD fail it
    (state how you confirmed the gate has teeth).
  - Verify: `make ilp32` green; `make test`, `make fixpoint` still green — paste
    each summary line; paste the fixture's old-`long`(ILP32-truncated) vs
    new(`tycho_int`) values proving the gate distinguishes them.

- [ ] **Phase 7 — spec + spec-plan: mark the reference impl conformant**
  - Scope: update `docs/spec/appendix-f-impl-defined.md` F.3 — reference compilers
    now realize `int` via fixed-width 64-bit `tycho_int`, conform on LP64, LLP64,
    ILP32 (strike "conforms on LP64 only"); add an `appendix-e-conformance.md` row
    citing the `ilp32` gate / new fixture; strike spec-plan #16's codegen
    follow-up and its §11/roadmap references. Docs only — no source.
  - Done when: F.3 records no non-conforming data model; the conformance row
    exists; #16's follow-up struck with commit citations.
  - Verify: `make spec-check`, `make check-links` — each its own command, paste
    each summary line; `git diff --stat` only `docs/`.
