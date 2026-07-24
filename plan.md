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

- [ ] **Phase 6a — CORRECTIVE (escalated from Phase 6): runtime int→string truncates on ILP32**
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

- [ ] **Phase 6b — CORRECTIVE (escalated from Phase 6): emitted int literals are 32-bit — tychoc0 MISCOMPILES on LP64 TODAY**
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
