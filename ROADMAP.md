# Tycho — improvement roadmap

> Companion to [STATUS.md](STATUS.md). STATUS says what's *shipped* and what's a
> *decided* non-goal. This says where the language could still move. It is
> deliberately opinionated and it *does* reopen a few STATUS non-goals — because
> that was the ask. Each such item is flagged **[reopens a STATUS non-goal]** and
> argued both ways, not smuggled in.
>
> Organizing principle: Tycho is feature-complete *for its thesis*
> ([docs/thesis.md](docs/thesis.md)). So "improvement" splits three ways —
> **(1) fits the thesis** (build freely), **(2) worth reopening** (the decision
> was defensible but not airtight), **(3) fights the thesis** (would make it a
> different language — listed for completeness, with the verdict). Sorted by that,
> not by size.

---

## Tier 1 — Fits the thesis cleanly (build freely)

These are pure additions. None touch value semantics, arenas, or the escape rule.
Ranked by value.

### 1.1 Sized / unsigned integer types — *this is the real "byte type" ask*
**State today:** the only integer is `int` = 64-bit signed (`types.md:18`). A byte
scalar already exists as `char` (0–255, `types.md:22`) and binary buffers as
`bytes`. Missing: `u8 u16 u32 u64 i8 i16 i32`, i.e. **unsigned and sized**.
`float` is likewise f64-only — no `f32`.

**Why it matters (highest-leverage item on this list):**
- **FFI correctness.** Right now `extern` cannot faithfully type a C `uint32_t`,
  `size_t`, `int16_t`, or `float`. Everything funnels through 64-bit signed `int`.
  That's a silent-truncation / sign-mismatch hazard at the exact trust boundary
  Rule 5 says to be paranoid about. Sized ints fix FFI more than any FFI-specific
  feature would (see 3.3).
- **corelib crypto/hashing/binary I/O.** SipHash, SHA-256, MD5, base64, CSV, and
  the arena hash all currently express byte/word math by masking a signed 64-bit
  `int` by hand. Native `u32`/`u64` with defined wrap makes that code correct by
  construction instead of correct by discipline.
- **Binary formats** (file headers, network protocols, `bytes` parsing) want
  fixed-width fields.

**Thesis fit:** total — scalars don't allocate, don't alias, don't escape. Zero
interaction with the arena model. The only real design work is the conversion/
promotion rules (explicit casts vs implicit widening) and keeping the "byte ≠
number" discipline that `char` already enforces (`types.md:44`).

**"Byte type" verdict:** you already have the byte *scalar* (`char`) and the byte
*buffer* (`bytes`). What you're missing is the **sized-integer family**. Ship that,
not a redundant `byte` alias.

**Effort:** medium. Type-checker + codegen + cast rules + corelib migration.
**Priority: high.**

**Status — substantially shipped (commit `3870125`).** Two slices are done:

1. **First-class `u32`/`u64`/`f32`.** Full in-language types: arithmetic, bitwise,
   shifts, div/mod, comparison, `[]u32` arrays, printing, and conversions
   (`to_u32`/`to_u64`/`to_f32`/`to_int`/`to_float`). `u32`/`u64` wrap at their C
   width (`unsigned int` / `unsigned long long`); `f32` is single precision. Both
   transpilers agree, ASan-clean, locked by `tests/sized_ints` and — for the u32
   path end-to-end — the NIST vectors in `corelib/test/sha256`, which this type
   rewrote to be correct *by construction* rather than by hand-masking.
2. **FFI-boundary `u8`…`i64`.** Valid in an `extern` signature (by-value param or
   return): the emitted C prototype uses the real fixed-width type so a call
   matches the C ABI instead of funneling through `long`. `tests/ffi` case (9).

**Remaining — small and demand-gated, not blocking:**
- *corelib crypto cleanup — DONE.* `corelib/md5` and `corelib/hash` (crc32, djb2,
  sdbm, fnv1a) now run their word math in `u32` (native wrap, `~`, `>> <<`)
  instead of `% 4294967296` / `4294967295 - x`; output is byte-identical to the
  goldens. `corelib/sha256` was already migrated.
- *Parity gap — FIXED.* tychoc accepted `u32 == <int literal>` and
  `f32 </== <numeric literal>` (it adapts literals at `src/tychoc.c:4374`) while
  tychoc0's `==`/`!=` and ordering branches omitted that adaptation. Root cause:
  the adaptation predicate was duplicated across three branches and drifted;
  `compiler/tychoc0.ty` now computes it once (`num_lit_ok`) shared by all three.
  `fuzz/run_typeparity.py` gained u32/u64/f32 to the exhaustive matrix (it had
  only covered int/float/char/string/bool, which is why the gap stayed latent) —
  now 4608/4608 cases agree, and `make fixpoint` stays byte-identical.
- *Literal suffixes* (`123u32`) — **YAGNI**; typed bindings (`x: u32 = 123`)
  already cover the need.
