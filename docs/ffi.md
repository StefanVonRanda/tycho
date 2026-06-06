# FFI — calling C from hier (design)

> Status: **Stages 1–3 SHIPPED** (scalars + string + opaque `ptr` in both compilers; Stage-3
> linking ergonomics in hierc — see §8). Inspired by the `tycho` language's FFI, adapted to
> hier's internals. Every hier claim below cites `file:line`; tycho claims cite the tycho repo
> as read on 2026-06-06. Regression: `make ffi` (`tests/ffi/`), wired into `make ci`.
> Real-world dogfood: **`examples/sqlite/`** binds in-memory SQLite (ptr handles, SQL string
> args, arena-copied column text, `--shim` for the out-param API, `--pkg`) — through both
> compilers, ASan-clean against a library whose returned text pointer is genuinely transient.

## 1. Goal & thesis fit

hier's thesis is value-semantics over an implicit arena: hier code never does manual
memory management. FFI is the one deliberate **escape hatch** — a narrow, opt-in boundary
to call existing C libraries (libm, libc, sqlite, SDL, …). The design keeps the boundary
narrow so the thesis stays intact *on the hier side*:

- Primitives cross by value (a `long`/`double`/`int` is a `long`/`double`/`int`).
- A C function that returns a string gets **copied into the caller's arena** at the call
  site (`scopy`), so hier never holds a pointer into C-owned memory.
- An opaque foreign pointer (`ptr`, Stage 2) is an integer handle hier can only pass back
  to C — it is never dereferenced by hier code, so there is nothing to manage.

So foreign memory never leaks into hier's value-semantic world. FFI is honestly an unsafe
boundary (you can still pass a bad handle to C and crash inside C); the design's job is to
make the *hier* side sound and the unsafe surface explicit (`extern`).

## 2. Prior art: tycho's FFI (what we borrow, what we drop)

tycho is the closest cousin (arena memory model, compiles to C, value semantics). Its FFI:

- **`extern fn` declaration**, bodyless, direct C symbol, optional `extern "Lib"` library
  name — `tycho/src/parser.zig:318` (`parseExternFn`), examples `tycho/std/mathf.ty:21`,
  `tycho/vendor/sdl.ty:113`.
- **Fat-pointer `str` forces a `cstring` type.** tycho `str` is `{const char* ptr; i32 len}`
  (`tycho/SPEC.md:823`), so calling C needs a distinct `cstring` (= `const char*`) and an
  explicit `c_str(str) -> cstring` at every call site (`tycho/src/codegen_c_builtins.zig:251`).
- **Lifetime rule (T3.1):** a thread-local "current arena" + shims that copy C-returned
  strings into it via `tycho_str_dup` to avoid use-after-free (`tycho/ROADMAP.md:246`,
  `tycho/vendor/README.md:79`).
- **Linking:** `-link <lib>` CLI flag, vendor-dir scan + `pkg-config` fallback + bare `-l`
  (`tycho/src/main.zig:217,1153`); `libm`/`libpthread` auto-linked. Per-library `*_shim.c`
  companion files auto-compiled.
- **No variadics; `cstring` can't be stored in struct fields; no auto bindgen** (hand-written
  `.ty` stubs + `_shim.c`).

**What we drop for hier:** the entire `cstring` + `c_str()` apparatus. See §4 — hier's `str`
is already a `char *`, so the conversion tycho is forced into is a no-op for us. **What we
keep:** `extern fn` surface, the arena-copy-on-return lifetime rule (hier already has the
idiom), the `extern "Lib"` → link-flag wiring, and the no-variadics limit.

## 3. Surface syntax

```
extern fn sqrt(x: float) -> float            # links libm (auto, already -lm)
extern fn getpid() -> int                    # no args, libc
extern "z" fn crc32(crc: int, buf: str, n: int) -> int   # links -lz
extern "SDL2" fn SDL_Init(flags: int) -> int             # links -lSDL2
```

- Bodyless `fn` decl (no `:` block). Reuses the existing `fn name(params) -> ret` parser
  shape; the difference is the leading `extern [ "Lib" ]` and the absence of a body.
- The hier symbol name **is** the C symbol name — no mangling, even inside a package (a C
  symbol is global). hierc resolves this for free: its call resolver only rewrites a name to
  the package-prefixed form *if that resolves*, else falls through to the bare extern sig.
  hierc0 mangles eagerly, so its mangler threads an `externs` list (collected at parse) and
  leaves those call names unmangled — like builtins. So an `extern` can be declared and called
  from within any package.
