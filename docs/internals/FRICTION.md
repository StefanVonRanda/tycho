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

### 235. `comparable(T)` admitted `int`, `char`, `float` and `string` only, so an `f32` or `u8` could not be sorted or passed to `math.max` — **FIXED 2026-11-24**

Found while fixing 232: `math.max(b, b)` with `b: f32` was "'max' instantiated with T = f32,
which does not satisfy `comparable(T)`", and so was `sort.asc` over a `[f32]` of depths or a
`[u8]` of bytes -- although `<` on two f32s or two u8s was always legal. `comparable` now also
admits the sized numerics `u8`…`u64`, `i8`…`i64`, `f32`.

> Pinned-by: grep -q "^sized=\[-1.0, -0.0, 0.0, 2.25, 3.5\] \[255, 200, 17, 3, 0\] 0,2,1,3$" corelib/test/sort.out

### 236. A loop calling a closure reset its scratch arena through four reloaded fields per iteration — **FIXED 2026-11-24**

`sort_by`'s merge loop passes its scratch arena to the compare closure, so after every call the
reset's early-out reloaded `head`, `bkt`, `freelist` and the stats flag: 16% of the loop in a
hand-edited measurement. `arena_recycle` now refuses an arena with no block (it cannot own the
buffer), which makes `head == NULL` imply no table and no free list, and the reset's early-out
is that one load. `sort_by` on 21.7M ints: ~2.06 s -> ~1.94 s.

> Pinned-by: grep -q "|| !a->head) return;" runtime/tycho_rt.c

### 237. `hash.crc32` ran bit by bit at 160 MB/s, took only a `string`, and `core:zip` had a second copy — **FIXED 2026-11-24**

Urani checks a pack file's CRC on every read, as Pandora did. Tycho's CRC-32 processed one bit
per step (64 MB in 398 ms; zlib does it in 2 ms), accepted only a `string` (so `bytes` paid a
copy through `to_str`), had no way to continue a CRC across chunks, and `core:zip` carried its own
identical bit-by-bit copy over `bytes`. Tycho has no global tables, so a fast table cannot live
in Tycho; `core:hash` now has a C shim: slicing-by-8 with tables built before `main`, or the
ARMv8 CRC32 instructions where the compiler targets them (same polynomial, same result).
`crc32_bytes(b)` and `crc32_update(crc, b)` are new; `zip.crc32` delegates. 64 MB: 5 ms.

> Pinned-by: grep -q "^crc update = cbf43926$" corelib/test/hash.out
> Pinned-by: test -f corelib/hash/hash_shim.c

### 238. A program could not find its own file, so data kept beside the binary was found only from its own directory — **FIXED 2026-11-24**

Urani boots from a base pack that sits beside its binary. The only handle was `args()[0]`, the
name the program was started by: run as `./urani` it worked, run as `urani` from `PATH` the
directory was `.` and the pack was not found. `os.exe_path()` returns the program's own file,
absolute, links resolved (`_NSGetExecutablePath` + `realpath`, `/proc/self/exe`,
`GetModuleFileNameA`), or `None` where the platform cannot say.

> Pinned-by: grep -q "^exe_path_absolute_and_ours=true$" corelib/test/os.out

### 239. `core:cli` kept every value of a repeated option but offered only the last — **FIXED 2026-11-24**

`--pak a.pak --pak b.pak` stacks layers in Urani, in order. `cli.get` returns the last value
("last one wins"), so the caller had to walk the `Cli` struct's parallel `keys`/`vals` fields by
hand. `cli.get_all(c, key)` returns them all, in order.

> Pinned-by: grep -q "^get_all    = \[a.pak, b.pak\] \[d\] 0 b.pak$" corelib/test/cli.out

### 240. A `const` did not adapt like its literal, so every constant describing a sized field needed a conversion per use — **FIXED 2026-11-24**