- *Signed/small int family first-class* (`u8`/`i8`/`i16`/`i32`/`i64` outside an
  `extern`) — **SHIPPED (commit `f824a2a`).** The whole fixed-width family is now a
  first-class in-language type, not extern-only. Every type-keyword name (`i8`…`u64`) is
  reserved as an identifier in **both** compilers (tychoc0 gained an identifier check at
  each binding/use site), which also closed a fuzz divergence. **With this, 1.1 is fully
  shipped** (only the YAGNI'd literal suffixes remain unbuilt, by choice).

### 1.2 `const` bindings and compile-time constants — **shipped**
`const NAME = <literal>` works at module top-level AND inside function bodies, for
int/float/string/bool/char literals. A const is an immutable named literal folded
at each use site (no runtime object — mirrors the nullary-enum-variant fold in
both compilers, so emitted C stays byte-identical). Reassignment and non-literal
RHS are compile errors; forward reference and shadowing work. Locked by
`tests/const_toplevel` + `tests/const_local` goldens and three
`tests/reject/const_*` fixtures (both compilers).

Negative-literal consts (`const MIN = -100`, `const T = -3.14`) are supported —
both compilers fold `-<numeric literal>` into a single negative literal at
const-parse time (locked by `tests/const_negative`).

Const expressions are folded at compile time by a shared `const_fold` in both
compilers: integer arithmetic/bitwise/unary (`const MB = 1024 * 1024`,
`const MASK = 1 << 8`, `const N = ~0`) and backward references to earlier
top-level consts (`const MB = KB * 1024`). Locked by `tests/const_expr` +
`tests/reject/const_expr_{divzero,float,localref}` (both compilers). This is the
prerequisite folder for const generics (1.6).

**Deferred by design:** float arithmetic in a const RHS stays a compile error
(only negative float literals fold — computing e.g. `1.0/2.0` byte-identically
across tychoc's numeric-`ival` literals and tychoc0's source-string literals is a
formatting-parity risk, low value); and a *local* const cannot reference another
const (the self-hosted compiler has no sibling-const table at local-parse time) —
both are symmetric limitations rejected by both compilers.

### 1.3 Compiler diagnostics — **substantially shipped**
The humane-error asks are all implemented in tychoc (the reference compiler):
`<file>:<line>: error:` with a source-line snippet, a `^` caret on parse errors,
"did you mean 'X'?" for unknown variable/type/assignment/procedure, type
mismatch showing both sides (`arithmetic requires two ints or two floats (got
int, string)`, `argument 1 of 'f' is string, expected int`, `declared type int
but value is string`), the failed `where`-predicate by name (`'add' instantiated
with T = string, which does not satisfy numeric(T)`), and arg-count/field/index
errors. Locked as goldens in `tests/diag/*.err` (tychoc only — tychoc0's
bootstrap diagnostics are deliberately simpler).

**Did-you-mean extended to struct fields — shipped.** `p.yy` on a `Point{x,y}`
now emits `struct Point has no field 'yy'; did you mean 'y'?`, reusing the existing
Levenshtein `suggest_*`/`dym_pick` machinery (`suggest_field`, `src/tychoc.c:850`;
wired at the field-miss site `:4041`). Only offers a suggestion within the same
edit-distance threshold as the other did-you-means, so a genuinely-unrelated name
(`p.zzzzzz`) still gets the plain message. tychoc only; locked by
`tests/diag/dym_field.err`.

**Fall-off-the-end "not all paths return" lint — shipped.** A non-void, non-extern,
non-generic proc whose body can reach its end without a return now warns in *both*
compilers (`not all paths of 'f' return a value` — codegen zero-fills that path).
It's a **warning, not a reject**: a body ending in an infinite loop is total yet not
provably so, and `tests/die.ty`'s `must_pos` (ends in an `else: die(...)`, where
`die` diverges but the language has no `noreturn`) is a legitimate shape a hard
reject would break. Reuses the existing `block_ends_in_return` in tychoc
(`src/tychoc.c`, now also called in the checker); mirrored by a new
`block_ends_in_return` over tychoc0's `Stmt` enum, hooked into `check_dups` (the
original-`prog.funcs` loop, so it sees the same pre-monomorphization funcs tychoc
does — no generic-instance warning divergence). Verified: corpus-wide both
compilers agree on which files warn; fixpoint byte-identical; typeparity
4608/4608; gated by the `fall-off-the-end` parity assertion in
`scripts/tools_check.sh`.

**Remaining — semantic-error carets: deferred (low value, structural cost).** Parse
errors already carry a `^` caret; semantic errors (type mismatch, unknown field,
arg-count) print `file:line: error:` + both sides + the source-line snippet but no
caret, because `Expr` carries only a `line`, not a column (`src/tychoc.c:1185`).
Adding one means a `col` on every `Expr` plus populating it at every parser
construction site, then setting `g_err_col` before each semantic `die_at` — a broad
structural edit for a cosmetic gain. Not worth it until a concrete demand appears.
None blocking.
**Effort:** the residual is small. **Priority: low** (the compounding wins landed).

### 1.4 corelib gaps
CONTRIBUTING lists "corelib gaps" as useful work. Suggested method: pick a target
task list (a small HTTP service, a log parser, a build tool) and build it against
corelib; every reach for something missing is a ticket. Likely gaps to audit
against the 25 shipped packages: TCP/UDP sockets (only `http` today), TLS,
compression (gzip/zlib), buffered I/O, a bignum/decimal type, richer `datetime`
(timezones), process/subprocess, environment/args helpers. **Priority: medium**,
demand-driven — don't build ahead of a real program that needs it.

