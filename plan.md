# `tycho-q`: a query tool, to test the type system

Previous plan complete and archived at
[docs/internals/plan-ar-DONE.md](docs/internals/plan-ar-DONE.md).
Its unclosed discoveries carry forward at the bottom.

## Goal

Write a real program that leans on the **type system** — recursive enums,
generics, closures, `Result`/`Option` plumbing — rather than on syscalls, and
find out what the language and corelib do badly there.

`tycho-q` runs a SQL-ish query over a CSV or JSON file:

```
tycho-q 'select name, qty * price as total
         from sales.csv
         where region == "eu" and qty > 10
         order by total desc
         limit 5'
```

Done looks like: a query text goes in, a recursive `Expr` tree comes out of the
parser, an evaluator walks it against real rows, `select`/`where`/`order by`/
`limit` all work over both CSV and JSON, a gate proves it against goldens, and
`FRICTION.md` has whatever the writing surfaced.

**Why this program, and why now.** `tycho-ar` was the batch/data probe and it
answered its brief with one finding about a *language default* — a parameter is
borrowed read-only, so a streaming digest is unnatural and the whole corelib
hashes one-shot. That finding came from the language, not from the corelib, and
it was found only because a real caller wanted something a demo never wants.
`tycho-ar` is also, structurally, a flat procedural program: it has one struct,
no enum of its own, no closure, and no recursion except a directory walk. So the
half of the language the marketing is loudest about — **recursive sum types,
generics with `where` constraints, first-class function values** — has still
never been exercised by anything but `corelib/json/json.ty` and the test corpus.
A query engine cannot avoid any of it: the AST *is* a recursive enum, the
evaluator *is* an exhaustive `match` over it, `order by` *is* a comparator, and
a value that can be a string or a number or null *is* a sum type the whole
program passes around.

Three corelib packages named out-of-scope by the previous plan are in scope here
because the program genuinely needs them, not to force a number up:
`core:decimal` (and `core:bignum` beneath it) for `qty * price` that does not
lie, `core:csv` and `core:json` for input, `core:sort` and `core:iter` for the
row pipeline.

## Pre-flight

- **Worst case:** a query tool that returns *wrong rows* — a `where` that drops
  a row it should keep, a comparison that orders `"10" < "9"`, an arithmetic
  result off by a rounding. Wrong data presented as data is worse than an error.
  Mitigation: every phase verifies against a **golden output over a fixture with
  the awkward cases in it** (empty field, quoted comma, negative, `0.1 + 0.2`,
  a null, a numeric-looking string), never against a row count alone.
- **Reversibility:** total. A new program under `tools/tycho-q/`; nothing
  existing changes unless a corelib gap forces it, which is the point.
- **Verified — the corelib surface this will sit on**, read, not assumed:
  - `corelib/csv/csv.ty@parse` returns `[[string]]`. There is no header
    handling, no typing, and no streaming — the whole file becomes one array of
    arrays of strings. Every column is a `string` until this program decides
    otherwise.
  - `corelib/json/json.ty@Json` is `JNull | JBool(bool) | JNum(int) | JStr(string)
    | JArr([Json]) | JObj([string], [Json])`. **`JNum` carries an `int`** —
    `corelib/json/json.ty@parse_number` has no float path at all. A JSON input
    with `1.5` in it does not survive the parser, and finding out exactly what
    it does instead is phase 3's job, not an assumption here.
  - `corelib/sort/sort.ty@by_key` is `fn(xs: [$T], key: fn($T) -> int) -> [$T]`.
    **The key is an `int`.** There is no comparator-taking sort in the package:
    `asc`/`desc`/`argsort`/`argsort_desc` are all `where comparable(T)` over the
    values themselves. So `order by` on a string column, or on a `Decimal`, or
    on two columns with mixed directions, has no corelib route and phase 3 will
    have to write one. That is a finding, and it is why `order by` is its own
    phase.
  - `corelib/iter/iter.ty@reduce` is `fn(xs: [$T], init: $T, f: fn($T, $T) -> $T)
    -> $T` — the accumulator is the *element* type, so a fold to a different
    type is not expressible. `corelib/iter/iter.ty@filter` takes
    `fn($T) -> int`, not `-> bool`.
  - `corelib/decimal/decimal.ty` has `from_int`, `from_str`, `add`, `sub`,
    `mul`, `cmp`, `rescale`, `to_str`, `neg`, `abs`, `is_zero`. **There is no
    `div`.** A query saying `price / qty` therefore has no exact answer
    available; phase 2 decides what the program does about that rather than
    silently converting to `float`.
  - `docs/spec/03-types.md` §5.3.6: enums are nominal sum types, payload up to 8
    types, **recursive payloads permitted** (arena-allocated). §5.3.8: `fn(...)
    -> R` is first-class, structurally identified, a closure over captured
    state, and a function with an `inout` parameter **cannot** be a first-class
    value. `docs/spec/11-functions.md:70` — **a lambda's body is a single
    expression**, and closures capture by deep copy at creation.
  - `docs/spec/03-types.md` §5.3.5: map keys may be `string`, `int`, a newtype
    over either, a fieldless enum, or a hashable composite — **an enum variant
    carrying a payload is not a legal key**. A `[Value: ...]` map is therefore
    not available; anything keyed by a cell value has to key by its rendering.