Urani's platform events are a packed struct of `i32` fields returned over the FFI, with the
event kinds as constants. `ev.kind == 1` compiled; `ev.kind == EV_QUIT` was "cannot compare i32
with int", as were `x: i32 = QUIT` and `take_u8(FLAG)`. There is no typed `const`
(`const X: i32 = 1` is a syntax error), so each use needed `to_int(ev.kind)` or a `to_i32`. The
name of an int or float `const` now adapts exactly as the literal it names: to a sized int (range
checked), to `f32`, to `float`, plain or qualified (`pkg.NAME`). A variable of the same name
still shadows it.

> Pinned-by: grep -q "^true true true true$" tests/const_adapt.out
> Pinned-by: test -f tests/reject/const_adapt_range.ty
> Pinned-by: grep -q "^const qualified = 128 true$" corelib/test/cbor.out

### 241. A program could not ask how much memory it holds — **FIXED 2026-11-24**

Urani's memory accountant puts one counter over CPU and GPU bytes. The GPU side the engine
counts itself; the CPU side had no source: `TYCHO_ARENA_STATS` prints arena use at exit for a
person, not to the program, and the arenas are not all the process holds (SDL, the shader
translator and the driver allocate too). `os.memory_bytes() -> Option(int)` returns the
footprint the OS reports: macOS's phys_footprint (what Activity Monitor shows), the Linux
resident set, Windows private bytes; None where the platform cannot say.

> Pinned-by: grep -q "^memory_bytes_grows=true$" corelib/test/os.out

### 242. No package handled the host's paths, so a path from the OS broke on Windows — **FIXED 2026-11-24**

`core:path` is POSIX on every host and `core:filepath` was Windows on every host. A program
taking the directory of `os.exe_path()` had to pick one: Urani used `core:path`, so on Windows
`path.dir("C:\Games\Urani\urani.exe")` gave `.` and the engine looked for `./urani.pak`. And
`filepath` borrowed Go's name without Go's meaning, so the reflex picked it on macOS and got
backslashes. `core:filepath` now follows the host, as Go's does: Windows rules on Windows,
`core:path`'s elsewhere; the always-Windows functions are `win_join`, `win_dir`, ...

> Pinned-by: grep -q "^host=true$" corelib/test/filepath.out

### 243. A method-style call to a builtin was typed `?`, so nothing after it was checked — **FIXED 2026-11-24**

Found by the typed IR (docs/internals/tir.md), whose sweep asks the checker for every
expression's type. `s.split(",")` resolved only through UFCS over user functions, which
skips builtins, so the checker typed the call `?`. A `?` is compatible with everything:
`n: string = s.split(",")` compiled, and `parts := s.split(",")` left `parts` unchecked for the
rest of the function. The C emitter resolved the call on its own, so the program ran. A builtin
with a signature now takes the receiver as its first argument, as the shape builtins already did.

> Pinned-by: grep -q "builtin split :arr(string)" tests/tir/ufcs_builtin.tir

### 244. `for x in` over bytes died in the emitter, and over a string it bound a char, not the int the spec says — **FIXED 2026-11-25**

Urani's script-image fuzz test looped `for x in img` over the bytes of a compiled script. The
checker accepted it (each element an `int`, as spec §10 says for a string) and the C emitter
refused it with an internal message, "for-in over 'bytes' -- only arrays here, maps are Phase 8".
Making bytes take the string path showed the second defect: that path bound each element as a
`char`, not an `int`, so `str(c)` printed the glyph and `t * 1000 + c` wrapped to a byte -- the
checker, the spec and the emitter disagreed, silently. Both now bind an `int`.

> Pinned-by: grep -q "^65122$" tests/forin_bytes.out
> Pinned-by: grep -q "^0,255,128,$" tests/forin_bytes.out

### 245. "The value, or stop" had no spelling in a function that returns nothing — **FIXED 2026-11-25**

Building Urani, the line `x := f() or_else e: fail(e)` was written about eight times in `main`
and in test programs, and refused each time: `or_else` returns its value from the enclosing
function, and `main` returns nothing. `unwrap_or` needs a default, and a loaded VM or a parsed
config has none, so each became a five-line `match` with a dead `return` after `exit`. The
checker also did not know `exit()` ends a path, so a function ending in it warned "not all paths
return". `result.expect(r, msg)` / `result.expect_some(o, msg)` give the value or print
`msg: error` and exit 1; a body ending in `exit(...)` counts as returning.

