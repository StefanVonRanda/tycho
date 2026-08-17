# Changelog

All notable changes are recorded here against the version that shipped them.
The version constant lives in `src/tychoc.c` (`TYCHO_VERSION`, printed by
`tychoc --version`); bump both together. Per-release publishing notes stay in
`RELEASE_NOTES.md`; this file is the accumulating record.

## [Unreleased]

Nothing yet.

## [0.7.0] — 2026-08-15

**Breaking.** Several shapes that used to compile no longer do — a copied
`handle`, a copied channel, `sink`/`inout` on an affine type, a `bounded[N]T`
generic field that had silently degraded — and two of those were live memory
errors, not style. `JsonErr` gains a variant, which breaks an exhaustive `match`
on it. Read "Language — breaking" and "Core library — breaking" below before
upgrading.

**Write new entries here, not under the release heading below** —
0.5.0 shipped without this block, so the only heading to write under was a
frozen one, and eight entries landed inside a tagged release that does not
contain them (see 0.6.0's opening note).

### Language — breaking

- **A handle can no longer be COPIED.** `g := f` on a `handle` value is a compile
  error: it gave one pointer two owners and two scope-exit destructor calls, and
  glibc reported `double free detected in tcache 2` from a four-line program.
  Reassignment (`g = f`) was already refused; only the declaration path was open.
  Migration: pass the handle as an argument, which borrows it, or bind the opener
  directly (`f := open(...)`). Nothing outside `tests/` declared a handle, so no
  known program is affected (FRICTION #43).
- **A bare handle may no longer be a struct field.** `f: File` in a struct is a
  compile error, matching every other aggregate — an array, map, tuple, `Option`
  and `Result` already refused one. `items: [R]` was refused before this only
  because the ARRAY intern helper checked its element; a handle that *is* the
  field type passed through no such helper (FRICTION #44).
- **`sink` and `inout` are refused on an affine type** — a `handle`, `Task(T)` or
  `Channel(T)` parameter may not carry either. Both were accepted and silently
  ignored: a `sink` handle callee borrowed exactly like a default parameter,
  leaving the value live and unclosed at return. Passing an affine value plainly
  is already a borrow, which is what a caller wants; consuming one would free it
  at the callee's scope exit while its destructor is emitted at the owner's
  (FRICTION #49).
- **A channel can no longer be COPIED.** `e := c` on a `Channel(T)` is a compile
  error: a channel-typed declaration is legal only from `channel(...)` itself,
  which is what the spec already required and nothing enforced. No double free
  came of the copy, but the alias hid every send and receive from the compiler's
  analysis — a program sending on every path still drew "nothing ever sends on
  channel 'c'". Passing a channel as an argument is unchanged, so a consumer
  still receives one normally (FRICTION #45).
- **A comment must now OPEN with `deprecated:` to mark a function.** The scan
  matched the marker anywhere in the line, so ordinary prose that merely
  mentioned it deprecated the next `fn` and every caller got a warning nobody
  wrote. Both real users in the tree already use the opening form, so no marker
  changed meaning (FRICTION #46).
- **Affine rules now hold through a GENERIC.** Every refusal that named a
  `handle`, `Channel(T)` or `Task(T)` was enforced on the template, where the
  field type is `$T` and not an affine type, and never re-checked on the
  instance. Four shapes compiled and now do not: an affine type as a generic
  struct field (`struct Box($T): c: $T` at a channel), as a generic enum
  payload, as a `sink $T` / `inout $T` parameter, and as the RETURN of a generic
  (`fn ident(x: $T) -> $T`). Two were real memory errors, not just rule gaps:
  the struct field aliased one channel to two owners, so a `send` through one
  was received through the other; the return double freed, with glibc reporting
  `double free detected in tcache 2` and exit 134. Migration: pass the affine
  value as a plain parameter, which is already a borrow. A **Task has no type
  syntax**, so a `$T` binding was the only way one could reach a field at all
  (FRICTION #53, #54, #55).
- **`bounded[N]T` in a generic struct stays bounded.** Instantiation rebuilt
  every array through the fixed-array constructor, dropping the capacity rule
  while keeping the size, so a field declared `bounded[4]$T` came out `[4]int`.
  A plain `[4]int` was accepted for it and pushes past the capacity grew instead
  of trapping. Migration: pass a real `bounded[N]T` — the fixed array that used
  to be accepted is now refused by name (FRICTION #52).

### Diagnostics

Four messages that an outside reader hit in the first ten minutes writing
`tools/tycho-diff` against `docs/` alone (`tools/tycho-diff/FRICTION-OUTSIDE.md`).

- **The builtin-collision warning no longer offers a remedy `package main`
  forbids.** It said "rename it, or call it qualified as `pkg.name(...)`" — but
  main has no package prefix to qualify with, and the next check makes the
  declaration a hard error, so the second remedy could not be taken. The library
  wording is unchanged, because there the qualified call really works.
- **A qualifier this file never imported says so.** `arrays.fill` without
  `import "core:arrays"` reported `package 'arrays' has no symbol 'fill'` — the
  symbol exists, and the message sent the reader to the package source, the one
  place that could not help.
- **`eprintln` names the stderr primitive.** Edit distance answered `println`,
  which is the wrong STREAM for an error and puts it on stdout, where it corrupts
  whatever the tool is piped into.
- **`main` returning a non-void type names `exit(code)`.** It named only
  `Result(void, string)`, which has two outcomes and prints one of them, so a
  0/1/2 contract like diff(1)'s looked inexpressible. It is not: `exit(code)` is
  a builtin and five programs under `tools/` already use it.

### Language — added

- **Explicit type arguments now parse on a package-qualified name.**
  `vp.ident$(int)(5)`, and with several parameters `vp.pair$(int, string)(1, "a")`.
  The `$` previously fell through to a field access and the spelling was a parse
  error in every form, while the unqualified `ident$(int)(5)` had always worked —
  so the two spellings of one call disagreed. §3/§4, Appendix A, §7.5 and §15.3
  move with it (FRICTION #39).
- **An empty call to a generic variadic may name its element type**:
  `count$(int)()` supplies the empty `[]int`. It was rejected with "pass at least
  one argument" even though the type was given, because the packing site never
  read the explicit type-argument list (FRICTION #40).

### Language — fixed

- **A `deprecated:` marker no longer leaks across files.** A note was matched to
  the `fn` below it by LINE NUMBER ALONE, and notes accumulate across every file
  in the import closure while line numbers restart in each — so a marker at line
  N in one package marked whatever `fn` sat at line N+1 in another. In corelib
  this made `crypto.aead_decrypt` report itself deprecated, quoting
  `sort.by_key`'s message: callers of a security function were told to "use
  sort_by(xs, cmp)". The note now carries its file and both must match.
  `tests/warn/pkg/deprec_crossfile/` constructs the collision in its own two
  packages rather than relying on the corelib coincidence, which would go quiet
  the moment either file gained a line.

- **A generic struct with a map-of-function field compiles.** `steps:
  [string: fn($T) -> $T]` emitted C naming an `FnC<id>` that was deliberately
  never defined, and cc rejected the program with `unknown type name 'FnC0'`.
  The array form of the same field was fixed earlier; the map body loop was the
  only one of five without the guard its siblings had (FRICTION #51).
- **A variadic called through a package qualifier now packs.** `vp.sum(1, 2, 3)`
  died with `'vp__sum' takes 1 argument(s), got 3`: the fold that gathers trailing
  arguments was skipped for a qualified callee, so the call reached the arity
  check unpacked. The same declaration called unqualified always worked, which is
  why three fixture files never caught it (FRICTION #38).
- **A deprecated function taken as a VALUE now warns.** `f := stale` warned
  nowhere, and the later `f(1)` names the binding rather than the function, so one
  line laundered the whole policy. The warning now also fires where the value is
  taken (FRICTION #47).

### Diagnostics

- **Imported types and callees are named as the source spells them.** Every
  diagnostic about a type from another package printed its mangled form —
  `declared type int but value is pool__Handle` for a user who wrote
  `pool.Handle`, and `argument 2 of 'money__add'`. This covered structs, enums,
  handles and newtypes alike, so it reached every corelib type in every message.
  Same-package types were never affected, which is why the tree never noticed
  (FRICTION #41).
- **`for k in m` over a map says what to do.** It reported `map key must be
  string, got int` — about an index the user never wrote, since the loop desugars
  to one — and named neither the loop nor the cure. It now points at `keys(m)`. A
  genuine wrong index keeps the precise old message (FRICTION #42).

### Tools

- **`tycho-make` refuses a damaged mtime in its stamp file.** Every other field
  of a stamp line was validated; the mtime went through the lax `parse_int`, so
  `17x` read as 17 and `abc` as 0. The mtime decides staleness, so a corrupted
  stamp file shipped a stale output as up to date (FRICTION #4).
- **`tycho-db` refuses a SQL integer literal outside int range.** The lexer's
  numeric span is all digits by construction, but `parse_int` returns 0 on
  overflow, so `WHERE id = 9223372036854775808` silently meant `WHERE id = 0`
  and matched the wrong rows with no error.

### Core library — added

- **`decimal.from_str_checked(s) -> Result(Decimal, DecErr)`.** The strict
  sibling of `from_str`, which fails open to a plausible WRONG NUMBER rather than
  to zero: it splits on the decimal point and lets the coefficient parse skip
  non-digits while the scale still counts them, so `1.5x` is `0.15`, `1.2.3` is
  `0.012` and the comma-decimal `1,5` is `1`. For the type this package exists to
  make exact, that is the worst failure it could have. The checked form accepts
  exactly `[-|+]digits[.digits]`; `from_str` is unchanged and still lax, so no
  caller moves (FRICTION #56).

### Core library — security

- **`core:crypto` no longer leaves the plaintext in freed memory.**
  `aead_encrypt` released the plaintext it decoded from hex, and `aead_decrypt`
  released the plaintext it had just recovered, both with a plain `free`. The
  discipline was already in the file and applied unevenly — `key_from_hex`
  cleanses its decode buffer. A third case: the recycled per-thread hex return
  buffer kept the last value it produced for the life of the thread, including
  the raw key from `key_export_hex`; it is now wiped before reuse.
- **`core:crypto` key import is constant-time.** The hex decode rejected at the
  first bad digit, timing the OFFSET of the error on the path `key_from_hex` and
  both `*_key_from_*` wrappers use. The digit decode is branch-free now. Measured
  under valgrind rather than with a stopwatch: the input is marked undefined and
  memcheck reports every branch derived from it — 7 before, 0 after. The input
  length and the single "was the hex well-formed" bit are declassified on
  purpose; nothing else about the key steers control flow.
- **`core:http` can be pointed at a private CA, and its certificate verification
  is now gated.** `SSL_CERT_FILE` and `SSL_CERT_DIR` are honoured — the same two
  `core:tls` already obeyed, so the tree's two HTTPS clients stop trusting
  different stores from one environment. They redirect trust and cannot disable
  it: no `CURLOPT_SSL_VERIFY*` is set anywhere in the shim, an unreadable path
  fails closed, and an empty value is treated as unset. Before this there was no
  way to trust a private CA at all, which blocked more than internal services: a
  gate needs a leg that must SUCCEED, and without one "the untrusted server was
  refused" reads exactly like "nothing connected", so a `CURLOPT_SSL_VERIFYPEER,
  0L` left in after debugging would have passed every lane in this tree.
  `make http-verify` closes that (FRICTION #57).
- **`math.sign` returns ±1 for the infinities.** It documented `-1 / 0 / 1` and
  derived its zero as `x - x` so one body could serve int and float. For an
  infinity that derived zero is NaN, every comparison against a NaN is false, and
  both tests fell through: `sign(inf)` and `sign(-inf)` both returned **0**. The
  derived zero stays — `zero$(T)` needs `defaultable(T)`, which does not see
  through a newtype, so it would have stopped `math.sign(Cents(-7))` compiling —
  and the NaN case is answered where it lands instead. `NaN` still returns 0, now
  deliberately rather than by fall-through. Int is untouched by construction:
  integer arithmetic cannot produce the NaN that reaches the new branch
  (FRICTION #65).
- **`fmath.round` rounds.** It documented "round half away from zero" and was
  `floor(x + 0.5)`, an addition that rounds before `floor` ever runs.
  `round(0.49999999999999994)` returned **1.0** for a value strictly *below* a
  half, and `round(4503599627370497.0)` **moved an input that was already an
  exact integer** — every double at or above 2^52 is one, so the whole range was
  at risk. Decided by the fraction now (`x - trunc(x)`, which is exact for every
  finite x). `0.5`/`1.5`/`2.5` were always correct, which is why the golden never
  noticed (FRICTION #66).
- **`fmath.lerp` returns its endpoints exactly.** `lerp(1e308, 1.0, 1.0)` gave
  **0.0** instead of `b`: `1.0 - 1e308` *is* `-1e308`, so `b` is lost before `t`
  is applied, and `a + (b - a) * t` collapses. The same subtraction overflows to
  an infinity for a far-apart opposite-sign pair, making `t = 0` return NaN
  instead of `a`. Both endpoints are special-cased. No monotonicity guarantee is
  claimed for the interior (FRICTION #67).
- **`strings.pad_left`/`pad_right` no longer overshoot a multi-byte pad.** The
  deficit was counted in bytes and decremented by one per iteration while
  `len(pad)` bytes were appended, so `pad_left("x", 5, "ab")` returned **nine**
  bytes for a width of five, and a UTF-8 pad (`"é"`) did the same — the case a
  formatter actually reaches for, and the one that looks right in a terminal
  while being far too wide. A pad that does not divide the deficit now leaves the
  result **short** of width rather than over, which also keeps it from being split
  mid-codepoint. Every caller in the tree passes a one-byte pad and is
  bit-identical (FRICTION #68).
- **`sqlite.exec` reports the rows it actually changed.** It read
  `sqlite3_changes`, which describes the most recent statement alone, while
  `sqlite3_exec` runs every statement in the string — so
  `exec("INSERT 'a'; INSERT 'b'")` inserted two rows and answered `Ok(1)`. It
  reports a delta of `sqlite3_total_changes` now. Single-statement callers see
  the identical number. **Documented alongside it, not changed:**
  `exec_params`/`query_params` go through `sqlite3_prepare_v2` with a NULL
  `pzTail`, so everything after the first statement is discarded and the call
  still returns `Ok` — the opposite of `exec`. Pass one statement per call to the
  `_params` forms; the limitation is pinned by a fixture rather than left to
  drift (FRICTION #69).
- **`core:sqlite`'s usage example no longer shows `defer`**, which this language
  refused in 2026-08-10. A reader copying the package header got *"a statement
  must be a declaration, assignment, or call"* — a message that never mentions
  `defer`. One instance tree-wide (FRICTION #69).
- **`sqlite` bound parameters survive an interior NUL.** `sqlite3_bind_text` was
  given `-1`, which reads to the first NUL, while a Tycho string is
  length-carrying and may contain one: a 5-byte `"hi\0zz"` stored **two** bytes
  and the call returned `Ok`. The failure mode is collision rather than lost
  characters — two values the program treats as different become one row, which
  matters for a token, a key or a filename. Now bound with `len(params[i])`; the
  value round-trips whole. `sqlite3_prepare_v2` got the same treatment
  (`len(sql)`), though that side is program-authored and is **not covered by a
  test** (FRICTION #70).
- **`toml.parse` refuses a repeated `[table]` header.** The second `[t]`
  **replaced** the first rather than merging, so `[t] x=1 [t] y=2` silently lost
  `x` — the same "override nobody can see" as the duplicate key, and worse, since
  that one at least kept a value. `[[arr]]` still repeats (that is how an array
  of tables is spelled) and `[a]` followed by `[a.b]` still nests; both are
  gated. **The header's contract is narrowed to match reality:** no input is
  accepted that would lose data or invent a value the text does not carry.
  "Nothing is guessed" was untrue of `a = 01`, `a = 1__0`, `a = 1.` and
  `a b = 1`, which stay accepted and are now listed in the header with what each
  yields — a documented subset rather than a validator (FRICTION #62).
- **`crypto.pbkdf2_sha256` no longer truncates a password at its first NUL.** The
  shim called `strlen`, so `"secret\0A"` and `"secret\0B"` derived the **same
  key** — and the same one as `"secret"`. The failure is collision, not weakness:
  the output is a good PBKDF2 hash of the wrong input, so two distinct
  credentials authenticate against each other. The length is passed explicitly
  now and fails closed rather than guessing. **A signature change**: the extern
  `cx_pbkdf2_sha256` takes a `pwlen`; the Tycho-facing `pbkdf2_sha256` is
  unchanged, so no caller moves (FRICTION #75).
- **`crypto.ct_equal` no longer compares two hex strings by their prefixes.** An
  interior NUL shortened both inputs, so two different hex strings truncating to
  the same prefix compared **equal**. **Not an authentication bypass** — a
  computed MAC carries no NUL, so its length disagreed with a truncated attacker
  value and the answer was already false — but a caller comparing two *supplied*
  values had a collision. Lengths are passed and a mismatch refuses. A signature
  change to the extern only; `ct_equal` itself is unchanged (FRICTION #76).
- **`http.get`/`get_body`/`get_status`/`post` refuse a URL with an interior
  NUL**, and **`datetime.offset_at` refuses a TZ with one.** Both truncated at
  the NUL: a `file://…/real.txt\0/ignored` fetch returned real.txt's bytes, and
  `offset_at("EST5\0UTC0")` applied EST rather than the string given. A truncated
  URL is not a shorter one, it is a different one. This closes the sweep — all 46
  corelib externs taking a `string` are now length-carrying or NUL-guarded
  (FRICTION #77, #78).
- **`json.parse_checked` refuses a document nested past 2000 instead of aborting
  the process.** The parser is recursive descent, so depth is C stack: at 50000
  the runtime killed the process with `tycho: stack overflow` and exit 1. That
  guard is safe, but a process that dies cannot return `Err` — and
  `parse_checked` is documented as the entry point that tells you whether the
  document survived. A server that chose it for exactly that reason still died on
  100 KB of `[`. **New `JsonErr.TooDeep` variant** — a breaking change for an
  exhaustive `match` on `JsonErr`; nothing in this tree matched on it outside the
  package (FRICTION #79).

### Core library — breaking

- **`io.write_bytes`, `io.write_at`, `io.set_mtime` and `io.sync` return
  `Result(void, IoErr)`**, not `Result(bool, IoErr)`. Every one of them returned
  `Ok(true)` or an `Err` — `Ok(false)` was unreachable — so the bool made
  `or_return` produce a value the caller had to bind and then guard against a
  case that could not happen (FRICTION #15). `io.write_bytes(p, b) or_return` is
  now a statement. Migration: drop the binding, and rewrite a `match` arm
  `Ok(x)` as `Ok()`. **`io.is_dir`, `io.make_dir` and `io.remove` are
  unchanged** — their `Ok(false)` is a real second answer ("it was already how
  you asked"), not a placeholder.

## [0.6.0] — 2026-08-11

Seventy commits since `v0.5.0`. **Eight of the entries below were written into
the `[0.5.0]` section after `v0.5.0` was tagged** and are moved here, where they
belong: `git merge-base --is-ancestor` puts every one of them outside the tag.
The cause was structural, and the `[Unreleased]` block above is the fix. Read
`compress.decompress`, `io.read_text`, qualified consts, the newtype-collection
fix, `or_return` in `main()`, the live-copy remedy, the `[string]`-as-a-value
diagnostic and the `tycho-ar` parser share as 0.6.0 changes; a `v0.5.0` tarball
does not contain them.

### BREAKING CHANGES

Six changes refuse or re-shape code that compiled under 0.5.0. All six migrations
are mechanical, and each is stated as "what you wrote" → "what you write".

- **A binding may not start with an uppercase letter.** Locals, parameters,
  destructuring targets, loop variables, and `match`/`select` bindings must now
  begin with a lowercase letter or `_`. The uppercase namespace belongs to
  compile-time entities — types, enum variants, consts — so it is no longer
  *expressible* for a binding to shadow a constructor. This is the
  OCaml/Haskell/Elm/SML approach: the bug class is removed by the grammar rather
  than diagnosed by a special case. The guard sits at one point, `vars_push` in
  `src/tychoc.c`, which all ten runtime binding forms reach, and it sits in the
  resolver rather than the parser deliberately — a bare `Name` in a payload slot
  is indistinguishable from a binding at parse time, so checking after the
  pattern is promoted keeps `Err(NotFound)` legal while still refusing a genuine
  binding.

  ```
  fn hash(K: int, S: int) -> int:   →   fn hash(k: int, s: int) -> int:
      A := K + S                            a := k + s
  ```

  **`const` is exempt and unchanged**: 264 of this tree's 266 const declarations
  start uppercase and none starts lowercase. Migrating this tree took 16 sites in
  6 files (md5, sha256, dijkstra, weblog, decimal); md5 and sha256 each keep a
  one-line note mapping the new names back to RFC 1321 and FIPS 180-4, because
  that mapping is how a reader checks the algorithm. Specified in
  `docs/spec/01-lexical.md` §3.5.1.

- **`is` is a reserved keyword.** It is the variant test (below), and it is
  reserved rather than contextual — an operator keyword shares its position with
  an ordinary name in `x is y`, which is exactly where a contextual spelling
  becomes ambiguous. **A variable, parameter or function named `is` no longer
  compiles.** No `.ty` file in this tree used it, so the migration cost here was
  zero, but the name is gone from the identifier space. Listed in
  `docs/spec/appendix-b-keywords.md`.

- **A function may not be named for a constructor, an enum variant or a const.**
  `fn Ok(...)`, `fn Err(...)`, `fn Some(...)`, `fn None(...)` and any `fn` whose
  name collides with an enum variant or a const are refused with `'<name>' is
  already defined`. Previously such a definition compiled and then shadowed the
  constructor at every use site, which is the same bug class the binding rule
  above closes, approached from the definition side. Rename the function.

- **`core:iter` predicates return `bool`, not `int`.** Four functions:
  `filter`, `try_filter`, `count` and `any` took `fn($T) -> int` and treated
  nonzero as true. A predicate is a question, and `int` let an *arithmetic*
  result stand in for an answer — `count(xs, fn(x) -> int: x - lim)` reads as a
  subtraction and counts everything unequal to `lim`, which is almost never what
  the author meant.

  ```
  iter.filter(xs, fn(x: int) -> int: x % 2)        →   iter.filter(xs, fn(x: int) -> bool: x % 2 != 0)
  iter.count(xs, fn(x: int) -> int: x - lim)       →   iter.count(xs, fn(x: int) -> bool: x != lim)
  ```

  A named predicate changes the same way: `fn even(x: int) -> int` returning
  `1`/`0` becomes `fn even(x: int) -> bool: return x % 2 == 0`. `map`, `try_map`
  and `reduce` are unchanged.

- **`image.decode` and `image.encode` return a `Result`.** `decode` answered a
  0×0 `Image` sentinel for every failure and `encode` answered empty `bytes`, so
  a truncated file, a JPEG handed to a PNG decoder and a genuinely zero-dimension
  image were the same value. Now `decode(data: bytes) -> Result(Image, ImgErr)`
  and `encode(img: Image) -> Result(bytes, ImgErr)`, with `ImgErr` naming the
  cause: `Empty`, `NotPng`, `Corrupt`, `BadDims`, `ShortPixels`, `Failed`.

  ```
  img := image.decode(b)          →   img := image.decode(b) or_return
  if img.width > 0:                   # or match Ok(img): / Err(e):
  ```

- **`compress.decompress` returns `Result(bytes, ZErr)`.** It returned bare
  `bytes`, empty on any failure — so a corrupt stream, a truncated one and a
  legitimately empty payload were the *same answer*. An archive can hold a
  zero-byte member, so for any container format a corrupt member read as an empty
  one: data loss that looks like data. Now `Corrupt` / `Truncated` / `Failed`,
  and `raw_decompress` likewise. `compress` itself still returns bare `bytes`.

  ```
  out := compress.decompress(b)   →   out := compress.decompress(b) or_return
  if len(out) == 0: ...
  ```

  The shim always knew which branch it took and discarded it; it now reports a
  status through an `inout int`, the FFI shape `core:io` already used. Callers
  updated: `core:zip` (its CRC check already covered the collapse),
  `tools/tycho-ar` (reports the cause instead of inferring damage from a length
  mismatch — the length check stays, since a payload that inflates cleanly to
  the wrong thing is forgery, not damage), and the worked example.

- **`tycho-ar x` exits non-zero when an mtime could not be restored.** Extraction
  used to `die` on the first failed `set_mtime`, leaving the archive half
  unpacked. It now warns per member on stderr, finishes the extraction, and exits
  1 — so the files are all there and the exit status still says the restore was
  incomplete. A script that ran `tycho-ar x` and checked `$?` will now see a
  failure where it previously saw success on a filesystem that refuses
  timestamps. Exit 0 continues to mean "fully restored".

### Language

- **`is`, the variant test.** `x is VariantName` yields `bool`, for a plain enum
  and for `Option` and `Result`. It is the tag test `match` already emitted, so
  it is true for a payload-carrying variant without naming the payload — the
  point is to *ask* without destructuring, where a `match` with a `_:` arm was
  the only way to phrase the question. The right-hand side is a variant name and
  is never resolved as an expression; it binds nothing. `is` does not chain:
  write `a is X and a is Y`. Precedence sits between additive and comparison.

  ```
  if v is VNull: ...        o is Some     r is Ok     r is Err
  ```

  `core:result` now asks with it internally — `is_ok` is `return r is Ok` — and
  `is_some` was rewritten the same way. Both keep their signatures and their
  meaning; this is an implementation change, not an API one.

- **A `[string]` may cross the FFI as an extern parameter.** It arrives in C as
  `(const char *const *, long)` — a pointer and a length, **not** a
  NULL-terminated `argv` — borrowed for the duration of the call, with nothing
  copied and nothing to free. An empty array may pass a null pointer with length
  0. Parameter position only: a `[string]` return is refused, and `inout string`
  stays refused. Documented in `docs/spec/14-ffi.md`.

  This retires a whole builder protocol. `core:os` used to reach C through
  `osx_argv_new` / `osx_argv_push` / `osx_argv_free` and a `ptr` handle; it now
  declares `extern fn osx_exec(argv: [string]) -> int` and passes the array.
  `os.exec` and `os.exec_out` keep byte-identical signatures — no caller outside
  corelib changes.

- **f-string interpolations evaluate left to right.** `f"{a()}{b()}{c()}"`
  printed A, B, C but *called* c, b, a: the holes lowered to arguments of a
  single `tycho_str_concatN`, and C leaves argument evaluation order
  unspecified, so gcc chose right to left. Each piece but the last is now bound
  to a temporary in a braced group, making evaluation follow source order.

  Observable only when a hole carries a call with side effects, which is exactly
  how it was found: an `io.remove` in one hole ran before the `io.mtime` printed
  to its left. Bare `+` concatenation and ordinary call arguments remain
  unspecified — this fixes the construct that *looks* sequential.

- **Top-level `const`s cross package boundaries.** `pkg.NAME` reads an exported
  constant, **folded to its literal at the use site** exactly as an unqualified
  const is — no runtime global is emitted, so the reference costs nothing. A
  package that wanted to publish a level had to ship it as a function before
  this, because `levels.DEBUG` resolved as an enum variant and died "has no
  variant".

  No export keyword and no new syntax: the leading-underscore rule the language
  already had does the privacy, so `pkg._NAME` is refused unchanged. One
  position is deliberately left out and marked `gap:` in the source — a
  qualified const as a fixed-array length (`[pkg.CAP]int`), because an array
  length is resolved while parsing, when the imported package may not be parsed
  yet; accepting it there would fail or not depending on file order.

- **A newtype over a collection now supports that collection's operations.**
  `type Ids = [int]` was effectively write-only: `len`, indexing, slicing,
  `push`, `pop`, `for … in`, index-assign and every map builtin all refused it,
  and `to_under` was the only door. ROADMAP.md recorded the symptom as "blocks
  `push`"; a probe found all of them refusing.

  A newtype's purpose is **distinctness** at assignment, parameter passing and
  comparison — not hiding what the underlying value can do. Those boundaries are
  unchanged: `[int]` still cannot be passed where `Ids` is wanted, in either
  direction, and two newtypes over the same underlying type stay incompatible.
  A slice or a `map_set` yields the *underlying* type, since it is a fresh value
  rather than the named one.

- **`or_return` works in `main()`.** `fn main() -> Result(void, string):` is a
  second legal shape for the entry point, so error propagation no longer stops
  one frame short of where a program starts. On `Ok` the process exits 0; on
  `Err(msg)` the message is written to stderr — **bare, with no prefix**, the
  same way `die(msg)` prints a user message — and the process exits 1.

  The error type is `string` and the ok payload is `void`, both deliberately:
  an `Err` reaching the entry point is *printed* and `has_str` admits no enum,
  so the error is the message; and a value in the ok slot would make the exit
  status ambiguous. Both wrong shapes are refused with a diagnostic that names
  which half is wrong.

### Core library

- **`io.mtime` and `io.set_mtime`.** `mtime(p) -> Result(int, IoErr)` reads a
  modification time in seconds; `set_mtime(p, secs) -> Result(bool, IoErr)`
  writes one. `set_mtime` does not create a missing file — that is
  `Err(NotFound)` — and a directory is fine. The access time is deliberately
  left alone (`utimensat` with `UTIME_OMIT`), so preserving a timestamp is
  `io.set_mtime(dst, io.mtime(src) or_return) or_return` and nothing else moves.
  Closes FRICTION #8; it is what let `tycho-ar` restore mtimes at all.

- **`strings.slice_bytes` and `strings.slice_str`**, returning
  `Result(_, SliceErr)` with `OutOfBounds` and `Inverted`. A `bytes` or `string`
  slice **clamps** silently — `b[2:10]` on a 5-byte value yields 3 bytes rather
  than failing — while an *array* slice aborts. That asymmetry is now written
  down as the normative rule, and these two are the fail-closed door for code
  that would rather hear about it: a parser gets a real error instead of a
  quietly shortened slice that looks like a short record.

- **`iter.try_map` and `iter.try_filter`**, the fallible pipeline stages.
  `try_map(xs, f: fn($T) -> Result($U, $E)) -> Result([$U], $E)` and
  `try_filter(xs, keep: fn($T) -> Result(bool, $E)) -> Result([$T], $E)`. Both
  stop at the first `Err` and return it unchanged, with no partial array — so a
  parse over a list is `nums := iter.try_map(lines, parse_int) or_return`
  instead of a hand-rolled loop with an early return. Closes FRICTION q#7.

- **`result.map_err_with(r, f)`**, `fn($E) -> $F`, so the cause survives the
  translation. `map_err` replaces an error with a *constant* of the new type,
  which discards whatever the original said; `map_err_with` runs a mapper, so a
  `ParseErr` can become an `AppErr` that still carries which field failed.

- **`decimal.div(a, b, scale, mode) -> Result(Decimal, DivErr)`**, with
  `HALF_UP` and `TOWARD_ZERO` (also spelled `decimal.half_up()` /
  `decimal.toward_zero()`). Three failure causes, not one: `DivByZero`,
  `BadScale(int)` for a negative scale, and `BadMode(int)` for an unknown mode.
  Closes FRICTION q#2. A worked example that claimed `DivByZero` was the only
  cause was corrected with it.

- **The `core:result` combinators work at `Result(void, E)`.** `is_ok`, `is_err`
  and `err_or` could not be instantiated at a void payload: each bound `v`, and
  `Ok(v)` is refused where the payload is void while a bare `Ok:` arm is refused
  everywhere else. Rewritten with `r is Ok` / `r is Err` and an `Err(e):` plus
  `_:` pair — **no compiler change was needed**, which is the useful part of the
  finding. `unwrap_or`, `map_err` and `map_err_with` are still out of reach at a
  void payload, because each must produce or accept the payload itself.

- **`core:io` gains `read_text(p) -> Result(string, IoErr)`.** `io.read` is
  fail-open: it answers `""` for a missing file, an unreadable one and a
  genuinely empty one alike — three different facts flattened into one value.
  `read_text` keeps them apart, reporting the same `NotFound` / `IsDir` /
  `Failed` causes `read_bytes` already did. `io.read` is unchanged and not
  deprecated.

- **`tools/tycho-ar` stops carrying its own integer parser.** `parse_uint` now
  shares `strings.parse_int_checked`'s lexical rule and keeps only the domain
  rule that belongs to the archive format — non-negative, and capped far below
  int64 so a forged length cannot drive a huge allocation. Not a straight swap:
  `parse_int_checked` accepts a leading `-` and the full int64 range, so
  replacing the call outright would have widened what a header field accepts.

### Diagnostics

Four messages stopped pointing at the wrong place or withholding the answer.

- **A refusal inside a generic names the call site.** An error raised while
  checking an instantiated template printed only the template's own location, so
  a user saw `corelib/result/result.ty:125: error: ...` with nothing connecting
  it to their code. A second line now follows: `./main.ty:9: note: required from
  here -- this call instantiated the generic`. Which call chose the types is the
  question a reader actually has.

- **A non-exhaustive `Result` or `Option` match names the uncovered variant.**
  A match that wrote both sides but covered only some `Err` variants got
  `match on a Result must cover both Ok and Err`, which is wrong twice over —
  both sides *were* written. It now reads `non-exhaustive match: missing variant
  <V> of <E>`, the same wording the plain-enum path already used. The original
  message survives for a genuinely absent side. `Option`'s partial `Some` side
  is fixed with it.

- **The binding rule outranks the use-site parse error.** `fn f(Ok: int)` died
  with `expected '(' after Ok` — a parser complaint about a construct the user
  never intended — and never named the rule being broken. It now says `'Ok'
  cannot name a binding -- a local, parameter or pattern binding must start with
  a lowercase letter or '_'; as a value it is a constructor: Ok(x)`, which names
  both the rule and the other reading.

- **A corelib diagnostic names the corelib file.** An error inside a generic's
  substituted body was reported against whatever file was current — typically
  the user's `./main.ty` — paired with the *template's* line number, so it quoted
  an innocent line of the wrong file. It now names the template's own file.

- **The live-copy warning no longer suggests a no-op.** It told you to "pass a
  copy you keep (`y := s`)". Measured on the emitted C: that spelling silences
  the warning and emits the *same* two copies, while making the value's last use
  the aggregate emits one. The warning itself is right — there is an avoidable
  copy — so it stays, with only the remedy that removes one.

- **`[string]` written where a value goes** now names both working forms
  (`[]string`, or `xs : [string] = []`) instead of "expected an expression".

### Fixed

- **`sig_find`'s pointer dangled across argument resolution — a use-after-free
  in the compiler.** `sig_find` returns a `Sig *` into the growable `g_sigs`
  table. Resolving an argument can instantiate a generic, which appends to
  `g_sigs` and reallocates it, leaving the callee's pointer pointing at freed
  memory for every check that followed — inout, arity, types. The symptom was a
  *bogus refusal read out of the freed slot*: a valid program rejected with
  something like `argument 1 of 'print' is inout`, and the rejection depended on
  allocator behaviour, so it came and went with unrelated edits.

  The fix re-derives the index rather than holding the pointer: `int si = (int)(s
  - g_sigs);` once, then `s = &g_sigs[si];` after each `resolve_exp`. Locked by a
  deterministic fixture, `tests/generic_sig_realloc.ty`, which forces 200
  distinct instantiations inside `print`/`str` arguments with deliberately fat
  bodies so it crosses several reallocation boundaries every run — not left to
  ASan to notice, because a use-after-free that reads plausible garbage is
  precisely the one a sanitizer-only gate can miss on a lucky allocation.

- **A `bytes` slice's clamping is now specified**, along with the array slice's
  abort, in the spec and in a worked example — behaviour is unchanged, but the
  difference between the two was previously implied rather than stated.

### Gates and tooling

Not user-facing API, but the record matters: **eight checks that could not fail
now can**, and two of them were sitting over live holes.

- **`corelib/run.sh` and `examples/corelib/run.sh` printed all-green while
  silently skipping.** A package or example whose dependency was missing was
  counted as passing. Both now count skips and name them —
  `N ok, M SKIPPED -- image(missing: libpng)`. This is the failure mode where a
  gate reports success *because* it did not run.
- **`tycho-ar`'s round-trip gate was green over the mtime hole.** Extraction
  restored no mtimes and the gate compared with `diff -r`, which ignores them.
  Leg 3b now compares the timestamps themselves — verified to fail, naming all
  eight members, with `set_mtime` stubbed out.
- **Reject fixtures may pin their diagnostic.** A reject fixture scored only on
  non-zero exit plus non-empty stderr, so a diagnostic that regressed to *wrong
  but still refusing* passed. An opt-in `# expect: <text>` line now pins the
  message.
- **`prunner` scores abort fixtures by their `.err` golden.** `judge_abort`
  never learned that branch, so `make test-fast` disagreed with `make test` and
  the `# expect:` fixtures were scored by nothing in the parallel runner.
- **`bench/` is compiled by a gate.** 51 `.ty` files under `bench/` were
  compiled by nothing; `scripts/entrypoints.sh` now sweeps them (24 → 75 entry
  points), so a language-rule change that breaks a bench file reddens instead of
  waiting to be found by hand.
- **Raw control bytes are rejected in tracked Markdown.** A literal NUL had
  landed in two tracked docs and passed every check; `check_links.sh` now
  rejects 00-08, 0B, 0C and 0E-1F.
- **Citations are gated in CI and pre-push.** `make check-links` — links plus
  `path:line` citations — was manual-only and run by neither, and a red citation
  gate had already ridden a commit onto `main`. Both now call it.
- **The pre-push hook dropped `make ci N=0`.** ~274s on every push taught
  bypassing, which is worse than no hook. It keeps the sub-second link and
  citation gate plus a fuzz smoke test, so it is cheap enough to actually run.
- A hard-coded fixture count in `tests/run.sh`'s prose was **dropped rather than
  refreshed** — it was stale by 34 and load-bearing for nothing, and a snapshot
  count in prose is stale again by the next commit.

## [0.5.0] — 2026-08-10

**The first release ever cut.** Everything below ships in it. The section was
split in two until 2026-08-10 — an `[Unreleased]` block on top of a `[0.5.0]`
block dated 2026-08-05 that was never tagged — which would have shipped a
changelog that did not describe its own artifact. The 08-05 block is a draft,
not history, so the two are merged here under the date this actually shipped.

Eight entries that were appended here *after* the tag were moved to `[0.6.0]`
on 2026-08-11; a `v0.5.0` tarball does not contain them.

- **DEPRECATED: `sort.by_key(xs, key)`** — use `sort.sort_by(xs, cmp)`. The
  rewrite is mechanical: `by_key(xs, k)` is
  `sort_by(xs, fn(a, b) -> int: k(a) - k(b))`. It keeps working for all of 0.x
  and is **removed in 1.0**. Calling it warns.

  This is the deprecation policy in `docs/guides/corelib.md` run end to end for
  the first time, which is the point — a policy that has never been executed is
  prose. Step (3), the compiler warning, is now a general mechanism rather than
  a one-off: a `# deprecated: <text>` comment line **directly above** a `fn`
  marks it, and every call site warns with that text. Locked by
  `tests/warn/deprecated.ty`.

- **`core:net`'s concurrency ceiling is now stated rather than implied.** There
  is no `poll`/`select`/`epoll`/`O_NONBLOCK` in any of its 12 exports, so a
  server's worker count is a hard limit on concurrent connections and one slow
  client occupies one worker. Written into the corelib guide instead of being
  fixed: readiness polling was costed at ~283 lines across 4 files plus a
  redesign of `core:httpd`'s read surface, and adding it later is additive.

- **The Windows parked-`recv` difference is a documented platform limit.** A
  thread parked in `recv` is not released by the shutdown handler as it is on
  Linux, so a Windows server winds down within its idle timeout rather than a
  millisecond. Nothing is lost or corrupted. Recorded in the README's platform
  notes; the measurement stays in `SECURITY.md`.

- **`pass`, the no-op statement.** A block cannot be empty, so a `match` arm or
  an `if` branch with nothing to do had to bind something nobody reads — `ok :=
  sz`, `_ := 0` — because a declaration is a legal statement where a bare
  expression is not. `pass` says it instead.

  It is **contextual, not reserved**: significant only when it is the whole
  statement. `pass` was already a variable name in `corelib/test/testing` and
  `tools/prunner`, and reserving it would have broken the runner that scores
  `make test-fast`, so `pass := 0` and `pass = pass + 1` still mean what they
  did. Used where a value is required — a value `if`/`match` branch tail — it is
  refused with a message naming the fix, unless a variable of that name is in
  scope, in which case it is that variable.

- **`core:sort` gains `sort_by(xs, cmp)`.** A three-way compare
  `fn($T, $T) -> int`, so an order can use several keys, mixed directions, or a
  type with no `comparable` instance — none of which fit `asc`/`desc`/`argsort`
  (values, one direction) or `by_key` (an `int` key, and no order-preserving
  injection of a string into one). Bottom-up merge, stable: equal elements keep
  their input order under a descending compare, which a total-by-index
  comparator would silently reverse.

- **`tychofmt` no longer writes `-1` as `- 1`.** Prefix negation is told from
  binary subtraction by what precedes it. Its two existing gates could not catch
  this: `- 1` is idempotent and emits identical C, so the formatter now also
  asserts canonical spellings.

- **`Result(void, E)`, for an operation that either fails with a reason or
  succeeds with nothing to report.** `void` is now spellable as a `Result`'s ok
  payload — and in no other position — as a type with no values: constructed by
  a zero-argument `Ok()`, matched by a bare `Ok:` arm, never bindable. The
  permission does not nest, so `Result(Option(void), E)` is still refused, and a
  `Result`'s error type still may not be void. Retires the one-field `Unit`
  struct that fallible-but-valueless helpers carried, and with it the
  meaningless `Ok(0)` at the end of every validator.

  Shipped with it, because the feature is unusable without it: **`f() or_return`
  is now a statement** when the ok payload is void. It was previously rejected
  as a bare expression with no effect, which left a void-payload `Result` no
  propagation form at all — `x := f() or_return` has nothing to bind. Over any
  other payload type it is still an error, now naming the type it would have
  dropped. `docs/internals/FRICTION.md` §4 recorded these as one defect with
  three independent sightings; they are closed together.

  Internally, the generic bind vector's "not yet bound" sentinel moved off
  `T_VOID` to its own `T_UNBOUND`. That collision was latent rather than live —
  `void` was not in the type grammar at all — but it is what would have made
  binding a type parameter to `void` read as unbound.

- **Demoted from 1.0.0 to 0.5.0.** `TYCHO_VERSION` in `src/tychoc.c` now reads
  `0.5.0`, and every document that promised a stable surface says pre-1.0
  instead. Nothing about the implementation changed and nothing regressed: the
  demotion is about the promise, not the code. 1.0 says "I will not break you",
  and that had never been tested — `git tag` showed only `v0.1.0`, the
  `[1.0.0]` entry below describes a release that was never cut, and no one
  outside this repo has written a line of Tycho. Withdrawn with the number: the
  corelib API freeze (`docs/guides/corelib.md`) and the spec's "first frozen
  version" (`docs/spec/00-conventions.md` §1.5). The spec stays normative and
  the implementation stays gated against it. `ROADMAP.md` gains "What 1.0
  requires" — the conditions, so the number is earned next time rather than
  declared.

### Earlier in the 0.5.0 cycle (recorded 2026-08-05, at the time as 1.0.0)

The promotion that was later withdrawn. The language surface and the spec are
**not** a stability contract at 0.5 — the README's status banner says so, and
`ROADMAP.md` lists what 1.0 would require. What follows is the chain of work
that landed before the version number was corrected.

- **Batteries-included corelib (phase 1).** Six new packages, each with a test
  + golden + guide entry: `core:testing` (assert/eq/report unit-test state),
  `core:toml` (recursive table/array parser, fail-closed), `core:log`
  (leveled logging, threaded state), `core:sqlite` (direct FFI over
  libsqlite3 — the dbquery pattern), `core:utf8` (validation and codepoint
  iteration, strict rejection of overlong/surrogate/truncated sequences), and
  `core:zip` (reader/writer over a new raw-deflate path in `core:compress`,
  CRC-32 in-package). `core:compress` gained raw deflate/inflate.
- **Vendored dependencies (phase 2).** The importer-relative import story
  documented as the vendoring convention (`vendor/` layout, flat-vendor
  shape); `tycho-fetch <url> <name>` downloads and unpacks a package tarball
  into `vendor/` with sha256 verification (dogfooding core:http + core:sha256
  + core:io); `core:http` gained `body_bytes(r)` — the string `body()`
  truncated at NULs, so binary downloads were impossible.
- **Debugger (phase 3).** `tycho debug <file.ty>` / `tools/tycho-debug/` — a
  gdb adapter: compiles with `-g`, drives a scripted gdb session over pipes
  (breakpoints by source line, step/next, locals under their Tycho names,
  Ctrl-C interrupts a running inferior). Gated by `make debug-check`.
- **1.0 promotion (this phase).** `tychoc --version` and the version constant;
  the README's status banner flips from research prototype to the 1.0
  contract (stable: the language surface and the spec; not stable:
  performance tuning and benches); a security review of the FFI shims
  (string/bytes ownership, the core:os shell-out paths, the TLS wrapper)
  with findings recorded in `SECURITY.md`; and the corelib 1.0 API-freeze
  decision with its deprecation path, recorded in `docs/guides/corelib.md`.

## [0.1.0] — earlier

The pre-1.0 research-prototype release. See `RELEASE_NOTES.md` at the
`v0.1.0` tag for what it contained; it carried no stability guarantees.