**Audit result:** several of these are already covered — `environment/args`
(the `args`/`getenv` builtins) and `buffered/line I/O` (`core:io`'s
`read_lines`/`write_lines`). **process/subprocess — shipped** as `core:os`: a
libc-only FFI shim (`popen`/`system`, no external dep) exposing `os.system(cmd)
-> int` and `os.run(cmd) -> Output{code, out}` (captures stdout). The capture
read-loop lives in `os_shim.c` (checked allocations, fail-closed, Windows-guarded)
so Tycho only sees the finished string; locked by `corelib/test/os` (all three
compile paths agree + golden), ASan/UBSan-clean over the buffer-growth and
alloc/free paths.

**Shipped 2026-07-12** (each: both compilers agree, golden, ASan/UBSan-clean):
- **TCP/UDP sockets — `core:net`.** A libc-only socket shim: `listen`/`accept`/
  `connect`/`port_of`/`write`/`read`/`close_fd` and `udp_bind`/`udp_send`/
  `udp_read`, fds as `int` (negative = failure), binary-safe `bytes` payloads.
- **compression (gzip) — `core:compress`.** gzip compress/decompress over zlib (a
  `deps` pkg-config module; the test skips where zlib is absent — the `http`
  precedent). `bytes -> bytes`, fail-closed on corrupt/truncated input.
- **bignum — `core:bignum`.** Arbitrary-precision integers in pure Tycho (base-10^9
  limbs, value-semantic): `from_int`/`from_str`/`to_str`/`to_int`, `add`/`sub`/`mul`/
  `divmod`/`div`/`mod`/`pow`, `abs`/`neg`/`cmp`/`is_zero`. (A *decimal* type is not
  built.)
- **datetime timezones — `core:datetime`.** Pure FIXED offsets (`from_unix_at`,
  `to_unix_at`, `format_iso_tz`) plus DST-aware SYSTEM/zone offsets via a libc shim
  (`local_offset`, `offset_at`, `now_local`). No IANA tz database.
- **decimal — `core:decimal`.** Arbitrary-precision base-10 fixed point, composed on
  `core:bignum` (a `Big` coefficient × 10^(-scale)) so decimal fractions are EXACT
  (`0.1 + 0.2 == 0.3`). `from_str`/`to_str`, `add`/`sub`/`mul` (exact), `cmp`, `neg`/
  `abs`, `rescale` (truncating). Division deferred (needs a rounding policy).
- **image (PNG) — `core:image`.** PNG decode/encode via libpng's simplified
  `png_image` API (a `deps` pkg-config module). `decode(bytes) -> Image{width,
  height, pixels}` (8-bit RGBA) and `encode(Image) -> bytes`; fail-closed on a
  non-PNG. JPEG (lossy, libjpeg) is a demand-gated follow-up.
- **TLS — `core:tls`.** A TLS 1.2/1.3 client over OpenSSL libssl (a `deps` module).
  Secure by default: `connect(host, port)` verifies the cert against the system CA
  store, checks the hostname, and sends SNI; failure returns a null handle (never a
  silent insecure connection). `write`/`read`/`close_conn` over the encrypted
  stream. Deterministic offline test (dead-port -> null, the http pattern); the live
  handshake is verified manually.

With TLS, **the explicit 1.4 corelib gaps are all shipped.** Several of the
"demand-gated extras" then landed too (2026-07-15), each verified 3-way (tychoc ==
tychoc0 == golden): **core:cli** (arg parser), **core:datetime** ISO-8601 parsing
(`parse_iso`/`parse_iso_tz`), **core:httpd** (HTTP/1.1 server over core:net,
ASan-clean), and **core:raster** (pure-Tycho BMP + QOI codecs, round-trip lossless).

`core:raster` needed a small language addition — **`to_bytes([int])`**, a builtin
that packs an int array (each elem `& 0xFF`) into a binary `bytes` buffer. Pure Tycho
otherwise can't assemble a binary buffer (strings drop interior 0x00, `bytes` is
immutable), so this is the enabler for *any* pure-Tycho binary output. Both compilers
+ runtime; gated by fixpoint + typeparity + corelib.

Still open, genuinely demand-gated: JPEG (needs libjpeg — absent on the dev box;
BMP/QOI cover the pure-Tycho raster need), richer `datetime` formatting beyond ISO,
bignum `gcd` — each to be built on a real need.