> Pinned-by: test -f tests/result_expect/main.ty
> Pinned-by: grep -q "^half of 7: odd$" tests/result_expect/expected.err

### 246. The C emitter typed four builtins and operators differently from the checker — **FIXED 2026-11-25**

Found by the type-agreement gate (`make type-agreement`, scripts/type_agreement.py), which compares
the type the checker and the emitter each give every expression across the tree. Its first run
listed 427 disagreements in 32 patterns; four were emitter bugs, the rest spellings it now folds.
- `write_file(...)` was an `int` to the emitter, a `bool` to the spec and the checker:
  `str(write_file(p, s))` printed `1`, and so did a bool variable holding it.
- An f32 beside a numeric literal (`x * 3.0`) was computed in double precision: the literal was
  a double and the f32 widened to it, so writing a value as a literal changed the result
  (`0.30000000447034836` against `0.30000001192092896` for `x * y`).
- `to_str(b)` was typed as its argument, `bytes`, and every variable assigned from it with it.
- `bytes + bytes` was typed `string`.
The last two share string's C form and showed no output difference yet; any emitter decision
on the type would have been made on the wrong one.

> Pinned-by: grep -q "^true true$" tests/emitter_types.out
> Pinned-by: grep -q "^0.30000001192092896 0.30000001192092896 true$" tests/emitter_types.out

### 247. Eight constructs were typed `?` by the checker, so nothing after them was checked — **FIXED 2026-11-25**

The type-agreement gate (FRICTION 246) lists every expression the checker typed `?` and the C
emitter typed: places no type rule reaches, since `?` is compatible with everything. It found
309 such expressions; eight causes accounted for 294 of them, each now typed:
- a nested pattern's bindings (`Err(cli.Unknown(tok))`: `tok` was `?`) -- from the nested
  variant's payload;
- a call through a subscript (`w.px(d)`, `c.member(i).name`) -- the type it yields;
- a call through a function-typed field (`t.less(k, t.keys[n])`) -- its arguments now checked
  against the field's parameters, its result the field's;
- `.get` on a map-valued expression (`st.counts.get(k, 0)`);
- `[]` under a map annotation (`m: [K: V] = []`), which the emitter makes the empty map;
- a `select` arm's `recv(ch, v)` binding -- the channel's element;
- a `$N` size parameter in a size-generic body -- an int;
- a value `match` (`name := match s: ...`), whose arms were typed as statements: each arm's
  tail is now its value. This one hid a real defect: a value match whose arms gave a string
  and an int was accepted, and the C compiler refused the C it produced (`h_x = 5LL` into a
  `char *`). It is now "if/match branches produce different types (string and int)".
- an explicit generic variant (`Leaf$(int)`, `box.Nothing$(int)`), left `?` as "6c" work --
  now its instance, its payload checked against that instance's variant.
The 15 expressions left (8 lines: a generic constructor of a bare `[]`, a pending Option, two
method-style builtins on a literal or a chain, a spread argument, a `[$N]T` parameter in a
size-generic body) are the gate's baseline, compiler/type_agreement.baseline.

> Pinned-by: test -f tests/reject/value_match_mixed_types.ty
> Pinned-by: test "$(grep -vc '^#' compiler/type_agreement.baseline)" -le 15
> Superseded-by: 248 (the baseline is empty)

### 248. The last five constructs the checker answered `?`, and a size-generic body checked only at `?` — **FIXED 2026-11-25**

The rest of FRICTION 247's list, closed:
- a builtin called method-style on a literal or a chain (`"hello".len()`,
  `n.to_float().to_int()`) -- typed as the builtin, in both call paths;
