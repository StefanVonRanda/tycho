# FRICTION

The defect log: one entry per place the language, corelib or gates fought back
while writing a real program. A blocker earns a plan instead; this file holds
what did not block.

**Closed entries keep only their heading and their pins.** The narrative of how
each was found and fixed is in git; the last full text is
`git show ba952872:docs/internals/FRICTION.md`. Code comments cite entries by
number (`FRICTION 104`), so a number is never reused or removed.

**Every closed entry carries `> Pinned-by: <shell command>`.** `make
friction-check` (`scripts/friction_check.py`) runs every pin, deduped, and names
the entry whose fix stopped holding; `make friction-check-light` runs only the
grep/`test -f` pins. An entry counts as closed when its heading says FIXED,
CLOSED, GATED, PINNED or DOCUMENTED, or it carries a pin. Write
`> Pinned-by: none -- <reason>` when nothing can assert the claim; an entry with
no pin at all is reported, not failed.

**Anchor new citations.** Write `path@SYMBOL` for a definition or `path:N@token`
for a line; `scripts/check_citations.py` checks both. A bare `path:N` only gets a
bounds check, and it drifts as soon as anything is inserted above it.

## Open

### 91. `make ci`'s wall clock moves ~2x between runs, so a speedup cannot be read off two totals — **OPEN, and it is the MEASUREMENT, not the gate**

On one machine, with nothing changed in between, `make corelib`, `conc`,
`make test`, `ilp32` and `fixpoint-check` all roughly halved from one full sweep
to the next (`corelib` 379 s → 168 s), while `fuzz-leak` stayed flat (168 s →
167 s). So the noise comes from machine state, not from a uniform clock error,
and it is bigger than most optimisations. Compare two versions only **back to
back in one session**, alternating if the runs are long. A per-lane figure that
was not measured that way is an observation, not a measurement. Not
investigated: the cause. Finding it needs pinned frequency, a dropped page cache
and an idle box.

### 97. 34 of the compiler's 335 diagnostics have no pinned wording — **PARTLY CLOSED 2026-09-19**

> Pinned-by: grep -q "^# expect: is package-private" tests/reject/pkg/privacy_cross/main.ty

Reject fixtures assert that a program fails, but an `# expect:` line pinning
*why* is optional. `tests/diag/` pins messages byte-exact. Measured 2026-09-19,
the two together covered 301 of the 335 distinct prose diagnostics the compiler
could build. The method: any 18-character run of the message found in the
pinned corpus. Prefix matching undercounts, because messages are built by
concatenation. The package-private refusal is now pinned. **33 remain.** Each
needs a fixture that reaches it, and some of them (`overlapping mutable access`,
the `sink`-in-a-loop refusals) need a program that trips a rule nothing else in
the corpus exercises.

## Closed

### 1. ~~The one-shot digest is what the language makes natural~~ — **DOWNGRADED 2026-08-18: `core:sha256` streams now**

### 2. ~~The wrong `string` compiles silently, and there is no wrong-looking output~~ — **the mechanism did not reproduce, 2026-08-10**

### 3. ~~`compress.decompress` cannot distinguish empty from corrupt~~ — **CLOSED 2026-08-10**

> Pinned-by: test -f corelib/test/compress/main.ty
> Pinned-by: make corelib

### 3. ~~`compress.decompress` cannot distinguish empty from corrupt~~ — **CLOSED 2026-08-18, by probe**

> Pinned-by: test -f corelib/compress/compress_shim.c
> Pinned-by: make corelib

### 4. ~~`strings.parse_int` fails open, so no format parser can use it~~ — **ALREADY FIXED; the last duplicate is gone 2026-08-10**

> Pinned-by: test -f corelib/test/strings/main.ty
> Pinned-by: make corelib

### ~~4. `strings.parse_int` fails open, so no format parser can use it~~ — **the strict counterpart EXISTS and the ask is paid; one caller was still fail-open and is fixed. Verdict 2026-08-14**

> Pinned-by: test -f corelib/strings/strings.ty
> Pinned-by: make corelib

### ~~5. `bytes` slices clamp, so a slice is not a bounds check~~ — **REPRODUCES, and the clamp is DELIBERATE; the entry's own residual ask was already paid. Verdict recorded 2026-08-13**

### 5. ~~`bytes` slices clamp, so a slice is not a bounds check~~ — **CLOSED 2026-08-18: deliberate, and PINNED**

> Pinned-by: test -f tests/bytes_slice_clamp.ty
> Pinned-by: make test

### ~~6. There is no `eprintln`, and the missing channel removed a feature~~ — **the channel was never missing; `eprint` has shipped since 2026-06-14, 2026-08-11**

> Pinned-by: grep -q '"eprintln"' surface.lock

### ~~7. gzip byte-determinism is real, undocumented, and load-bearing~~ — **entry was right; the guarantee is now written down, 2026-08-11**

> Pinned-by: test -f corelib/compress/compress_shim.c
> Pinned-by: make corelib

### 8. ~~mtime is captured faithfully and cannot be restored~~ — CLOSED 2026-08-11 in `core:io`

> Pinned-by: test -f corelib/io/io_shim.c
> Pinned-by: make corelib

### 1. ~~`core:json` accepts input it cannot represent, three ways, and cannot report any of them~~ — **FIXED 2026-08-01, re-probed 2026-08-11**

> Pinned-by: test -f corelib/json/json.ty
> Pinned-by: make corelib

### 2. ~~`core:decimal` has no `div`, so the ordinary averaging query has no answer~~ — **CLOSED 2026-08-11 (fixed 2026-08-02)**

> Pinned-by: test -f corelib/decimal/decimal.ty
> Pinned-by: make corelib

### 3. ~~`core:sort` has no comparator-taking sort at all~~ — **CLOSED 2026-08-10**

> Pinned-by: test -f corelib/sort/sort.ty
> Pinned-by: make corelib

### 4. ~~`Result(void, E)` is not expressible, and a bare `or_return` is not a statement — one defect, three sightings~~ — **CLOSED 2026-08-09**

> Pinned-by: make test

### 5. ~~An enum cannot be asked which variant it is without binding a payload~~ — **FIXED 2026-08-11: the `is` operator**

> Pinned-by: test -f tests/enum_is.ty
> Pinned-by: make test

### 6. ~~Two error types cannot share an `or_return` chain~~ — **WRONG MECHANISM, corrected 2026-08-11; CLOSED by `result.map_err_with` the same day**

> Pinned-by: make test

### 7. ~~`core:iter` is unusable for a fallible pipeline stage~~ — **FIXED 2026-08-11**, both halves

> Pinned-by: test -f corelib/iter/iter.ty
> Pinned-by: make corelib

### 8. ~~`iter.try_map` has no `Result(void, E)` shape, and says so from inside corelib~~ — **CLOSED 2026-08-12: deliberate; re-probed 2026-08-13**

> Pinned-by: test -f corelib/iter/iter.ty
> Pinned-by: make corelib