### 1.5 Tooling maturity
- **LSP completeness** — hover-types, go-to-def, find-refs, rename, completion
  are all **shipped** (`tools/lsp.ty`, compiler-backed via `tychoc --symbols`).
  Follow-ups: rename/references now reach identifiers inside f-string holes
  (**shipped** — `find_occurrences` descends `{...}`); cross-package completion +
  hover on imported members (`strings.trim`) also **shipped** — the LSP resolves
  `import "core:X"` by running `--symbols` on the file in its real directory
  (package-aware; needs `TYCHO_CORELIB` in the server env). signatureHelp,
  workspace-symbol, and **semanticTokens** are shipped (below).
  **Package-aware diagnostics — shipped:** a file with a `package` decl is
  compiled package-aware. The buffer's package directory is mirrored into a clean
  temp with the live buffer swapped in for the active file (`pkg_mirror`,
  `tools/lsp.ty`), so same-directory siblings and `core:` imports resolve and the
  active file's real errors surface — where a lone temp used to scan `/tmp`,
  fail to resolve siblings, and drop them all. A relative *subdirectory* package
  import isn't mirrored and degrades to no diagnostics (a headerless compiler
  error, dropped) rather than a wrong one; sibling-file errors stay on their own
  file, never the active buffer. Gated by the `pkgdiag` assertion in
  `scripts/tools_check.sh`.
  **signatureHelp — shipped:** typing inside a call shows the callee's signature
  with the active parameter highlighted. Resolves a local call via the symbol
  index (`sym_describe`) and a `pkg.member` call via the project index
  (`pkg_fn_sig`); the parameter list is sliced depth-aware from the signature
  string so a tuple-typed param (`t: (int, int)`) counts as one. `call_sig_query`
  back-scans the current line for the innermost unclosed `(` and the active-arg
  index (`tools/lsp.ty`). Known limit: cursor inside a paren/tuple *literal* arg
  finds that group's `(` (no callee) → no help. Gated by the `sighelp` assertion
  in `scripts/tools_check.sh`.
  **workspace/symbol — shipped:** `Cmd+T` fuzzy symbol search over *all* open
  buffers (top-level fn/struct/enum), case-insensitive substring on the name,
  empty query returns everything; each result's location carries its own buffer
  uri. Reuses documentSymbol's SymbolInformation emit across the parallel
  uris/syms arrays (`handle_workspace_symbol`, `tools/lsp.ty`). Gated by the
  `wsym` assertion in `scripts/tools_check.sh`.
  **semanticTokens — shipped:** a whole-buffer lexical classifier (comments,
  strings/char literals, numbers, keywords, built-in types, functions via a
  trailing `(`, else variables) emitted as the delta-encoded token array. Coarser
  than a full symbol resolve, but it nails what a TextMate grammar can't: exact
  string/comment boundaries and the type/function/variable split
  (`handle_semantic_tokens`, `tools/lsp.ty`). Gated by the `semtok` assertion in
  `scripts/tools_check.sh`.
- **Debugging story** — **shipped.** `tychoc -g` emits `#line N "src.ty"`
  directives (single-file builds) and compiles with `-O0 -g`, so `gdb`/`lldb`
  step the `.ty` source via DWARF. Default builds emit no `#line` (byte-identical
  fixpoint/corelib preserved); tychoc-only, opt-in. Package builds are skipped
  (merged files lose per-node identity) — a documented limit. See
  [docs/debugging.md](docs/debugging.md); locked by the `line-info` check in
  `scripts/tools_check.sh`.
- **Editor reach** beyond VS Code/Zed if anyone asks (Neovim via the LSP is
  near-free). Still open, demand-gated.

**Priority: medium.**

### 1.6 Const generics (array-length parameters) — **shipped**
Two phases. **Phase A:** `[N]T` fixed-size arrays — length fixed at compile time (an int
literal or an int `const`), stored inline, value-copied, static bounds, `len` = the constant
N; works as a struct field, by-value parameter, and return (`src/tychoc.c`
`fixarr_of`/`IS_FIXARR`, `compiler/tychoc0.ty` `is_fixarr`/`afam`). **Phase B:** generic over
size — a `[$N]T` parameter infers its length from the (fixed-size) argument and monomorphizes
one instance per length, exactly like a `$T` type binding; in the body `N` is an `int` const.
Size and element params compose (`[$N]$T` infers both). Rejected in every stored position
(struct field, enum payload, newtype) and for a return-only `$N` — fail closed
(`src/tychoc.c` sizeparam encoding in the arrc `size` field + `match_type`/`subst_type`;
`compiler/tychoc0.ty` `is_sizeparam_arr` + `match_typaram_str`). Byte-identical at fixpoint.

### 1.7 Early `close(h)` on typed handles — **shipped**
Handles auto-free at scope exit; `close(h)` runs the destructor early and NULLs the
handle, and the scope-exit free is null-guarded so the C `free_fn` runs exactly once
even though both paths reach it (`src/tychoc.c:4310`/`:7533`, `compiler/tychoc0.ty:5214`/`:6825`).
A *use* after close passes NULL to C rather than a dangling pointer. Both compilers;
locked by the `tests/ffi` `use_res_close` case (freed exactly once, ASan-clean) and
`tests/reject/close_handle_nonvar`. See `docs/internals/typed-handles-design.md:71`.

---

### 1.8 Formal language specification

Tycho's behavioral contract lives in three places: the `docs/reference/` pages
(12 feature pages, example-driven), the two-compiler parity gates (differential
fuzzing + byte-identical fixpoint), and the golden test suite (274+ fixtures).
None of these is a single formal specification — the kind that says "an
`if`-expression arm is evaluated in a fresh scope whose arena is freed before
the next arm" with a grammar production and a defined semantics for every
construct.

