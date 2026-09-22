# Friction writing `tycho-du`, from outside

`ROADMAP.md` §1 asks for programs written by someone who did not write the
compiler, with the friction recorded. This is one: a recursive disk-usage
reporter whose open directory stream is a typed `handle`, written 2026-09-21
against `docs/`, `corelib/`, `examples/` and `tools/` only, with no access to
`src/tychoc.c` or `compiler/` (the probe tree is stripped —
[`docs/internals/probe-procedure.md`](../../docs/internals/probe-procedure.md)).

**Read this as a first-contact record, not as a description of the tree today.**
Eight of its fourteen findings were fixed the same day; the wording quoted in
each is what the docs or the compiler said BEFORE, and the report is left in the
tense it was written in, because the point of this file is the experience rather
than the changelog. What is fixed, what is still open, and one finding that is
**wrong** are scored in
[`docs/internals/probe-handle-2026-09-21.md`](../../docs/internals/probe-handle-2026-09-21.md)
and [FRICTION 122-128](../../docs/internals/FRICTION.md).

Two specific corrections, because leaving them unmarked would let this file rot:

- **Finding 7's table of five other "dead links" is an artifact of the probe
  setup**, which deletes `docs/internals`, `docs/rfc` and `tests`. Every target
  named exists in a full checkout and `make check-links` is green. The
  self-links it opens with were real and are fixed.
- The program below is called `tydu` and lives here as `tycho-du`; its shim
  `dirwalk.c` is `du.c`, and the stale comment about the destructor receiving
  NULL — which the report itself disproves in finding 5 — is corrected in the
  imported copy.

---


First contact with Tycho, 2026-09-21, macOS arm64 (Darwin 27.0.0, Apple clang),
`tychoc 0.8.5`. Everything below came from `docs/`, `corelib/`, `examples/`,
`tools/` and `README.md` in this directory, plus measurement. I did not read the
compiler source, did not search the web, and deliberately did not load the
`tycho-syntax` skill that this harness offers — that skill is distilled
second-hand knowledge and using it would have answered exactly the questions
this probe exists to ask.

## The program

`tydu/` — "where the disk went". A recursive disk-usage reporter: largest files,
heaviest extensions, totals, and an explicit count of what it could not read.

```
$ ./tydu --top 3 ../docs
largest files
    60.7 KiB  ../docs/reference/corelib.md
    49.3 KiB  ../docs/spec/12-aggregates.md
    28.7 KiB  ../docs/spec/18-library.md

by extension
   717.9 KiB      55  .md

total 717.9 KiB in 55 files, 3 directories
```

- `tydu/dirwalk.c` — the shim. `opendir`/`readdir`/`closedir`/`lstat`, plus
  live/open/close counters so the affine claim is *measured* rather than assumed.
- `tydu/main.ty` — `handle Dir: free: dw_close`, five `extern fn`s, a recursive
  `walk` returning `Result(int, WalkErr)`.
- `tydu/run.sh` — builds it, checks the totals against `find(1)`, exercises both
  error paths, and runs the leak detector. `sh tydu/run.sh` prints `tydu: green`.
- `probes/p1.ty … p18.ty` — the eighteen one-file experiments the findings below
  quote. Kept because a claim about a diagnostic is worth nothing without the
  file that produced it. `probes/dirwalk.c` is a cut-down shim for them.