### 9. ~~A `string` across the FFI truncates at its first NUL, silently~~ — **verdict upheld and gated 2026-08-12, re-probed 2026-08-13**

> Pinned-by: test -f tests/ffi/main.ty
> Pinned-by: make test

### 10. ~~A two-key comparator cannot be written inline, and the composition that replaces it is invisible~~ — **WITHDRAWN 2026-08-13**

### 10 (original, 2026-08-12). A two-key comparator cannot be written inline, and the composition that replaces it is invisible

### 11. ~~A first `--shim` C file must hand-declare `tycho_int`, because there is no header to include~~ — **WITHDRAWN 2026-08-13**

### 11. ~~A first `--shim` C file must hand-declare `tycho_int`, because there is no header to include~~ — **CLOSED 2026-08-18**

> Pinned-by: test -f corelib/strings/strings_shim.c
> Pinned-by: make corelib

### 12. ~~A `for` binding does not destructure a tuple, and a tuple is not indexable~~ — **all three halves reproduce; the rule stays and the MESSAGE was the defect, fixed 2026-08-13**

> Pinned-by: test -f tests/reject/for_two_binders.ty
> Pinned-by: make test

### 12. ~~A `for` binding does not destructure a tuple, and a tuple is not indexable~~ — **CLOSED 2026-08-18: deliberate, and both halves PINNED**

> Pinned-by: test -f tests/reject/for_two_binders.ty
> Pinned-by: make test

### 13. A package-qualified function was not a value, and the message said the name did not exist — FIXED

> Pinned-by: test -f tests/pkg/fnval/main.ty
> Pinned-by: make test

### 14. ~~`pass` exists and nobody reaches for it~~ — **CLOSED 2026-08-12 by placement**

> Pinned-by: make test

### 15. ~~`or_return` on a `Result(bool, E)` is a three-time papercut~~ — **CLOSED 2026-08-12; the entry's own reasoning was wrong**

> Pinned-by: test -f corelib/test/io/main.ty
> Pinned-by: make corelib

### 16. ~~`parallel for` did not capture a function value — the error escaped to `cc`~~ — **CLOSED 2026-08-12**

> Pinned-by: test -f tests/conc/parfor.ty
> Pinned-by: make test

### 17. `Item($A)` is rejected where `Item($T)` is accepted — **verdict: deliberate and permanent, now documented**

> Pinned-by: make test

### 18. ~~`(int, $T)` does not match a `(int, int)` argument~~ — **CLOSED 2026-08-12**

> Pinned-by: test -f tests/generic_channel.ty
> Pinned-by: make test

### 19. A generic struct's `[fn($T) -> $T]` field escaped to cc — **fixed**

> Pinned-by: test -f tests/generic_fn_array_field.ty
> Pinned-by: make test

### 20. `soa [S($T)]` escaped to cc — **fixed**

> Pinned-by: test -f tests/generic_soa_param.ty
> Pinned-by: make test

### 21. A lambda did not capture a method-call receiver — **fixed**

> Pinned-by: test -f tests/lambda_capture_qual.ty
> Pinned-by: make test

### 22. `str(float)` emitted text the program could not read back — **fixed**

> Pinned-by: test -f tests/float_roundtrip.ty
> Pinned-by: make test

### 23. `strings.parse_float` refused every subnormal — **fixed**

> Pinned-by: make format-diff

### 24. the `sink` consume diagnostic described a rule the compiler does not implement — **fixed**

> Pinned-by: test -f tests/reject/sink_consume_mentions.ty
> Pinned-by: make test

### 25. ~~a keyword used as a field or a variable does not say it is a keyword~~ — **CLOSED 2026-08-13**

> Pinned-by: test -f tests/reject/kw_as_field_name.ty
> Pinned-by: make test

### 26. ~~a foreign enum's variant in a `match` arm is refused as if it did not exist~~ — **FIXED 2026-08-13**

> Pinned-by: test -f tests/enum_bare_variant_local.ty
> Pinned-by: make test

### 27. ~~bounded parallelism over a graph read at runtime has exactly one expressible shape~~ — **WRONG, WITHDRAWN 2026-08-13**

### 28. ~~a struct field cannot name a type declared later in the same file~~ — **FIXED 2026-08-13**

> Pinned-by: test -f tests/reject/struct_mutual_by_value.ty
> Pinned-by: make test

### 29. ~~`cli.parse_spec`'s schema names must be UNDASHED, and getting it wrong is silent~~ — **FIXED 2026-08-13 (`c46952de`); the advisory `unknown()` residue closed 2026-08-14 by `cli.parse_checked`**

> Pinned-by: test -f corelib/cli/cli.ty
> Pinned-by: make corelib

### 30. ~~`[string] + [string]` is refused with a reason that is false~~ — **FIXED 2026-08-13 (`c46952de`), both sites**

> Pinned-by: make test

### 31. ~~A generic struct is constructed from its FIELDS, in field order, not from its type parameters~~ — **RECORDED, not proposed: this is the ordinary struct-literal rule applied to a generic**

### 32. ~~Mangled instantiation names reach the user~~ — **RECORDED, cosmetic; one pass over `type_name`/callee spelling, not a phase**

### 33. ~~`_` is an ordinary variable, not a discard — and it reads like one~~ — **the DIAGNOSTIC is fixed 2026-08-13; the language is unchanged and deliberately so**

> Pinned-by: test -f tests/reject/underscore_not_discard.ty
> Pinned-by: make test

### 34. ~~The obvious `parse_int` is the fail-open one, and the safe one is longer~~ — **RENAMED 2026-10-19: `parse_int` is strict; `parse_int_prefix` is the explicit lax form**

> Pinned-by: test -f corelib/test/strings/main.ty
> Pinned-by: make corelib
> Pinned-by: grep -q 'fn parse_int_prefix(s: string) -> int' corelib/strings/strings.ty
> Pinned-by: grep -q 'fn parse_int(s: string) -> Result(int, IntErr)' corelib/strings/strings.ty

### 35. ~~A `where hashable(K)` constraint does not admit `K` as a map key~~ — **FIXED 2026-08-13: it does now, and the constraint is enforced at instantiation instead**

> Pinned-by: test -f tests/generic_map_key.ty
> Pinned-by: make test

### 36. ~~Two spellings a generic cannot use, and one it cannot infer~~ — **all three resolved 2026-08-13: two spellings fixed, and the third was a FALSE CLAIM — the explicit type-argument form always existed; its diagnostic is what was missing**

> Pinned-by: test -f tests/generic_explicit_typearg.ty
> Pinned-by: make test

### 37. ~~`sink` cannot express a builder~~ — **CLOSED 2026-08-18: deliberate, and PINNED**

> Pinned-by: test -f tests/reject/sink_builder_two_mentions.ty
> Pinned-by: make test

### 38. ~~A variadic called through a package qualifier did not pack~~ — **FIXED 2026-08-14**

> Pinned-by: make test

### 39. ~~Explicit type arguments did not parse on a qualified name~~ — **FIXED 2026-08-14**

> Pinned-by: make test