**Why it matters:** the self-hosting fixpoint proves the two compilers agree
*today*, but it doesn't define the language for a third implementation, a
future rewrite, or a user reasoning about edge cases without reading 10k lines
of C. A spec is the non-negotiable deliverable for any versioned release — it's
what lets a third party implement Tycho from scratch and produce byte-identical
output.

**Thesis fit:** total — a spec is a document, not a language feature. It
documents the arena model and value semantics without changing them.

**Effort:** high. Formalizing every construct, its arena lifetime, its
conversion rules, and its interaction with every other construct is a book, not
a README. The reference docs are a start; a spec means tightening them to
grammar-level precision.

**Priority: medium** — only if a versioned release is the goal. The current
PoC is self-verifying (two implementations, byte-identical output), which is
arguably *stronger* than a spec for the current project scope.

---

## Tier 2 — Worth reopening (the ask items; decision was defensible, not airtight)

### 2.1 The ternary — reframed as **expression-valued `if`/`match`** — **shipped**
`if` and `match` are now value-producing in **tail position**: the whole RHS of a `:=`, a typed
`x : T =`, a plain `x =` / place assignment, or a `return`. Each branch/arm is a single
expression; a value `if` requires an `else`; a value `match` stays exhaustive; all branches must
unify to one type (which the binding infers). It desugars to the exact declare-then-assign-in-
each-arm C the docs already showed by hand — the non-decl positions rewrite each tail to an
`S_RETURN`/`S_ASSIGN`/place-set at parse time (reusing resolve + codegen wholesale); only `:=`
carries the control node and infers the type by unifying tails. No new arena mechanism (the
value lands in the destination arena exactly like a return). Both compilers, byte-identical
fixpoint; locked by `tests/if_expr` + `tests/match_expr` goldens and four
`tests/reject/{if_expr_type_mismatch,if_expr_no_else,match_expr_nonexhaustive,if_expr_multistatement}`
fixtures (both compilers reject). The move-on-last-use read counter, push-fusion, and
bounds-elision walkers were taught to descend into the value-decl's control node (a latent
correctness gap for a variable read only inside a value tail).

**Deferred by design** (ponytail follow-ups, marked in-code): multi-statement value branches;
a diverging arm (`None: return -1` instead of yielding — `block_ends_in_return` is the hook);
and use as a nested sub-expression (`1 + if …`) — the tail-position grammar is what keeps the
indentation-block-as-value unambiguous. The statement form covers all three today.

The rest of this section is the original design argument, kept for the record.

You said the rejection wasn't on sound basis. Here's the honest read, both ways.

**The rejection *is* internally consistent.** Tycho is statement-oriented on
purpose: `match` is a statement, not an expression (`enums-options.md:98`); `if`
likewise. In that world, banning `?:` is *uniformity*, not arbitrariness — no
control flow anywhere produces a value, so the ternary would be the lone
exception. That's a sound basis. It's just an unstated one; STATUS lists the
conclusion (`ternary operator`) without the premise (statement-orientation).

**Where it's weak:** the premise itself is the real decision, and it's the thing
worth reopening — not the ternary. The ternary is a symptom. The question is:

> Should `if` and `match` be **expressions** (Rust/Kotlin style — a block whose
> value is its last expression / its taken arm)?

If **yes**, then `x := if c { a } else { b }` and `x := match v { ... }` subsume
the ternary entirely, are strictly more powerful (n-way, pattern-matching,
multi-statement arms), and *remove* the current boilerplate the docs themselves
show — `enums-options.md:98` literally instructs you to declare-then-assign-in-
each-arm because there's no expression form. That boilerplate is the actual cost
the user feels; `?:` only patches the 2-way case of it.

**Thesis fit: clean.** An expression that yields a value is just an rvalue — it
lands in the destination's arena exactly like a function return already does
(§4a return-slot move). No aliasing, no new escape path. This does *not* fight
value semantics.

**Recommendation:** don't add C's `?:`. If you want the ergonomics, add
**block-valued `if`/`match`** — more uniform, subsumes the ternary, and it's the
sound version of the same wish. If you *don't* want expression-oriented control
flow as a language identity, then keep both rejected, but write the premise into
STATUS so "no ternary" reads as a consequence, not a whim.
**Priority: medium-high** *if* you want the ergonomics; it's a genuine identity call.

