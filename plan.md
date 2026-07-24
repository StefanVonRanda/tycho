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

**ENVIRONMENT GOTCHA (Phase 1, 2026-07-24) — run EVERY gate as `env -u LD_PRELOAD make …`.**
The interactive shell sets `LD_PRELOAD=/home/igzo/phonic/tools/block-nnp.so`. It
loads before the dynamically-linked `libasan.so.8`, so `tests/run.sh`'s ASan
binaries abort with "ASan runtime does not come first in initial library list"
→ all 231 sanitizer fixtures report `sanitizer exit 1`. This is NOT a code
regression and NOT a reason to weaken the harness's link-order check — it is a
foreign preload in the dev shell. Unsetting it (`env -u LD_PRELOAD`) restores a
clean run. This affects `make test`/`conc`/`corelib`/`rtparity`/`fixpoint`/
`ilp32` — any gate that runs an ASan binary. Do NOT add `verify_asan_link_order=0`
to `run.sh` (that would blind the check for real link-order bugs).

- [x] **Phase 1 — audit every `long` site + probe the ILP32 toolchain (no code change)**
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
  - DONE (2026-07-24). Docs-only; no compiler/runtime/corelib source touched.
    - Audit: `docs/internals/int64-migration-audit.md` (280 lines). Per-file
      `long` lines / tokens, split NON-INT (protect) vs INT-SEMANTIC (migrate) vs
      AMBIGUOUS: `src/tychoc.c` 204/242; `compiler/tychoc0.ty` 175/411;
      `runtime/tycho_rt.c` 384/559; corelib shims: net 27, tls 5, regex 12, image
      12, crypto 8, os 5, datetime 5, compress 5, http 2, io 2. `%l*` specifiers:
      55 (tychoc) + 25 (tychoc0) + 26 (rt). NON-INT surface is well-bounded:
      `i64`/`u64` type maps (~6 sites/compiler), hash/mask `unsigned long` families
      (~33 tychoc / ~50 tychoc0 / ~66 rt), shift-COUNT `long long`, and two
      libc-return `long`s (`tycho_rt.c:845` sysconf, datetime/os epoch). Everything
      else = Tycho int / length-cap header / map key / FFI crossing → `tycho_int`.
    - PRELUDE CORRECTION (feeds Phase 2): the plan assumed one shared prelude —
      FALSE. Two parity-locked runtime texts: `runtime/tycho_rt.c` embedded via
      `src/tychoc.c:26`/`:10126`, and `compiler/tychoc0.ty` `preamble()` (`:9697`).
      The typedef + guard + includes must land in BOTH, textually identical.
      Phase 2 scope updated accordingly.
    - BOOTSTRAP (pre-answers Phase 5): NO pre-generated `tychoc0` C artifact is
      committed (`Makefile:308` lists `tycho.c`/`tychofmt.c`/`tycho-lsp.c` as
      generated+removable; no `*.c` under `compiler/`). Phase 5 is a documented
      no-op unless `make bootstrap` regenerates from source.
    - ILP32 probe — PASS (multilib installed). Trivial `int64_t` program, scratchpad:
      - `gcc -m32 m32probe.c -o m32probe && ./m32probe` → `5000000000 szlong=4`
        (under ILP32 `long` is 4 bytes — the OLD lowering WOULD truncate 5e9;
        `int64_t` holds it). Native control → `5000000000 szlong=8`.
      - `_Static_assert(sizeof(int64_t)==8)` compiles under `-m32`.
      - `dpkg -l` confirms `gcc-multilib`, `lib32gcc-15-dev`, `lib32stdc++6` present.
      This is the divergence Phase 6's `make ilp32` gate will lock; the proving
      toolchain is available, so the plan proceeds.
    - Verify: `git diff --stat` = `docs/internals/int64-migration-audit.md` (new)
      + `plan.md` (this phase's own text) only — no source. `make test` green
      (no-op guard; summary line pasted at commit time).

- [x] **Phase 2 — emitted-C + runtime prelude: define `tycho_int`, migrate the runtime**
  - Scope: Phase 1 found there is NO single shared prelude — TWO parity-locked
    runtime/prelude texts (`runtime/tycho_rt.c` embedded via `src/tychoc.c`, and
    `compiler/tychoc0.ty` `preamble()`). Add to BOTH, textually identical:
    `#include <stdint.h>`, `#include <inttypes.h>`, `typedef int64_t tycho_int;`,
    `#define TY_PRId PRId64`,
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
  - DONE (2026-07-24). Prelude added TEXTUALLY IDENTICAL to both runtimes; only
    `runtime/tycho_rt.c` INT-SEMANTIC sites migrated (tychoc0 emission is Phase 4).
    - **Prelude block (byte-identical in both — verified emitted bytes match):**
      ```
      #include <stdint.h>
      #include <inttypes.h>
      typedef int64_t tycho_int;
      #define TY_PRId PRId64
      _Static_assert(sizeof(tycho_int)==8, "tycho int must be 64 bits");
      ```
      - `runtime/tycho_rt.c:50-54` (verbatim C, right after the include block, emitted
        first into every tychoc program by `src/tychoc.c:26`/`:10126`).
      - `compiler/tychoc0.ty:9699` — one `out = out + "…"` in `preamble()` (`:9697`),
        right after the include-block string (`:9698`), emitting the same 5 C lines
        (`\n`/`\"` escaped). tychoc0-emitted programs therefore DEFINE `TY_PRId` and
        include `<inttypes.h>` too (this is why corelib/conc/fixpoint compiled).
    - **Runtime migration (`runtime/tycho_rt.c` only):** 331 lines had a standalone
      `long`→`tycho_int`; all 26 `%ld`→`%" TY_PRId "`. NON-INT protected & untouched
      (verified counts): `unsigned long long`=13, `long long`=14, `unsigned long`
      (hash/mask)=66; shift COUNT `long long n` kept (`:129/:134` operand→tycho_int,
      count stays); `uint_to_str`/`u2s` unsigned stays. **AMBIGUOUS left as C `long`
      (fail-closed):** `tycho_ncpu()` return + `atol` + `sysconf` (`:842/:844/:845`
      orig) — libc CPU-count helper, not a Tycho int (audit §3). **AMBIGUOUS migrated
      per audit recommendation:** channel struct + ring positions (`_Atomic long`→
      `_Atomic tycho_int`) and `g_live_tasks`, so `cap` and the positions share one
      width; `tycho_now()`→`tycho_int` (epoch, 2038-safe on ILP32).
    - **Drift caught + fixed (RULE 6):** first `make rtparity` FAILED with 11 diffs.
      Root cause: `rtparity` (`tests/rtparity/run.py:92` `RE_MSG`) diffs the SET of
      emitted `"tycho: …"` trap-text keys textually; tychoc0's `preamble()` carries a
      SECOND hand-maintained copy of these runtime diagnostics, so migrating only
      `tycho_rt.c`'s `%ld` drifted them. Fix: ported `%ld`→`%" TY_PRId "` into the 6
      FIXED-runtime diagnostics in `tychoc0.ty` (`:9803` too-many-tasks, `:9844` hs,
      `:9846` hi_sidx, `:9850` hi_bchk, `:9873` hi_cap_check, `:9896` hi_chr; 8
      occurrences). LEFT the per-type array templates (`:9950-9971`) and slice
      (`:5329/:10027`) as `%ld` — they mirror `src/tychoc.c`'s still-`%ld` inline
      emission (Phase 4), so `tycho: index %ld out of bounds (len %ld)` stays SHARED.
      Empirically re-diffed emitted C: symbol sets now equal (28 shared, both diffs
      empty). No allowlist entry added (this was incomplete migration, not intended
      divergence).
    - **Phase 4 notes (scope-locked discoveries):** (a) `tycho_rt.c:104/:109` div-guard
      uses `LONG_MIN` on now-`tycho_int` a/b — on ILP32 `LONG_MIN`≠`INT64_MIN`, so
      Phase 4/6 must switch it to `INT64_MIN`. (b) `tycho_rt.c:132` shift keeps
      `(unsigned long)x` inner cast (audit-scoped) — Phase 4/6 must review for ILP32
      (would truncate the shift). (c) Phase 4 must migrate tychoc0's WHOLE inline
      runtime (channels/headers/hi_* `long`, per-type `%ld`) + both compilers' type
      emission in lockstep, or rtparity/fixpoint drift again.
    - **Gates (each `env -u LD_PRELOAD make …`):**
      - `make rtparity` → `env knobs 3 shared, 0 diff` · `diagnostics 28 shared, 0 diff`
        · `arena-stats rows 5 shared, 0 diff` · "the two runtimes agree".
      - `make fixpoint` → `ok B == C : tychoc0 reproduces itself byte-identically
        (34679 lines C)` · `fixpoint: all green`.
      - `make test` → `passed: 408   failed: 0` · `all green`.
      - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`.
      - `make conc` → `conc: passed 36   failed 0`.

- [x] **Phase 3 — corelib FFI shims + any hand-written `long` ABI surface**
  - Scope: migrate INT-SEMANTIC `long` in `corelib/net/net_shim.c`,
    `corelib/tls/tls_shim.c`, and any other hand-written C the audit flags, to
    `tycho_int`, so the shim ABI matches what Phase 4 emits. Leave genuine
    C-library `long`/`socklen_t`/etc. alone. Precedes Phase 4 so the FFI boundary
    is ready before the compilers emit `tycho_int` across it.
  - Done when: shims use `tycho_int` on the Tycho-facing side; tree green.
  - Verify: `make test`, `make corelib`, `make ffi`, `make fixpoint` — each its
    own command, paste each summary line.
  - DONE (2026-07-24). All 10 shims migrated INT-SEMANTIC `long`→`tycho_int` on the
    Tycho-facing side; genuine libc types left C `long`/`int`. No compiler/runtime/
    fixture touched.
    - **CRITICAL build fact (verified before editing):** shims are compiled as
      SEPARATE translation units and linked — `cc … base.c <pkg>_shim.c -lm`
      (`src/tychoc.c:11457`; `corelib/run.sh:41,45`; `tests/ffi/run.sh:36`). They do
      NOT receive the emitted runtime prelude, so a shim does NOT see the runtime's
      `tycho_int` typedef. Each migrated shim therefore now carries its own
      `#include <stdint.h>` + guarded `#ifndef TYCHO_INT_T / typedef int64_t tycho_int;`.
      Confirmed each sees it via standalone `gcc -fsyntax-only -Wall`: net, tls,
      regex, os, datetime, io, crypto, compress, http all clean. On LP64
      `tycho_int`==`long`, so the still-`long` Phase-4-pending emission stays
      ABI-compatible and every gate is green now; the `-m32` catch is Phase 6.
    - **Per-file INT-SEMANTIC migrated vs libc-facing LEFT:**
      - `net_shim.c` — migrated every fd/port/len/max/off/n/outlen param+return+local
        and the `(long)`→`(tycho_int)` casts of `socket/accept/send/recv/sendto/
        recvfrom/ntohs` returns; `"%ld",port`→`"%d",(int)port` (port validated
        0..65535). LEFT: `(int)fd` casts into the socket API, `(size_t)max/len`. No
        genuine libc `long` present.
      - `tls_shim.c` — migrated `port` (tcp_connect+tlsx_connect), `tlsx_write`
        len+ret, `off`, `tlsx_read` max+outlen; `%ld`→`%d,(int)port`. LEFT: tcp_connect
        RETURN `int` and `Tls.fd int` (internal libc fd, never crosses to Tycho), and
        `int n = SSL_read/SSL_write` (OpenSSL int).
      - `regex_shim.c` — all 12 → `tycho_int` (returns, `long n` group indices, `r`,
        and `(tycho_int)` casts of `rm_so/rm_eo/re_nsub`). `rx_compile` (void*/char*)
        unchanged. None left.
      - `image_shim.c` — all 12 → `tycho_int` (Img.w/h, decode len, encode plen/w/h,
        `need`, outlen×2, width/height/nbytes casts). None libc-facing. **NOT
        compile-tested in this env: libpng/`png.h` absent** (`gcc -E` on `<png.h>`
        fails; corelib skips image). Migrated by mirror of the other bytes-out shims;
        compile proof deferred to a host with libpng (and Phase 6 `-m32`).
      - `crypto_shim.c` — 8 → `tycho_int` (cx_key_random n, cx_key_len ret+cast,
        cx_random_hex n, pbkdf2 iters+dklen, ct_equal ret+eq, ed25519_verify ret+ok).
        LEFT untouched: every `(int)` cast into OpenSSL (RAND_bytes, PKCS5_PBKDF2_HMAC,
        strlen), the `1L<<20` bound literals, `size_t`.
      - `os_shim.c` — 5 → `tycho_int` (ty_os_decode return, both #ifdef branches;
        osx_system return; `OsRun.code`; osx_run_code return). REVIEW resolved: the
        `int st` param of `ty_os_decode` is the libc wait-status from `system`/`pclose`
        and STAYS `int`; only the decoded exit code is a Tycho int.
      - `datetime_shim.c` — 5 → `tycho_int` (dtx_local_offset ret+secs, dtx_offset_at
        ret+secs, `off`, both `tm_gmtoff` casts). REVIEW resolved: `secs` (epoch) and
        the returned offset are the Tycho ints the `.ty` side passes/receives; the
        libc `time_t t=(time_t)secs` cast and glibc's `long tm_gmtoff` read stay
        C-side (the gmtoff is converted into the `tycho_int` return). No genuine libc
        `long`-typed FFI boundary remains.
      - `compress_shim.c` — 5 → `tycho_int` (zx_compress/zx_decompress len+outlen, two
        `(tycho_int)s.total_out`). zlib `uLong/uInt/size_t` internals untouched.
      - `http_shim.c` — 1 migrated: `http_status` RETURN → `tycho_int` (+cast). 1 LEFT
        DELIBERATELY (fail-closed, RULE 7): `Resp.status` stays C `long` because
        `curl_easy_getinfo(CURLINFO_RESPONSE_CODE, &r->status)` (`:66`) writes a genuine
        libc `long` into it — an int64_t field would be a real ILP32 ABI bug (curl
        writes 4 bytes into an 8-byte slot). The Tycho boundary is only the return.
      - `io_shim.c` — 2 → `tycho_int` (iox_read_file outlen param + `(tycho_int)len`).
    - **Bug caught + fixed (RULE 6):** first `make corelib` → `FAIL regex (tychoc
      compile)`, ld `undefined reference to rx_compile`. False assumption: my full-file
      rewrite of `regex_shim.c` had dropped the untouched `rx_compile` function. Root
      cause read from the linker error (only rx_compile undefined, other rx_* resolved
      → single missing symbol, not a broken TU). Restored `rx_compile` verbatim;
      re-ran — green. `git diff` audited for net/image/regex: every removed def line has
      a type-renamed `+` counterpart, no other function lost.
    - **Gates (each `env -u LD_PRELOAD make …`):**
      - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`
        (exercises net/tls/regex/os/datetime/io/crypto/compress/http shims; image test
        skipped — no libpng).
      - `make ffi` → `ffi: green (tychoc + tychoc0 agree, ASan-clean, match golden …
        -L + --shim, package-scoped extern)`.
      - `make fixpoint` → `ok B == C : tychoc0 reproduces itself byte-identically
        (34679 lines C)` · `fixpoint: all green`.
      - `make test` → `passed: 408   failed: 0` · `all green`.
    - **Phase 6 note (scope-locked):** `image_shim.c` needs a libpng host to compile;
      the `-m32` gate must run where libpng-dev (multilib) is present, or explicitly
      skip image with a loud notice.

- [x] **Phase 4 — BOTH compilers emit `tycho_int` (atomic, self-hosting-critical)**
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
  - DONE (2026-07-24). ONE commit; the emitted ABI now uses `tycho_int`. Both
    compilers changed in lockstep; emission stays symmetric and self-hosts
    byte-identically. INT-SEMANTIC emission sites migrated per file:
    - `src/tychoc.c`: 136 emitted `long`→`tycho_int` (int/char base type `:1181-1182`,
      `[int]*` `:3391`, length headers `((const tycho_int *)…)[-1]`, map keys
      `:10096/:10101`, len/cap locals, Soa/parallel/range bounds, FFI sigs
      `:9767-9776`, all composite struct/array/map/soa helpers) + 23 emitted
      `%%ld`→`%%\" TY_PRId \"` (index/slice diagnostics). Bool stays emitted `int`
      (already 4-byte, no truncation). Host-side compiler `long` (fold, size
      binds, `ArrType.size`) left untouched — not an emitted site (audit §1
      AMBIGUOUS; irrelevant to the `-m32` emitted gate; note for Phase 6 if the
      compiler is ever built ILP32).
    - `compiler/tychoc0.ty`: 278 emitted `long`→`tycho_int` (int/char/bool type
      `:4450-4454`, whole inline runtime — channels/headers/string+map helpers/
      `hi_*`, FFI `:8896-8917`, slice/range/for bounds) + 15 emitted `%ld`→
      `%\" TY_PRId \"` (slice + per-type index diagnostics) + 1 shift-cast. Every
      `long` in this file is inside an emitted C string literal or a `#` comment
      (Tycho has no `long` type), verified: 0 English/prose false-positives, all
      `#`-comment `long` skipped.
    - Both migrations done with a string-literal-scoped transform (a real
      lexer masks in-string chars; `unsigned long long`/`long long`/`unsigned long`
      hash-mask/i64/u64/shift-count families PROTECTED and untouched; every
      quote-adjacent conversion + every skipped in-string `long` printed and
      eyeballed before writing). Residual int-semantic bare `long` emission:
      `grep 'return "long "|const long*|((const long'` = 0 in both files.
    - **Two ILP32 hazards applied (plan.md:177-182):**
      (a) `runtime/tycho_rt.c:109/:114` div-guard now `if (a == INT64_MIN && b == -1)`
      (`INT64_MIN` from `<stdint.h>`; comments `:103/:105/:41` updated off `LONG_MIN`).
      tychoc0's inline `hi_idiv`/`hi_imod` ALREADY used the ILP32-safe literal
      `(-9223372036854775807L - 1L)` (`tychoc0.ty:9855/:9856`) — left as-is (equal
      to `INT64_MIN`, `L` promotes to `long long` on ILP32).
      (b) shift inner cast widened to 64-bit: `runtime/tycho_rt.c:137`
      `return (tycho_int)((uint64_t)x << n);` and `tychoc0.ty:9859`
      `... return (tycho_int)((uint64_t)x << n); ...` (shift COUNT stays `long long n`,
      NON-INT). `(unsigned long)x` would truncate the shift to 32-bit on ILP32.
    - **Probe** (scratchpad, not committed) — int value 5e9 (>2^31), array + length
      header, int-keyed map, range loop, printed ints; compiled by tychoc and by
      the self-hosted tychoc0, `cmp` of stdouts: `cmp-exit=0` (identical):
      `big=5000000000 / len=4 / sum=100 / mapsum=600 maplen=3 / xs2=30`.
    - **Gates (each `env -u LD_PRELOAD make …`):**
      - `make fixpoint` → `ok B == C : tychoc0 reproduces itself byte-identically
        (34679 lines C)` · split-package self-host E==F · `fixpoint: all green`.
      - `make rtparity` → `env knobs 3 shared, 0 diff` · `diagnostics 27 shared,
        0 diff` · `arena-stats rows 5 shared, 0 diff` · "the two runtimes agree".
        (28→27: per-size fixed-array index messages collapse at the `%\" TY_PRId \"`
        split; SYMMETRIC in both compilers → 0 diff, and 27 ≥ anti-vacuity floor 25.)
      - `make test` → `passed: 408   failed: 0` · `all green`.
      - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`.
      - `make conc` → `conc: passed 36   failed 0`.
      - `make spec-check` → `spec-examples: 7 runnable example(s), all pass`.
      - `make ffi` (extra, FFI crossing width changed) → `ffi: green (tychoc +
        tychoc0 agree, ASan-clean, match golden …)`.
    - LP64 caveat holds: these gates prove no REGRESSION + self-host stability +
      diagnostic parity, NOT ILP32 completeness (`tycho_int`==`long` here). Phase 6's
      `-m32` gate is the only completeness proof; migration was made exhaustive
      against the audit rather than relying on local green.

- [x] **Phase 5 — regenerate the embedded/bootstrap C if the tree ships one**
  - Scope: Phase 1 determines whether a pre-generated `tychoc0` C artifact is
    committed (embed/bootstrap). If so, regenerate it from the Phase-4 compiler so
    the checked-in bootstrap also emits `tycho_int`, keeping `make bootstrap`
    consistent. If none is committed, this phase is a no-op — RECORD that finding
    and skip; do not invent work.
  - Done when: `make bootstrap` green from clean, or documented no-op.
  - Verify: `make bootstrap`, `make fixpoint` — paste summary lines.
  - DONE (2026-07-24) — NO-OP as Phase 1 pre-determined. No code change.
    - Confirmed no committed generated-C artifact: `git ls-files '*.c'` has nothing
      under `compiler/` or a `bootstrap` path; `git ls-files compiler/` is only
      `*.ty` sources + `*.sh` scripts + `compiler/tests/*.ty`. The `bootstrap`
      target (`Makefile`) runs `sh compiler/run.sh`, which regenerates the C from
      `compiler/tychoc0.ty` at build time — there is no checked-in C to re-emit.
    - So the Phase-4 `tycho_int` emission flows into the bootstrap automatically;
      nothing to regenerate. Verified the tree still bootstraps + self-hosts on the
      post-Phase-4 sources (each `env -u LD_PRELOAD make …`):
      - `make bootstrap` → `bootstrap: all green (tychoc0 matches the C compiler)`.
      - `make fixpoint` → `ok B == C : tychoc0 reproduces itself byte-identically
        (34679 lines C)` / `fixpoint: all green`.
    - Only plan.md changed this phase (this DONE block); no source touched.

- [x] **Phase 6a — CORRECTIVE (escalated from Phase 6): runtime int→string truncates on ILP32**
  - Raised by Phase 6's new `make ilp32` gate on 2026-07-24. This is a **missed
    INT-SEMANTIC site from Phase 2** (runtime migration), invisible on LP64.
  - **Root cause, generalized:** Phase 2 protected the whole `unsigned long`
    family as NON-INT (hash/mask). That over-protected. `unsigned long` is
    **32 bits on ILP32/LLP64**, so *every* use of it that holds a 64-bit quantity
    narrows there — whether the quantity is a Tycho int, a hash word, or a
    capacity bound. `runtime/tycho_rt.c` has 46 `unsigned long` lines (vs 7
    `unsigned long long`); tychoc0's emitted runtime carries 8 of the casts.
  - **Confirmed-broken sites (each observed failing, not inferred):**
    1. `runtime/tycho_rt.c:1203` (+ parity copy `compiler/tychoc0.ty:9880`, the
       emitted `i2s(...)`) — `unsigned long u = n < 0 ? -(unsigned long)n :
       (unsigned long)n;`. The parameter is correctly `tycho_int`, but the
       magnitude narrows, so **every printed int above 2^32 is reduced mod 2^32**.
       This alone accounts for 7 of the 8 `make ilp32` failures.
    2. `runtime/tycho_rt.c:196` — `tycho_cap_check`:
       `if (n < 0 || (unsigned long)n > (size_t)-1 / elem)`. **This guard FAILS
       OPEN on ILP32.** `reserve(a, 2305843009213693953)` (= 2^61+1) narrows to
       `1`, the bound test passes, and the very heap-overflow the guard exists to
       stop proceeds silently. `tests/abort/reserve_range.ty` stops aborting →
       `FAIL abort_reserve_range (runtime abort did not fire (exit 0))`. Treat as
       the highest-severity item here: it is a memory-safety guard, not a format.
    3. `runtime/tycho_rt.c:1700-1736` — siphash13's 64-bit words are
       `unsigned long`, so `(unsigned long)in[4] << 32 … << 56` shift past the
       type width on ILP32 (gcc `-Wshift-count-overflow` fires on every one), and
       the seeds `0x736f6d6570736575UL` / `0x646f72616e646f6dUL` (`:1688-1689`)
       and `0x9e3779b97f4a7c15UL` (`:2027`) truncate (gcc `-Woverflow`). Map
       hashing is therefore a different function on ILP32.
    4. `runtime/tycho_rt.c` map `mask`/`icap` family (`:1755,1764,1823,1898,…`) —
       derived from a `tycho_int` capacity; retype with the rest for consistency.
  - **Observed truncations** (`gcc -m32`, existing fixtures, golden vs actual):
    `clock` `sum=4999950000`→`704982704`; `int_overflow` INT64_MIN/MAX
    `-9223372036854775808`/`9223372036854775807`→`-0`/`4294967295`; `shift_edge`,
    `sized_array`, `sized_family`, `pkg_sized_pkg` `1099511627776` (2^40)→`0`;
    `strbuild` `9223372036854775807|-9223372036854775808`→`4294967295|-0`. The
    shift/arith helpers are correct — the narrowing is in the print and guard
    paths.
  - Fix: retype these to a **fixed-width** 64-bit unsigned (`uint64_t` /
    `UINT64_C` for the seed constants) in BOTH runtimes, textually identical, so
    `rtparity` does not drift. Leave genuinely `size_t`/libc-facing values alone.
  - Done when: `make ilp32` no longer truncates printed ints AND
    `abort_reserve_range` aborts again under `-m32`; `make rtparity` still 0 diff
    (both runtimes changed in lockstep); `make fixpoint` B==C.
  - Verify: `make test`, `make corelib`, `make rtparity`, `make conc`,
    `make fixpoint`, `make ilp32` — each its own command, paste each summary line.
  - **DONE (2026-07-24) — `make ilp32` went 400/8 → 408/0. All four site groups
    retyped to fixed-width `uint64_t` / `UINT64_C(...)`, in BOTH runtimes.**
    69 lines changed by a fail-closed line-targeted transform (every edit
    asserted its expected old text before substituting; a miss aborted the run).
    - **Group 1 — int→string magnitude (7 of the 8 failures).**
      `runtime/tycho_rt.c:1203` and its parity twin `compiler/tychoc0.ty:9880`
      (emitted `i2s`): `unsigned long u = n < 0 ? -(unsigned long)n :
      (unsigned long)n;` → `uint64_t u = n < 0 ? -(uint64_t)n : (uint64_t)n;`.
      The `tycho_int` parameter was already correct; only the magnitude narrowed.
    - **Group 2 — HIGHEST SEVERITY, the fail-open memory-safety guard.**
      `runtime/tycho_rt.c:196` + twin `compiler/tychoc0.ty:9873`
      (`tycho_cap_check` / `hi_cap_check`): `(unsigned long)n` → `(uint64_t)n`.
      The `(size_t)-1 / elem` right-hand side was left `size_t` deliberately — it
      is the genuine allocator bound and promotes to `uint64_t` in the compare.
    - **Group 3 — hash words, seeds and shifts.** `runtime/tycho_rt.c:1688-1690`
      (`tycho_hash_k0/k1`, `tycho_ik_seed`), `:1693,1700-1701` (seed init),
      `:1714-1736` (`tycho_siphash13`, `tycho_si_hash`), `:2026-2030`
      (`tycho_ik_hash`), `:2035-2048` (`tycho_arr_int/float/str_hash`) — all
      `unsigned long` → `uint64_t`, and every 64-bit literal `0x…UL` /
      `1099511628211UL` → `UINT64_C(…)`. Twins: `compiler/tychoc0.ty:9710-9713`
      (seeds + `mhash_seed_init`) and `:10158-10160` (`siphash13`, `mhash`,
      `ikhash`).
    - **Group 4 — map `mask`/`icap` family.** `runtime/tycho_rt.c:1755, 1764,
      1823, 1898, 1907, 1966, 2085, 2094, 2154, 2224, 2233, 2292` —
      `unsigned long mask = (unsigned long)m[.|->]icap - 1;` →
      `uint64_t mask = (uint64_t)m[.|->]icap - 1;`. Twins:
      `compiler/tychoc0.ty:10379-10380` (emitted `_find` / `_ixput`).
    - **Lockstep confirmed.** Every group was changed on both sides in the same
      transform; `make rtparity` is 0 diff on all three lanes (below). Residual
      `unsigned long` in `runtime/tycho_rt.c` is now ONLY `unsigned long long`
      (`:119,123,154,159,172,178,1213`) — the u64 type, ≥64-bit on every data
      model, correctly left alone. `size_t`/libc-facing values untouched.
    - **Site-2 proof — the abort fires again under `-m32`** (emit the fixture's
      C with `./tychoc --emit-c`, build `gcc -m32 -O2 -fwrapv -std=c11`):
      - AFTER (`uint64_t`): stderr `tycho: reserve capacity 2305843009213693953
        out of range`, `exit=1`.
      - CONTROL, same emitted C with ONLY that one cast sed'd back to
        `(unsigned long)`: prints `1`, `exit=0`. That is the guard failing OPEN —
        2^61+1 narrows to 1, the bound test passes, `push` proceeds. This is a
        direct before/after on the same binary source, not an inference.
    - **Site-1 proof — printed ints above 2^32 are correct under `-m32`.**
      `shift_edge`, `int_overflow`, `strbuild` emitted, built `gcc -m32`, run:
      each one's stdout is byte-identical to its unchanged LP64 golden.
      `shift_edge` under `-m32` now prints `1099511627776` (2^40) and
      `2147483648`, where it printed `0` before.
    - **Group-3 proof — the shifts no longer exceed the type width.**
      `gcc -m32 -Wall -Wextra -c` on the emitted C for `tests/rtparity/surface.ty`
      reports **0** `-Wshift-count-overflow` / `-Woverflow` diagnostics (was one
      per `<< 32 … << 56` term plus the truncated seeds).
    - **Gates (each its own command; 64-bit lanes `env -u LD_PRELOAD make …`):**
      - `make test` → `passed: 408   failed: 0` · `all green`
      - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`
      - `make rtparity` → `env knobs 3 shared, 0 allowlisted difference(s) (ok)` ·
        `diagnostics 27 shared, 0 …(ok)` · `arena-stats rows 5 shared, 0 …(ok)` ·
        `rtparity: the two runtimes agree on env knobs, diagnostics and arena stats`
      - `make conc` → `conc: passed 36   failed 0`
      - `make fixpoint` → `ok B == C : tychoc0 reproduces itself byte-identically
        (34679 lines C)` · `fixpoint: all green`
      - `make ilp32` → **`passed: 408   failed: 0` · `all green`** (was
        `passed: 400   failed: 8`)
    - **ilp32 before → after, per fixture.** All 8 previously-red fixtures are
      green: `clock`, `int_overflow`, `shift_edge`, `sized_array`,
      `sized_family`, `strbuild`, `pkg_sized_pkg` (group 1, the print path) and
      `abort_reserve_range` (group 2, the fail-open guard). **Zero remaining
      failures**, so there is nothing to attribute to 6b on this lane — 6b's
      fixture is still parked at `tests/ilp32/int64_width.ty`, outside the
      `tests/*.ty` glob, so `make ilp32` does not run it yet (6b owns the move).
      The lane is nonetheless no longer vacuous: the 7 group-1 fixtures above all
      exercise values past 2^32 (2^40, INT64_MIN/MAX, `sum=4999950000`).
    - **No 64-bit golden shifted and no golden was re-recorded.** On LP64
      `uint64_t` is `unsigned long`, so the change is a provable no-op there;
      `test`/`corelib`/`conc`/`fixpoint` all matched their existing goldens
      unchanged, which is the evidence that nothing width-sensitive was retyped
      by mistake.
    - **Out of scope, appended as Phase 6c (not silently absorbed):** the
      per-type struct/tuple/array hash *emitters* in both compilers still declare
      `unsigned long` accumulators seeded from the now-`uint64_t` seed. Hash
      quality only; no observable output change (map iteration is insertion-
      ordered). See Phase 6c.

- [x] **Phase 6b — CORRECTIVE (escalated from Phase 6): emitted int literals are 32-bit — tychoc0 MISCOMPILES on LP64 TODAY**
  - Raised by Phase 6's new fixture on 2026-07-24. **Severity upgraded while
    investigating: this is NOT only an ILP32 portability defect. `tychoc0`
    miscompiles integer-literal arithmetic on THIS LP64 box, right now**, and
    `make fixpoint` catches it the moment a fixture multiplies two literals past
    2^31. It predates the int64 migration; nothing covered it because no fixture
    did such a multiply.
  - **Demonstrated divergence (LP64, `tests/ilp32/int64_width.ty`, same source):**
    | expression | tychoc | tychoc0 |
    |---|---|---|
    | `5000000000` | `5000000000` | `5000000000` |
    | `100000 * 100000` | `10000000000` | **`1410065408`** |
    | `1 << 40` | `1099511627776` | `1099511627776` |
    | sum | `1114511627776` | **`1105921693184`** |
    Emitted C: tychoc `tycho_int h_prod = (100000L * 100000L);` vs tychoc0
    `tycho_int h_prod = (100000 * 100000);`. `make fixpoint` compares the two
    binaries' **runtime output** (`compiler/fixpoint.sh:24-29`), so this reports as
    `FAIL int64_width.ty (B differs from the C compiler)`.
  - Sites (the two compilers disagree here, and BOTH are wrong off-LP64):
    - `src/tychoc.c:8343-8344` — `case E_INT: … return sfmt("%ldL", e->ival);`
      and `case E_CHAR: return sfmt("%ldL", e->ival);`. The `L` suffix is C
      `long` → **32-bit on ILP32**.
    - `compiler/tychoc0.ty:6007-6011` — `gen_expr`'s `EInt(t,_el)/EBool/EChar`
      arms `return t`, emitting the literal **bare**, i.e. C `int` → also 32-bit.
  - Why it is wrong: Phase 4 migrated the emitted *type keyword* `long` →
    `tycho_int`, but an integer **literal's width suffix** is not a type keyword,
    so the string-literal transform never saw it. The destination variable is
    64-bit, but the *arithmetic* is evaluated at the literal's own rank and
    truncates **before** the store:
    - tychoc `…L` = C `long` → 32-bit on ILP32/LLP64 (correct on LP64 only).
    - tychoc0 bare = C **`int`** → 32-bit on **every** data model, LP64 included.
      This is the live miscompile above.
    `5000000000` survives in both (a decimal constant too large for its suffix
    rank is promoted to `long long`); it is the *small* literals whose product
    overflows that break.
  - Fix: emit a width-safe literal in BOTH compilers — an `LL` suffix (`long
    long`, ≥64-bit on every data model) for int and char literals, leaving the
    `U`/`ULL` (u32/u64) forms alone. This also *removes* the tychoc/tychoc0
    emission asymmetry (`100000L` vs `100000`), so it should improve, not
    endanger, agreement.
  - **Also owned by this phase: un-park the fixture.** Phase 6 committed it to
    `tests/ilp32/int64_width.ty` + `.out` — deliberately OUTSIDE the `tests/*.ty`
    glob so it could not redden `make test`/`make fixpoint`/`make ci` while the
    bug is unfixed. Once literals are width-safe, `git mv` both files up into
    `tests/` so the main suite, fixpoint AND `make ilp32` all cover them. **Until
    that move, `make ilp32` is VACUOUS** (no in-glob fixture exercises a value
    above 2^32) and must not be read as evidence of ILP32 conformance.
  - Done when: emitted int arithmetic is 64-bit under `-m32` AND tychoc0 agrees
    with tychoc on LP64; the fixture is back in `tests/` and every gate is green.
  - Verify: `make test`, `make corelib`, `make rtparity`, `make conc`,
    `make fixpoint`, `make ffi`, `make spec-check`, `make ilp32` — each its own
    command, paste each summary line. `fixpoint` MUST stay B==C **and** must now
    pass `int64_width`.
  - **DONE 2026-07-24.** Emitted int/char literals are now width-safe in both
    compilers; the live LP64 tychoc0 miscompile is gone.
  - **The two literal-emission sites changed (old → new):**
    - `src/tychoc.c:8348-8349` (`gen_expr`, after the u32/u64 early-returns,
      which were left untouched):
      `return sfmt("%ldL", e->ival);` → `return sfmt("%ldLL", e->ival);`
      `case E_CHAR: return sfmt("%ldL", e->ival);` → `... sfmt("%ldLL", ...)`
    - `compiler/tychoc0.ty:6007-6012` (`gen_expr`):
      `EInt(t, _el): return t` → `return t + "LL"`
      `EChar(t, _el): return t` → `return t + "LL"`
    - **`EBool` deliberately left bare** (`return t`). Checked before deciding:
      tychoc emits `case E_BOOL: return sfmt("%ld", e->ival);`
      (`src/tychoc.c:8361`) — also bare, no suffix. A bool is 0/1, so width is
      not a correctness question, and leaving both bare keeps the two compilers
      SYMMETRIC. Suffixing only tychoc0 would have *introduced* an asymmetry.
  - **Why `LL` and not `L`:** `L` is C `long` — 32-bit under ILP32/LLP64, so
    `100000L * 100000L` truncates *in the multiply*, before the store into the
    64-bit destination. `LL` is C `long long`, ≥64-bit on every data model.
    A bare literal is worse still: C `int`, 32-bit even on LP64 — that was the
    live miscompile.
  - **Before/after, LP64, same source (`int64_width.ty`), proving the miscompile
    is gone.** tychoc = the C compiler; tychoc0 = the self-hosted compiler
    (`./tychoc compiler/tychoc0.ty -o A`, then `A` compiles the fixture, cc'd):
    | expression | tychoc (before & after) | tychoc0 BEFORE | tychoc0 AFTER |
    |---|---|---|---|
    | `5000000000` | `5000000000` | `5000000000` | `5000000000` |
    | `100000 * 100000` | `10000000000` | **`1410065408`** | `10000000000` |
    | `1 << 40` | `1099511627776` | `1099511627776` | `1099511627776` |
    | sum | `1114511627776` | **`1105921693184`** | `1114511627776` |
    Emitted C, before: tychoc `tycho_int h_prod = (100000L * 100000L);` vs
    tychoc0 `tycho_int h_prod = (100000 * 100000);`. After, BOTH emit
    `tycho_int h_prod = (100000LL * 100000LL);` and
    `tycho_int h_big = 5000000000LL;` — the emission asymmetry is removed.
  - **Fixture un-parked (the gate is no longer vacuous):** `git mv` of
    `tests/ilp32/int64_width.ty` → `tests/int64_width.ty` and `.out` likewise, so
    the `tests/*.ty` glob now covers it in `make test`, `make fixpoint` AND
    `make ilp32`. `tests/ilp32/` removed (empty), and the now-dead
    `!/tests/ilp32/*.out` un-ignore stanza dropped from `.gitignore` (the
    fixture is covered by the pre-existing `!/tests/*.out` rule).
    Confirmed in-glob under `-m32`: `make ilp32` prints `ok    int64_width`.
  - **`ilp32` wired into CI:** `scripts/ci.sh` now runs `make -s ilp32` as step
    `[2b/19]`, immediately after `make test` — matching the file's own existing
    inserted-lane convention (`[10b/19]` for `fuzz-pkg`), which avoids
    renumbering 17 unrelated labels. `sh -n scripts/ci.sh` clean.
  - **Gates (each its own foreground command, `env -u LD_PRELOAD` for the 64-bit
    lanes per the dev-shell LD_PRELOAD note):**
    - `make test` → `passed: 409   failed: 0` / `all green`
      (408 → 409: the un-parked fixture joined the main suite)
    - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`
    - `make rtparity` → `rtparity: the two runtimes agree on env knobs, diagnostics and arena stats`
      (3 env knobs / 27 diagnostics / 5 arena-stats rows, **0** differences)
    - `make conc` → `conc: passed 36   failed 0`
    - `make fixpoint` → `ok   B == C : tychoc0 reproduces itself byte-identically (34679 lines C)`
      and `fixpoint: all green (self-hosting; B==C; single files + packages;
      tychoc0 self-split dogfood)`. **B==C held, and the `FAIL int64_width.ty
      (B differs from the C compiler)` line is gone** — this was the exact gate
      the bug reddened.
    - `make ffi` → `ffi: green (tychoc + tychoc0 agree, ASan-clean, match golden ...)`
    - `make spec-check` → `spec-examples: 7 runnable example(s), all pass`
    - `make ilp32` → `passed: 409   failed: 0` / `all green`, **and this is the
      first NON-VACUOUS ILP32 result**: `ok    int64_width` appears in the run,
      so an in-glob fixture whose every value exceeds 2^31 now really is rebuilt
      and executed under 32-bit `long`.
  - **No golden was re-recorded.** `git status` after the full sweep shows only
    the two compilers, `.gitignore`, `scripts/ci.sh` and the fixture rename —
    zero modified `.out` files, so nothing shifted on LP64 except the
    previously-miscompiled tychoc0 output becoming correct.
  - **Note (cosmetic, not fixed — out of this phase's scope):**
    `tests/int64_width.ty`'s trailing comment on the `total` line reads
    `# 16099511627776`, but the correct sum (and the tracked golden) is
    `1114511627776`. Comment only; the golden and the assertion are right.

- [x] **Phase 6 — add the `make ilp32` gate (the real proof) + lock a fixture**
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
  - **HALTED, NOT TICKED (2026-07-24) — the gate works and it caught real bugs.**
    The infrastructure this phase owes is BUILT and COMMITTED (target, harness
    flag, fixture, golden), but `make ilp32` is RED, because the emitted C really
    does truncate on ILP32. Per this plan's own rule ("a missed site is caught by
    the `-m32` gate … not shipped") and RULE 6, the failure was NOT patched under
    this phase and the gate was NOT weakened; two corrective phases (**6a**, **6b**
    above) were appended with file:line evidence and must land FIRST. Re-run this
    phase's Verify block after 6a+6b.
    - **Delivered here (committed, green where it can be):**
      - `Makefile` target `ilp32` (after `mandelbrot`, before `ffi`):
        preflight-compiles a `_Static_assert(sizeof(long)==4)` + `int64_t` probe
        with `gcc -m32` and **exits nonzero with a loud stderr banner if the
        multilib toolchain is absent — never silently skips** (RULE 4); then runs
        `CC="gcc -m32" TYCHO_NO_ASAN=1 sh tests/run.sh`.
      - REUSES `tests/run.sh` rather than duplicating fixture enumeration: the
        existing `for hi in examples/*.ty tests/*.ty` loop and the existing
        `tests/*.out` goldens are the ILP32 oracle unchanged. The only harness
        change is a new `NO_ASAN="${TYCHO_NO_ASAN:-0}"` knob that skips the
        sanitizer BUILD, RUN and native-vs-ASan diff while keeping the native
        build, run and golden compare.
      - **ASan lane is SKIPPED for ilp32 and this is LOGGED, not silent** — the
        target echoes `ilp32: ASan lane SKIPPED for ilp32 (32-bit ASan runtime
        absent under multilib; 64-bit 'make test' covers ASan)`. ASan coverage is
        unchanged on 64-bit.
      - Fixture `tests/ilp32/int64_width.ty` + golden `tests/ilp32/int64_width.out`
        (recorded from the reference compiler's native 64-bit stdout): four values
        all above 2^31 — `5000000000`, `100000 * 100000`, `1 << 40`, and their sum.
        **PARKED under `tests/ilp32/` on purpose.** It was first placed at
        `tests/int64_width.ty`, where `make test` passed it **409/0 all green**,
        but `make fixpoint` went **RED** — `FAIL int64_width.ty (B differs from the
        C compiler)` — because tychoc0 miscompiles `100000 * 100000` on LP64 (see
        6b). Rather than commit a red `fixpoint` (it gates `make ci` and the
        pre-push hook) or weaken the fixture, it is parked outside every glob
        (`tests/*.ty`, `tests/{pkg,reject,abort,diag,warn}` are all unaffected by a
        new `tests/ilp32/` directory) so the whole tree stays green. **6b owns
        moving it back into `tests/`.** Parking it costs the gate nothing today:
        the gate is emphatically NOT vacuous without it (see the run below).
    - **TEETH PROVEN ON REAL CODE (stronger than the planned hand-edit):** the
      gate does not need a synthetic reverted-width build to show it discriminates
      — the same emitted C, same goldens, only the data model changed:
      | value | native LP64 (golden) | `gcc -m32` ILP32 (actual) |
      |---|---|---|
      | `5000000000` | `5000000000` | `705032704` |
      | `100000 * 100000` | `10000000000` | `1410065408` |
      | `1 << 40` | `1099511627776` | `0` |
      | sum | `1114511627776` | `2115098112` |
      A confirming control was also run: hand-swapping `tycho_int`→`long` in the
      emitted C is not even compilable (`typedef int64_t long;`), which is itself
      evidence that `tycho_int` is now the single width authority in the prelude.
      The isolated-literal probe `gcc -m32` on `int64_t a=5000000000L,
      b=100000L*100000L, c=1L<<40, d=100000LL*100000LL, e=1LL<<40` printed
      `aL=5000000000 bL=1410065408 cL=0 dLL=10000000000 eLL=1099511627776 szl=4`,
      isolating 6b's fix (`LL`) from 6a's (print path).
    - **Gates run this phase (each `env -u LD_PRELOAD make …`):**
      - `make test` → `passed: 409   failed: 0` · `all green` (409 = 408 + the new
        fixture, run while it was still in `tests/`; it is correct on LP64).
      - `make fixpoint` (fixture in `tests/`) → **RED**: `FAIL int64_width.ty (B
        differs from the C compiler)` · `fixpoint: FAIL` — the 6b LP64 miscompile.
      - `make fixpoint` (fixture parked, tree as committed) → `fixpoint: all green
        (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)`.
      - `make ilp32` → **RED, and this is the deliverable working:**
        `ilp32: -m32 toolchain OK (32-bit long, 64-bit int64_t verified)` ·
        `ilp32: ASan lane SKIPPED for ilp32 (…)` · `passed: 400   failed: 8` ·
        `failed: clock int_overflow shift_edge sized_array sized_family strbuild
        pkg_sized_pkg abort_reserve_range`.
        **8 pre-existing fixtures — none of them mine — truncate on ILP32**, and
        one (`abort_reserve_range`) is a memory-safety guard failing OPEN. The
        gate needed no bespoke fixture to find real bugs; see 6a for the
        per-failure attribution.
      - `ilp32` is a standalone target, deliberately NOT wired into
        `scripts/ci.sh` (verified absent) until 6a+6b make it green, so `make ci`
        and the pre-push hook are unaffected. Wiring it in is 6b's closing step.
  - **CLOSED (2026-07-24) — ticked by Phase 8's gate sweep. The HALTED narrative
    above stands unedited; this is only the re-run it asked for.**
    - This phase halted on a RED `make ilp32` and instructed: "Re-run this phase's
      Verify block after 6a+6b." Its infrastructure (the `ilp32` target, the
      `NO_ASAN` knob in `tests/run.sh`, the fixture + golden) was committed at the
      time and is unchanged. Nothing was re-run to *make* it pass and no gate was
      weakened — the three defects the gate caught were fixed by the corrective
      phases it spawned:
      - `38b04ba` — **6a**: the 8 ILP32-truncating sites, incl. `abort_reserve_range`
        (a memory-safety guard that was failing OPEN).
      - `04a6357` — **6b**: the LP64 `100000 * 100000` tychoc0 miscompile that had
        forced the fixture to be parked in `tests/ilp32/`; the fixture moved back
        into `tests/` and `fixpoint`'s `FAIL int64_width.ty (B differs from the C
        compiler)` line is gone.
      - `8c754bb` — **6c**: the emitted per-type hash accumulators narrowing on ILP32.
    - **Verify block re-run (one sweep, shared with Phase 8 — these are the same
      three commands, reported once, not re-run per phase):**
      - `make ilp32` → `passed: 410   failed: 0` · `all green` — the gate that was
        RED at 400/8 is green, and non-vacuously so (`int64_width` is back inside
        `tests/*.ty` and really is rebuilt + executed under 32-bit `long`).
      - `make test` → `passed: 410   failed: 0` · `all green`
      - `make fixpoint` → `ok   B == C : tychoc0 reproduces itself byte-identically
        (34679 lines C)` · `fixpoint: all green (self-hosting; B==C; single files +
        packages; tychoc0 self-split dogfood)`
    - Counts read 410 (not the 409 recorded when 6c landed) purely because Phase 8
      added `tests/const_fold_width.ty` in the same commit. No golden was
      re-recorded: `git status --porcelain` shows zero modified `.out` files.
    - The one loose end this phase logged — "wiring it in is 6b's closing step" —
      is **already done**, verified not assumed: `scripts/ci.sh:37-38` now runs
      `make -s ilp32` as step `[2b/19]`. So the gate is not merely green, it is
      enforced by `make ci` and the pre-push hook. Nothing is left outstanding.

- [x] **Phase 6c — follow-up discovered by 6a (scope-locked out of it): per-type hash emitters still narrow on ILP32**
  - Raised by Phase 6a on 2026-07-24. 6a retyped the runtime's hash words, seeds
    and `mask` family to `uint64_t`, but the *per-type* hash helpers that each
    compiler EMITS for user structs / tuples / composite arrays still declare
    `unsigned long` accumulators, seeded from the now-`uint64_t` seed:
    - `src/tychoc.c:10304, 10308, 10343, 10353, 10367` (prototypes),
      `:10429-10430, 10442-10443` (`tycho_hash_S_*`, `tycho_hash_T%d`),
      `:10486-10489, 10518-10521, 10584-10585` (`tycho_arr_C%d_hash`), and the
      emitted map `mask` at `:10620, 10625, 10662`.
    - `compiler/tychoc0.ty:9386, 9397, 10278` (`<T>_hash` emitters) and
      `:16026, 16031, 16036` (their prototypes).
  - **Severity: hash quality on ILP32 only — NOT a correctness or safety bug.**
    On ILP32 the accumulator truncates the 64-bit seed to 32 bits, so struct- and
    array-keyed maps get a weaker (but still well-defined) hash. `keys()` and
    `for k in m` iterate in INSERTION order (`runtime/tycho_rt.c:1685-1687`), so
    no program output changes; `make ilp32` is 408/0 with these left as-is.
    Deliberately NOT folded into 6a: 6a's scope was the four confirmed-broken
    site groups, and `src/tychoc.c` is not a runtime.
  - Fix: retype these emitted accumulators/prototypes to `uint64_t` in BOTH
    compilers in one pass, keeping the two emitters textually parallel.
  - Done when: no emitted hash helper declares `unsigned long`; every gate green.
  - Verify: `make test`, `make corelib`, `make rtparity`, `make conc`,
    `make fixpoint`, `make ilp32` — each its own command, paste each summary line.

  - **DONE 2026-07-24.** Every emitted hash accumulator, seed and 64-bit constant
    is now fixed-width. Line numbers below are the PRE-EDIT ones, re-verified
    against the tree at `04a6357` (6a's filing was stale for the mask sites —
    see the correction note).

    **`src/tychoc.c` — 13 emitter sites (all `unsigned long` -> `uint64_t`, all
    `…UL` 64-bit constants -> `UINT64_C(…)`):**
    | site | old emitted text | new emitted text |
    |---|---|---|
    | `:10307` | `static unsigned long tycho_hash_S_%s(…);` | `static uint64_t …` |
    | `:10311` | `static unsigned long tycho_hash_T%d(…);` | `static uint64_t …` |
    | `:10346,10356,10370` | `static unsigned long tycho_arr_C%d_hash(…);` | `static uint64_t …` |
    | `:10432-10433` | `static unsigned long tycho_hash_S_%s(…) {` / `unsigned long h = tycho_hash_k0;` | `uint64_t` both |
    | `:10436` | `h = h * 1099511628211UL ^ …` | `h = h * UINT64_C(1099511628211) ^ …` |
    | `:10445-10446` | tuple hash sig + `unsigned long h` | `uint64_t` both |
    | `:10449` | `1099511628211UL` | `UINT64_C(1099511628211)` |
    | `:10489-10492` | bounded[N]T hash: sig, `unsigned long h = 1469598103934665603UL`, `unsigned long e`, `* 1099511628211UL`, cast `(unsigned long)xs.v[i]` | `uint64_t` / `UINT64_C(1469598103934665603)` / `UINT64_C(1099511628211)` / `(uint64_t)xs.v[i]` |
    | `:10521-10524` | fixed `[N]T` hash: same five spellings | same five, fixed-width |
    | `:10587-10589` | composite-elem array hash: sig, `unsigned long h = tycho_hash_k0`, `1099511628211UL` | `uint64_t` / `UINT64_C(…)` |
    | `:10623, 10628, 10665` | `unsigned long mask = (unsigned long)m…icap - 1` | `uint64_t mask = (uint64_t)m…icap - 1` |

    **`compiler/tychoc0.ty` — 6 emitter sites, the same transform:**
    | site | old | new |
    |---|---|---|
    | `:9386` (`gen_tuple_hash`) | `"static unsigned long " … " v) { unsigned long h = hash_k0;"` | `uint64_t` both |
    | `:9389` | `" h = h * 1099511628211UL ^ "` | `" h = h * UINT64_C(1099511628211) ^ "` |
    | `:9397` (`gen_arr_hash`) | `unsigned long` return + `unsigned long h` + `1099511628211UL` | `uint64_t` ×2 + `UINT64_C(…)` |
    | `:10278` (`gen_struct_hash`) | `unsigned long` return + `unsigned long h = hash_k0` | `uint64_t` both |
    | `:10281` | `1099511628211UL` | `UINT64_C(1099511628211)` |
    | `:16026, 16031, 16036` | `static unsigned long <T>_hash(…);` prototypes | `static uint64_t …` |

  - **Both compilers changed identically — but "identically" is SEMANTIC, not
    byte-identical, and the phase brief's premise was wrong.** The two emitters
    were never textually parallel: tychoc spells the helpers `tycho_hash_S_X` /
    `tycho_hash_T%d` / `tycho_arr_C%d_hash` seeded from `tycho_hash_k0`, tychoc0
    spells them `X_hash` / `Tup_*_hash` / `Arr_*_hash` seeded from `hash_k0`.
    `compiler/fixpoint.sh:21` compares tychoc0 against ITSELF byte-for-byte
    (`cmp cA.c cB.c`) and against tychoc only BEHAVIOURALLY (`:23-30`), so
    cross-compiler byte-identity is not a gate and never held. What was applied
    to both is the same transform on the same construct: every hash return type,
    accumulator, element temp and 64-bit FNV constant is now fixed-width.
  - **Correction to 6a's filing:** the emitted map `mask` was cited as pending in
    BOTH compilers. Re-reading the tree showed tychoc0 `:10379-10380` was already
    `uint64_t mask = (uint64_t)m…icap - 1` (6a fixed it there). Only
    `src/tychoc.c:10623/10628/10665` still emitted `unsigned long mask`; retyping
    them CLOSES a real cross-compiler divergence rather than adding one.

  - **`-m32 -Wall -Wextra` on emitted C — before/after (RULE 5 evidence).**
    Probes: `tests/{mapstructkey,maptuplekey,maparraykey,map_literal_composite_key,enum_key,generic_hashable}.ty`
    plus a scratch `fixarrkey.ty` (a `[3]int` and a `bounded[4]int` map key) —
    needed because NO tracked fixture reaches `src/tychoc.c:10489/10521`, the two
    sites carrying the 64-bit FNV offset basis.

    | | total warnings | `-Woverflow` / `-Wshift-count-overflow` | `unsigned long` in emitted C |
    |---|---|---|---|
    | before | 688 | **2** | present in all 7 probes |
    | after  | 686 | **0** | **0 in all 7** |

    The delta is exactly the two overflow warnings; the 686 residual are
    pre-existing `-Wunused-function` (496), `-Wmisleading-indentation` (161) and
    `-Wunused-parameter` (29), unchanged by this phase. The two that vanished
    were the real defect, and gcc named the corruption precisely:

    ```
    fixarrkey.c:2502:23: warning: conversion from 'long long unsigned int' to
      'long unsigned int' changes value from '1469598103934665603' to '1939669891' [-Woverflow]
    fixarrkey.c:2591:23: warning: conversion from 'long long unsigned int' to
      'long unsigned int' changes value from '1469598103934665603' to '1939669891' [-Woverflow]
    ```

    i.e. on ILP32 the FNV-1a offset basis was silently truncated to its low 32
    bits, so `tycho_arr_C*_hash` for fixed/bounded array keys was seeded with a
    DIFFERENT constant than on LP64. That is why the gates alone could not
    distinguish this phase: a differently-seeded hash is still a well-defined
    hash.

  - **No golden shifted; the change is provably a no-op on LP64.** `git status`
    after the edit lists exactly three modified files — `src/tychoc.c`,
    `compiler/tychoc0.ty`, `tests/int64_width.ty` — and **zero** `.out`/
    `.expected` files. Two independent reasons this had to hold:
    1. `uint64_t` IS `unsigned long` on LP64, and `UINT64_C(x)` expands to the
       same value as `xUL`; asserted by a compiled probe:
       `_Static_assert(sizeof(uint64_t)==sizeof(unsigned long))` plus
       `UINT64_C(1099511628211)==1099511628211UL` and
       `UINT64_C(1469598103934665603)==1469598103934665603UL` — all pass. The
       before/after emitted-C diff contains ONLY type and constant spellings.
    2. Even where the hash VALUE does change (ILP32), no output can move:
       `runtime/tycho_rt.c:1661` and `:1682-1686` state, and the code confirms,
       that `keys()` / `for k in m` iterate in INSERTION order via the
       `tycho_ord_*` linked list (`:1675-1678`), independent of bucket layout or
       seed. 6a's "hash quality only" characterization is re-verified, not
       assumed.

  - **Comment sweep (6b's finding), comment-only:** `tests/int64_width.ty:13-14`
    read `# 16099511627776` on both `total := big + prod + shifted` and its
    `println`. The true sum is 5000000000 + 10000000000 + 1099511627776 =
    **1114511627776**, which is what the tracked golden `tests/int64_width.out`
    line 4 already contains. Both comments corrected; fixture code and golden
    untouched.

  - **Gate summary lines (each its own foreground command, 64-bit lanes run as
    `env -u LD_PRELOAD` per the dev-shell `block-nnp.so` workaround):**
    ```
    make test      -> passed: 409   failed: 0 / all green
    make corelib   -> corelib: all green (tychoc and tychoc0 agree, match goldens)
    make rtparity  -> env knobs 3 shared, diagnostics 27 shared, arena-stats rows 5 shared,
                      0 allowlisted difference(s) each -- the two runtimes agree
    make conc      -> conc: passed 36   failed 0
    make fixpoint  -> ok B == C : tychoc0 reproduces itself byte-identically (34679 lines C)
                      ok split tychoc0 (2 packages) self-hosts E==F and matches the single-file compiler
                      fixpoint: all green
    make ilp32     -> passed: 409   failed: 0 / all green
    ```

  - **Left alone deliberately (RULE 7, fail closed) — the only two
    `unsigned long` spellings that survive a repo-wide grep of both compilers:**
    - `compiler/tychoc0.ty:9908` — `hbox(Arena* ar, unsigned long n, void* src)`
      feeds `n` straight to `amem`/`memcpy`. A byte count, libc-facing; a size,
      not a hash word. Explicitly out of scope.
    - `src/tychoc.c:3854` — `r = y >= 64 ? 0 : (long)((unsigned long)x << y);`
      is tychoc's OWN constant folder for `<<`, i.e. host arithmetic inside the
      compiler process, not emitted text. It is width-correct on every host the
      compiler is actually built for today (`make ilp32` cross-compiles the
      EMITTED programs with `-m32`; tychoc itself is always built 64-bit). It
      would fold shifts wrongly only if tychoc were itself compiled ILP32.
      **Appended below as Phase 8 rather than silently absorbed** (scope lock).
    - `src/tychoc.c:1184` / `compiler/tychoc0.ty:2090,4277,4458` and the
      `hi_udiv`/`hi_shl_u64` family (`tychoc0.ty:9857-9866,9881`) are the `u64`
      TYPE's `unsigned long long` lowering — correct as written, untouched.

- [x] **Phase 7 — spec + spec-plan: mark the reference impl conformant**
  - Scope: update `docs/spec/appendix-f-impl-defined.md` F.3 — reference compilers
    now realize `int` via fixed-width 64-bit `tycho_int`, conform on LP64, LLP64,
    ILP32 (strike "conforms on LP64 only"); add an `appendix-e-conformance.md` row
    citing the `ilp32` gate / new fixture; strike spec-plan #16's codegen
    follow-up and its §11/roadmap references. Docs only — no source.
  - Done when: F.3 records no non-conforming data model; the conformance row
    exists; #16's follow-up struck with commit citations.
  - Verify: `make spec-check`, `make check-links` — each its own command, paste
    each summary line; `git diff --stat` only `docs/`.
  - **DONE 2026-07-24.** Docs only — no source, no fixture, no Makefile touched.

    **1. `docs/spec/appendix-f-impl-defined.md` §F.3 — the note rewritten.** The
    struck text was `:66-76`: "The reference compilers (`tychoc`, `tychoc0`)
    currently lower `int` to C `long`… `long` is **32-bit on LLP64** (64-bit
    Windows) and **ILP32** — targets on which the reference codegen does **not**
    conform. Migrating the lowering … is a tracked follow-up". Replaced by:

    > **Reference-implementation note (not a spec allowance).** The required 64-bit
    > `int` width above is normative for *every* conforming implementation; it is
    > **not** implementation-defined. The reference compilers (`tychoc`, `tychoc0`)
    > realize `int` as a **fixed-width 64-bit** C type — `typedef int64_t tycho_int;`
    > in the emitted prelude, which is the single width authority for `int`, the
    > `int`-carried `char` representation, array/slice length headers, map keys and
    > the FFI crossing signatures — and emit `long long`-suffixed integer literals so
    > that constant arithmetic is evaluated at 64-bit rank. `int64_t` does not vary
    > with the C data model, so the lowering is 64-bit on **LP64**, **LLP64** and
    > **ILP32** alike, and a build in which it were not is rejected outright by the
    > always-on `_Static_assert(sizeof(tycho_int)==8, "tycho int must be 64 bits");`
    > carried in the same prelude. The reference implementation therefore conforms to
    > the 64-bit `int` requirement on all three data models; no target is excluded.
    >
    > *Extent of the evidence (so the claim is not read as broader than it is).* The
    > data-model independence above is a property of `int64_t` plus the static
    > assertion, and holds by construction. It is additionally **gated empirically on
    > ILP32**: `make ilp32` rebuilds the emitted C of the whole fixture suite with
    > `gcc -m32` and re-runs it against the unmodified 64-bit goldens on every CI run
    > (`scripts/ci.sh`), so a width regression fails the build (see
    > [Appendix E §5.2.1](appendix-e-conformance.md#5-types)). That lane runs an ILP32
    > data model on an x86-64 host; it is not a test on 64-bit Windows hardware, and
    > **LLP64 is asserted architecturally, not measured**. The ILP32 lane also
    > deliberately omits the sanitizer pass (no 32-bit ASan runtime under multilib);
    > ASan coverage comes from the 64-bit `make test` lane.

    Every claim in it was read out of the tree, not from the phase brief:
    `runtime/tycho_rt.c:52` `typedef int64_t tycho_int;` and `:54` the
    `_Static_assert`; the byte-identical twin emitted by
    `compiler/tychoc0.ty:9699`; `Makefile:204` `ilp32:`; `tests/int64_width.ty`
    tracked in-glob.

    **2. The honesty caveat, and why that one.** The second paragraph exists
    because the one-line version ("conforms on LP64, LLP64 and ILP32") is true but
    would be *read* as "all three were tested". They were not. The caveat splits
    the claim in two: what is **architecturally guaranteed** (`int64_t` is
    fixed-width by definition, and the `_Static_assert` turns any host where it
    were not into a build failure rather than a silent miscompile — so LLP64
    cannot be wrong without failing to build) versus what is **empirically
    gated** (`make ilp32`, whole suite, every CI run). It names the two limits
    explicitly: the ILP32 lane is `gcc -m32` on an x86-64 host, NOT real 64-bit
    Windows hardware, and it carries no ASan (64-bit `make test` does). This is a
    normative document making a conformance claim — RULE 5/10: state the
    mechanism, do not let the reader infer a stronger one.
    Not claimed anywhere: that `corelib/image` was compile-verified. It still is
    not (no libpng in this env, Phase 3), but it is an extended-tier corelib
    package, not part of the `int` width claim, and Appendix E already scopes
    `deps`-tier packages as extended-tier only (`appendix-e-conformance.md:201`).

    **3. `docs/spec/appendix-e-conformance.md` — one row added** under `### §5
    Types`, directly after the existing `§5.2.1` row, matching the file's
    `| Clause | Requirement (abbrev.) | Fixture(s) |` format:

    ```
    | §5.2.1 | `int` stays 64-bit under a non-LP64 C data model (no truncation of values, literal arithmetic or length headers) | `tests/int64_width`, the `make ilp32` lane (whole suite rebuilt `gcc -m32`, 64-bit goldens unchanged) |
    ```

    Citation checked against the gate that enforces it: `scripts/spec_check.sh:50`
    greps backticked ``tests/…`` spans and asserts each resolves as a file, dir or
    `.ty`; `tests/int64_width.ty` + `.out` are tracked, so it resolves.
    ``make ilp32`` is not matched by that regex (not a `tests/` path), so naming
    the lane cannot create a dangling citation.

    **4. `docs/internals/spec-plan.md` — punch-list #16's codegen half CLOSED.**
    Item #16 (`:336-341`) previously ended "…is noted as an impl limitation
    (Appendix F.3), with a fixed-width 64-bit codegen migration (`int64_t`/`long
    long`) tracked as a follow-up (not done in this pass)." That sentence is gone;
    #16 now records **CODEGEN FOLLOW-UP CLOSED (2026-07-24)** with the commit
    chain `1d79400` (prelude + runtime) → `c43d745` (corelib FFI shims) →
    `e5a7a4e` (both compilers emit `tycho_int`; `INT64_MIN` div-guard, 64-bit
    shift cast) → `38b04ba` (`unsigned long` narrowing in both runtimes, incl. the
    fail-open `tycho_cap_check`) → `04a6357` (`LL`-suffixed literals; also fixed a
    live tychoc0 LP64 miscompile of `100000*100000`) → `8c754bb` (hash
    accumulators → `uint64_t`), with `a09dbb6` adding the `make ilp32` gate
    (409/0, non-vacuous via `tests/int64_width`). The §11 residual-decisions entry
    (`:580-583`), which read "reference `long` lowering conforms on LP64 only", is
    struck the same way and points back at #16 for the commit list. Grep confirms
    those were the ONLY two references to #16 in the file, and the only remaining
    `LP64/LLP64/ILP32` mentions outside the archived audit/plan docs.
    `docs/spec/03-types.md:14` was deliberately left alone: it constrains *any* C
    backend ("a C backend MUST realize `int` as a 64-bit type even on a target
    where C `long` is 32 bits"), which this change makes *satisfied*, not false.

    **5. TRANSPARENT SCOPE DEVIATION — one line outside the three named files.**
    `make check-links` was **already RED at HEAD (`8c754bb`), before this phase
    touched anything** — verified by `git stash` → re-run → same single failure →
    `git stash pop`. The dead link is
    `docs/internals/plan-1.0-freeze-DONE.md:436`, `[§2](00-conventions.md)`, a
    quotation of `appendix-a-grammar.md:21` whose *relative* target stopped
    resolving when commit `3508a29` (this plan's own archival commit) moved the
    file from `docs/spec/` into `docs/internals/`. Fixed minimally —
    `(00-conventions.md)` → `(../spec/00-conventions.md)`, one line, still
    `docs/`-only — because this phase's Verify block names `check-links` and a
    gate that was red before the phase began cannot certify it. Nothing else in
    that archived file was touched.

    **Gate summary lines (each its own foreground command):**
    ```
    env -u LD_PRELOAD make spec-check  -> spec-check: Appendix A grammar matches §3/§4 (ok)
                                          spec-check: all Appendix E fixture citations resolve (ok)
                                          spec-examples: 7 runnable example(s), all pass
    env -u LD_PRELOAD make check-links -> link check: ok (118 markdown files, no dead relative links)
    ```
    `git diff --stat` before commit: `docs/internals/plan-1.0-freeze-DONE.md`,
    `docs/internals/spec-plan.md`, `docs/spec/appendix-e-conformance.md`,
    `docs/spec/appendix-f-impl-defined.md`, `plan.md` — no source, no fixture, no
    Makefile.

  - **NOTE for whoever runs next (not fixed here, scope lock):** Phase 6's
    checkbox above is still `- [ ]`. It halted deliberately on a RED `make ilp32`
    and its own text says "Re-run this phase's Verify block after 6a+6b"; 6a/6b/6c
    have since landed and `make ilp32` is `passed: 409 failed: 0`. Ticking it is a
    bookkeeping action belonging to whoever re-runs that Verify block, not to this
    docs phase.

- [x] **Phase 8 — HOST portability of tychoc's own constant folder (discovered by 6c, scope-locked out of it)**
  - Raised by Phase 6c on 2026-07-24 while sweeping emitted `unsigned long`.
    Everything 6a/6b/6c fixed concerns the code the compilers EMIT. This is the
    one remaining `unsigned long` in the compilers' own arithmetic:
    - `src/tychoc.c:3854` — `r = y >= 64 ? 0 : (long)((unsigned long)x << y);`
      inside tychoc's compile-time `<<` folder. On an ILP32 HOST `unsigned long`
      is 32 bits, so folding `1 << 40` would yield 0 instead of 1099511627776 —
      the compiler would miscompile a constant expression that the emitted code
      (now `int64_t` throughout) handles correctly.
  - **Severity: latent, zero impact today.** `make ilp32` cross-compiles the
    EMITTED programs with `-m32`; tychoc itself is always built 64-bit, where
    `unsigned long` is 64 bits and the fold is exact. This bites only if someone
    builds the compiler for a 32-bit host. Deliberately not folded into 6c: 6c's
    scope was emitted text, and a host-arithmetic change cannot be validated by
    the `-m32` emitted-code gate that justified 6c.
  - Fix: retype the fold to `(int64_t)((uint64_t)x << y)` (and audit the
    neighbouring fold arms for the same `long`-width assumption). Check whether
    `compiler/tychoc0.ty` has a matching fold that needs the same treatment.
  - Done when: tychoc's constant folder makes no `sizeof(long)` assumption.
  - Verify: `make test`, `make fixpoint`, `make ilp32`; ideally add a fixture
    folding a >2^31 shift at compile time and confirm it is exact.
  - **DONE (2026-07-24) — and the ILP32-host bug was REPRODUCED, not just argued.**
    - **The defect was WIDER than the one line this phase named.** 6c reported the
      `unsigned long` cast at (then) `src/tychoc.c:3854`. Reading the folder end to
      end showed the cast is only the last link: the compile-time integer *value*
      travels `lexer accumulator -> Tok.ival -> Expr.ival -> fold locals -> emitted
      literal`, and **every one of those was C `long`**. Fixing only the cast would
      have been measurably useless — `n->ival = r` would truncate straight back on
      an ILP32 host. All five links were retyped to fixed-width `int64_t`:

      | site (post-edit line) | old | new |
      |---|---|---|
      | `src/tychoc.c:23-24` | *(no `<stdint.h>`/`<inttypes.h>`)* | both `#include`d |
      | `src/tychoc.c:130` `Tok.ival` | `long    ival;` | `int64_t ival;` |
      | `src/tychoc.c:294,299` lexer literal accumulator | `long v = 0;` / `v > (LONG_MAX - d) / 10` | `int64_t v = 0;` / `v > (INT64_MAX - d) / 10` |
      | `src/tychoc.c:1307` `Expr.ival` | `long     ival;` | `int64_t  ival;` |
      | `src/tychoc.c:3851` fold locals (**all arms**, not just `<<`) | `long x = a->ival, y = b->ival, r;` | `int64_t x = a->ival, y = b->ival, r;` |
      | `src/tychoc.c:3862` the `<<` arm | `r = y >= 64 ? 0 : (long)((unsigned long)x << y);` | `r = y >= 64 ? 0 : (int64_t)((uint64_t)x << y);` |

      Retyping the locals is what makes the **neighbouring arms** (`+ - * / % & \| ^ >>`)
      width-safe too; the `<<` arm additionally needed its explicit cast pair fixed.
    - **Format-string fallout, found by compiling `-m32` with `-Wall -Wextra`.**
      `int64_t` is `long` on LP64 but `long long` on ILP32, so every `%ld` consuming
      `ival` becomes UB there. `-Wformat` caught 2 (`die_at`, lines 4508/4510); the
      other **7 are invisible to the compiler** because `sfmt` is an unchecked
      vararg helper — they were found by grep, not by the build, and they are the
      sites that bake the literal into the emitted C (`8354, 8355, 8359, 8360,
      8372, 8413, 8815`). All 9 now use `%lld` + an explicit `(long long)` cast,
      which is correct on **every** data model. On LP64 the printed digits are
      byte-identical, which is why no golden moved.
    - **`compiler/tychoc0.ty` needs NO change — verified by reading it, not assumed.**
      `fold_ii` (`compiler/tychoc0.ty:2975-3005`) does its arithmetic in Tycho `int`,
      which *is* `int64_t` by construction, and stores results as **strings**
      (`return EInt(str(x << y), line)` at `:3000`) — there is no `long` anywhere in
      the path. Its `<<` lowers to the runtime helper `hi_shl_i`, emitted at
      `compiler/tychoc0.ty:9859` as `(tycho_int)((uint64_t)x << n)` — already the
      exact shape Phase 8 gives tychoc. tychoc0 is host-width-immune for free.
    - **ILP32-HOST BUILD: ATTEMPTED AND IT WORKED — this is measured, not argued.**
      `CC` flows straight through (`Makefile:7 CC ?= cc`, `Makefile:31-32
      $(CC) $(CFLAGS) -Ibuild src/tychoc.c -o tychoc`), so tychoc itself builds
      32-bit with `gcc -m32 -O2 -fwrapv -Wall -Wextra -std=c11 -Ibuild src/tychoc.c`.
      It links (tychoc needs only libc) and both hosts now emit **zero** new
      warnings — the 3 residual `missing initializer for field 'is_sink'` warnings
      are pre-existing and byte-identical on 64-bit and 32-bit.
      Control experiment on `const BIG = 1 << 40`, reading the emitted C:

      | tychoc host | before the fix | after the fix |
      |---|---|---|
      | LP64 (`cc`) | `tycho_int_to_str(&_t, 1099511627776LL)` | `tycho_int_to_str(&_t, 1099511627776LL)` |
      | **ILP32 (`gcc -m32`)** | **`tycho_int_to_str(&_t, 256LL)`** ← miscompile | `tycho_int_to_str(&_t, 1099511627776LL)` |

      `256` is `1 << (40 mod 32)` — the shift count wrapping against a 32-bit
      `unsigned long`, exactly the failure 6c predicted. **This is genuine
      ILP32-host validation: the bug was reproduced on a 32-bit-hosted compiler and
      the fix removed it.** It confirms the phase's premise too — LP64 output is
      unchanged in both columns, so no LP64 gate could ever have caught this.
    - **Fixture `tests/const_fold_width.ty` + golden `tests/const_fold_width.out`.**
      Six top-level `const`s, all > 2^31, exercising `<<` (x2), `*`, `+`, `-` and
      `>>`. A top-level `const` MUST fold to a single literal or tychoc rejects it
      (`parse_const` -> `const_fold` -> `is_literal_expr`), so the folder is
      guaranteed to be on the path.
    - **The fixture is NON-VACUOUS — checked, not assumed.** Emitted C contains all
      six values as folded literals (`1099511627776LL`, `4611686018427387904LL`,
      `10000000000LL`, `5000000000LL`, `1099511627775LL`, `4398046511104LL`) and
      **`grep -c 'hi_shl_i(' == 0`** — zero runtime shift helpers, so the constants
      really are computed by the compiler's folder, not deferred to run time. The
      `-m32`-hosted tychoc emits all six identically (6/6 match).
    - **Gates (one sweep, each run once, `env -u LD_PRELOAD` on the 64-bit lanes):**
      - `make test` → `passed: 410   failed: 0` / `all green` (409 -> 410: the new fixture)
      - `make corelib` → `corelib: all green (tychoc and tychoc0 agree, match goldens)`
      - `make rtparity` → `rtparity: the two runtimes agree on env knobs, diagnostics and arena stats` (27 diagnostics / 5 arena-stats rows, 0 differences)
      - `make conc` → `conc: passed 36   failed 0`
      - `make fixpoint` → `ok   B == C : tychoc0 reproduces itself byte-identically (34679 lines C)` / `fixpoint: all green (self-hosting; B==C; single files + packages; tychoc0 self-split dogfood)`
      - `make ilp32` → `passed: 410   failed: 0` / `all green`
    - **No golden was re-recorded.** `git status --porcelain` after the sweep:
      `M src/tychoc.c`, `?? tests/const_fold_width.ty`, `?? tests/const_fold_width.out`
      — zero modified `.out` files. The `%ld`->`%lld` sweep is provably output-neutral
      on LP64, and `fixpoint`'s byte-identical B==C is the strongest witness.
    - **Assumption record (RULE 13).** Verified: the value path is width-fixed
      end to end on both hosts, by reproduction. Not verified: tychoc has never been
      *run* as a 32-bit binary across the whole suite — `make ilp32` still
      cross-compiles the EMITTED programs with a 64-bit-hosted tychoc, and the
      32-bit-hosted tychoc was exercised only on the two fold fixtures above. Risk
      if wrong: some *other* host-width assumption outside the constant folder
      (e.g. `long` used for sizes/offsets — `arrc_sized` `:658`, `fixarr_size` `:668`,
      `sizeparam_enc` `:707` all still take `long`) could bite a 32-bit-hosted build. Those
      are size/index quantities, not tycho `int` values, so they are not part of
      this phase's defect; logged as Phase 9 rather than silently absorbed (RULE 6
      scope lock).

- [ ] **Phase 9 — (discovered by Phase 8, NOT fixed there) sweep the remaining host `long`s outside the value path**
  - Phase 8 made the compile-time *integer value* path width-fixed and proved it on
    a `-m32`-hosted tychoc. It deliberately did NOT touch the other host `long`s,
    which carry **sizes/indices**, not tycho `int` values: `ArrType.size`
    (`src/tychoc.c:640`), `arrc_sized_b`/`arrc_sized`/`fixarr_of`/`bounded_of`
    (`:645,658,660,661`), `bounded_cap`/`fixarr_size` (`:663,668`), `sizeparam_enc`/
    `sizeparam_id`/`g_sizebinds` (`:707,715,718`), `GInst.spvals` (`:4024`).
  - Why it was left: on an ILP32 host these are 32-bit, which is ample for an array
    capacity or a tuple arity, so there is no known miscompile — unlike the value
    path, where a real one was reproduced. Changing them is a wide, low-yield diff
    with its own `%ld` fallout, and Phase 8's scope was the constant folder.
  - Done when: either they are retyped to a fixed width, or a comment at
    `src/tychoc.c:640` records the deliberate decision that sizes stay host-`long`
    and why that is safe.
  - Verify: `make test`, `make fixpoint`, `make ilp32` green; plus a `gcc -m32`
    -hosted tychoc build with `-Wall -Wextra` showing no new warnings.

- [ ] **Phase 10 — CORRECTIVE: tychoc0 rejects unary `-` applied directly to an index expression**
  - Discovered 2026-07-24 by the new numeric-boundary fixtures (the hardening
    pass that followed Phase 8), NOT by the int64 migration itself. It is a
    pre-existing tychoc/tychoc0 divergence; nothing in the 408-fixture suite
    negated a subscript directly.
  - **The divergence:** `tychoc` accepts, `tychoc0` rejects a VALID program.
    ```
    line 33: unary - needs an int or a float
            println(str(-v[0]))
                        ^
    ```
    Minimal characterization (`v := [7,8]` array, `m := []int: int` map):
    | form | tychoc | tychoc0 |
    |---|---|---|
    | `-v[0]` (array index) | OK | **REJECT** |
    | `-m[0]` (map index) | OK | **REJECT** |
    | `-(v[0])` (parenthesized) | OK | OK |
    | `x := v[0]` then `-x` | OK | OK |
    | `0 - v[0]` (binary minus) | OK | OK |
    | `-len(v)` (call) | OK | OK |
    | `-7` (literal) | OK | OK |
    So the gap is specifically **unary minus whose operand is a subscript**;
    every other operand shape types correctly. Parenthesizing is a workaround —
    do NOT use it to make the fixture pass; the compiler is what is wrong.
  - **Severity: fail-CLOSED, not a miscompile.** tychoc0 refuses to compile a
    valid program rather than compiling it wrongly, so no output is corrupted.
    But it is a genuine two-compiler divergence, which is the exact property the
    project's harness exists to prevent, and it makes `make fixpoint` red the
    moment any fixture negates a subscript (`FAIL boundary_i2s.ty (B differs
    from the C compiler)`).
  - Fix: in `compiler/tychoc0.ty`, find the unary-minus type check (the site
    emitting "unary - needs an int or a float") and teach it the index-expression
    operand shape, matching how `src/tychoc.c` types it. Read tychoc's unary
    handling first and mirror it.
  - **Also owned by this phase: un-park the fixture.** `tests/pending/boundary_i2s.ty`
    + `.out` are committed OUTSIDE every `run.sh` glob (`tests/*.ty`,
    `tests/{pkg,reject,abort,diag,warn}/`) so the tree stays green while the bug
    is open — the same parking pattern Phase 6/6b used. Once tychoc0 accepts the
    form, `git mv` both files up into `tests/` and delete `tests/pending/`.
    Until then the int→string boundary class (the Phase 6a bug class) is NOT
    covered by the suite.
  - Done when: both compilers accept `-v[0]`/`-m[0]`; `boundary_i2s` is back in
    `tests/` and passes under both compilers and under `make ilp32`; all gates green.
  - Verify: `make test`, `make corelib`, `make conc`, `make fixpoint`,
    `make ilp32`, `make spec-check` — each its own command, paste summary lines.