### 40. ~~An empty generic variadic refused the type it was handed~~ — **FIXED 2026-08-14**

> Pinned-by: make stat-check

### 41. ~~Every imported type and callee printed its MANGLED name~~ — **FIXED 2026-08-14**

> Pinned-by: make test

### 42. ~~`for k in m` reported an index the user never wrote~~ — **FIXED 2026-08-14**

> Pinned-by: make test

### 43. ~~A handle could be COPIED, and the copy double-freed~~ — **FIXED 2026-08-14**

> Pinned-by: make fh-check

### 44. ~~A BARE handle as a struct field was accepted~~ — **FIXED 2026-08-14**

> Pinned-by: test -f tests/reject/affine_handle_container_type.ty
> Pinned-by: make test

### 45. ~~A channel can be copied too, and it makes the compiler's own warning lie~~ — **FIXED 2026-08-14**

> Pinned-by: test -f tests/reject/affine_chan_copy.ty
> Pinned-by: make test

### 49. ~~`sink` and `inout` on an affine type were accepted and silently ignored~~ — **FIXED 2026-08-14**

> Pinned-by: make test

### 50. ~~The spec claimed the `sink` consume rule applied to every type~~ — **SPEC CORRECTED 2026-08-14**

### 46. ~~Prose that merely mentioned the marker deprecated the function below it~~ — **FIXED 2026-08-14**

> Pinned-by: test -f tests/warn/deprecated.ty
> Pinned-by: make test

### 47. ~~A deprecated function taken as a VALUE warned nowhere~~ — **FIXED 2026-08-14**

> Pinned-by: make grid-check

### 48. A subscript cannot read two fields of its receiver, so 2-D indexing on a flat array is inexpressible — **RECORDED, not open**

> Pinned-by: make grid-check

### 51. A generic struct with a `[K: fn($T)->$T]` field emitted an undefined `FnC0` — **FIXED**

> Pinned-by: make test

### 52. `bounded[N]T` in a generic struct silently became `[N]T` — **FIXED**

> Pinned-by: make test

### 53. A generic field or payload bound at an affine type escaped the ban — **FIXED**

> Pinned-by: make test

### 54. `sink $T` bound to an affine type escaped the rule the parse site names — **FIXED**

> Pinned-by: make test

### 55. A generic returning `$T` at a handle DOUBLE FREED — **FIXED**

> Pinned-by: make test

### 56. ~~`decimal.from_str` fails open to a WRONG NUMBER, not to zero~~ — **CLOSED 2026-08-18: the lax name is deprecated**

> Pinned-by: test -f corelib/test/decimal/main.ty
> Pinned-by: make corelib

### 57. `core:http` cannot be pointed at a private CA — **FIXED 2026-08-15**

> Pinned-by: make http-verify

### 58. `uuid.v4` looks like 122 random bits and carries at most 32 — **documented 2026-08-15**

> Pinned-by: grep -q 'NOT unguessable' corelib/uuid/uuid.ty && grep -q 'not unguessable' docs/reference/corelib.md

### 59. A strict check downstream of a lenient parse never runs — **FIXED 2026-08-15**

> Pinned-by: test -f corelib/httpd/httpd.ty
> Pinned-by: make corelib

### 60. `markdown.render` escaped the text and not the SCHEME — **FIXED 2026-08-15**

> Pinned-by: test -f corelib/test/markdown/main.ty
> Pinned-by: make corelib

### 61. `parse(stringify(rows)) == rows` was false for one shape — **FIXED 2026-08-15**

> Pinned-by: make format-diff

### 62. `core:toml` guessed, in both directions — **FIXED 2026-08-15**

> Pinned-by: test -f corelib/test/toml/main.ty
> Pinned-by: make corelib

### `core:markdown` attribute break-out: probed, clean, and now pinned — 2026-08-15

> Pinned-by: test -f corelib/test/markdown/main.ty
> Pinned-by: make corelib

### 71. `core:zip` told the caller there was no traversal hazard — **FIXED 2026-08-15**

> Pinned-by: make ar-check

### 63. A test lane that leaks a server degrades every other lane — **FIXED 2026-08-15**

> Pinned-by: grep -q 'kill -KILL' tools/tycho-kvsrv/run.sh

### 64. `gcd` returned a NEGATIVE gcd where the answer fits — **FIXED 2026-08-15**

> Pinned-by: make math-diff

### 65. `sign()` returned 0 for both infinities — **FIXED 2026-08-15**

> Pinned-by: make math-diff

### 66. `fmath.round` was `floor(x + 0.5)`, which is not rounding — **FIXED 2026-08-15**

> Pinned-by: make math-diff

### 67. `fmath.lerp` did not return its endpoints — **FIXED 2026-08-15**

> Pinned-by: make math-diff

### 68. `pad_left`/`pad_right` overshot on any pad wider than a byte — **FIXED 2026-08-15**

> Pinned-by: test -f corelib/test/strings/main.ty
> Pinned-by: make corelib

### 69. `core:sqlite`'s two halves disagree about a multi-statement string — **exec FIXED, `_params` PINNED, 2026-08-15**

> Pinned-by: make corelib

### 70. A bound parameter was truncated at its first NUL — **FIXED 2026-08-15**

> Pinned-by: test -f corelib/test/sqlite/main.ty
> Pinned-by: make corelib

### 72. The server's traversal defence is two layers and nothing could tell — **GATED 2026-08-15**

> Pinned-by: make traversal-check

### 73. `core:io` holds its contract, including the one nobody wrote down — 2026-08-15

### 74. `core:net` really does distinguish its four cases — 2026-08-15

### 75. A password was truncated at its first NUL, so two credentials derived one key — **FIXED 2026-08-15**

> Pinned-by: sh scripts/crypto_hygiene.sh

### 76. `ct_equal` compared two hex strings by their prefixes — **FIXED 2026-08-15**

> Pinned-by: sh scripts/crypto_hygiene.sh

### 77. The interior-NUL rule was normative, swept once, and still missed the two worst sites — 2026-08-15

> Pinned-by: make corelib

### 78. The last two of the NUL sweep: a URL and a TZ string — **FIXED 2026-08-15**

> Pinned-by: make corelib

### 79. `json.parse_checked`'s error channel had a hole where the attacker is — **FIXED 2026-08-15**

> Pinned-by: make format-diff

### 80. `core:decimal` holds every claim in its header — 2026-08-15

### 81. `csv.stringify` writes a spreadsheet formula verbatim — **DOCUMENTED, not "fixed", 2026-08-15**

> Pinned-by: make format-diff

### 82. `decimal`'s scale is a size SQUARED, and nothing said so — **DOCUMENTED 2026-08-15**

> Pinned-by: sh scripts/bignum_diff.sh

### 83. `bignum`'s division identity holds on every sign combination — 2026-08-15

> Pinned-by: sh scripts/bignum_diff.sh

### 84. `make ci N=0` silently cut a differential to 4.8% of its corpus — **FIXED 2026-08-15**

> Pinned-by: sh scripts/format_diff.sh

