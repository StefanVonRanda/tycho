# Open

One phase, not completable inside a coding session. Everything else from the
2026-08-15 sweep is done and in `git log` — evidence lives in the commit
messages, per the rule in `CLAUDE.md`. Publishing 0.7.0 was phase 1 and is
deleted rather than ticked, per the same rule.

## Get the external review (ROADMAP §7)

- **Scope:** send `docs/internals/audit-brief.md` to a reviewer outside the project.
- **Done when:** someone who did not write this code has reported findings, or
  reported which classes they looked for and found nothing in — the second is
  worth as much and the brief asks for it.
- **Verify:** nothing to run. This phase has no gate by construction.
- **Why it cannot be absorbed into a coding phase:** FRICTION #77. The
  interior-NUL rule is normative in `docs/spec/14-ffi.md`, a deliberate sweep ran
  for it on 2026-08-13, and three packages carry guards naming each other — yet
  the two highest-severity sites were never on that list, one collapsing two
  passwords into a single derived key. A sweep covers the sites its author has in
  mind, which are the ones already fixed.

## From the FFI probe (dusize, 2026-08-19)

An LLM agent built a 245-line directory analyser using `extern fn`, `handle` and
`soa` from `docs/` alone. It reported 12 findings; me rebuilt the program on
`main` and checked each. **The probe ran against the v0.7.0 tarball, so some of
its findings were already fixed** -- `bytes` as a parameter name now says
"'bytes' is a reserved keyword and cannot be used as a parameter name", not
"expected a parameter name". Only what survived checking is below.

### 1. A failed handle opener cannot be detected

```
d := c_open("/nonexistent")     # extern fn c_open(p: string) -> D
if is_null(d):                  # error: argument 1 of 'is_null' is D, expected ptr
```
`is_null` takes `ptr` (`docs/reference/ffi.md@is_null`), and a `handle` is not a
`ptr` -- the probe found no way to ask whether the opener succeeded. The
destructor then runs the free function on NULL at scope exit. The shim can
return a sentinel and be checked with a separate extern, but nothing in the docs
says so and the obvious spelling is refused.
- **Done when:** either `is_null` accepts a handle, or `docs/reference/ffi.md`
  states the supported way to detect a failed open.
- **Gates:** `make test`, `make fh-check` (the only lane that runs a handle).

### 2. The FFI doc does not say a returned C string must still be live

The probe's own shim did `closedir(d); return ent->d_name;` -- and ASan caught a
heap-use-after-free in `tycho_str_from_c`, because the boundary copies the
string AFTER the C function returns. `docs/reference/ffi.md:37` says a C-returned
string is "copied into the caller's arena", which reads as "you may return
anything". It does not say the pointer must still be valid at the moment of
return.
- **Why it matters:** this is the first thing a new FFI user gets wrong, and the
  plain build passed -- only ASan saw it.
- **Done when:** `ffi.md` says the returned pointer must remain valid until the
  call returns, with the `readdir`/`closedir` shape as the example.
- **Gates:** the two doc gates. `make docs-fences` if it gains a fence.

### 3. Land dusize as §1's third program -- BLOCKED on fixing its shim

It compiles and runs on `main`, and would close §1. It must not land while its
shim has the use-after-free above. Fix (strdup before `closedir`, or keep the
stream open), then file it as `tools/tycho-du/` with the LOG as
`FRICTION-OUTSIDE.md`, matching tycho-diff and tycho-hash.
- **Gates:** `sh scripts/entrypoints.sh`, and a `run.sh` + lane if it is kept.

### Checked and NOT actionable

`chr` byte range, no int/float mixing, no structs across the FFI, `sink`
semantics -- all documented, and the probe's log says so itself. The `inout`
overlap refusal on `&stats[j], &stats[j+1]` is real and is the limitation
already recorded as FRICTION #48 for tycho-grid.

## Blocked, not scheduled

**ROADMAP §1** wants three non-trivial programs by **two** people; three exist,
all by one non-author. The missing half is a second mental model, so it cannot be
worked around by writing a fourth program — ROADMAP says this in as many words.
Listed here so it is visible, not because anything can be planned against it.