- **Assuming — and each of these is written as a thing to check, not a fact:**
  - That an enum declared in a *program* (not the corelib) may be recursive
    through an array payload (`EBin(int, Expr, Expr)`, `ECall(string, [Expr])`).
    §5.3.6 says recursive payloads are permitted; it does not say they are
    permitted in every position. Risk if wrong: the AST needs indices into a
    node array instead of direct recursion, which is a bigger phase 1 and is
    itself the finding. **Phase 1 establishes this before anything else.**
  - That a `match` arm can bind a payload and recurse into the same function.
    `corelib/json/json.ty@stringify` does exactly this, so the risk is low, but
    it is a corelib file compiled by a corelib path.
  - That `core:decimal` is fast enough to put on the per-cell path. If not, the
    fix is to keep `VInt` and only widen on a fractional literal — which is the
    design anyway.
- **Deliberately not in this program:** `group by`, aggregates (`count`, `sum`),
  joins, `insert`/`update`, and any form of index. Each is a plan of its own,
  and none is needed to exercise the axis this program exists to exercise.

## Phases

- [ ] **Phase 1 — the value model, the parser, and `--explain`**
  - Scope: `tools/tycho-q/main.ty`, new. The `Value` enum, the `Expr` enum, a
    lexer over the query text, a recursive-descent parser with precedence, and
    an `--explain` mode that prints the parsed AST as an s-expression. No file
    is read and no row is evaluated in this phase.
  - **Establish first, before writing the parser:** compile a three-line probe
    declaring a recursive enum with a direct payload (`EBin(int, Expr, Expr)`)
    and one with an array payload (`ECall(string, [Expr])`). Record in the
    evidence which forms compile and the exact diagnostic for any that do not.
    If direct recursion is rejected, say so and use an index-into-array AST —
    do not silently pick one.
  - Done when: `tycho-q --explain '<query>'` prints a stable s-expression for
    each of a fixed set of queries covering literals (int, decimal, string,
    bool, null), a column reference, every binary operator with correct
    precedence and associativity, parenthesisation, `and`/`or`/`not`, `as`
    aliases, `select *`, and all four clauses; and a malformed query exits
    non-zero with a message naming the offending token and its byte offset.
  - Verify: build with `./tychoc tools/tycho-q/main.ty -o /tmp/q`, run the fixed
    query set, paste the actual output into the evidence block. Precedence is
    proved by an s-expression, not by prose — `1 + 2 * 3` must print as
    `(+ 1 (* 2 3))`.
  - Do NOT run `make test` or `make ci` — this phase adds one new file under
    `tools/` and touches no corelib, no fixture and no golden. `python3
    scripts/check_citations.py` if the evidence block cites a `path:line`.