- `extern "Lib"` records `Lib` for linking (§6). Bare `extern fn` assumes the symbol is in
  something already linked (libc, or libm via the existing `-lm`).

## 4. Type mapping (hier → C)

hier's emitted C types are fixed at `src/hierc.c:662-674`:

| hier type | emitted C (`src/hierc.c`) | FFI direction | notes |
|-----------|---------------------------|---------------|-------|
| `int`     | `long` (:662)             | in/out        | zero-cost |
| `char`    | `long` (:663)             | in/out        | zero-cost |
| `float`   | `double` (:664)           | in/out        | zero-cost |
| `bool`    | `int` (:665)              | in/out        | zero-cost |
| `str`     | `char *` (:666)           | **in: zero-cost**, **out: arena-copied** | §5 |
| (none)    | `void` (:674)             | out only      | bodyless return-less extern |
| `ptr`     | `void *`                  | in/out        | opaque handle, never dereferenced in hier; `null` literal + `is_null(p)` |

**The string win.** hier `str` is a null-terminated `char *` (`:666`; built by
`hier_str_*` in `runtime/hier_rt.c:178-322`, always NUL-terminated). A C function taking
`const char*` therefore takes a hier `str` directly — **no conversion, no `cstring` type,
no `c_str()` call.** This is the single biggest simplification over tycho, whose fat-pointer
`str` makes that conversion mandatory.

Composite hier types (`[int]`, maps, structs, `Option`/`Result`, tuples) are **not** allowed
across the FFI boundary in any stage — their C representations (`HierArrInt`, `E_* *`, …) are
hier-internal, not a stable C ABI. Fail closed: the type-checker rejects an `extern fn` whose
param/return is anything outside the table above.

## 5. Lifetime & safety rule

Exactly tycho's T3.1, using hier's existing idiom — no new runtime needed:

- **`str` argument (hier → C):** pass the `char *` through unchanged. Zero-cost.
- **`str` return (C → hier):** the call lowers to `scopy(<arena>, <call>)` so the C-owned
  bytes are copied into the caller's arena. This is *literally* what `hier_getenv` already
  does — `runtime/hier_rt.c:377` is `getenv()` + `scopy`, and hierc0 emits the same shape at
  `compiler/hierc0.hi:4392` (`hi_getenv` = `getenv` + `scopy(ar, v?v:"")`). An `extern fn …
  -> str` is the *general* case of the builtin we already ship.
  - NULL guard: a C function may return `NULL`. The lowering must be
    `scopy(<arena>, ({ char* __t = <call>; __t ? __t : ""; }))` so a NULL return becomes `""`
    rather than a crash in `scopy`/`strlen`. (tycho hit this exact bug — `tycho/ROADMAP.md:295`.)
- **`ptr`:** opaque `void *`. hier has no operators on it except pass-to-C, `==`/`!=` (against
  another `ptr` or the `null` literal), and `is_null(p)`. No deref, no arithmetic. So hier never
  touches foreign memory — it only shuttles the handle back to C.
- **No variadics** (same as tycho). `printf`-style functions need a fixed-arity wrapper.
- **No struct-by-value, no callbacks into hier** in v1 (callbacks would need hier's fat
  function-pointer rep to cross out, which is not C-ABI; defer).

## 6. Linking

The C reference compiler shells out exactly once, at **`src/hierc.c:5800`**:

```c
char *cmd = sfmt("%s -O2 -o %s %s -lm", cc, base, c_path);   /* -lm already here */
int rc = system(cmd);                                        /* :5801 */
```

- Collect every distinct `"Lib"` from `extern "Lib"` decls → append ` -l<Lib>` to that
  format string. `-lm` stays (covers bare `extern fn sqrt`).
- **Stage 3 CLI passthrough (hierc, `main()`):** `-L<dir>`/`-I<dir>` (both attached and
  separated forms), `--link <lib>` (a bare `-l` for libs not named in source), `--pkg <name>`
  (`pkg-config --cflags --libs`, via `pkg_config_flags`/`popen`), and `--shim <file.c>` (a
  companion C source compiled+linked alongside the generated `.c` — the `*_shim.c` pattern, but
  an explicit flag rather than tycho's auto-discovery, matching hier's no-magic style). All
  accumulate onto the single cc line; libs trail the objects that need them. The `--cc
  <compiler>` override already exists and composes.
- **hierc0 has no linking step** — it emits C to stdout, so these flags are hierc-only; the user
  links hierc0's output with their own cc (passing the same `-l`/`-L`/shim).
- **Still out of scope:** tycho's vendor-dir scan / collection roots. `pkg-config` and shim
  companions now ship; vendoring can come if a real binding needs it.