### 2.2 Expanding generics — the wall is the constraint set, not the shapes
**State today:** generics are already broad — monomorphized generic
structs/enums/fns, recursive + nested, through containers/channels/Tasks
(STATUS "Shipped"). The constraint mechanism is a **closed, transpiler-known
predicate set**: `numeric(T)`, `comparable(T)`, `has_str(T)` (`generics.md:29–34`),
deliberately not user-extensible (that's the anti-traits stance).

**So "expand generics" means one of two very different things:**
- **(a) More predicates / more reach** — add predicates (`hashable(T)`,
  `defaultable(T)`, `ordered(T)`), variadic generics, generic UFCS method
  resolution, const generics (1.6). All fit the closed-set model. Incremental,
  thesis-safe. **This is the real generics roadmap.** **Priority: medium**,
  demand-driven.

  **Status audit (on building the list out):** most of this is either already
  done or not the cheap cleanup the phrasing implies.
  - **generic UFCS — already shipped, both compilers.** A generic free fn is
    callable as a method (`x.foo()` == `foo(x)`), dispatched by matching the
    receiver against the template's first-param pattern, then instantiated;
    scalar/string/array receivers + chaining. tychoc `ufcs_generic`
    (`src/tychoc.c:3499`), tychoc0 `ufcs_name` + `match_typaram_str`
    (`compiler/tychoc0.ty:3785`), locked by `tests/generic_ufcs`.
  - **`defaultable(T)` + `zero$(T)` — shipped.** A bare predicate gates nothing,
    so it shipped paired with the `zero$(T)` builtin it exists to constrain:
    `zero$(T)` yields the type's zero (`0`/`0.0`/`false`/`""`) so a generic
    accumulator can seed from the zero (`acc := zero$(T)`) and work on an *empty*
    input instead of crashing on `xs[0]`. `zero$(T)` reuses the existing explicit
    type-arg syntax (`empty$(int)`) and lowers to the scalar-zero literal at
    resolve (tychoc) / gen (tychoc0) — the same node the `m[k]` read-default uses,
    so no new codegen. v1 covers exactly the four scalar-zero types (int, float,
    bool, string), which `defaultable(T)` gates; composite/Option/enum are
    *not* defaultable (fail-closed — a `(T){0}` memset-zero isn't a valid Tycho
    value for a heap-bearing type), add on demand. Locked by
    `tests/generic_defaultable` (numeric + string accumulators, empty inputs) and
    `tests/reject/{where_defaultable_bad,zero_bad_type}` (both compilers reject a
    struct/array type). Verified: full suite, `fixpoint` byte-identical,
    `typeparity` 4608/4608, behavioral parity tychoc == tychoc0.
  - **variadic parameters + generics — shipped.** `fn f(xs: ...T)`: a final
    variadic parameter is a `[T]` the call packs its trailing args into
    (`f(a,b,c)`, `f()` -> `[]T`, `f(arr...)` spread; fixed params may precede it).
    The generic form `...$T` infers `T` from the arguments. It's sugar — the call
    folds trailing args into one array-literal arg, then it's an ordinary call to
    `f(fixed..., xs: [T])`, so `[$T]` vs the packed `[int]` unifies in the existing
    monomorphizer with no new inference. Both compilers, byte-identical fixpoint;
    locked by `tests/variadic` + four `tests/reject/variadic_*`. (In-language only;
    FFI variadics stays a decided non-goal.) See `docs/reference/functions.md`.

  **`hashable(T)` — shipped.** Constrains a type parameter to be usable as a map
  key, so a generic body can build `[T: V]` with a clean signature error at
  instantiation instead of a deep body error. The check reuses the standalone
  map-key validity path already in both compilers (`key_hashable` /
  `mapkey_composite` / fieldless-enum / int·string via newtype) — a new
  `key_type_ok` predicate, ~6 lines × 2 compilers, no codegen (a compile-time
  accept/reject only, so emitted C is byte-identical). Accepts int, string,
  fieldless enums, and composite struct/tuple/array keys of hashable leaves;
  rejects float/bool/char and non-hashable composites. Locked by
  `tests/generic_hashable` (int + string + enum + struct keys) and
  `tests/reject/where_hashable_bad` (float key, both compilers reject). Verified:
  full suite 274/0, `fixpoint` byte-identical, `typeparity` 4608/4608.

  **The other two candidates are dead, on inspection:** `ordered(T)` would be a
  redundant alias of `comparable` (which already gates `< > <= >=`); `==`/`!=`
  already works on *any* generic T with no predicate (`gen_eq` recurses
  structurally), so no `equatable(T)` is needed. `defaultable(T)` remains
  buildable but thin (only scalars have a defined zero) — add on first real
  demand.
- **(b) User-defined constraints** — letting *users* name new predicates over
  types. That is traits/typeclasses by another name, and it's a hard STATUS
  non-goal. See 3.2. Don't drift into it by accident while doing (a).

The honest framing: generics aren't under-powered, they're *deliberately
un-abstracted*. Growth happens by widening the built-in predicate set, not by
handing the predicate mechanism to users.

### 2.3 FFI improvements — most of the win is 1.1, the rest hits the non-goals [partially reopens]
**State today:** `extern` over scalars/string/bytes/opaque `ptr`/typed handles,
nullable `Option(string)` returns, `inout` out-params (STATUS "FFI"). Declared
non-goals: variadics, callbacks-into-Tycho, struct-by-value, auto-bindgen.

**What improves FFI without touching any non-goal:**
- **Sized integer types (1.1)** — the single biggest FFI correctness win, full
  stop. Do this first; it's already Tier 1.
- **Richer handle/type mapping** — **`[int]`/`[float]` now cross the FFI both ways.**
  A param passes as `(const T*, long)`; a return uses the out-param shim
  (`T** out, long* outlen`) + `tycho_arr_*_from_c`, exactly like `bytes`. Both
  compilers, byte-identical fixpoint, locked by `tests/ffi`. Still open: more
  faithful `const`-correctness, and `f32` element arrays.
- **Linking/build ergonomics** and clearer FFI docs (CONTRIBUTING notes
  "read-once-borrow docs" are thin).

**What would help but reopens a non-goal:**
- **Callbacks into Tycho** [reopens]. This is the real FFI ceiling — `qsort`,
  event loops, and most callback-driven C APIs are unreachable. But it's a non-goal
  for a *thesis* reason, not a whim: a C callback fires on a thread/stack Tycho
  didn't create, with no arena in scope — it breaks the "private arena per call,
  deep-copy at the boundary" invariant the concurrency safety rests on
  (thesis §7). Reopening it means designing an arena-entry shim for foreign call-ins.
  Real design work, not a small feature. **Verdict: hard, and it's load-bearing —
  only if a concrete need justifies the design cost.**
- **struct-by-value / auto-bindgen** — ergonomic, not thesis-threatening, but you
  declared them out to keep FFI's surface small. Reopen only if FFI becomes a
  headline use case (it reads today as a supporting feature, not a pillar).

### 2.4 User-defined projections / yielding subscripts — **shipped**
`subscript name(recv, args) -> inout U: yield &<place>` lets a library expose a
zero-copy view into part of a value, generalizing the built-in `&m[k]`. Called as a
method (`g.edge(i)`), usable as a place (`g.edge(i).w = v`, `&g.edge(i).w`) or an
rvalue read. A subscript is a **compile-time place-macro**: at the call site the
yielded place is inlined with the arguments substituted for the parameters, then read/
written through the existing lvalue machinery — no runtime object, no arena
interaction, no RC (the `m.get` desugar pattern). Fail-closed at compile time: the
place must be rooted in a parameter (else it would dangle), each parameter appears at
most once (no argument double-evaluation), and the declared `-> inout U` must match the
yielded place's type.

Both compilers; the self-hosting fixpoint stays byte-identical (tychoc0.ty uses no
subscripts, and existing codegen is untouched). Locked by `tests/subscript` (both value
shapes; write/read/compound/`&`-inout-field) and four `tests/reject/subscript_*`
(dangling, param-twice, non-place, type-mismatch). Verified: `make test` 284/0,
`make fixpoint` B==C byte-identical, `make fuzz` 500/500, `make tools-check` ok,
ASan/UBSan-clean, tychoc0 output + emitted-C parity with tychoc. See
[docs/reference/subscripts.md](docs/reference/subscripts.md).

**v1 scope (per the RFC's index-pool example):** inout-yield, method-call form, one
yielded place. Deferred: `let`-yield (read-only views), index-operator overload
(`g[i]`), multi-use parameters (needs argument hoisting). *Inherited* limit (pre-existing,
not introduced here): a bare scalar `&arr[i]` — as opposed to `&arr[i].field` — passed
as an inout *argument* hits a built-in codegen gap; scalar-projection assignment and
reads are unaffected.

The design argument, kept for the record: this is the **one** feature-work direction
CONTRIBUTING blesses and the `limited-references-spike.md` RFC blessed as compatible —
the single limited-reference idea that fits the arena + deep-copy-boundary model
(scoped, transient, never stored, never crossing the thread boundary). It was
demand-gated ("scope it to a real need"); built against the RFC's own index-pool
ergonomics example.

---

## Tier 3 — Fights the thesis (verdict: don't build; here's why, so it stays decided)

Listed so the roadmap is complete and the "no" is on record with its reason, per
the project's own honest-limits culture (thesis §5).

| Item | Verdict | Reason |
|---|---|---|
| **Shared-mutable / graph references** (observer graphs, ref-cyclic structs, stored aliases) | **No — defining boundary** | This *is* what value semantics forbids by construction (thesis §5). `inout` already covers the reachable part (call-scoped exclusive borrow). Storable aliasing would make it a different language. Graphs → index pools. |
| **Traits / typeclasses** (user-defined constraints) | **No — but note it's the ceiling on 2.2b** | Removes the closed-predicate-set simplicity. The whole generics-constraint story is built on *not* having this. |
| **GC / refcounting / COW** | **No — thesis-defining** | The entire claim is "no GC, arenas made implicit by value semantics." Adding any of these dissolves the point. |
| **Hindley-Milner / global inference** | **No** | "Every expression typed at its own site, no unification" is a stated design property (`types.md:112`). Bidirectional local inference is the deliberate choice. |
| **Package manager** | **No — decided** | Odin-style local packages ship; a *manager* (registry, versioning, network fetch) is scope the PoC doesn't want. |

---

## Cross-cutting axes (not language features)

- **More codegen / runtime-layout optimizations.** Three thesis-carrying ones have
  shipped: return-slot move (§4a), in-place append (§4b), and — newest — the
  **compact indexed-dict map layout** (2026-07-15). It replaces the value-inline hash
  table + `nxt`/`prv` insertion-order list with an `int32` index table over *dense*
  insertion-ordered entries, across all four map sites (runtime `ii`/`si`/`sf`/`if`,
  tychoc `mapc%d`, tychoc0's generator). Measured, checksums byte-identical: **trie
  119 → 58.7 MB** (3.2× → 1.55× C, wall now below Go's), **lru 40 → 32.6 MB** (ahead of
  Go on both memory and wall), map-native invindex 127 → 64 MB; `json`/`dijkstra` flat.
  This is what superseded typed sub-pools for the memory story (next bullet). Design +
  full gate: [docs/internals/compact-dict-map-design.md](docs/internals/compact-dict-map-design.md).
  Remaining candidates that fit: inlining hints, small-value stack promotion, SIMD in hot
  corelib paths. All measurable against the existing `bench-guard`. **Priority:
  opportunistic, evidence-gated** — the `bench/*/RESULTS.md` discipline is the right filter.
- **Type-homogeneous sub-pools — investigated and rejected on evidence (2026-07-13).**
  cachegrind on the pointer-heavy benches showed last-level data-cache miss rates are
  already low (interp 0.9%, trie 1.8%) and interp's locality *beats* C's (0.9% vs 2.6%),
  so a locality optimization has nothing to reclaim: the tycho slowdowns are compute-bound
  (boxing, tag dispatch, value-copy), not memory-latency-bound. Typed pools also cannot
  touch the trie storage gap. Not built.
- **Alternate backends** (WASM, native/LLVM). Interesting reach, but C-as-target is
  part of the PoC's leverage (portability, DWARF, ASan verification). **Verdict:
  out of scope unless the PoC's goal changes** from "prove the model" to "ship a
  toolchain."
- **Verification surface** is already exceptional (16 gates, differential fuzzing,
  byte-identical self-host). Marginal add: property-based tests in `corelib/test`.
  Low priority — this is the project's strongest area, not its weakest.
- **Date-based versioning.** Tycho currently has no version number — it's tracked
  by git commit. If formal releases ever happen, a date-based scheme
  (`Tycho 2027`, `Tycho 2028.01`) tells you at a glance how stale your build is
  and encourages a regular update cadence without the baggage of semver promises
  for a language still proving its thesis. Odin adopted this for the same reason:
  a monthly release schedule with year-named milestones. **Priority: low** —
  only relevant if versioned releases become the project's mode.

---

## Where things stand — and what's next

The three items this roadmap last flagged as the cycle's priorities have **all shipped**:

1. **Sized/unsigned integer types (1.1)** — done. The whole fixed-width family (`u8`…`i64`,
   plus `u32`/`u64`/`f32`) is first-class in-language, not only at the FFI boundary
   (commit `f824a2a`); type-keyword names are reserved as identifiers in both compilers.
2. **Compiler diagnostics (1.3)** — substantially shipped (line + caret, did-you-mean
   incl. struct fields, fall-off-the-end lint); only semantic-error carets are deferred.
3. **Expression-orientation (2.1)** — resolved: `if`/`match` are value-producing in tail
   position, both compilers, byte-identical fixpoint. (Not C's `?:` — the sound version.)

Two more foundational deliverables landed alongside:

- **Formal specification (1.8) — ratified as Tycho 1.0** (commit `9f97ada`): grammar,
  per-construct semantics, conformance matrix, and a spec-check gate. The self-host is no
  longer the *only* contract.
- **Compact indexed-dict map layout** — closed the value-semantic ~3× RAM story on the
  pointer-shaped benchmarks (see Cross-cutting axes; trie 1.55× C, lru ahead of Go).

**What's next — the thesis is proven and the pillars are in place; remaining work is
keeping the two compilers honest plus demand-gated polish, not new language identity.**
Foundation before feature breadth:

1. **Hold spec + two-compiler parity in lockstep** — the standing foundational task, and
   an active one. A differential drift hunt found and fixed four tychoc/tychoc0 divergences
   of a single recurring shape: the fixed-width integer family was made first-class, but
   individual sites still enumerated only `u32/u64/f32` — the `[]T` parser lookahead,
   `type_of`'s binary-op result, the conversion-arg checks, and package `mangle_type`. The
   durable fix was to make the differential fuzzer *emit* that surface (sized ints, bool
   arrays) so the gap can't silently reopen, then audit every `u32/u64` enumeration across
   both compilers and the corelib (the crypto/raster uses are fixed-width by spec, not gaps).
   Next: extend the same differential coverage to the other under-fuzzed surface — f-strings,
   closures, generic instantiation, FFI types, multi-file packages — before a real program
   trips it.
2. **CI-hygiene** — `make ci` went red unnoticed (a `datetime`-FFI link break in the `site`
   dogfood) because the pre-push hook runs only `test + fixpoint`. A green `make test` is not
   a green tree; decide whether to widen the pre-push gate or run the full `make ci` on a
   cadence.
3. **Demand-gated corelib / tooling extras** — genuine 1.4 leftovers (JPEG / other image
   formats needing `libjpeg`, richer `datetime` formatting beyond ISO, bignum `gcd`). Each
   built against a real program, never ahead of one.
4. **Opportunistic codegen** now that the map-memory gap is closed — inlining hints,
   small-value stack promotion, SIMD in hot corelib paths — all evidence-gated against
   `bench-guard`.

No open item here requires reopening a thesis non-goal; Tier 3 stays decided.