- [ ] **Phase 2 — rows in, rows out: `from`, `where`, `select` over CSV**
  - Scope: `tools/tycho-q/main.ty` only. Read a CSV via `core:csv`, take row 0
    as the header, evaluate `where` per row, evaluate each `select` item per
    surviving row, print the result as CSV on stdout.
  - Cell typing is the substance of this phase: a cell arrives as a `string` and
    becomes a `Value`. Decide and **write down in a comment block** the rule for
    `""` (null or empty string?), for `007` (int or string?), for `1.5`
    (decimal), for `1e3`, and for a value that is numeric in one row and not in
    the next — then make the code match the block. Fail closed: a cell that
    cannot be classified is a `VStr`, never a guessed number.
  - `price / qty` has no `decimal.div` (Pre-flight). Decide what `/` does —
    reject at parse time, integer-divide, or float — and record the choice and
    its cost in the evidence. Do not add a `div` to `core:decimal` in this
    phase; that is a corelib change and goes to `plan.md` as a new phase.
  - Done when: over a fixture CSV containing an empty field, a quoted field with
    a comma, a quoted field with a newline, a negative number, a decimal, a
    numeric-looking string with a leading zero, and a UTF-8 field, each of a
    fixed set of `select`/`where` queries produces the expected CSV — including
    `where` on a string column, on a decimal comparison, on `and`/`or`/`not`,
    and one query whose `where` matches no rows (header only, exit 0).
  - Verify: the query set run against the fixture, actual stdout in the
    evidence. `0.1 + 0.2` must print `0.3` and the evidence must say so.
  - Do NOT run `make test`, `make ci` or `make ar-check`.

- [ ] **Phase 3 — `order by`, `limit`, and JSON input**
  - Scope: `tools/tycho-q/main.ty` only.
  - `order by` needs a comparator sort and `core:sort` has none (Pre-flight).
    Write the sort in the program, **stable**, multi-key, per-key direction.
    State in a comment why it is not `sort.by_key` — that is the finding, and it
    must be recorded where the next reader of the program will meet it.
  - Ordering across mixed `Value` variants needs a total order. Define it
    explicitly (where do nulls sort? does a `VStr` compare against a `VInt`?)
    in a comment block, and test both directions of every rule you write.
  - JSON input: `from x.json` over an array of objects. `JNum` is an `int`
    (Pre-flight) — establish by probe what `corelib/json/json.ty@parse_number`
    actually does with `1.5`, record the observed behaviour, and make
    `tycho-q`'s handling of it explicit rather than inherited.
  - Done when: `order by` over a decimal column, a string column, and two keys
    with mixed `asc`/`desc` each produce the expected order; stability is proved
    by a fixture with duplicate keys; `limit` truncates and `limit 0` yields the
    header only; and the same logical query over an equivalent `.csv` and
    `.json` fixture produces byte-identical output.
  - Verify: the query set run against both fixtures, actual stdout in the
    evidence, including the CSV-vs-JSON identity as a `cmp`.
  - Do NOT run `make test` or `make ci`.

- [ ] **Phase 4 — the gate, the CI step, and what the program surfaced**
  - Scope: `tools/tycho-q/run.sh` (new), `tools/tycho-q/expected.out` (new,
    **and tracked** — see carried-forward phase 19, which is exactly this trap),
    `.gitignore`, `Makefile` (`q-check`), `scripts/ci.sh` (a new step),
    `CLAUDE.md`'s gate table, `FRICTION.md`.
  - The runner builds `tools/tycho-q/main.ty`, writes its own fixtures from
    literals (never `/dev/urandom`, never copied out of the tree, so the golden
    carries no host detail), runs the whole query set, and compares against the
    golden. It must also assert the failure legs: a malformed query, a missing
    file, an unknown column, and a type error in a comparison each exit non-zero
    with the expected message on stderr.
  - `FRICTION.md` gets a new dated section, ranked worst-first, in the shape of
    the `tycho-ar` one — including the entries that got *smaller* as they were
    written, labelled as such.
  - Done when: `make q-check` is green; it reddens when broken, proved in both
    directions by an actual edit-and-revert; `git ls-files --error-unmatch
    tools/tycho-q/expected.out` succeeds; and `sh scripts/ci.sh` lists the new
    step.
  - Verify: `make q-check` green; the deliberate-break run pasted in; `python3
    scripts/check_citations.py` and `sh scripts/check_links.sh` for the prose.
    **This is the closing phase of the chain, so `make ci` runs once here** —
    and it is also a phase that adds a CI step, which is the other condition
    that earns it. Read which step reddens and fix with that step's own gate;
    never re-run `make ci` as the debugging loop.

## Carried forward

Unclosed discoveries from `docs/internals/plan-ar-DONE.md`, preserved with their
original numbering. None is part of this plan's completion.

- [ ] **Phase 5** — a skipped shim is compiled by nothing on this host: `make
      corelib` and `scripts/shim_check.sh` both skip `image` for the same missing
      libpng, so a real defect there is invisible here.
- [ ] **Phase 6** — nothing checks that a document is *reachable*; the cheap
      version of that gate would have stayed green through `docs/bootstrap.md`'s
      entire outage.