## 7. Implementation across BOTH compilers (the self-hosting tax)

Every piece lands twice and the fixpoint must stay byte-identical (so externs are only
emitted when actually used/declared, and the package mangler skips them):

| Piece | hierc (C reference) | hierc0 (self-hosted) |
|-------|---------------------|----------------------|
| parse `extern [ "Lib" ] fn …` | parser, near the `fn` decl path | parser in `compiler/hierc0.hi` |
| record signature for type-check | `g_sigs[]`, `src/hierc.c:1970` (model on `getenv`/`read_file` rows :1970/:1974) | `is_builtin_call`/`sig_ret`, `compiler/hierc0.hi:1240,2045` (model on `getenv` :2084) |
| emit `extern <cret> <name>(<cparams>);` prototype | new pass after the embedded runtime, before user code | mirror in hierc0's emit |
| lower call (`name(args)`; arena-copy if `-> str`) | builtin/call dispatch, `src/hierc.c:3687-3691` (the `hier_getenv` pattern) | `compiler/hierc0.hi:2716-2725` (the `hi_getenv` pattern) |
| C type map | `src/hierc.c:662-674` (reuse as-is) | mirror |
| link flags | `src/hierc.c:5800` | hierc0 driver's cc shell-out |

Gates (per the project's CI, all local — see `docs/` / `make ci`):
- `make test` + `make fixpoint` (byte-identical self-build) — proves both compilers agree.
- New golden fixtures under `tests/` (an `extern fn sqrt` round-trip, a `str`-returning
  extern, a NULL-return extern).
- Fuzzer (`fuzz/gen.py`) — add an `extern` kind once stable.
- `make corelib` if any corelib package starts using an extern (e.g. wrapping libm).

## 8. Staged rollout

- **Stage 1 — scalars + `str`. ✅ DONE (both compilers).** Types: `int`/`char`/`float`/`bool`/
  `str`/void. No new type node. Unlocks libm, most of libc, and any C lib with a scalar/string
  ABI. The full mechanism (parse → sig → prototype → call → arena-copy → link) ships in hierc
  (commit "FFI Stage 1 in hierc") and hierc0 (this commit), fixpoint byte-identical, `make ffi`
  green (both compilers agree, ASan-clean, scalar+string both directions, NULL-return guarded).
- **Stage 2 — opaque `ptr`. ✅ DONE (both compilers).** New primitive `ptr` = `void *`, plus the
  `null` literal and `is_null(p) -> bool`. Unlocks handle-based libraries (SDL window, sqlite db,
  curl handle). `ptr` is a non-heap opaque scalar: it rides the existing scalar paths (`c_type`/
  `cty` → `void*`, `copy_into` → by-value, `gen_eq` → `==`, `type_is_heap` → 0), so it also works
  as a local, a regular-fn param/return, a struct field, and an array element (copied by value —
  hier never owns or dereferences it). Only deref/arithmetic are absent (no syntax for them).
  `make ffi` exercises an `ffi_open`/`ffi_read` handle round-trip + `null`/`is_null` through both
  compilers, fixpoint byte-identical.
- **Stage 3 — ergonomics. ✅ DONE (hierc).** `-L`/`-I`/`--link`/`--pkg`/`--shim` on the cc line
  (see §6). `--pkg` shells out to `pkg-config`; `--shim` compiles+links a companion C file (the
  `*_shim.c` pattern, tycho `vendor/README.md:99`, as an explicit flag). hierc-only — hierc0
  emits C and doesn't link. `make ffi` now links its fixture via `-L` and covers a `--shim`
  build; `--pkg zlib` verified manually (crc32 resolved).

Each stage ships independently and is independently useful. Stop after Stage 1 and hier can
already call libm/libc.

## 9. Open decisions

1. **Keyword:** `extern fn` (tycho's, proposed) vs `foreign fn` vs `c fn`. `extern` reads as
   "defined elsewhere" and matches C intuition.
2. **`ptr` now or later:** Stage 1 deliberately omits it. If the first real target is SDL/
   sqlite rather than libm, fold Stage 2 in earlier.
3. **Bare `extern fn` with no `"Lib"`:** assume already-linked (libc/libm) — or require an
   explicit `--link`? Proposed: assume, since libc/libm cover the common case and `-lm` is
   already passed.
4. **`str` return ownership:** always arena-copy (proposed, safe) vs an opt-out
   borrow for hot paths (unsafe, deferred — tycho started borrowed and moved to copied for
   exactly this reason, `tycho/ROADMAP.md:295`).