### 85. Two things a third party would have hit first — **FIXED 2026-08-15**

> Pinned-by: grep -q 'no private reporting channel configured' SECURITY.md

### 86. A gated timer nobody ran cost 1.7x, and the gate that caught it went unread — **FIXED 2026-08-18**

> Pinned-by: none -- a timing claim, and a gate asserting a timing is a coin toss (the entry says so itself)

### 87. ~~`tychoc` and `tychoc1` report a different number of instantiation frames~~ — **FIXED 2026-09-11: tychoc now walks the chain (the owner's call)**

> Pinned-by: make test

### 88. ~~`make server-check` fails inside `make ci` and passes alone~~ — **FIXED 2026-09-11: the watchdog inherits the cleanup trap and is cancelled with TERM**

> Pinned-by: make server-check

### 89. ~~`make ci` is gcc-only: vector-check's control does not discriminate under clang~~ — **FIXED 2026-09-18: leg [3] picks its form from the measured control (the owner's call)**

> Pinned-by: grep -q 'the vector form does not exceed its control' scripts/vector_check.sh
> Pinned-by: make vector-check

### 90. ~~`shim-warn`'s locked baseline is gcc's: a gcc `-Wunused-function` dodge trips clang's `-Wunused-variable`~~ — **FIXED 2026-09-18: the dodge is now an attribute, and `make ci` runs under clang**

> Pinned-by: grep -q '__attribute__((unused))' corelib/os/os_shim.c
> Pinned-by: make shim-warn
> Pinned-by: make shim-check

### 92. ~~`crypto_hygiene`'s ctgrind control is gcc-shaped: clang if-converts the branch it exists to have~~ — **FIXED 2026-09-18: the control builds at `-O0`, the subject stays at `-O1`**

> Pinned-by: grep -q 'ct_run "$T/ctl" -O0' scripts/crypto_hygiene.sh
> Pinned-by: sh scripts/crypto_hygiene.sh

### 93. ~~Five tool gates advertise a `TYCHOC` override, discard it, and test the BOOTSTRAP compiler~~ — **FIXED 2026-09-18: absolutise the value instead of replacing it**

> Pinned-by: test -z "$(grep -lE "^TYCHOC=.\\$PWD/tychoc." tools/*/run.sh)"
> Pinned-by: make sim-check
> Pinned-by: make sheet-check

### 94. ~~`floor` is a shipped builtin that no fixture exercised~~ — **FIXED 2026-09-19: `tests/float_floor`**

> Pinned-by: test -f tests/float_floor.ty
> Pinned-by: test -f tests/float_floor.out

### 95. ~~Appendix E maps two concurrency clauses to the wrong section numbers~~ — **FIXED 2026-09-19**

> Pinned-by: grep -q "^| §21 | spawn / Task / wait" docs/spec/appendix-e-conformance.md
> Pinned-by: grep -q "^| §22 | parallel-for" docs/spec/appendix-e-conformance.md

### 96. The corelib does not use the visibility control the language already has, so 127 internal helpers are frozen public API — **FIXED 2026-09-19**

> Pinned-by: grep -q '^fn _mag_cmp' corelib/bignum/bignum.ty
> Pinned-by: python3 -c "import json;d=json.load(open('surface.lock'));assert not [k for k in d['corelib'] if '._' in k], 'a package-private name is back in the freeze'"
> Pinned-by: make surface-check

### 98. 34 error-forwarding blocks predate `or_return`; the other 42 want a form that does not exist — **FIXED 2026-09-25: `or_else`**

> Pinned-by: grep -q '"or_else"' surface.lock
> Pinned-by: test -f tests/or_else.ty

### 99. The corelib reference told readers `core:net` cannot poll, and it can — **FIXED 2026-09-19**

> Pinned-by: grep -q "wait_readable(fds, ms)" docs/reference/corelib.md
> Pinned-by: sh -c '! grep -q "no .poll., .select., .epoll. or .O_NONBLOCK. anywhere" docs/reference/corelib.md'
> Pinned-by: make net-poll-check
> Pinned-by: python3 scripts/corelib_doc_check.py

### 100. `docs-fences`' verdict depended on whether some OTHER lane had built `./tycho` — **FIXED 2026-09-19**

> Pinned-by: grep -qE '^docs-fences: tychoc1 tycho$' Makefile

### 101. Every Tycho server refuses connections under a burst on macOS, because the listen backlog is a literal 16 — **FIXED 2026-09-19**

> Pinned-by: grep -q 'listen((int)fd, SOMAXCONN)' corelib/net/net_shim.c

### 102. The loopback-split fix was applied to one of the two sites that needed it, and the other one ABORTS — **FIXED 2026-09-19**

> Pinned-by: sh -c 'grep -q "chunk2 := to_str(data_of(net.read(cli2, 4096)))" corelib/test/httpd/main.ty'

### 103. Two claims about this project's own state that no gate can read — **FIXED 2026-09-19**

> Pinned-by: sh -c '! grep -q "There is no Darwin or ARM machine here and no qemu" ROADMAP.md'
> Pinned-by: sh -c '! grep -q "102 entries; 93 closed" STATUS.md'

### 104. The executable defect log is executed by nobody — `make ci` has never called `friction-check` — **FIXED 2026-09-19: option (1), the fact pins, as lane `[1d2/13]`**

> Pinned-by: grep -q 'make -s friction-check-light' scripts/ci.sh
> Pinned-by: grep -q '^friction-check-light:' Makefile
> Pinned-by: python3 scripts/friction_check.py --selfcheck

### 105. A backtick in a `ci.sh` step banner is a COMMAND SUBSTITUTION, and two of them were live — **FIXED 2026-09-19**

> Pinned-by: python3 scripts/check_scripts.py
> Pinned-by: python3 scripts/check_scripts.py --selfcheck

### 106. `parse-check` is RED, has been all day, and is in neither `make ci` nor the hook — **DRIFT CLEARED and `corpus-check` GATED 2026-09-19; the divergence CLOSED 2026-09-22; the lane's absence from the sweep is still the owner's call**

> Pinned-by: grep -q 'make -s corpus-check' scripts/ci.sh
> Pinned-by: make corpus-check

### 107. `strings.len(...)` sent the reader looking for a symbol that was never going to exist — **FIXED 2026-09-19**

> Pinned-by: sh scripts/builtin_qualified.sh

### 108. A retry that never waited: 50 iterations in 0 ms — **FIXED 2026-09-19**

> Pinned-by: grep -q 'time.elapsed_ms(sw) >= 5000' corelib/test/net/main.ty
> Pinned-by: sh -c '! grep -qE "^ +for t := 0; t < 50; t \\+= 1:" corelib/test/net/main.ty'

### 109. The macOS artifact would have shipped unexamined — **FIXED 2026-09-19: `darwin-arm64` built, reproducible, and content-gated**

> Pinned-by: grep -q 'check_native_darwin' scripts/release_content.sh
> Pinned-by: sh -c 'grep -q "mingw_skip=" scripts/release_content.sh'
> Pinned-by: sh scripts/release_content.sh --selfcheck