Why this program and not `list_dir`: the builtin exists
(`docs/reference/builtins.md`: "`list_dir(path)` | `string -> [string]` | Entries
excluding `.`/`..` (filesystem order); **empty if it can't be opened**"). It gives
no sizes, no entry kinds, and returns the same empty array for an empty directory
and an unreadable one — which is the one distinction a disk-usage tool must
report. So the shim earns its place.

**Correctness.** Checked against `find(1)` on this directory: tydu reports
`616 files, 235 directories, 5573303 bytes`; `find . -type f | wc -l` = 616,
`find . -type d | wc -l` = 235, `find . -type f -exec stat -f %z {} \;` summed =
5573303. Built from `--emit-c` under `-fsanitize=address,undefined
-fno-sanitize-recover=all` with the corelib shims from `tychoc --print-shims`:
clean over a 620-file walk and over 50 strict-mode error unwinds.

---

# Findings

## 1. The normative spec's own spelling of `handle` does not compile

`BRIEF.md` sent me to `docs/spec/14-ffi.md` §25. Its first sentence is:

> A `handle Name: free: c_free` declares a nominal, affine, opaque C resource — a
> `void*` whose destructor `c_free` runs automatically at scope exit (RAII).

I typed that (`probes/p18.ty`):

```
p18.ty:1: error: expected newline
     1 | handle Dir: free: dw_close
       |             ^
p18.ty:3: error: unknown type 'Dir'
```

The correct form is two lines, and the only prose that says so is in a *different*
document — `docs/reference/ffi.md`:

> a `handle Name:` header over an indented `free: c_fn` line (a block, not
> a one-liner — see the `handle Db:` example below)

`docs/spec/appendix-a-grammar.md:85` is also right:
`Handle ::= "handle" IDENT ":" NEWLINE INDENT "free" ":" IDENT NEWLINE DEDENT`.
So two of three normative places are correct and the one the brief points at is
not. This was the first thing I wrote and the first thing that failed.

The cascading `unknown type 'Dir'` on line 3 is noise from the first error; three
more followed it in longer files. Nothing tells you they are consequences.

## 2. The spec and the reference contradict each other on reassignment — and the spec is the wrong one

`docs/spec/14-ffi.md` §25, "Affine, exactly one owner":

> A handle MUST NOT be copied; **reassigning a handle variable frees the previous
> handle first.**

`docs/reference/ffi.md`:

> it cannot be copied (`g := f`), **reassigned**, stored in an array, map, struct,
> tuple, `Option` or `Result` …

These describe opposite behaviours. Measured (`probes/p6.ty`):

```
p6.ty:11: error: a handle variable cannot be reassigned -- it is freed once, at the end of its scope
```

The reference is right and the normative spec documents a feature that does not
exist. I had planned my walker around reassignment (a loop that re-opens a
directory into the same variable); I backed that out.

## 3. `is_null` on a handle exists in exactly one sentence, and the shipped example says it is impossible

The only place in all of `docs/` that says a handle can be null-tested is
`docs/reference/ffi.md:57`:

> `is_null` accepts a `handle` too, which is how you check whether an opener
> succeeded; the scope-exit free is null-guarded, so a failed open is safe to let
> fall out of scope.

Everywhere normative it is typed `ptr`-only:

- `docs/reference/builtins.md`: `` `is_null(p)` | `ptr -> bool` | Test an opaque FFI `ptr` for null. ``
- `docs/spec/16-builtins.md:110`: `` `is_null(p)` | `ptr -> bool`: test an opaque FFI pointer for `NULL` ``
- `docs/spec/14-ffi.md:58` (§24.1, the crossable-types list): "an opaque `void*`
  Tycho never dereferences; the `null` literal and `is_null(p)` apply" — said of
  `ptr`; the `handle` bullet immediately below it says nothing.
- `docs/spec/03-types.md` §5.3.9, the section the brief names: no mention.

And the only `handle` program in the whole tree, `tools/tycho-fh/fh.c`, ships a
C function whose entire reason for existing is the belief that it *cannot* be done:

```c
/* A handle cannot be tested against null in Tycho -- it may not go in an Option
 * or a Result, and there is no null literal for one -- so asking C is the only
 * way to find out whether the open succeeded. */
int64_t fh_ok(void *h) { return h ? 1 : 0; }
```

`is_null(d)` on a `Dir` handle compiles and works — it is what `tydu/main.ty`
uses, and the permission-denied path in the test tree proves it fires. So the
tree's one worked example carries a hand-written C function it does not need,
justified by a comment the reference contradicts. I believed the comment first
(it was in running code), wrote a `dw_ok` into my shim, then tried `is_null`
anyway and deleted `dw_ok`.

## 4. An opener result that is never bound to a variable leaks silently

This is the largest hole I found. `docs/reference/ffi.md` headlines handles as:

> the transpiler frees it automatically at the end of the scope that owns it, so
> it **can't leak or be used after close**

and `docs/spec/14-ffi.md` §25: "**Scope-exit free.** The **owning variable's**
destructor runs at every scope exit." Both are careful to say *owning variable*.
Neither says what happens when there is no owning variable, and nothing refuses
it. `probes/p14.ty`:

```text
fn unbound_arg():
    match dw_next(dw_open(".")):        # opener result passed straight as an argument
        Some(_): pass
        None: pass

fn bare_stmt():
    dw_open(".")                        # opener called as a statement

fn bound():
    d := dw_open(".")                   # the documented shape
    if is_null(d):
        die("no")
```

Five calls of each, counters read from the shim:

```
bound:       opens 5 closes 5
unbound arg: opens 10 closes 5
bare stmt:   opens 15 closes 5
```

Both unbound forms leak a `DIR *` per call, with no error, no warning, and no
mention in any document. This is sharpened by the fact that the language is
*aggressively* strict one inch away: `docs/reference/basics.md` — "A local
declared with `:=` … and never read again is a compile **error** — `'x' declared
and not used`" — and an unused *import* is an error too (finding 10). So Tycho
refuses an unused `int` and accepts a leaked file descriptor. Given that an
`extern fn` opener is the *only* thing in the language allowed to produce a
handle, requiring its result to be bound looks cheap.

## 5. The destructor is never called with NULL — the guard is on the Tycho side, and the shipped shim assumes the opposite

Spec §25: "the scope-exit finalizer is **null-guarded**, so the destructor runs
**exactly once**." It does not say *which side* the guard is on, and that is the
only thing a shim author needs to know, because it decides whether close-counting
balances open-counting.

Measured: `./tydu --selftest=3 t/locked` (a `chmod 000` directory, so every
`dw_open` returns NULL) prints `opens 0 closes 0`. And after an explicit
`close(d)` followed by scope exit (`probes/p1.ty`): `opens 1 closes 1`. So
`dw_close` is simply not called when the handle is null.

The generated C confirms it literally. `probes/p15.ty` (a `free:` naming a
function that does not exist) leaves the `.c` behind, and the line is:

```c
    if (h_d) dw_shut(h_d);
```

Against this, `tools/tycho-fh/fh.c` says:

```c
static int64_t g_closes = 0;  /* closes, ever -- including the null-guarded no-ops */
...
/* A NULL arrives when the open failed or the handle was already closed; counting
 * those separately is what lets the gate tell "closed twice" from "never opened". */
```

A NULL never arrives. I wrote my shim's `dw_close` to count null calls on the
strength of that comment, then had to re-derive the real behaviour by experiment
before the leak self-test in `tydu` meant anything.

## 6. `free:` naming an undeclared function falls through to `cc`

`probes/p15.ty`. Every other mistake I made got a Tycho line, a caret, and often
a `note:`. This one got:

```
p15.c:3226:14: error: call to undeclared function 'dw_shut'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
 3226 |     if (h_d) dw_shut(h_d);
      |              ^
tychoc: C compilation failed (cc -O3 -fwrapv -ffp-contract=off -pthread -I'../corelib' -o 'p15' 'p15.c' 'dirwalk.c' -lm )
```

A generated file, a generated line number, and no reference to `p15.ty:2` where I
wrote the typo. Since `free:` takes a bare identifier that must resolve to an
`extern fn`, this seems checkable in the front end. (Related: `probes/p16.ty`
declares the destructor as `extern fn dw_close(d: ptr) -> int` — the wrong Tycho
type for `handle Dir` — and compiles without comment.)

## 7. `docs/reference/ffi.md` links to itself, twice, as a different document

Line 88: "the full rules are in `[the FFI design note](../reference/ffi.md)`".
Line 103: "The `[FFI reference](../reference/ffi.md)` is the short version; this
page is the full rule set." From inside `docs/reference/ffi.md`, both resolve to
the page you are already reading. The second one told me there was a shorter
companion page I should read first and a fuller one I was on; I spent time
looking for the other document before realising there isn't one.

Related dead links reached from the pages I needed:

| Link | From |
|---|---|
| `rfc/ffi-threading-design-review.md` | `docs/reference/ffi.md` |
| `docs/internals/design-aggregate-ref.md` | `docs/reference/basics.md` |
| `docs/internals/value-semantics-limits.md` | `docs/reference/corelib.md` |
| `internals/README.md`, `rfc/README.md` | `docs/README.md` ("Also here") |
| `tests/ffi/`, `tests/reject/handle_dup_name.ty` | `docs/reference/ffi.md`, spec §25 |

Spec §25 cites `tests/reject/handle_dup_name.ty`, `handle_then_struct.ty`,
`handle_then_enum.ty` and `handle_then_newtype.ty` as the fixtures that lock the
handle/type name-collision rule. None are here, so I could not read the locked
behaviour and did not test that rule.

## 8. `close(h)` on a handle is missing from the reference builtins page

`docs/reference/builtins.md` lists `close` once, under **Concurrency**:
`` `close(ch)` | `Channel(T) -> void` ``. The handle form is only in
`docs/spec/16-builtins.md:264` ("Also `close(h)` on a **handle** variable
(`[§25](14-ffi.md)`)") and in the ffi pages. Since `docs/README.md` says
"**`[reference/](reference/)`** is the single answer layer", looking up `close`
where the docs tell you to look gives you the wrong answer.

`probes/p7.ty` — `close(dw_open("."))` — is correctly refused, with a terse but
adequate message: `close(h) takes a handle variable`.

## 9. `bytes` is a reserved word and cannot be a struct field name

My first `Stats` struct had a `bytes: int` field, which for a disk-usage tool is
the obvious name.

```
./main.ty:27: error: 'bytes' is a reserved keyword and cannot be used as a field name
    27 |     bytes: int
       |     ^
```

Correct, and it *is* documented — `docs/spec/appendix-b-keywords.md` §B.1 lists
all 42. But nothing in the tutorial, `reference/basics.md` or
`reference/structs-tuples.md` mentions that the reserved set includes the type
keywords, and a field name shares no namespace with a type. It cost a minute, not
an hour; I record it because "check appendix B before naming a field" is not
advice any entry-path page gives. The field is `nbytes` now.

## 10. A value `if` is documented as an expression, and the diagnostic for the case it doesn't cover says nothing about it

```text
println("total " + ... + " directories" +
        (if st.skipped > 0: ", " + str(st.skipped) + " skipped" else: ""))
```

```
./main.ty:176: error: expected an expression
   176 |             (if st.skipped > 0: ", " + str(st.skipped) + " skipped" else: ""))
       |              ^
```

This one is my fault — `docs/reference/enums-options.md` says so plainly: "Not
yet supported (deliberate follow-ups): … use as a nested sub-expression (e.g.
`1 + if …`)". I had read it and reached for the C reflex anyway. What is worth
recording is the *message*: `expected an expression` at the `if`, which reads as
"an `if` is never an expression" when the rule is "an `if` is an expression only
as the whole RHS of a `:=`, `x : T =`, `x =` or `return`". Naming the tail
position would have turned a doc lookup into a one-line fix. Worked around by
hoisting to `tail := if …`.

## 11. `cc` warnings from corelib leak into my build output

A clean `built tydu` was preceded by:

```
tydu.c:4683:217: warning: expression result unused [-Wunused-value]
 4683 |     ({ TychoRes3 _or80 = h_strings___slice_ok(&_scope, tycho_str_len(h_b), h_start, h_stop); ...
```

Two of them, both from `core:strings`' `slice_bytes`/`slice_str` — code I never
call, pulled in because I imported the package. The location is a generated line
in a file that is then deleted, so there is nothing to act on, and on a first
build it is not obvious whether you have done something wrong. `docs/debugging.md`
does not cover it.

## 12. The worked binding the FFI docs point at for handles contains no handle

`docs/reference/ffi.md` says three times that the complete example is
`examples/sqlite/`:

> A worked binding lives in [`examples/sqlite/`](../../examples/sqlite/): opaque
> `db`/`stmt` handles, SQL string arguments, arena-copied column text, `--shim`
> for the out-parameter API, and `--pkg` for linking

`examples/sqlite/demo.ty` uses raw `ptr` throughout and calls `sqlite3_close(db)`
by hand; there is no `handle` declaration and no `--shim` (its own README says
"**ZERO hand-written shim**"). The `handle Db:`/`free: sqlite3_close` snippet in
the docs is marked `<!-- fence-skip: … and no main -->`, i.e. it is never
compiled. The only real handle program in the tree is `tools/tycho-fh/`, which
the FFI documentation never mentions; I found it by grepping `^handle ` across
every `.ty` file. That grep returned exactly one hit in the whole distribution.

`tools/tycho-fh/` also links its shim as a hand-built static library
(`extern "fhdemo"` + `-lfhdemo`, assembled by a 10 KB `run.sh`), so nothing in
the tree demonstrates the combination the brief asks for and the docs recommend —
a `handle` plus `--shim`. It does work; I had to try it to find out.

## 13. Documented-and-correct, but only findable if you already know the word

Three things I needed were in the docs and took a keyword search to reach, which
I record because "correct but unfindable" counts:

- **`-> Option(string)` on an `extern`.** `readdir` returning NULL at end-of-stream
  is exactly the case, and the docs have it: "A nullable C return is declared
  `-> Option(string)` … a `NULL` surfaces as `None` and any other pointer as
  `Some(<arena-copied>)`. This removes the need for sentinel strings on nullable C
  getters." Without it I would have used `""` as an end marker, which is wrong for
  a filesystem. It is a paragraph inside the middle of `reference/ffi.md`, under
  a heading ("Nullable string return") that does not appear in any index.
- **`--print-shims`.** I needed it to build the ASan binary by hand, and found it
  only in `tychoc --help`. `docs/reference/ffi.md`'s Linking section lists
  `--link`, `--pkg` and `--shim` and stops.
- **`tychoc --help` itself** is more informative about the FFI command line than
  the Linking sections of either FFI page, which never show a complete
  `tychoc x.ty -o x --shim y.c` invocation.

## 14. `continue` in spec §25's scope-exit list is dangerously ambiguous

> **Scope-exit free.** The owning variable's destructor runs at every scope exit —
> block end, early `return`, `break`, `continue`, `or_return`.

My `walk` has a `continue` inside the loop that iterates the directory — while the
`Dir` handle owned by the enclosing function body is live. Read literally, that
sentence says the `continue` frees the stream I am in the middle of reading. It
does not, and the generated C proves it (the `continue;` carries no `dw_close`).
The list is about the scope that *owns* the variable, not about every statement
named. I stopped and built `probes/p12.ty` before trusting it.

---

# What went right

Most of this went well, and several things went better than I expected.

**The affine diagnostics are the best I have seen for a resource-ownership rule.**
Every violation I tried was refused, at the right line, in language that explains
the model rather than the parse:

```text
p2.ty:11: error: a handle cannot be copied -- it is freed once, at the end of its scope, and a second name would free it twice; bind the opener directly (f := open(...)), or pass this one as an argument, which borrows it
p2.ty:1: note: `Dir` is a handle, declared here -- `dw_close` frees it once at scope exit
```

```
p3.ty:9: error: a Tycho fn cannot return a handle -- only an `extern fn` opener may; a handle is freed at the end of its scope and cannot escape it
p9.ty:9: error: 'd' is sink, which an affine type cannot be -- it has one owner and is freed at that scope's exit, so there is nothing to consume or copy back. Pass it plainly: that is already a borrow
```

The `note:` pointing back at the `handle` declaration is exactly right. The
`sink` message answering the question I would have asked next ("then how do I
hand it over?" — "pass it plainly: that is already a borrow") saved a doc lookup.
`probes/p4.ty` (array) and `p5.ty` (`Option`) are refused with the same message.

**The affine rule shaped the program, correctly.** Because `walk` cannot return a
`Dir`, and cannot store one, the tree walk *had* to be recursive with the handle
owned by the frame that opened it. That is the right design for this program and
I would not necessarily have chosen it. `tools/tycho-fh/main.ty` says this out
loud and it is the most useful comment in the tree: "It CANNOT return the handle,
so every function that opens one must also finish with it. That is the affine
rule doing its job, and it shapes the program."

**RAII holds on every exit, and I can quote the proof.** `walk` has six exits.
The generated C carries the free on all of them, including the `or_return`:

```c
h_total = (h_total + ({ TychoRes6 _or96 = h_walk(...); if (!_or96.ok) { TychoRes6 _rr96 = ...; if (h_d) dw_close(h_d); arena_free(&_scr94); arena_free(&_scope); return _rr96; } _or96.okv; }));
```

Measured end to end: `./tydu --selftest=400 t` walks a tree containing a
`chmod 000` directory 400 times in strict mode, so 400 `or_return`s unwind out of
a function whose `Dir` is live —

```
reps 400 errs 400
opens 400 closes 400
live 0
balance 400
ok
```

and 6000 `opendir`s under `ulimit -n 64` complete without a single failure. This
is the thing the feature promises and it delivers it.

**Spec §24.1.1 told me exactly how to write `dw_stat`, and it was right.** I
needed a call that returns both a size and a "could not stat" classification. The
spec has a whole section on it, names the two arrangements, and says which one is
forced:

> A scalar payload can occupy either slot, so nothing above decides it and a
> second constraint does: the two halves must not share one integer's code space.
> An epoch second, a file size or a byte count can take *any* value, including
> every value the status codes use … The classification therefore keeps the return
> … and the payload takes the `inout`

`extern fn dw_stat(p: string, size: inout int) -> int` is that shape, the C
signature it predicts (`int64_t dw_stat(const char *, int64_t *)`) is the one
that linked, and the "set it on every path, first" rule is why `*size = 0` is the
first statement in my shim. This is the best-written page in the documentation.

**Two warnings that named my bug before I wrote it.** From `reference/ffi.md`:

> `closedir(d); return ent->d_name;` is the shape that gets this wrong: the
> `dirent` belongs to the stream just closed.

That is *literally* the API I was binding, called out by name. And:

> **The copy is all the call site does — it never frees your pointer**, so a
> `malloc`ed return leaks, silently, in a normal build. … Return a `static` or
> `__thread` buffer instead

which is why `dw_errstr` is a `static __thread char[256]`. Two classes of bug
pre-empted by prose. That is documentation doing its job.

**`string` needs no marshalling.** "a Tycho `string` is already a NUL-terminated
`char *`, so there is no wrapper type and no `c_str()` conversion at the call
site". True — `dw_open(path)` against `const char *path` just works, and a
returned `const char *` becomes an arena-copied `string` with no ceremony. For a
tool that is mostly passing paths around, this removed an entire category of
boilerplate.

**Strict unused checks caught two real mistakes.** `'x' declared and not used` is
an error and so is an unused import:

```
./main.ty:12: error: `core:cli` imported and not used in this file
./main.ty:13: error: `core:path` imported and not used in this file
```

Both fired on scaffolding I had left behind. (The contrast with finding 4 is the
whole point of listing it here.)

**Keeping the `.c` when `cc` fails is the right call**, and `tychoc --help` says
so up front: "The .c is KEPT when cc fails, as evidence." It is the only reason I
could read `if (h_d) dw_shut(h_d);` and settle which side the null guard is on.

**The obvious spelling was usually the correct one.** `push`, `pop`, `len`,
`keys(m)`, `m.get(k, 0)`, `for x in xs`, `match`/`Ok`/`Err`, `or_return`,
`struct`, `enum`, `str()` — I wrote all of these from the tutorial on the first
try and none of them surprised me. `path.join`, `path.ext`, `strings.to_lower`,
`strings.pad_left`, `strings.parse_int_checked`, `sort.argsort_desc` and
`cli.parse_checked` were all where `docs/reference/corelib.md` said they were,
with the signatures it gave. The whole non-FFI half of this program compiled
essentially first-try.

**`parse_int_checked` versus `parse_int`.** The corelib page flags it: "the bare
`parse_int` **fails open** (`"3x"` is `3`, not an error), so anything read from a
file, a socket or a user wants the checked one." `--top` and `--depth` come from
a user, so they use the checked one, and `./tydu --top abc t` says
`tydu: --top wants a number`. Naming the failure mode in the catalogue entry, at
the point of choice, is better than a note somewhere else.

---

# What I tried that did not work

- **`handle Dir: free: dw_close` on one line** — the spec's own spelling
  (`probes/p18.ty`). Backed out to the two-line block form. Finding 1.
- **Reassigning a handle variable** — spec §25 says it frees the previous handle
  first; the compiler refuses (`probes/p6.ty`). I had a loop design that re-opened
  into one variable; rewrote it as recursion. Finding 2.
- **A `dw_ok(void *) -> int` shim function**, copied from
  `tools/tycho-fh/fh.c`'s `fh_ok`, on the strength of its comment that a handle
  cannot be null-tested in Tycho. Deleted after `is_null(d)` turned out to compile.
  Finding 3.
- **`bytes: int` as a struct field** — reserved word. Finding 9.
- **A nested `(if … else …)` inside a string concatenation** — hoisted to a
  local. Finding 10.
- **`take(d: sink Dir)`** — I wanted to express "this function finishes with the
  handle". Refused, and the message explained that a plain parameter is already
  the borrow I wanted (`probes/p9.ty`).
- **Storing the handle in the `Stats` struct**, so `walk` would not need two
  parameters. Refused (`probes/p4.ty`/`p5.ty` are the reduced cases). This is
  correct and I do not want it back; it is listed because it was my first design.
- **`cc -fsanitize=… tydu_gen.c dirwalk.c`** without the corelib shims — undefined
  `_pathx_real`, `_strx_format_g17`. `tychoc --print-shims` is the answer and it
  is not in the FFI docs. Finding 13.

---

# What I could not do at all

- **I could not learn, from the documentation, what happens to a handle that is
  never bound to a variable.** No page states it. I had to add open/close counters
  to my own shim and measure (finding 4). The answer — it leaks, silently — is the
  one thing about this feature I would most want written down.
- **I could not learn which side the null guard is on** without reading generated
  C from a deliberately broken program (finding 5).
- **I could not test the handle/struct/enum name-collision rule** that spec §25
  says is "Locked by `tests/reject/handle_dup_name.ty`" and three siblings. No
  `tests/` directory is present.
- **I could not use LeakSanitizer**: `detect_leaks` is unsupported on
  darwin/arm64, so `examples/sqlite/run.sh`'s sanitizer recipe only gives me ASan
  and UBSan here. Instead of a leak report I built the counter-based
  `--selftest` and an `ulimit -n 64` fd-exhaustion stress, which is what
  `tools/tycho-fh/` does and, for this particular claim, is better evidence.
- **There is no way to ask, in Tycho, whether a handle is still live** after a
  `close(h)` — `is_null` reads the variable, which `close` nulls, so it answers
  "closed" and "never opened" identically. Using a handle after `close` is
  documented as permitted ("a logic bug, **not** memory corruption, and … **not**
  compile-rejected"); I confirmed it (`probes/p8.ty` prints `none`, because my
  shim null-checks). For a resource whose whole selling point is static
  exactly-once release, having the one remaining misuse be undetectable in both
  directions is a gap I worked around by never calling `close` at all in `tydu` —
  scope exit does everything I need.