- a generic struct built from a bare `[]` (`a: Stack(int) = Stack([])`) -- the expected instance
  types its fields when every value fits it (otherwise the old "declared type Stack(int) but
  value is Stack(string)" stands);
- `o := None` then `o = Some(n)` -- the assignment grounds the declaration's open type, as a first
  push grounds a bare `[]`, so `match o: Some(v)` types `v`;
- a spread filling a generic variadic (`count(xs...)`) -- one argument of the array's element type;
- a size-generic function's body (`fn op_named(t: [$N]Meta, ...)`) was memoised by its name
  alone: the first instance, checked from a caller that could not type its argument, stood for
  all of them, so the body was only ever checked with `t` as `?`. It is now checked per argument
  type, and not from an argument that is `?`.
The type-agreement baseline is empty: every expression the C emitter types, the checker types,
and the same.

> Pinned-by: test "$(grep -vc '^#' compiler/type_agreement.baseline)" -eq 0

### 249. The type-agreement gate left two classes unproven; closing them found two line-attribution defects — **FIXED 2026-11-25**

After FRICTION 248 the gate still accepted two classes on argument alone: 151 literals the
checker adapted to their context (an f32, a u8) that the emitter records unadapted, and 3,143
expressions only the emitter typed. Both are now proven per expression. The emitter records each
expression's ancestors, and:
- an adapted literal is proven when C converts it at a boundary (a declaration, a return: no
  parent expression) or its enclosing expression has the same type on both sides -- a literal
  operated on in a wider type shows in its parent's type, as FRICTION 246's f32 arithmetic did;
  a literal inside a proven literal (`-0.0`) is proven with it;
- an emitter-only expression is one of the emitter's own temporaries (`_mk59`, `_po68`), the
  literal inside a negation the checker typed whole, or part of an expression both sides type
  the same (its nearest ancestor the checker typed agrees). Anything else fails the gate.
Proving them found two defects in the emitter's source positions:
- an f-string hole was re-tokenized with lines counted from 1, so every expression in a hole
  sat on line 1 of its file (about 1,200 expressions the gate could not pair); its tokens now
  carry the f-string's line;
- a subscript's body, inlined at each call, was emitted with the CALLER's file current, so its
  expressions named the caller's file at the subscript's line numbers; the subscript's own file
  is now current while its body is emitted, as for a function body.
Neither changed a program's behaviour in the tree. Result: 224,119 expressions typed by both
sides, all agreeing; 151 of 151 adapted literals proven; 2,062 emitter-only expressions all
explained.

> Pinned-by: grep -q "unexplained" scripts/type_agreement.py

### 250. `has_str(T)` refused types `str` formats, so `result.expect` refused every enum error — **FIXED 2026-11-25**

Urani's `urani cook` called `result.expect(io.read_text(p), "...")` and got "'expect' instantiated
with E = io.IoErr, which does not satisfy `has_str(E)`". `str` formats an enum (`Missing`,
`Bad(4)`), a struct, a char, bytes and every container, but the predicate admitted only int,
bool, float and string -- so the generic helper FRICTION 245 added for "the value or stop"
refused the error types most of corelib returns. `has_str(T)` now admits exactly what `str`
accepts (the checker asks the same function), and spec 05's table says so.

> Pinned-by: grep -q "^Bad(4) | Bad(4)$" tests/has_str_all.out

### 251. A binding that only reads deep-copied its source, and `xs = xs[0:n]` copied twice — **FIXED 2026-11-25**

Profiling Urani's script VM found the same trap twice: `d := vm.p.descs[i]` and
`fn_ := vm.p.funcs[f]`, each only read, deep-copied a struct -- its strings and arrays -- on
every call; together they were most of the VM's time on struct-heavy and call-heavy scripts.
And `xs = xs[0:n]`, to shrink an array, copied the kept prefix twice. Both compile without a
word, and value semantics make the copies unobservable, so the compiler may drop them:
- a read-only borrow: `f := <place>` shares the source when neither can change while f is in
  scope -- f and the source's root variable each appear afterwards only as the base of a field
  or index read; a bare use, an assignment, a store, `&`, a method call, a mutating builtin on a
  chain rooted there, a re-declaration or a mention in a lambda keeps the copy, as does any
  spawn or parallel in the function (`TYCHOC_NO_RO_BORROW=1` turns it off);