- [ ] **Phase 7** — `corelib/test/result/main.ty` claims a construct "still
      fails"; a compile probe shows it builds clean. Disproved, not yet corrected.
- [ ] **Phase 8** — `README.md:223` documents `make bootstrap` and `make
      fixpoint`; neither target exists in the `Makefile`.
- [ ] **Phase 9** — `io.read_bytes` has no counterpart: writing bytes is
      `io.write(p, to_str(b))`, correct only because `tycho_write_file` is
      length-header-driven, which the `string` signature does not say.
- [ ] **Phase 10** — there is no `eprintln`: `die` is the only route to stderr
      and it exits. A non-fatal warning is inexpressible, so it lands on stdout
      alongside a tool's actual output. Measured against `tycho-ar`'s `t`, which
      became all-or-nothing to avoid it, at the cost of a feature `tar t` has.
- [ ] **Phase 11** — no `mkdir -p` in `core:io`. `io.make_dir` is one `mkdir(2)`
      (`corelib/io/io_shim.c@iox_make_dir`), which is the correct primitive;
      every caller that writes into a tree it does not own has to build the
      component chain itself, as `tools/tycho-ar/main.ty@mkdir_p` does in 18
      lines.
- [ ] **Phase 12** — mtime is readable and not writable. `io.mtime` exists;
      nothing in `corelib/io/io.ty` or `corelib/io/io_shim.c` sets one
      (no `utimensat`/`utimes`, and no `chmod` either). `tycho-ar` therefore
      stores a faithful mtime it cannot restore, and `diff -r` does not compare
      mtimes, so the gap is invisible to the check that would otherwise catch it.
- [ ] **Phase 13** — `strings.parse_int` fails open: `""` and a leading
      non-digit both return 0, and it stops silently at the first non-digit
      (`corelib/strings/strings.ty@parse_int`). Right for user input, wrong for
      any parser with a length field. There is no strict or `Result`-returning
      counterpart in `core:strings`. **`tycho-q` phase 2 will meet this again**
      when it classifies a cell, and should cite this item rather than re-file it.
- [ ] **Phase 14** — no incremental digest anywhere in the corelib.
      `core:sha256` is `digest(msg)` / `hex(msg)` over a whole `string`;
      `corelib/sha256`, `corelib/md5`, `corelib/crypto` and `corelib/hash`
      grepped together for `sha256_(init|update|final)`, `EVP_DigestUpdate` and
      `fn (init|update|final)` return zero hits. Hashing a large file in bounded
      memory means writing your own, as `tools/tycho-ar/main.ty@sha_feed` does.
- [ ] **Phase 15** — the reason phase 14 exists: a Tycho parameter is borrowed
      read-only and `y := a` is a copy, so a streaming state cannot be threaded
      through calls without `inout`. Not a defect — a language default steering
      library shape. A `FRICTION.md` entry, not a code change.
- [ ] **Phase 16** — a package cannot mark a top-level function internal. Every
      `fn` in `corelib/sha256/sha256.ty` is reachable as `sha256.<name>` from an
      importing program. Every corelib helper is public API by accident: any
      rename breaks callers the author never knew about.
- [ ] **Phase 17** — `chr(n)` is the only route from a number to a byte; there is
      no `bytes` builder from integers, only `to_bytes(string)`. Pairs with
      phase 9.
- [ ] **Phase 19** — a new lane's golden is invisible until someone clones.
      `.gitignore` ignores `*.out` broadly and un-ignores it per directory, one
      line per lane that ever recorded a golden. `tools/tycho-ar/expected.out`
      was written, `make ar-check` went green, and the file never appeared in
      `git status`. The cheap gate is mechanical: for every `*/run.sh` naming a
      golden path, `git ls-files --error-unmatch` it. **`tycho-q` phase 4 walks
      into this same trap by construction**, which is why its scope names the
      `.gitignore` line explicitly.

(Phase 18 of that plan — `core:io` being path-based with no file handles — was
closed by its own phase 4, filed in `FRICTION.md`.)

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item. Unchanged
  from the previous plan: still a language change and still its own plan.
- **`decimal.div`.** Phase 2 will meet its absence and record it; adding it is a
  corelib change and becomes a new unchecked phase here if phase 2 wants it.
- **`group by`, aggregates, joins, indexes.** Named in the Pre-flight.