### 110. aarch64 Linux was compile-only for a month; a VM on the Mac closed it in an hour, and caught me repeating 100 — **FIXED 2026-09-19**

> Pinned-by: grep -q '^builtin-qualified: tychoc tychoc1' Makefile
> Pinned-by: make builtin-qualified

### 111. `make test` has an undocumented OS prerequisite, and fails loudly without it — 2026-09-19

> Pinned-by: grep -q 'locale-gen' README.md

### 112. The C compiler decided how many times to round, and the answer changed with the architecture — **FIXED 2026-09-19**

> Pinned-by: grep -q 'ffp-contract=off' compiler/driver/driver.ty

### 113. `x86_64-macos` was the last compile-only target — 2026-09-19

> Pinned-by: none -- the claim is that binaries RAN on a host; a lane cannot
> assert an architecture this repo's gate does not run on. Recorded with its
> method so it can be repeated.

### 114. Four conc fixtures SIGILL under TSan on aarch64 — **ROOT-CAUSED 2026-09-19: TSan's shadow layout, not Tycho**

> Pinned-by: none -- the subject is a sanitizer's behaviour on one host, which no
> lane in this tree can assert. Recorded with its reproduction and its evidence.

### 115. The fail-open audit reached three parsers and missed a fourth, on the untrusted side — **FIXED 2026-09-19**

> Pinned-by: make weblog
> Pinned-by: grep -q 'strings.parse_int(bytes_s)' examples/weblog/main.ty
> Pinned-by: sh -c '! grep -q "strings.parse_int_prefix(bytes_s)" examples/weblog/main.ty'

### 116. A plausible typo in a post's date rendered as 1970 and sorted first — **FIXED 2026-09-19**

> Pinned-by: make site
> Pinned-by: grep -q 'strings.parse_int(draw)' examples/site/main.ty

### 117. The NUL sweep reached six packages and missed the one where the string is a security decision — **FIXED 2026-09-19**

> Pinned-by: make tls-verify
> Pinned-by: grep -q '_has_nul(host)' corelib/tls/tls.ty

### 118. The catalogue gate reported 431/431 by matching substrings; 11 were never named — **FIXED 2026-09-19**

> Pinned-by: python3 scripts/corelib_doc_check.py --selfcheck
> Pinned-by: grep -q 'def named(name, cat)' scripts/corelib_doc_check.py
> Pinned-by: python3 scripts/corelib_doc_check.py

### 119. The normative conformance table cites five "lanes" that nothing ran — **FIXED for three, MEASURED for two**

> Pinned-by: grep -q 'make -s parity-fuzz' scripts/ci.sh
> Pinned-by: make parity-fuzz

### 120. Appendix E's sub-case citations were unchecked — **GATED 2026-09-19, and no defect found**

> Pinned-by: make spec-fast
> Pinned-by: grep -q 'Appendix E sub-case citation' scripts/spec_check.sh

### 121. A wavefront proved by a stopwatch: `make-check`'s race leg flaked a second time — **FIXED 2026-09-19**

> Pinned-by: make make-check
> Pinned-by: grep -q '_race_attempts=3' tools/tycho-make/run.sh

### 122. The probe watchdog scored every macOS probe as finished — **FIXED 2026-09-21**

> Pinned-by: grep -q 'ps -o time=' scripts/probe_run.sh
> Pinned-by: sh scripts/probe_run.sh --selfcheck

### 123. An opener's result that is never bound was never freed — **FIXED 2026-09-21**

> Pinned-by: test -f tests/reject/handle_unbound_stmt.ty
> Pinned-by: test -f tests/reject/handle_unbound_arg.ty
> Pinned-by: grep -q 'hdl_decl' compiler/types/tcheck.ty
> Pinned-by: make test

### 124. The over-wide vector lint named an ISA the host is not, and prescribed a flag `cc` rejects — **FIXED 2026-09-21**

> Pinned-by: grep -q 'use a narrower vector, or on x86-64 build' compiler/types/zsema.ty
> Pinned-by: make vector-check

### 125. Three things the handle documentation said that are not true — **FIXED 2026-09-21**

> Pinned-by: grep -q 'header over an indented' docs/spec/14-ffi.md
> Pinned-by: grep -q 'handle -> bool' docs/reference/builtins.md
> Pinned-by: grep -q 'tycho-fh' docs/reference/ffi.md
> Pinned-by: make docs-fences

### 126. Nothing ever checked that a handle's `free:` names its destructor — **FIXED 2026-09-21**

> Pinned-by: test -f tests/reject/handle_free_undeclared.ty
> Pinned-by: test -f tests/reject/handle_free_wrong_type.ty
> Pinned-by: make test

### 127. A probe measured its own transcription error and filed it as a layout defect — **CLOSED 2026-09-22: refuted by measurement**

