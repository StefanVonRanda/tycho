# `int` → `tycho_int` (int64_t) codegen migration — `long`-site audit

Phase 1 of `plan.md`. **Docs/probe only — no source changed.** This is the
classified inventory the later phases edit against. Every line number below was
re-verified against the working tree (RULE 3); where `plan.md`'s grep snapshot
had drifted, the corrected line is given.

Legend:
- **INT-SEMANTIC** — a `long` carrying a Tycho `int`/`char`/`bool` value, an
  array/slice/string **length or capacity header**, a map key rep, a loop/range
  bound over such, or an FFI crossing width for a Tycho int. → becomes
  `tycho_int` in Phases 2–4.
- **NON-INT** — a genuine C `long long`/`unsigned long long` realizing the
  `i64`/`u64` types, a hash/mask `unsigned long`, or a libc-facing width. Stays
  C `long`/`long long`. **Do not touch.**
- **AMBIGUOUS / REVIEW** — flagged for human decision; reasoning given. (RULE 7:
  fail closed — a flagged site is fine, a silently-misclassified one is a latent
  off-LP64 ABI bug.)

---

## 0. The prelude — there is **NO single shared file**. TWO parity-locked runtimes.

`plan.md` assumed "one shared emitted-C prelude." **That is false.** The two
compilers carry two independently hand-maintained runtime/prelude texts, kept
byte-equivalent by `make rtparity` (Makefile:212 — "tycho ships TWO
hand-maintained runtimes"). The typedef + `_Static_assert` + `<stdint.h>`/
`<inttypes.h>` includes must be added to **BOTH**, and Phase 2/rtparity keeps
them in sync.

| Compiler | Prelude location (emitted into every program) | Evidence |
|---|---|---|
| `tychoc` (C) | **`runtime/tycho_rt.c`**, embedded verbatim | `src/tychoc.c:26` `#include "tycho_rt_embed.h"  /* defines: static const char *TYCHO_RUNTIME */`; `src/tychoc.c:10126` `fputs(TYCHO_RUNTIME, o);` writes it as the first thing in the output `.c`. The embed is generated from `runtime/tycho_rt.c` at build (`Makefile:13-14` `EMBED := build/tycho_rt_embed.h`, `RUNTIME := runtime/tycho_rt.c`). |
| `tychoc0` (self-host `.ty`) | **`compiler/tychoc0.ty` `fn preamble()`** + the inline runtime string-literals that follow it | `compiler/tychoc0.ty:9697` `fn preamble() -> string:`; `:9698` emits the `#include` block; the runtime bodies are concatenated as string literals through ~`:10400`. |

**Current includes (neither has `<stdint.h>`/`<inttypes.h>` yet — Phase 2 adds them):**
- `runtime/tycho_rt.c:37-40`: `<stdio.h> <stdlib.h> <string.h> <stddef.h>` (+ more below).
- `tychoc0.ty:9698`: `<stdio.h> <stdlib.h> <string.h> <stddef.h> <dirent.h> <pthread.h> <unistd.h> <stdatomic.h> <sched.h> <time.h>`.

**Where the `typedef int64_t tycho_int;` + `#define TY_PRId PRId64` +
`_Static_assert(sizeof(tycho_int)==8,...)` go (Phase 2):** the top of
`runtime/tycho_rt.c` (right after its include block, ~`:40`) AND the equivalent
spot in `tychoc0.ty preamble()` (`:9698`). They MUST stay textually parity-equal
or `make rtparity` fails.

---

## 1. `src/tychoc.c` — 204 lines carry `long` (242 `long` tokens)

NON-INT token sources: `unsigned long long`=3, standalone `long long`=4,
`unsigned long`(hash/mask)=26. Everything else is INT-SEMANTIC.

### INT-SEMANTIC (→ `tycho_int`)
- **Emitted int/char/bool type** — the core: `:1181-1182` region; concretely the
  emitted C base type for `int` is `long` (see `c_type`); char literal carried as
  `%ldL` at `:8349` (`case E_CHAR: return sfmt("%ldL", e->ival)`).
- **`[int]` array element type**: `:3391` `if (t == T_ARRAY_INT) return "const long *";`.
- **Length header read** (`((const long*)p)[-1]`): `:8969`, `:9194`.
- **Emitted len/cap locals**: `:7351` `long _fl%d ... _fc%d` (len/cap); `:8462`
  `long _gi%d` (Soa bound index).
- **Array-return / out-param sigs**: `:8243`, `:8265` (`T_ARRAY_INT ? "long"`),
  `:9775` `unsigned char **, long *`, `:9776` array-int out `long`.
- **Map key rep**: `:10096` `if (mapkey_intrep(k)) return "long ";`, `:10101`
  `return sfmt("long k")`.
- **Emitted composite struct/array/map field types & helpers**: `:10214`
  (`long len; long cap;`), `:10217` (map `long len; ecount; ecap; icap`), `:10228-10229`
  (Soa `long len; cap`, `Soa%d_bound(...,long i)`), `:10356/:10359/:10526/:10533/:10548/:10549`
  (arr `with_cap`/`grow` `long cap/len/nc`), `:10609/:10612/:10619/:10624/:10630-10640`
  (mapc `with_cap`/`find`/`idxput`/rehash/`append` — `long cap/i/ei/ec/ic/e`).
- **Host-side capacity/size fields** (compiler's own state describing Tycho array
  sizes — see AMBIGUOUS note): `:631/:640/:656/:658` (`bounded` cap: `long len/size/n`),
  `:1682` (`long cap`), `:5667` (`long cap = bounded_cap`), `:10457` (`long n = ...size`).

### NON-INT (stay)
- **`i64` type → `long long`**: `:1191` `case T_I64: return "long long ";`; FFI i64
  `:3409` `if (!strcmp(n,"i64")) return "long long ";`.
- **`u64` type → `unsigned long long`**: `:1184` `case T_U64: return "unsigned long long ";`.
- **Hash functions / FNV / SipHash return + accumulators** (`unsigned long`):
  `:10304`, `:10308`, `:10343`, `:10353`, `:10367`, `:10429-10430`, `:10442-10443`,
  `:10486-10487`. **Index mask** (`unsigned long mask`): `:10620`, `:10625`.

### AMBIGUOUS / REVIEW
- **`:3854`** `r = y >= 64 ? 0 : (long)((unsigned long)x << y);` — this is the
  compiler's **host-side** constant folder evaluating a Tycho-`int` shift at
  compile time. It is *not emitted*; it runs in the compiler process. On an LP64
  build host `long`==64-bit and folds correctly; only a compiler *built* ILP32
  would misfold. Same class: **`:713`** `static long *g_sizebinds` and **`:6691`**
  `long *saved_sb` (host-side size-param bindings). **Recommendation:** migrate
  these to `tycho_int` too for host-portability *and* to keep fold results
  identical to emitted arithmetic — but this is a host concern, not an emitted-ABI
  one, so it does not affect the `-m32` emitted gate. Phase 4 note: include, but
  they are not what the ILP32 gate proves.

---

## 2. `compiler/tychoc0.ty` — 175 lines carry `long` (411 `long` tokens)

This file is bigger because it holds tychoc0's **entire inline runtime** as C
string literals (the tychoc equivalent lives in the separate
`runtime/tycho_rt.c`). NON-INT token sources: standalone `long long`≈17,
`unsigned long long`=17, `unsigned long`(hash/mask)=50.

### INT-SEMANTIC (→ `tycho_int`) — emitted type + runtime
- **Emitted int/char/bool type**: `:4450`, `:4452`, `:4454` (all `return "long "`).
- **Length header read** (`((const long*)…)[-1]`): `:6444`, `:6622`, `:6624`,
  `:8337`, `:8421`, `:8511`, `:8571`, and pervasively in the string runtime
  `:9843-9894` (`hs`, `hi_intern`, `sc`/`sc3..sc6`, `scopy`, `str_cmp`,
  `s_from_c`, `substr`, `hi_find`, `hi_sidx`, `hi_puts`, `hi_append`, `bi_from_ints`).
- **Header write** (`*(long*)base = n`, `((long*)(*s))[-1] = *len`): `:9843`,
  `:9844`, `:9889`, `:9890`.
- **Slice bounds guard**: `:5329` (`long _lo/_hi`, prints `[%ld:%ld] len %ld`).
- **Array-return / FFI-crossing sigs**: `:6645`/`:6649` (`ep="long"`, `long _aol`),
  `:8898` `"const long*, long"`, `:8917` `"long**, long*"`, `:8878` comment (bytes
  crosses as `(const unsigned char*, long)`).
- **Emitted len/cap locals**: `:8663`, `:8686` (`long _fl/_fc`).
- **Emitted `for (long i…)` loops over `.len`**: `:9397`, `:9586`, `:9587`.
- **Runtime int/str/bool/char helpers**: `:9849` `hi_bchk(long i,long n)`, `:9866`
  `hi_f2i`→`long`, `:9872` `hi_cap_check(long n,…)`, `:9878` `sc_char(…,long c)`,
  `:9879` `i2s(Arena*,long n)`, `:9881` `b2s(long b)`, `:9887` `bi_from_ints(const long* p,long n)`,
  `:9892` `substr(…,long a,long b)`, `:9893` `hi_find`→`long`.
- **`ikhash`/map-find key + index** (the `long`, *not* the `unsigned long` mask):
  `:10158` `long n = ((const long*)s)[-1]` (mhash length), `:10159`
  `ikhash(long k)` (int map key), `:10378` `_find(… long i …)`, `:10379`
  `_ixput(…,long ei)`.

### NON-INT (stay)
- **`i64`/`u64` type maps**: `:2090`/`:4458` `unsigned long long ` (u64);
  `:2098`/`:4472`/`:4273`/`:4277` `long long` (i64).
- **Generic/sized shift + div helpers** (`long long`/`unsigned long long` for the
  shift *count* and u64/i64 operands): `:9856-9865` (`hi_udiv`, `hi_umod`,
  `hi_shl_u32/u64`, `hi_shr_*`, `hi_shln`, `hi_shrn`). NOTE the **mixed** lines
  `:9858 hi_shl_i(long x, long long n)` / `:9859 hi_shr_i` — the `long x` operand
  and `long` return are **INT-SEMANTIC** (the Tycho int being shifted) while the
  `long long n` **count stays**. Per-token edit required (RULE 7).
- **Hash machinery** (`unsigned long`): `:9386`, `:9397`(accumulator; the
  `for(long i…)` loop var is INT-SEMANTIC), `:9709-9712`, `:10157`(siphash13),
  `:10277`, `:10378-10379`(mask only), `:16025`, `:16030`, `:16035`.
- **`u2s(unsigned long long u)`**: `:9880` (u64→str).
- **`hbox(Arena*, unsigned long n, void*)`**: `:9907` — `n` is a raw byte count
  for `memcpy` (allocation size), not a Tycho int → stays. (Borderline; classified
  NON-INT because it is a `size`-role, never printed as an int.)

### AMBIGUOUS / REVIEW
- **Channel internals** `:9814-9828`: `_Atomic long seq`, `long pos`, `HChan{ long cap …
  _Atomic long enq/deq }`, `long i/c2/d` in `tycho_chan_new/send_cell/recv_cell/
  try_recv/free`. `cap` is a user-facing Tycho `int` (chan capacity) → **INT-SEMANTIC**;
  the monotonic ring positions (`pos`,`enq`,`deq`,`seq`) are internal counters. On
  ILP32 a 32-bit `pos` wraps far sooner and the `& (cap-1)` masking assumes the
  same width as `cap`. **Recommendation:** migrate the whole channel struct to
  `tycho_int` (incl. the `_Atomic long`→`_Atomic tycho_int`) so `cap` and the
  positions share one width. Flagged because it is internal, not directly an int
  value the user sees. This is a `runtime/tycho_rt.c` mirror too (see §3).

---

## 3. `runtime/tycho_rt.c` — 384 lines carry `long` (559 `long` tokens)

Mirror of tychoc0's inline runtime; whatever §2 does here, `rtparity` requires the
same in tychoc0 (§2) and vice-versa. NON-INT token sources: standalone
`long long`≈14, `unsigned long long`=13, `unsigned long`(hash/mask)=66.

### INT-SEMANTIC (→ `tycho_int`)
- **Length header everywhere** (`((const long*)…)[-1]`, `*(long*)base = n`,
  `#define TYCHO_SLEN`): `:854`, `:863`, `:870`, `:881`, `:916`, `:930`, `:936`,
  `:959`, `:967`, `:1003`, `:1010`, `:1018` (`tycho_str_len`), `:1025-1033`
  (`tycho_str_get(const char*, long i)`), and throughout the string block.
- **String/array/bytes len+idx helpers**: `:190` `tycho_cap_check(long n,size_t)`,
  `:854` `tycho_str_alloc(Arena*,long n)`, `:988/:1003` `tycho_bytes_from_c(…,long n)`.
- **Shift operand** (INT-SEMANTIC operand, count stays `long long`): `:129`
  `tycho_shl_i(long x, long long n)` → `:132 (long)((unsigned long)x << n)`; `:134`
  `tycho_shr_i(long x, long long n)`. → `long x`/return become `tycho_int`.
- **Loop bounds over `.len`/`.cap`**: `:669`, `:831`, and the many `for (long i…)`.
- **Map key `long k` + `_find`/`ixput` index `long i`** (mask stays unsigned): the
  map block around `:1750-2149` — the `long i` indices are INT-SEMANTIC, the
  `unsigned long mask` are NON-INT.

### NON-INT (stay)
- **`i64`/`u64` div/shift**: `:114-118` (`tycho_udiv`/`umod`, `unsigned long long`),
  `:139-173` (`tycho_shl_u32/u64`, `tycho_shr_*`, `tycho_shln`, `tycho_shrn` —
  `long long`/`unsigned long long`).
- **`tycho_uint_to_str(Arena*, unsigned long long u)`**: `:1208`.
- **Hash / SipHash / masks** (`unsigned long`): `:1683-1685` (`hash_k0/k1/ik_seed`),
  `:1688`, `:1695`, `:1709-1731` (`tycho_siphash13`, `tycho_si_hash`), `:2021-2022`
  (`tycho_ik_hash`), `:2030-2041` (`tycho_arr_*_hash`), and every `unsigned long
  mask` (`:1750`, `:1759`, `:1818`, `:1893`, `:1902`, `:1961`, `:2080`, `:2089`, `:2149`).
- **`tycho_cap_check` internal range cast** `:191` `(unsigned long)n > (size_t)-1/elem`
  — the *cast* stays; the `long n` **param** is INT-SEMANTIC.
- **`tycho_int_to_str` internal** `:1198` `unsigned long u = n<0 ? -(unsigned long)n …`
  — magnitude cast stays; the input value is a Tycho int.

### AMBIGUOUS / REVIEW
- **Channel internals** `:656-831` (`tycho_chan_new(long cap)`, ring `long pos/i/d`,
  `_Atomic long`): same call as §2 tychoc0 `:9814-9828`. Migrate as a unit; keep
  the two runtimes parity-equal.
- **`:845`** `long n = sysconf(_SC_NPROCESSORS_ONLN);` — `sysconf` returns C `long`
  (libc contract). This is a **libc return width**, not a Tycho int. **Keep C
  `long`** (or `int`), do NOT make it `tycho_int` — flagged so Phase 2 does not
  blanket-replace it. NON-INT-leaning; documented here for the reviewer.

---

## 4. `corelib/**/*.c` — FFI shims

All corelib shims are hand-written C at the FFI boundary. A `long` that appears in
a function **signature facing Tycho** (params/returns the compiler passes Tycho
`int` into/out of) is **INT-SEMANTIC** and must match what Phase 4 emits — those
migrate in **Phase 3**. Internal `(int)fd` casts to libc stay.

| File | `long` lines | Classification |
|---|---|---|
| `corelib/net/net_shim.c` | 27 | **INT-SEMANTIC** — `long port/fd/len/max/off/n` are all Tycho-facing ABI (`netx_listen(const char*, long port)` `:81`, `netx_accept(long fd)` `:96`, `netx_connect` `:103`, `netx_port_of(long)` `:117`, `netx_write(long,…,long len)` `:127`, `netx_read(long,long max,…,long*outlen)` `:142`, udp variants `:163-195`, `make_sock`→`long` `:67-71`, `resolve4(…,long port,…)` `:48`). The `(int)fd`/`(int)st` casts to the socket API stay. → **Phase 3.** |
| `corelib/tls/tls_shim.c` | 5 | **INT-SEMANTIC** — `tcp_connect(const char*, long port)` `:26`, `tlsx_connect(…,long port)` `:47`, `tlsx_write(void*,…,long len)` `:72`, `tlsx_read(void*,long max,…,long*outlen)` `:85`. → **Phase 3.** |
| `corelib/regex/regex_shim.c` | 12 | INT-SEMANTIC (Tycho-facing lengths/indices) — verify each sig in Phase 3. |
| `corelib/image/image_shim.c` | 12 | INT-SEMANTIC — width/height/len crossings. Phase 3. |
| `corelib/crypto/crypto_shim.c` | 8 | INT-SEMANTIC — bytes-length crossings. Phase 3. |
| `corelib/os/os_shim.c` | 5 | INT-SEMANTIC — int returns/args. Phase 3 (watch for libc `long` returns). |
| `corelib/datetime/datetime_shim.c` | 5 | **REVIEW** — some may be libc `time_t`/`long` (e.g. epoch seconds). `time_t` is its own type; a raw libc `long` return stays. Phase 3 must read each sig. |
| `corelib/compress/compress_shim.c` | 5 | INT-SEMANTIC (lengths). Phase 3. |
| `corelib/http/http_shim.c` | 2 | INT-SEMANTIC. Phase 3. |
| `corelib/io/io_shim.c` | 2 | INT-SEMANTIC. Phase 3. |
| `corelib/tls/tls_shim.c` | (above) | — |

No corelib shim contains `long long`/`unsigned long`/`unsigned long long` (grep
returned none), so there are **no NON-INT `long` sites in corelib** to protect —
every `long` there is a Tycho-facing width EXCEPT any raw libc return
(datetime/os — flagged). Phase 3 reads each signature before editing (RULE 7).

---

## 5. `%ld` / `%lu` / `%lx` printf specifiers

| File | count | Classification |
|---|---|---|
| `src/tychoc.c` | 55 | Mostly **emitted** format strings for Tycho int/len values → `%" TY_PRId "` (INT-SEMANTIC). A minority are host-side diagnostics printing `unsigned long` hashes/sizes (NON-INT) — Phase 4 reads each. Representative emitted int/len: `:8349` `%ldL`, plus the emitted error strings in the composite-helper block `:10609-10640`. |
| `compiler/tychoc0.ty` | 25 | Emitted Tycho int/len diagnostics → `%" TY_PRId "`: slice `:5329` `[%ld:%ld] len %ld`, `hs` length `:9843`, `hi_sidx` `:9845` `index %ld len %ld`, `hi_bchk` `:9849`, `hi_cap_check` `:9872`, `i2s` `:9879`, `substr`/`hi_find` region. All INT-SEMANTIC. |
| `runtime/tycho_rt.c` | 26 | Mirror of the above — emitted alongside the runtime; the ones printing header lengths/indices/int values → `%" TY_PRId "`. The `%lu`/hash-printing ones (if any) stay. Parity-locked with tychoc0. |
| `corelib/net/net_shim.c` | 1 | Read in Phase 3 (likely an error diag on a Tycho int). |
| `corelib/tls/tls_shim.c` | 1 | Read in Phase 3. |

**Rule for Phase 4:** a `%ld` whose argument is a Tycho int/`char`/`bool`/length/
index → `%" TY_PRId "`. A `%lu`/`%lx` whose argument is an `unsigned long` hash or
mask → **leave as `%lu`/`%lx`** (its argument stays `unsigned long`). Match each
specifier to its argument, do not blanket-replace.

---

## 6. Per-file totals & split (summary)

| File | lines w/ `long` | `long` tokens | NON-INT sites (protect) | INT-SEMANTIC (migrate) | `%l*` |
|---|---|---|---|---|---|
| `src/tychoc.c` | 204 | 242 | i64/u64 type maps (`:1184/:1191/:3409`) + ~33 hash/mask `unsigned long` (`:10304-10487`, masks `:10620/:10625`) | emitted int/char/bool, `[int]*` `:3391`, length headers `:8969/:9194`, map keys `:10096`, all emitted composite helpers | 55 |
| `compiler/tychoc0.ty` | 175 | 411 | i64/u64 maps (`:2090/:2098/:4458/:4472/:4273/:4277`), shift-count `long long` (`:9856-9865`), hash/mask `unsigned long` (~50, `:9386-10379`), `u2s`/`hbox` | emitted type `:4450-4454`, length headers, string runtime `:9843-9894`, FFI `:8898/:8917`, slice `:5329` | 25 |
| `runtime/tycho_rt.c` | 384 | 559 | i64/u64 div+shift (`:114-173`), `uint_to_str` `:1208`, hash/SipHash/masks (~66, `:1683-2149`), `sysconf` `:845` | length headers + str/idx helpers, shift operand `:129/:134`, map indices, cap checks | 26 |
| `corelib/net/net_shim.c` | 27 | 47 | none (watch libc casts only) | all Tycho-facing ABI | 1 |
| `corelib/tls/tls_shim.c` | 5 | 7 | none | all Tycho-facing ABI | 1 |
| `corelib/regex_shim.c` | 12 | 16 | none | Tycho-facing (verify) | 0 |
| `corelib/image_shim.c` | 12 | 14 | none | Tycho-facing | 0 |
| `corelib/crypto_shim.c` | 8 | 10 | none | Tycho-facing | 0 |
| `corelib/os_shim.c` | 5 | 6 | REVIEW libc returns | Tycho-facing | 0 |
| `corelib/datetime_shim.c` | 5 | 7 | REVIEW `time_t`/libc `long` | Tycho-facing | 0 |
| `corelib/compress_shim.c` | 5 | 7 | none | Tycho-facing | 0 |
| `corelib/http_shim.c` | 2 | 2 | none | Tycho-facing | 0 |
| `corelib/io_shim.c` | 2 | 2 | none | Tycho-facing | 0 |

**Headline:** the migration is dominated by INT-SEMANTIC sites. The protected
NON-INT surface is well-bounded: the `i64`/`u64` type maps (≈6 sites/compiler),
the hash/mask `unsigned long` families (~33 in tychoc.c, ~50 in tychoc0.ty, ~66 in
tycho_rt.c), the shift-*count* `long long`, and two libc-return `long`s
(`sysconf` `tycho_rt.c:845`, datetime/os epoch returns). Everything else is a
Tycho int, a length/capacity header, a map key, or an FFI crossing → `tycho_int`.

## 7. Bootstrap artifact (Phase 5 pre-answer)

No pre-generated `tychoc0` C artifact is committed. `Makefile:308`'s clean target
lists `tycho.c`/`tychofmt.c`/`tycho-lsp.c` as **generated, removable** outputs,
and no `*.c` under `compiler/` exists (only `tychoc0.ty`). **Phase 5 is a
documented no-op unless `make bootstrap` regenerates from source** — recorded so
Phase 5 does not invent work.

## 8. ILP32 toolchain probe — see the DONE block appended to `docs/internals/plan-int64-DONE.md` phase 1.