- `xs = xs[0:n]` (or `xs[:n]`) on an array is a length change where xs stands, trapping as the
  slice does when n is outside 0..len.
Urani's VM written the natural way (`d := vm.p.descs[...]`) now runs as fast as the hand-made
workaround (0.106 s against 0.176 s without the borrow).

> Pinned-by: grep -q "^source written: 1 99$" tests/ro_borrow.out
> Pinned-by: grep -q "^10,20,99, 3$" tests/self_truncate.out
> Pinned-by: test -f tests/abort/self_truncate_oob.ty

### 252. A script host could not take a record across an extern — **FIXED 2026-11-25**

Urani's entities keep their typed fields in the engine, declared by the game script as a
record (`struct Fields` of int, float and bool fields) that the script reads and writes whole:
`extern fn ent_fields(e: int) -> Fields`. The C checker already lets a POD struct cross an
extern, but the typed IR's script subset refused anything but scalars and strings ("the
extern 'ent_fields' returning Fields is not in the script subset"), so the only way to give a
script typed entity fields was one engine call per field, by a name string checked at run time.
TIR lowering now lets a struct cross when all its fields are int, float or bool (a record the
host copies cell by cell); one with a string, array, enum, sized-scalar or nested-struct field
is still refused by name. The host still verifies the record's shape when it loads the bytecode.

> Pinned-by: grep -q "extern ent_fields(int) -> @Fields" tests/tir/extern_record.tir
> Pinned-by: grep -q "taking Packed is not in the script subset" tests/tir/refuse_extern_record/main.ty

### 253. `result.map_err` and `result.expect` refuse `Result(void, E)` — **FIXED 2026-11-25**

Urani's saves wanted `result.map_err(io.write_bytes(p, b), "cannot write " + p) or_return`
to turn an `io.IoErr` into its own `string` error, and its tests wanted
`result.expect(entity.remove(&s, e), "remove")`. Both fail to instantiate for an ok type
of `void`: the generic bodies bind the ok value (`v := r or_else ...`, `Ok(v): return v`)
and a void value cannot be bound ("cannot bind a void value", "Result(void, string)
carries no ok value -- write Ok(), not Ok(x)", "Ok carries no value here"). Every caller
with a `Result(void, E)` writes its own match: Urani has `_io` in src/main/save.ty and
`must` in tests/entity. Two fixes: treat `void` as a unit type when a generic is
instantiated (`v := <void>` binds nothing, `Ok(v)` at a void ok type is `Ok()`, `return v`
of a void value returns), which fixes every generic of this shape; or add void siblings
to corelib/result. The first is a language decision.

**Fixed: a generic's `$T` bound to `void` is an empty placeholder in its instance.** Why
`void` had no values: on 2026-08-09 the old C compiler's `T_VOID` meant both "returns
nothing" and "type parameter not yet bound", so `Result(void, E)` was made spellable by
re-marking the sentinel, as a type with no values -- the cheap route; a real unit type was
rated "cleanest, most work" and not taken. The self-hosted compiler marks an unbound `$T` as
`?`, and the C already stores the void ok payload in a `char okv`, so nothing stands in the
way now. Inside an instance whose `$T` is `void`, a `$T` value may be bound (`v := r
or_else ...`, an `Ok(v)` arm), passed to `Ok(v)` (emitted as `Ok()` after evaluating `v`),
returned from a `$T`-returning function (emitted as `return;`), and be an `or_else`
clause's value there. Code that names void itself keeps today's rules: a direct call to a
non-generic function returning nothing still cannot be bound, in an instance or not
(`die`/`exit` excepted). `result.map_err`, `map_err_with` and `expect` now work at
`Result(void, E)`; Urani's two stand-ins go.

> Pinned-by: grep -q "^void_expect = passed$" corelib/test/result.out
> Pinned-by: grep -q "^retry: true true true$" tests/result_void_generic.out
> Pinned-by: test -f tests/reject/void_bind_in_void_generic.ty
> Pinned-by: test -f tests/abort/result_void_generic_die.err