> Pinned-by: grep -q '__attribute__((packed))' compiler/emit/*.ty
> Pinned-by: make test

### 128. The residue: fourteen findings from 2026-09-21 that nothing has acted on — **FIXED 2026-09-24**

> Pinned-by: grep -q 'The strip also guarantees one class of false finding' docs/internals/probe-procedure.md
> Pinned-by: grep -q 'Write down which model ran' docs/internals/probe-procedure.md

### 129. Every user's build drew two `-Wunused-value` warnings out of `core:strings` — **FIXED 2026-09-24**

> Pinned-by: ! ( ./tychoc --emit-c tests/pkg/vendor_deps/main.ty | grep '= h_strings___slice_ok(' | grep -qv '((void)0); })' )

### 130. A use after `close(h)` compiled, and `is_null` could not tell closed from never opened — **FIXED 2026-09-24**

> Pinned-by: ./tychoc --emit-c -o /dev/null tests/reject/handle_use_after_close.ty 2>&1 | grep -q 'is used after'
> Pinned-by: ./tychoc1 --typecheck tests/reject/handle_close_twice.ty 2>&1 | grep -q 'is used after'
> Pinned-by: sh tests/ffi/run.sh
> Pinned-by: ./tychoc1 --typecheck tests/reject/handle_close_param.ty 2>&1 | grep -q 'is a handle parameter'

### 131. `tychoc` could not build an inline array reassigned from a call inside a loop — **FIXED 2026-09-24**

> Pinned-by: make -s grade-check

### 132. `math.clamp` refused a vector, so a grading kernel's clamp left the vector unit — **FIXED 2026-09-24**

> Pinned-by: make -s vector-check
> Pinned-by: make -s grade-check

### 133. Inferred-local placeholders collided at a decimal prefix — **FIXED 2026-09-26**

> Pinned-by: make -s test

### 134. Generic constructor resolution retained pointers across type-table growth — **FIXED 2026-09-26**

> Pinned-by: make -s test

### 135. `tychoc1` left sized and element-wise integer division unguarded — **FIXED 2026-09-26**

> Pinned-by: make -s test

### 136. Empty literals slipped through fixed-array arity checks — **FIXED 2026-09-26**

> Pinned-by: make -s test

### 137. Const-sized arrays lost their capacity in the self-hosted checker and emitter — **FIXED 2026-09-26**

> Pinned-by: make -s test

### 138. If/elif chains were nested and quadratically emitted — **FIXED 2026-09-27**

> Pinned-by: sh tests/elif_chain.sh

### 139. Native C emission nested every elif arm — **FIXED 2026-09-27**

> Pinned-by: sh tests/elif_chain.sh

### 140. Builtin shadowing and self-named initializers miscompiled — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 141. Native source loading assumed seekable files — **FIXED 2026-09-27**

> Pinned-by: sh tests/stream_source.sh

### 142. Literal fit checks were skipped by several coercion paths — **FIXED 2026-09-27**

> Pinned-by: make parse-check; TYCHO_THREADS=8 make -s test

### 143. Place stores formed stale addresses before an inout RHS could grow the container — **FIXED 2026-09-27**

> Pinned-by: sh tests/place_store_order.sh

### 144. `tychoc1 -g` emitted unescaped source paths in `#line` directives — **FIXED 2026-09-27**

> Pinned-by: sh tests/debug_path.sh

### 145. `program_in` retained every growing struct-definition prefix — **FIXED 2026-09-27**

> Pinned-by: sh tests/struct_defs_scale.sh

### 146. Deep integer sums and interpolated strings overflowed compiler/C nesting — **FIXED 2026-09-27**

> Pinned-by: sh tests/expression_scale.sh

### 147. Typed tuple literals used synthesized element types in C — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 148. The self-hosted emitter accepted `pow` and SoA loops in typechecking but had no lowering — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 149. F-string holes ignored trailing syntax and rejected surrounding whitespace — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 150. Sized float conversions differed from the total wrapping contract — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 151. Equal float leaves in composite map keys hashed differently — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 152. Self-hosted composite map-key validation admitted un-emittable leaves — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 153. Large unsigned shift counts were misread as negative — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 154. Synthesized locals could shadow user bindings — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 155. Resolver recursion depth leaked across recovered diagnostics — **FIXED 2026-09-27**

> Pinned-by: sh tests/depth_recovery.sh

### 156. Enum variant spellings leaked into generated C identifiers — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 157. Deep non-addition binary spines could exhaust the self-hosted typechecker — **FIXED 2026-09-27**

> Pinned-by: sh tests/expression_scale.sh

### 158. Double underscores in source type names were mistaken for compiler mangling — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 159. Large constant array literals gave gcc one store per element to optimize — **FIXED 2026-09-27**

> Pinned-by: sh tests/array_literal_scale.sh

### 160. F-string C joins and lambda scope snapshots grew quadratically — **FIXED 2026-09-27**

> Pinned-by: sh tests/emitter_memory_scale.sh

### 161. Packed fields passed to `inout` were dereferenced at natural alignment — **FIXED 2026-09-27**

> Pinned-by: make -s test
> Pinned-by: sh tests/packed_inout_ffi.sh

### 162. The debug gate accepted only one GDB spelling for a missing line — **FIXED 2026-09-27**

> Pinned-by: make -s debug-check

### 163. Native generic mismatch diagnostics exposed package-mangled names — **FIXED 2026-09-27**

> Pinned-by: sh tests/package_diagnostic_name.sh

### 164. C-string shims silently discarded bytes after an embedded NUL — **FIXED 2026-09-27**

> Pinned-by: make -s corelib

### 165. Compression truncated zlib lengths and under-enforced its output ceiling — **FIXED 2026-09-27**

> Pinned-by: make -s corelib
> Pinned-by: sh tests/compress_limits.sh

### 166. POSIX timezone offsets accumulated unbounded decimal fields — **FIXED 2026-09-27**

> Pinned-by: make -s corelib

### 167. Handle destructors could return aggregate values that require hidden out-parameters — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 168. A checkpoint could truncate the only committed store before its replacement was durable — **FIXED 2026-09-27**

> Pinned-by: make -s db-check

### 169. A mutation remained visible after its WAL append was refused — **FIXED 2026-09-27**

> Pinned-by: make -s db-check

### 170. Separate tycho-db processes could overwrite one another's commits — **FIXED 2026-09-27**

> Pinned-by: make -s db-check

### 171. Store decoding trusted row shape and integer encodings — **FIXED 2026-09-27**

> Pinned-by: make -s db-check

### 172. A corrupt store plus an empty WAL could be rebuilt and overwritten — **FIXED 2026-09-27**

> Pinned-by: make -s db-check

### 173. A silent tycho-db client could monopolize the serial server — **FIXED 2026-09-27**

> Pinned-by: make -s db-check

### 174. An indexed cross-type comparison returned no rows instead of an error — **FIXED 2026-09-27**

> Pinned-by: make -s db-check

### 175. Sorted-array KEY index maintenance copied every shifted entry — **FIXED 2026-09-27**

> Pinned-by: make -s db-check

### 176. `recv(ch)` returned a payload allocated in the callee's freed scope — **FIXED 2026-09-27**

> Pinned-by: make -s test
> Pinned-by: sh tests/recv_loop_scale.sh

### 177. `bytes` and scoped string accumulators bypassed self-hosted in-place append — **FIXED 2026-09-27**

> Pinned-by: make -s test
> Pinned-by: sh tests/accumulator_scope.sh

### 178. Several untrusted sizes reached allocation or narrowing operations unchecked — **FIXED 2026-09-27**

> Pinned-by: make -s test
> Pinned-by: sh tests/audit_runtime_guards.sh
> Pinned-by: sh tests/http_header_guard.sh

### 179. TLS timeout wrappers existed only as package-private helpers — **FIXED 2026-09-27**

> Pinned-by: make -s corelib

### 180. HTTP parsing conflated absent fields, malformed framing and malformed request grammar — **FIXED 2026-09-27**

> Pinned-by: make -s server-check

### 181. The FFI integer table and SQLite example contradicted the compiler — **FIXED 2026-09-27**

> Pinned-by: make -s docs-fences
> Pinned-by: make -s corelib-doc-check

### 182. TOML table placement discarded implicit parents and array-of-table children — **FIXED 2026-09-27**

> Pinned-by: make -s corelib

### 183. TOML parsing had unbounded recursion and quadratic table reconstruction — **FIXED 2026-09-27**

> Pinned-by: make -s corelib

### 184. ZIP creation truncated names and counts into classic-format fields — **FIXED 2026-09-27**

> Pinned-by: make -s corelib
> Pinned-by: make -s snap-check

### 185. TOML dotted keys split inside quotes and malformed strings became valid values — **FIXED 2026-09-27**

> Pinned-by: make -s corelib

### 186. JSON object parsing rescanned all prior keys for every member — **FIXED 2026-09-27**

> Pinned-by: make -s corelib

### 187. Markdown rescanned the same found `]` for every unmatched `[` — **FIXED 2026-09-27**

> Pinned-by: make -s corelib

### 188. ZIP extraction rescanned the central directory and unbounded file suffix — **FIXED 2026-09-27**

> Pinned-by: make -s corelib
> Pinned-by: make -s snap-check

### 189. Option reference claimed a bare `None` comparison was valid — **FIXED 2026-09-27**

> Pinned-by: make -s parse-check
> Pinned-by: make -s docs-fences

### 190. Self-hosted builtin-collision advice mislabeled subpackages as `main` — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 191. Self-hosted emitter could not construct generic enum variants through a package qualifier — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 192. Self-hosted loops retained overwritten strings and variadic packs in `_scope` — **FIXED 2026-09-27**

> Pinned-by: make -s test
> Pinned-by: sh tests/loop_temp_recycle.sh

### 193. Replacing a heap value in a long-lived map abandoned its owned graph — **FIXED 2026-09-27**

> Pinned-by: make -s test
> Pinned-by: sh tests/map_overwrite_scale.sh

### 194. HTTP request-budget overflow mapped a declared oversized body to 431 — **FIXED 2026-09-27**

> Pinned-by: make -s corelib
> Pinned-by: make -s server-check

### 195. `resolve_under` treated a dangling symlink as an ordinary missing leaf — **FIXED 2026-09-27**

> Pinned-by: make -s server-check

### 196. Package suggestions exposed names that the next compile rejects as private — **FIXED 2026-09-27**

> Pinned-by: make -s test

### 197. Compiler CLIs accepted empty output names, swallowed linker paths and indexed invalid programs — **FIXED 2026-09-27**

> Pinned-by: make -s test
> Pinned-by: sh tests/cli_flags.sh

### 198. Native procedure-signature lookup scaled quadratically — **FIXED 2026-09-29**

> Pinned-by: sh tests/proc_signature_scale.sh
> Pinned-by: sh tests/name_registry_scale.sh

### 199. Same-arena copies from replaceable string slots aliased recycled buffers — **FIXED 2026-09-27**

> Pinned-by: make -s test
> Pinned-by: sh tests/run.sh --one tests/elem_recycle.ty

### 200. Nested generic calls and constructor payloads repeated subtree work — **FIXED 2026-09-29**

> Pinned-by: sh tests/emitter_nested_calls.sh

### 201. Native constant, variant, generic and warning-location scans scaled with all prior input — **FIXED 2026-09-29**

> Pinned-by: sh tests/name_registry_scale.sh
> Pinned-by: sh tests/proc_signature_scale.sh

### 202. Native codegen refused deeply nested loops that the self-hosted compiler accepted — **FIXED 2026-09-30**

> Pinned-by: sh tests/nested_blocks_scale.sh

### 203. Package-local builtin names disagreed between compilers and produced false collision advice — **FIXED 2026-09-30**

> Pinned-by: sh tests/package_builtin_shadow.sh

### 204. A missing composite map read segfaulted instead of returning the zero — **FIXED 2026-10-18**

> Pinned-by: sh tests/run.sh --one tests/map_missing_composite.ty

### 205. Self-hosted inout through an array-typed subscript grew into a freed scope, and subscript writeback neither copied nor recycled — **FIXED 2026-10-18**

> Pinned-by: sh tests/run.sh --one tests/subscript.ty

### 206. Self-hosted push/pop/reserve/index-assign through a subscript accepted what the reference refuses — **FIXED 2026-10-18**

> Pinned-by: test -f tests/reject/push_subscript_place.ty
> Pinned-by: test -f tests/reject/pop_subscript_place.ty
> Pinned-by: test -f tests/reject/reserve_subscript_place.ty
> Pinned-by: test -f tests/reject/index_subscript_place.ty

### 207. Every `core:sort` sort except `sort_by`, and `arrays.sort`, was insertion sort, with nothing at the call site saying so — **FIXED 2026-11-23**

> Pinned-by: sh -c '! grep -q "for j >= 0 and" corelib/sort/sort.ty corelib/arrays/arrays.ty'
> Pinned-by: grep -q "^ref=0$" corelib/test/sort.out

### 208. A call spelled with a keyword (`select(x)`) said only "expected an expression" — **FIXED 2026-11-23**

> Pinned-by: test -f tests/diag/call_reserved_keyword.err

### 209. `return` in a lambda body, or a lambda body on the next line, said only "expected an expression" — **FIXED 2026-11-23**

> Pinned-by: test -f tests/diag/lambda_return.err
> Pinned-by: test -f tests/diag/lambda_block_body.err

### 210. Python's `a if c else b` said "expected newline, found kw 'if'" and never pointed at the value `if` — **FIXED 2026-11-23**

> Pinned-by: test -f tests/diag/inline_ternary.err

### 211. No public float <-> bit-pattern conversion; binary formats decoded IEEE-754 by hand — **FIXED 2026-11-23**

> Pinned-by: grep -q "^roundtrip=0$" corelib/test/bits.out

### 212. `core:gfx` folded mouse press and release, logical and pixel resize, and WASD with the arrows into the same events — **FIXED 2026-11-23**

> Pinned-by: sh tests/gfx_events.sh

### 213. An extern could not take `[f32]` or `[u32]`, so GPU and audio data went through `[float]` and a per-element conversion — **FIXED 2026-11-23**

> Pinned-by: grep -q "^sized f32sum=1.75 u32last=4000000000 u8sum=300 empty=-1$" tests/ffi/expected.out

### 214. `arrays.fill` grew by `push` from empty, peaking at 4x its result (1.3 GiB for a 305 MiB fill) — **FIXED 2026-11-23**

> Pinned-by: grep -q "reserve(out, n)" corelib/arrays/arrays.ty

### 215. `core:gfx` windows could not be resized, though every gfx program handles resize events — **FIXED 2026-11-23**

> Pinned-by: grep -q "SDL_CreateWindow(title, g->w, g->h, SDL_WINDOW_RESIZABLE)" corelib/gfx/gfx_shim.c

### 216. `core:gfx` had no mouse-wheel event, so scroll-to-zoom could not be written — **FIXED 2026-11-23**

> Pinned-by: sh tests/gfx_events.sh

### 217. `to_float` refused a `float` argument even inside a generic body, where `$T` is float in one instance and not in another — **FIXED 2026-11-23**

> Pinned-by: grep -q "already := a0 == \"float\" and cs.instdepth > 0" compiler/types/tcheck.ty
> Pinned-by: grep -q "^vdotf=true vlenf=true$" corelib/test/linalg.out

### 218. `core:linalg` modelled vectors as structs with per-width functions, steering programs away from the built-in `vector[N]T` — **FIXED 2026-11-23**

> Pinned-by: grep -q "^vcross=true$" corelib/test/linalg.out

### 219. A generic could not take `vector[$N]$T`, so vector code could not be written once for every width and lane type — **FIXED 2026-11-23**

> Pinned-by: grep -q "^fn dot(a: vector\[\$N\]\$T, b: vector\[\$N\]\$T) -> \$T:" corelib/linalg/linalg.ty
> Pinned-by: grep -q "^vdotf=true vlenf=true$" corelib/test/linalg.out

### 220. A swizzle read was only a tuple, so a vector cross product had to be written lane by lane — **FIXED 2026-11-23**

> Pinned-by: grep -q "^swizzle \[0.0, 0.0, 1.0, 0.0\]" tests/vector_ergonomics.out
> Pinned-by: test -f tests/reject/swizzle_lane_range.ty

### 221. Unary `-` was refused on vectors, `f32` and the signed sized ints — **FIXED 2026-11-23**

> Pinned-by: grep -q "^neg \[-1.0, 2.0, -3.0, -0.0\]" tests/vector_ergonomics.out
> Pinned-by: test -f tests/reject/neg_unsigned.ty

### 222. A `float` scalar variable could not scale an `f32` vector without `to_f32` at every use — **FIXED 2026-11-23**

> Pinned-by: grep -q "^adapt \[0.5, 1.0, 1.5, 0.0\]" tests/vector_ergonomics.out

### 223. `f32` never widened to `float`, so every lane read into `float` maths needed `to_float` — **FIXED 2026-11-23**

> Pinned-by: grep -q "^widen 3.0 0.0 true 3.0 0.75" tests/vector_ergonomics.out
> Pinned-by: test -f tests/reject/f32_widen_inout.ty
> Pinned-by: test -f tests/reject/f32_widen_array.ty

### 224. A vector could not be filled from a scalar or built from `float` variables without a conversion per lane — **FIXED 2026-11-23**

> Pinned-by: grep -q "^fill \[1.5, 2.0, 3.0, 0.0\] \[0.0, 0.0, 0.0, 0.0\]" tests/vector_ergonomics.out

### 225. There was no sequential counting loop: every count was `for i := 0; i < n; i += 1:` — **FIXED 2026-11-23**

> Pinned-by: grep -q "^80 9 0$" tests/for_range.out

### 226. A bool could not become a number, so every flag handed to a shader took three lines — **FIXED 2026-11-23**

> Pinned-by: grep -q "^bool=1.0 0$" tests/floats.out

### 227. A packed struct could not hold a vector, so a file record spelled every lane as its own field — **FIXED 2026-11-24**

> Pinned-by: grep -q "^33 66 true$" tests/packed_array_bytes.out

### 228. Only `[int]` (as bytes) and arrays of packed structs crossed the bytes bridge, so a `[f32]` needed a one-field wrapper struct and a copy loop each way — **FIXED 2026-11-24**

> Pinned-by: grep -q "^8 0 0 192 63$" tests/packed_array_bytes.out

### 229. `core:buffer` kept one byte per `int`, and `push([int], bytes)` in a loop silently kept nothing — **FIXED 2026-11-24**

> Pinned-by: grep -q "^3000 122 400 1 122$" tests/push_bytes.out
> Pinned-by: grep -q "^1008 65 0 254 113 1008$" tests/push_bytes.out
> Pinned-by: grep -q "data: \[u8\]" corelib/buffer/buffer.ty

### 230. There was no in-place append of one array to another, so building an array from parts was element loops or a quadratic `concat` — **FIXED 2026-11-24**

> Pinned-by: grep -q "^extend=1,2,3,4 big=2000 999 names=a,b,c$" corelib/test/arrays.out
> Pinned-by: grep -q "^\[7, 0, 1, 2, 3, 4\] \[7, 0, 1, 2, 99\] \[0, 1, 10, 11, 20, 21\] three \[7, 0, 1, 2, 3\]$" tests/push_bytes.out

### 231. `sort.asc` was 1.5x slower than `sort_by` with a closure, and every sort copied its buffer back after each pass — **FIXED 2026-11-24**

The meshlet viewer's Morton sort (7.2M packed ints) used `sort_by(xs, fn(a, b) -> int: a - b)`
rather than the obvious `sort.asc(xs)`, because `asc` was the slower one: it sorted an index
permutation and gathered through it (21.7M random ints: `asc` 2.9 s, `sort_by` 1.9 s, C `qsort`
1.7 s). `asc`/`desc` now merge the values directly, making the same comparisons in the same
order (so the result is unchanged, NaN and `-0.0` placement included), and every merge pass
alternates buffers instead of copying back: `asc` 1.46 s, `sort_by` 1.75 s.

> Pinned-by: grep -q "^zeros=\[-1.0, 0.0, -0.0, -0.0, 0.0, 0.0, 1.0\]" corelib/test/sort.out

### 232. f32 widened to float in arithmetic but not in an array literal or at a generic `$T` — **FIXED 2026-11-24**

FRICTION 223 widened `f32` to `float` in operations, declarations and by-value `float`
parameters. Two places it stopped short: `[x, v.y]` with `x: float` was "array elements must
all have the same type", and `math.min(x, v.y)` was "argument 2 of 'min' is f32, which does
not fit the parameter pattern". Both now widen (`[float]`, `T = float`). The emitter had to
agree: it took a literal's element type from element 0 and bound `$T` from the first
argument, so `[v.y, x]` and `min(v.y, x)` would have narrowed `x` to `f32`.

> Pinned-by: grep -q "^\[0.1, 2.5\] \[2.5, 0.1\] true$" tests/f32_widen_mixed.out
> Pinned-by: test -f tests/reject/f32_widen_generic_inout.ty

### 233. The spec said `parallel for` deep-copies every capture into each chunk; the implementation shares them — **FIXED 2026-11-24** (spec)

The meshlet viewer captured a 300 MB quadric array and a 170 MB index array into a `parallel
for` and paid nothing per chunk: each chunk receives the capture's handle, as it always had.
Sharing is unobservable -- the body cannot write an outer variable, a function parameter is
read-only, the enclosing scope waits for every chunk, and a copy the body makes is its own --
so the spec (13 §22) and the concurrency reference now say shared, which is the guarantee a
program that captures large data relies on.

> Pinned-by: grep -q "Captured values are \*\*shared, not copied\*\*" docs/spec/13-concurrency.md
> Pinned-by: grep -q " 4320 64$" tests/conc/parfor.out

### 234. `TYCHO_ARENA_STATS` made a `parallel for` up to 80x slower, so any timing taken with it was wrong — **FIXED 2026-11-24**

Every allocation incremented shared atomics (a compare-and-swap loop for the peak, a per-label
block), and every scope exit read the clock twice, even for an empty scratch arena. From 15
threads the shared cache lines serialised the program: a small-allocation `parallel for` ran
25 ms with stats off and 2029 ms with them on, and the meshlet viewer's DAG build read 10.7 s
instead of 4.2 s. Each thread now counts into its own block and publishes at task end; live
bytes are published at once when a thread's own live bytes reach a new high and otherwise
every 4 KiB of change; an empty arena's teardown is counted but not timed. 2029 ms -> 65 ms,
the DAG build 2.29 s against 2.14 s off. Totals and per-function counts stay exact; a peak is
exact in one thread and off by under 4 KiB per other thread across several.

> Pinned-by: sh tests/arena_stats_threads.sh
> Pinned-by: grep -q "static __thread StTl st_tl;" runtime/tycho_rt.c
