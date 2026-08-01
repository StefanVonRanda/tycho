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

- [x] **Phase 1 — the value model, the parser, and `--explain`**
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

  - **Verified 2026-08-01.**

    **The probe, run before a line of the parser was written.** Both recursive
    forms compile and run. Direct recursion is NOT rejected, so the AST uses
    direct `Expr` payloads and NOT indices into a node array.

    ```
    $ ./tychoc <scratch>/probe/main.ty -o <scratch>/probe/p
    built <scratch>/probe/p
    $ <scratch>/probe/p
    (42 1 (f 7 (43 2 3)))
    ```

    The probe declared, in a `package main` program (not the corelib):

    ```
    enum Expr:
        ENum(int)
        EBin(int, Expr, Expr)        # DIRECT recursion
        ECall(string, [Expr])        # recursion through an ARRAY payload
    ```

    and a `show(e: Expr) -> string` that `match`es all three arms and recurses
    into itself from `EBin` and from `ECall`'s array. Zero diagnostics. The
    Pre-flight's first "Assuming" bullet is therefore CONFIRMED, in both the
    direct and the array-payload position, and its "Risk if wrong" (an
    index-into-array AST) does not apply.

    A second probe settled three more things the parser depends on: an enum
    payload may carry another package's struct (`VDec(decimal.Decimal)`);
    `decimal.from_str("1.50")` round-trips through `decimal.to_str` as
    `"1.50"`, scale preserved, so a decimal literal survives `--explain`
    byte-exactly; and a struct literal is `Tok(1, "hi", 0)`, positional — the
    `Tok{kind: 1, ...}` spelling is a lex error, "unexpected character '{'".

    **Build.**

    ```
    $ TYCHO_CORELIB=$PWD/corelib ./tychoc tools/tycho-q/main.ty -o /tmp/q
    built /tmp/q
    ```

    **The fixed query set — 19 queries, all exit 0.** Precedence and
    associativity are proved by the s-expressions, not by prose: `1 + 2 * 3`
    is `(+ 1 (* 2 3))` and `1 - 2 - 3` is `(- (- 1 2) 3)`.

    ```
    $ tycho-q --explain 'select 1 + 2 * 3 from x.csv'
    (query
      (select (+ 1 (* 2 3)))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select 1 - 2 - 3 from x.csv'
    (query
      (select (- (- 1 2) 3))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select 2 * 3 + 4 * 5 from x.csv'
    (query
      (select (+ (* 2 3) (* 4 5)))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select (1 + 2) * 3 from x.csv'
    (query
      (select (* (+ 1 2) 3))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select 10 % 3 / 2 from x.csv'
    (query
      (select (/ (% 10 3) 2))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select -1 + 2 from x.csv'
    (query
      (select (+ (neg 1) 2))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select 1, 1.50, 0.1, "eu", 'sq', true, false, null from x.csv'
    (query
      (select 1 1.50 0.1 "eu" "sq" true false null)
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select qty from x.csv'
    (query
      (select (col qty))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select a == 1, a != 1, a < 1, a <= 1, a > 1, a >= 1 from x.csv'
    (query
      (select (== (col a) 1) (!= (col a) 1) (< (col a) 1) (<= (col a) 1) (> (col a) 1) (>= (col a) 1))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select a == 1 and b > 2 or c < 3 from x.csv'
    (query
      (select (or (and (== (col a) 1) (> (col b) 2)) (< (col c) 3)))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select not a and b from x.csv'
    (query
      (select (and (not (col a)) (col b)))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select not (a == b) from x.csv'
    (query
      (select (not (== (col a) (col b))))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select a + 1 < b * 2 from x.csv'
    (query
      (select (< (+ (col a) 1) (* (col b) 2)))
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select * from x.csv'
    (query
      (select *)
      (from "x.csv")
    )
    -- exit 0

    $ tycho-q --explain 'select *, qty * price as total from data/sales.csv'
    (query
      (select * (as total (* (col qty) (col price))))
      (from "data/sales.csv")
    )
    -- exit 0

    $ tycho-q --explain 'SELECT Qty AS Total FROM Sales.CSV WHERE Qty > 10 ORDER BY Total DESC LIMIT 5'
    (query
      (select (as Total (col Qty)))
      (from "Sales.CSV")
      (where (> (col Qty) 10))
      (order (key (col Total) desc))
      (limit 5)
    )
    -- exit 0

    $ tycho-q --explain 'select name, qty * price as total
             from sales.csv
             where region == "eu" and qty > 10
             order by total desc
             limit 5'
    (query
      (select (col name) (as total (* (col qty) (col price))))
      (from "sales.csv")
      (where (and (== (col region) "eu") (> (col qty) 10)))
      (order (key (col total) desc))
      (limit 5)
    )
    -- exit 0

    $ tycho-q --explain 'select a from x.csv order by a, b desc, c asc limit 0'
    (query
      (select (col a))
      (from "x.csv")
      (order (key (col a) asc) (key (col b) desc) (key (col c) asc))
      (limit 0)
    )
    -- exit 0

    $ tycho-q --explain 'select a from 'has space.csv''
    (query
      (select (col a))
      (from "has space.csv")
    )
    -- exit 0
    ```

    **Malformed queries — 12, every one exit 1, every one naming the offending
    token and its byte offset, every one with an EMPTY stdout.**

    ```
    $ tycho-q --explain 'select a form x.csv'
    tycho-q: parse error at byte 9: unexpected token form (expected `from` after the select list)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'from x.csv'
    tycho-q: parse error at byte 0: unexpected token from (expected `select` at the start of the query)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select a, from x.csv'
    tycho-q: parse error at byte 10: unexpected token from (expected an operand (that word is a reserved keyword))
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select (1 + 2 from x.csv'
    tycho-q: parse error at byte 14: unexpected token from (expected `)` to close the group)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select a from x.csv limitt 3'
    tycho-q: parse error at byte 20: unexpected token limitt (expected the end of the query)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select 1e3 from x.csv'
    tycho-q: parse error at byte 8: unexpected token e3 (expected `from` after the select list)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select 1. from x.csv'
    tycho-q: parse error at byte 7: unexpected token 1. (expected a digit after the decimal point)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select a from x.csv where a = 1'
    tycho-q: parse error at byte 28: unexpected token = (expected an operator or an operand)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select a from x.csv where a @ 1'
    tycho-q: parse error at byte 28: unexpected token @ (expected an operator or an operand)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select 'unterminated from x.csv'
    tycho-q: parse error at byte 7: unexpected token ' (expected a closing ' for the string literal opened here)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select a as from from x.csv'
    tycho-q: parse error at byte 12: unexpected token from (expected an alias name after `as`)
    -- exit 1, stdout 0 bytes
    $ tycho-q --explain 'select a from where'
    tycho-q: parse error at byte 14: unexpected token where (expected a source path after `from`)
    -- exit 1, stdout 0 bytes
    ```

    **Friction found, all three recorded in the program's own header where the
    next reader meets them; they are the raw material for phase 4's
    `FRICTION.md` section, whose scope already names that file.**

    1. **A bare `or_return` is not a statement, and `Result(void, E)` does not
       exist — one defect seen twice.** A helper that can only fail must still
       return something, so `eat_kw` returns `Result(int, PErr)` ending in a
       meaningless `Ok(0)`; and that meaningless value must then be BOUND at
       every call site, because `eat_kw(...) or_return` alone on a line is
       rejected with "a statement must be a declaration, assignment, or call --
       a bare expression has no effect". `docs/spec/10-statements.md:16-18`
       names `or_return` among the forms refused as a bare-expression
       statement, so this is specified, not a compiler quirk.
       `tools/tycho-q/main.ty` opens `parse_query` with `_ := eat_kw(...)`
       and carries `_ = ...` twice more purely to satisfy it.
    2. **There is no no-op statement.** An absent `where` should print nothing,
       so the `None` arm of the match has no work — and an empty arm cannot be
       spelled. `pass` is not a keyword (the same "must be a declaration,
       assignment, or call" error), and grepping `corelib/`, `tools/` and
       `server/` for a bare `pass` line returns nothing outside this file's
       own rejected draft, so nothing in the tree had hit it before. The
       workaround is structural: lift the match into a function whose arms all
       `return`, carrying the emptiness as a value —
       `tools/tycho-q/main.ty@where_line` and
       `tools/tycho-q/main.ty@limit_line` exist only for that reason.
    3. **A cursor threaded by `inout` cannot also be passed by value in the
       same call.** `lex_string(s, n, pos, &pos)` is refused: "variable 'pos'
       is passed to an inout parameter and also by value in the same call to
       'lex_string' (overlapping access -- the by-value copy would alias the
       inout'd value)". The diagnostic is good and the rule is right; the
       consequence is a shape rule for anyone writing a lexer here — a function
       wanting both "where I started" and "where I am" derives the first from
       the second on entry, which is why `lex_string` and `lex_number` take no
       `start` parameter.

    **Not friction, recorded so it is not re-derived:** `Option(Expr)` and
    `Option(int)` are usable as struct fields over a program-local recursive
    enum, `None` infers its type from the field it is assigned to, and
    `Some(parse_expr(...) or_return)` composes — the `Result`/`Option` half of
    the Pre-flight's second "Assuming" bullet needed no workaround anywhere.

    **Gates.** Per this phase's brief and `CLAUDE.md`'s gate table: no
    `make test`, no `make test-fast`, no `make ci`, no `make ar-check` — the
    phase adds one new file under `tools/` and touches no corelib, no fixture
    and no golden, so none of them can redden for it.
    `python3 scripts/check_citations.py` was run for this evidence block.

- [x] **Phase 2 — rows in, rows out: `from`, `where`, `select` over CSV**
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

  - **Verified 2026-08-01.**

    **Build.**

    ```
    $ TYCHO_CORELIB=$PWD/corelib ./tychoc tools/tycho-q/main.ty -o /tmp/q
    built /tmp/q
    ```

    **The fixture**, written to the scratchpad (not into the tree — phase 4
    owns fixtures). It carries every awkward case the phase names: an empty
    field (`Di`'s region), a quoted field with a comma, a quoted field with an
    embedded newline, a negative number (`-3`), decimals at two scales, a
    numeric-looking string with a leading zero (`007`, `0080`), and a UTF-8
    field (`café`).

    ```
    name,region,qty,price,code,note
    Ada,eu,12,1.50,007,"Hello, world"
    Bo,us,-3,0.10,42,"line one
    line two"
    Cy,eu,7,0.20,0080,café
    Di,,0,2.00,9,plain
    ```

    ### DECISION 1 — cell typing, by ROUND TRIP

    Written in full in `tools/tycho-q/main.ty`'s header under `DECISION 1 --
    HOW A CSV CELL BECOMES A `Value``, and implemented by
    `tools/tycho-q/main.ty@num_shape` and `tools/tycho-q/main.ty@classify`.
    The rule in one sentence: **a cell becomes a number only if rendering that
    number back reproduces the cell's bytes exactly; otherwise it stays a
    `VStr`. An empty cell is `VNull`.** In order: `""` → `VNull`; a shape check
    for `-? digit+ ( "." digit+ )?` and nothing else; `VInt` if `str(n) ==
    cell`; `VDec` if `decimal.to_str(d) == cell`; else `VStr`.

    `strings.parse_int` is **bracketed, not replaced** — the shape check runs
    before it so it never sees a byte it could mis-read, and the round trip
    runs after it so a value it read differently from how this program renders
    is discarded. Bracketing beats reimplementing because the round trip also
    catches integer overflow for free. This is
    `docs/internals/plan-ar-DONE.md` phase 13, carried forward, met exactly
    where that item predicted, and **cited rather than re-filed**.

    **The rule is verified per case, not asserted.** The probe fixture below
    puts one edge case per row; `v * 1` succeeds only on a numeric cell, and
    the failure message names the kind the classifier chose, so each line is a
    verdict on one cell.

    ```
    $ cat types.csv
    n,v
    1,007
    2,0
    3,-0
    4,-3
    5,1.50
    6,1e3
    7,+1
    8, 1
    9,1.
    10,.5
    11,12345678901234567890123456
    12,

    $ for k in 1..12; do tycho-q "select v * 1 as t from types.csv where n == $k"; done
    n=1   tycho-q: `*` needs two numbers, got string "007" and int 1
    n=2   0
    n=3   tycho-q: `*` needs two numbers, got string "-0" and int 1
    n=4   -3
    n=5   1.50
    n=6   tycho-q: `*` needs two numbers, got string "1e3" and int 1
    n=7   tycho-q: `*` needs two numbers, got string "+1" and int 1
    n=8   tycho-q: `*` needs two numbers, got string " 1" and int 1
    n=9   tycho-q: `*` needs two numbers, got string "1." and int 1
    n=10  tycho-q: `*` needs two numbers, got string ".5" and int 1
    n=11  12345678901234567890123456
    n=12  tycho-q: `*` needs two numbers, got null (empty) and int 1
    ```

    Reading that table against the decisions the phase asked for:

    - **`""` is `VNull`**, not the empty string (n=12 reports `null (empty)`).
      CSV has exactly one in-band way to say "nothing here". If `""` were
      `VStr("")` then absence would be inexpressible from data and `VNull`
      would be reachable only from a query literal. It is free on the way out
      (both render as an empty field) and costs one thing on the way in, stated
      in the header: `where region == ""` does **not** match an empty field —
      write `where region == null`, which works, proved below.
    - **`007` is a `VStr`** (n=1). The leading zero is *evidence* that the
      column is a code. A part number, a zip code and a phone number all die if
      `007` becomes `7`.
    - **`1.5`/`1.50` are `VDec`** (n=5 prints `1.50`) — `core:decimal`
      preserves the parsed scale, so trailing zeros survive.
    - **`1e3` is a `VStr`** (n=6): it fails the shape check at `e`, which is
      the same answer phase 1's query lexer gives the literal `1e3`. The same
      text means the same thing in both positions.
    - **`-0` is a `VStr`** (n=3), and this one was not on the phase's list by
      accident — it is the case that makes the round-trip rule earn its keep.
      Both numeric paths render `-0` as `0`, so typing it would **rewrite the
      cell on the way out**, and a query that neither filters nor computes on
      a column must never alter it.
    - **A column numeric in one row and not the next**: typing is **per cell**,
      never per column. `code` yields `VStr, VInt, VStr, VInt` across four
      rows. The alternative (infer a column type, coerce the rest) must either
      reject the file or corrupt a cell. What happens instead is a loud error
      naming both kinds — see the `code == 42` failure leg.
    - **Bonus, and it was free:** a 26-digit integer (n=11) survives exactly.
      `VInt` is rejected because integer overflow *wraps*
      (`docs/spec/09-expressions.md:31`) so the wrapped value fails the round
      trip; `VDec` then takes it on `core:bignum`. The round trip is therefore
      also this program's overflow check.

    **The typing fixture round-trips byte-exactly**, which is the strongest
    statement of the rule — no cell in it is altered by being read and written:

    ```
    $ tycho-q 'select * from types.csv' | cmp - types.csv && echo byte-identical
    byte-identical
    ```

    ### DECISION 2 — `/` is exact-only

    `corelib/decimal/decimal.ty` has no `div`, and its own header says why:
    division needs a target scale and a rounding policy and it has neither.
    The phase offered three options and all three lose:

    - **Reject `/` at parse time** — wrong twice. `/` is already in the
      accepted grammar and in phase 1's verified `--explain` output (`10 % 3 /
      2` → `(/ (% 10 3) 2)`), so removing it would invalidate verified work;
      and it would refuse `6 / 2`, which has a perfectly good exact answer.
    - **Convert to float** — there is no float in this program on purpose.
      `qty * price / 2` would then lie in exactly the way `core:decimal` exists
      to prevent, and lie *quietly*, which is the Pre-flight's worst case.
    - **Integer-divide (truncate)** — honest for `VInt / VInt` if documented,
      but it has no answer at all when either side is a `VDec`, so it decides
      half the question and leaves the other half to be decided again.

    **Chosen: `/` computes when the answer is exact and errors when it is
    not.** `VInt / VInt` with remainder 0 → `VInt`; inexact → error; anything
    involving a `VDec` → error; divisor zero → error, and the zero is checked
    *before* the operator because division by a zero value **aborts the
    process** (`docs/spec/09-expressions.md:27-28`) and an abort is not an
    error message. `%` is the same shape, two `VInt`s only.

    ```
    $ tycho-q 'select name, qty / 3 as third from fix.csv where name == 'Ada''
    name,third
    Ada,4
    -- exit 0
    ```

    **The cost, and it is large.** `select total / count` — the ordinary
    averaging query, the single most common reason anyone writes `/` in SQL —
    fails on almost all real data. That is not a small loss and this is not
    pretending otherwise; it is the price of refusing to invent a rounding
    policy silently. The fix is a real `decimal.div(a, b, scale, mode)` with
    scale and mode named by the caller. That is a corelib change and is filed
    as **phase 20** below rather than smuggled in here, as this plan's "Out of
    scope" section requires.

    ### The query set — actual stdout, all exit 0

    ```
    $ tycho-q 'select * from fix.csv'
    name,region,qty,price,code,note
    Ada,eu,12,1.50,007,"Hello, world"
    Bo,us,-3,0.10,42,"line one
    line two"
    Cy,eu,7,0.20,0080,café
    Di,,0,2.00,9,plain
    -- exit 0

    $ tycho-q 'select name, code, note from fix.csv'
    name,code,note
    Ada,007,"Hello, world"
    Bo,42,"line one
    line two"
    Cy,0080,café
    Di,9,plain
    -- exit 0

    $ tycho-q 'select name from fix.csv where region == 'eu''      # where on a string column
    name
    Ada
    Cy
    -- exit 0

    $ tycho-q 'select name, price from fix.csv where price > 0.15'  # decimal comparison
    name,price
    Ada,1.50
    Cy,0.20
    Di,2.00
    -- exit 0

    $ tycho-q 'select name from fix.csv where region == 'eu' and qty > 10'
    name
    Ada
    -- exit 0

    $ tycho-q 'select name from fix.csv where region == 'eu' or qty < 0'
    name
    Ada
    Bo
    Cy
    -- exit 0

    $ tycho-q 'select name from fix.csv where not (region == 'eu')'
    name
    Bo
    Di
    -- exit 0

    $ tycho-q 'select name, qty * price as total from fix.csv where qty > 0'
    name,total
    Ada,18.00
    Cy,1.40
    -- exit 0

    $ tycho-q 'select 0.1 + 0.2 as sum from fix.csv where name == 'Ada''
    sum
    0.3
    -- exit 0

    $ tycho-q 'select name from fix.csv where region == 'zz''       # matches no rows
    name
    -- exit 0

    $ tycho-q 'select name from fix.csv where region == null'       # the null test
    name
    Di
    -- exit 0

    $ tycho-q 'select *, qty * 2 as dbl from fix.csv where name == 'Bo''
    name,region,qty,price,code,note,dbl
    Bo,us,-3,0.10,42,"line one
    line two",-6
    -- exit 0

    $ tycho-q 'select name, qty * price from fix.csv where name == 'Cy''
    name,(* (col qty) (col price))
    Cy,1.40
    -- exit 0

    $ tycho-q 'select name, qty / 3 as third from fix.csv where name == 'Ada''
    name,third
    Ada,4
    -- exit 0
    ```

    **`0.1 + 0.2` prints `0.3`** — stated explicitly because the phase requires
    it. Not `0.30000000000000004` and not `0.30`: `core:decimal` aligns the two
    scale-1 operands and adds coefficients exactly, and `decimal.to_str` prints
    the stored scale, so the answer is the three characters `0.3`. There is no
    float anywhere in this program.

    Two more things that row set proves and that a row count would not have:

    - **`select *` round-trips the fixture byte-for-byte**, including the
      quoted comma, the embedded newline, `café`, `007`, `0080` and the empty
      field. This is the Pre-flight's "wrong rows presented as data" check in
      its strongest form:

      ```
      $ tycho-q 'select * from fix.csv' | cmp - fix.csv && echo byte-identical
      byte-identical
      ```
    - **The empty `where` result is a header and nothing else, exit 0** — an
      empty result is not an error and must not look like one.
    - An un-aliased computed column gets its s-expression as a header
      (`(* (col qty) (col price))`). Ugly on purpose: it is a legible
      instruction to write `as`.

    ### The failure legs — every one exit 1, every one with EMPTY stdout

    The whole result is built before anything is printed, which is what makes
    the empty-stdout guarantee true rather than incidental: an error on the
    last row of a million must not leave 999,999 rows already emitted, because
    a consumer reading a truncated CSV cannot tell that it is truncated.

    ```
    $ tycho-q 'select nope from fix.csv'                       # unknown column
    tycho-q: no such column: nope (the header has: name, region, qty, price, code, note)
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select name from fix.csv where code == 42'      # incompatible variants
    tycho-q: cannot compare string "007" with int 42
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select * from missing.csv'                      # missing input file
    tycho-q: no such file: missing.csv
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select name, qty / 2 as h from fix.csv'         # DECISION 2, inexact
    tycho-q: `/` is exact-only: -3 / 2 is not a whole number, and core:decimal has no div, so there is no rounding policy to apply (see DECISION 2 at the top of tools/tycho-q/main.ty)
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select name, price / qty as unit from fix.csv'  # DECISION 2, decimal
    tycho-q: `/` on a decimal has no exact result: core:decimal has no div (see DECISION 2 at the top of tools/tycho-q/main.ty)
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select name from fix.csv where qty'             # no truthiness
    tycho-q: `where` needs a boolean, got int 12 -- there is no truthiness here
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select name from fix.csv where price < 'x''     # incompatible ordering
    tycho-q: cannot compare decimal 1.50 with string "x"
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select name, qty + note as bad from fix.csv'    # `+` is not concatenation
    tycho-q: `+` needs two numbers, got int 12 and string "Hello, world"
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select * from ragged.csv'                       # a,b / 1,2 / 3
    tycho-q: ragged.csv: row 3 has 1 fields but the header has 2
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select * from dup.csv'                          # header a,a
    tycho-q: the header of dup.csv names the column `a` twice
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select * from empty.csv'                        # zero bytes
    tycho-q: empty file, so there is no header row: empty.csv
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select * from .'                                # a directory
    tycho-q: parse error at byte 14: unexpected token . (expected an operator or an operand)
    -- exit 1, stdout 0 bytes
    ```

    **Refused, not ignored.** `order by`, `limit` and a `.json` source all still
    parse — `--explain` prints them, so phase 1's golden is untouched — and are
    refused with a non-zero exit when a query is actually *run*:

    ```
    $ tycho-q 'select name from fix.csv order by name'
    tycho-q: `order by` is not implemented yet -- refused rather than ignored
    -- exit 1, stdout 0 bytes
    $ tycho-q 'select name from fix.csv limit 2'
    tycho-q: `limit` is not implemented yet -- refused rather than ignored
    -- exit 1, stdout 0 bytes
    $ tycho-q 'select * from fix.json'
    tycho-q: JSON input is not implemented yet -- refused rather than ignored: fix.json
    -- exit 1, stdout 0 bytes
    ```

    A query tool that accepts `limit 5` and returns 400 rows is worse than one
    that does not accept `limit` at all, because the first one is believed.

    **Unknown columns are checked once, before any row is evaluated**
    (`tools/tycho-q/main.ty@check_cols`). Without that, a query over a
    header-only file would print a header naming a column that does not exist
    and exit 0 — a check that runs per row cannot fail on a file with no rows:

    ```
    $ printf 'a,b\n' > hdronly.csv
    $ tycho-q 'select nope from hdronly.csv'
    tycho-q: no such column: nope (the header has: a, b)
    -- exit 1, stdout 0 bytes
    $ tycho-q 'select a from hdronly.csv'
    a
    -- exit 0
    ```

    ### Friction found

    **1. `core:iter` is unusable for any pipeline stage that can fail. Two
    walls, both probed, and the second is fatal.** The row filter is the exact
    shape `core:iter` exists for and it cannot be written with it.

    ```
    $ # probe 1: a predicate returning the language's own bool
    ys := iter.filter(xs, fn(x: int) -> bool: x > 1)
    error: argument 2 of 'iter__filter' is fn(int) -> bool, which does not fit
           the parameter pattern

    $ # probe 2: a predicate that can fail -- the real case here
    ys := iter.filter(xs, fn(x: int) -> int: chk(x) or_return)
    error: or_return requires the enclosing function to return a Result, but it
           returns int
    ```

    `corelib/iter/iter.ty@filter` is `fn(xs: [$T], keep: fn($T) -> int)`, so
    every caller spells its predicate as a 0/1 `int` in a language that has
    `bool` — the Pre-flight recorded that signature and this measures what it
    costs. The second wall is the one that decides the program: this
    predicate *can* fail (an incomparable pair, a `/` with no exact answer), so
    it is a `Result`, and a lambda cannot propagate one. A lambda's body is a
    single expression (`docs/spec/11-functions.md:70`) and its return type is
    fixed by the parameter pattern it is passed to, so there is no way to widen
    it to `Result` and no way to write the handling `match` inline.
    `core:iter` has no fallible counterpart — no `try_filter`, no `filter` over
    `fn($T) -> Result(int, $E)`. The consequence is not a workaround, it is a
    different program: a plain `for` loop with `or_return` in the body. **Every
    higher-order combinator in `core:iter` is unavailable to every stage of a
    query engine that can fail, which is most of them.**

    **2. `Result(void, E)` is still not expressible — the third sighting of the
    defect phase 1 found twice.** `tools/tycho-q/main.ty@check_cols` only
    succeeds or fails, and so must return `Result(int, RErr)` with an `Ok(0)`
    nobody reads, whose value the recursive call must then *bind* (`_ :=
    check_cols(l, hdr) or_return`) because a bare `or_return` is not a
    statement. Phase 1 met this in `eat_kw`; it is not a parser-shaped problem,
    it is what happens to any validator.

    **3. The no-op statement, twice more, and the shape of the workaround is
    now clear.** Phase 1 found that an empty `match` arm cannot be spelled.
    Two new sites hit it where phase 1's fix (lift the match into a function
    whose arms all `return`) does not apply, because the match is in the middle
    of a statement sequence: the `Ok(sz)` arm of the `io.size` check wants only
    "stat succeeded", and the `IStar` arm of the column check has no column to
    check. **A declaration is a legal statement where a bare expression is
    not**, so the workaround at both sites is a dummy binding — `ok := sz` and
    `_ := 0`. That is cheaper than phase 1's lift and it is worth recording as
    the general answer: when an empty arm is needed mid-sequence, bind
    something.

    **4. An enum has no way to read its variant without binding a payload.**
    There is no `is`, no tag accessor, and a match arm must name a binder it
    never uses. `Value` needs its tag constantly — every comparison rule and
    every arithmetic rule is "what kind is this" — so
    `tools/tycho-q/main.ty@kind` is the tag accessor the language does not
    have, hand-written once, with three binders that are pure decoration. It
    compiles, so writing it once and switching on an `int` everywhere is much
    the lesser evil; the alternative is that decoration at every site.

    **5. Two error types cannot share an `or_return` chain.** `PErr` (parse,
    has a byte offset) and `RErr` (runtime, has a column name or nothing)
    genuinely differ, so folding them would put a meaningless `off: 0` on two
    thirds of the messages. But `Result(T, PErr)` and `Result(T, RErr)` are
    different types and there is no `From`-style error conversion, so no
    function can propagate across the boundary. Here they meet only in `main`
    and the cost is one extra `match`; a program with three error types and a
    call graph that mixed them would pay this at every boundary, with no
    language feature to make it cheaper.

    **Not friction, recorded so it is not re-derived:** nested patterns over
    another package's enum work — `Err(io.NotFound)` and `Err(io.IsDir)` match
    directly, qualified by package (the unqualified `NotFound` is *not* a
    variant in scope: "error: 'NotFound' is not a variant of io__IoErr"). And
    `print` exists as a builtin alongside `println` (`src/tychoc.c:4519`),
    which matters here because `csv.stringify` already newline-terminates every
    row and `println` would add a blank line a byte-exact golden must carry.

    **A tool wart, not a language one:** phase 1's bare-path lexer starts at a
    letter, so an **absolute** source path cannot be written bare — `from
    /tmp/x.csv` lexes the leading `/` as division and fails with "expected a
    source path after `from`". Phase 1's escape hatch already covers it
    (`from '/tmp/x.csv'`, verified working), and it is now spelled out in the
    header because the diagnostic talks about division while the cause is
    quoting.

    ### Out-of-scope work found, and what was done about it

    **The citation gate was already RED at this phase's parent commit
    (66608ad), and this phase fixed it.** Phase 1's evidence says
    `check_citations.py` "was run for this evidence block", which is true of
    the block and was not true of the tree: `git stash && python3
    scripts/check_citations.py` at HEAD reported **`FAILED (4 stale
    citation(s))`**, all four in `tools/tycho-q/main.ty`, all four rotating
    "`plan.md` phase N" references of exactly the class `CLAUDE.md` describes.
    All four are inside this phase's scope file, and the gate could not be made
    green without them, so they were repaired here rather than filed: two
    now name `docs/internals/plan-ar-DONE.md` phases 10 and 13 (verified —
    that document declares both, at lines 698 and 720), and two were rewritten
    to carry the fact without the rotating pointer. This is recorded rather
    than done quietly because the lesson generalises: **a gate that a phase
    "ran" can still be red, if what was run was checked against the evidence
    instead of against the tree.**

    ### Gates

    Per this phase's brief and `CLAUDE.md`'s gate table: **no `make test`, no
    `make test-fast`, no `make ci`, no `make ar-check`.** This phase edits one
    file under `tools/` and touches no corelib, no fixture and no golden, so
    none of them can redden for it; the fixtures it wrote live in the
    scratchpad, not in the tree. Run: the compile after every edit (shown
    above), the query set, and `python3 scripts/check_citations.py`, which is
    now **`ok`** — up from `FAILED (4 stale)` at the parent commit.

- [x] **Phase 3 — `order by`, `limit`, and JSON input**
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

  - **Verified 2026-08-01.**

    **Build.**

    ```
    $ TYCHO_CORELIB=$PWD/corelib ./tychoc tools/tycho-q/main.ty -o /tmp/q
    built /tmp/q
    ```

    ### The `1.5` probe — three different failures, none of them an error

    The Pre-flight recorded that `corelib/json/json.ty@parse_number` has no
    float path. It does not say what happens instead, and the phase brief
    forbade assuming. Reading first: that function takes an optional `-`, then
    digits, and returns — it never consumes the `.`. `corelib/json/json.ty:81-92`
    (`parse_array`) loops until `]` and advances only on `,`. That predicts
    non-termination, so the probe was run under `ulimit -v 2000000` and
    `timeout`. **The prediction was right for one of three cases and the other
    two are worse**, because they exit 0:

    ```
    $ ./pj '1.5'
    in:  1.5
    kind:num
    out: 1
    exit 0

    $ ./pj '[1.5]'
    in:  [1.5]
    tycho: out of memory
    exit 1

    $ ./pj '[{"a":1.5}]'
    in:  [{"a":1.5}]
    kind:arr
    out: [{"a":1,"5}]":null}]
    exit 0
    ```

    - **Bare `1.5` → `JNum(1)`, exit 0.** Silent truncation.
    - **`[1.5]` → out of memory, exit 1.** `parse_value` consumes nothing at
      `.`, the array loop never advances, and `JNum(0)` is pushed until memory
      runs out. Five bytes of input.
    - **`[{"a":1.5}]` → exit 0 with a corrupted tree.** The leftover `.5}]` is
      read as the next KEY. This is the shape `tycho-q` actually reads, and it
      is the one with no symptom at all.

    `json.parse` returns `Json` and not `Result(Json, E)` — **there is no error
    channel in the package** — so no caller can ask whether any of this
    happened. `corelib/json/json.ty:12-13` documents the parser as lenient, and
    it is, in the sense that it always returns something.

    **So `tycho-q` validates before it parses** rather than inheriting this:
    `tools/tycho-q/main.ty@json_guard` walks the raw bytes and accepts only
    what `core:json` can faithfully represent. Two layers, and the second is
    load-bearing rather than tidy: the token alphabet, and **bracket nesting**,
    because the non-termination needs a byte that `parse_value` consumes none
    of sitting at a value position inside an array — `}` and `:` are the only
    two the alphabet still admits, and requiring brackets to nest is what makes
    them unreachable there. Proof that the hole is actually closed, on the
    smallest spinning input, run under the same memory cap:

    ```
    $ tycho-q 'select * from float.json'          # [{"a":1.5}]
    tycho-q: float.json: byte 6: JSON numbers here must be integers, and `1.5`
    is not -- core:json has no float path at all and would read it as 1 with no
    diagnostic. Write it as the string "1.5" if the text is what matters (see
    DECISION 3 at the top of tools/tycho-q/main.ty)
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select * from spin.json'           # [}]  -- OOMs raw json.parse
    tycho-q: spin.json: byte 1: a `}` here closes nothing that was opened
    -- exit 1, stdout 0 bytes
    ```

    What the guard does NOT claim is full JSON validation: `[1 2]` passes it
    and `core:json` reads two elements. That is lenient but not dangerous — it
    terminates and invents no data — and closing it would mean writing a second
    JSON parser to check the first.

    ### The total order (ORDER 1 in `tools/tycho-q/main.ty`)

    `where` refuses a cross-kind comparison. A sort **cannot** refuse: it
    assigns a seat, not a truth value, and a comparator that errors part-way
    yields no output at all on a file whose only crime is a column with two
    kinds in it — which DECISION 1 calls normal. So ordering uses a separate,
    total order, and that asymmetry is the decision:

    - **Kind rank first:** `null < bool < number < string`. Nulls sort first
      ascending; `desc` negates the whole comparison including the rank, so
      nulls sort last descending. There is no NULLS FIRST/LAST modifier.
    - **Within a rank, the same rule `where` uses**, structurally rather than
      by promise: `tools/tycho-q/main.ty@ord_cmp` calls `cmp_vals`, the same
      function `where` calls, and equal rank guarantees it cannot fail.
      `VInt` and `VDec` share one rank so they compare BY VALUE — `7` equals
      `7.0` here exactly as it does to `==`.

    **Every rule tested in both directions.** `mix.csv` holds one awkward value
    per row; all keys are distinct except the `7`/`7.0` pair, so `desc` must be
    the exact reverse of `asc` *except* there:

    ```
    $ tycho-q 'select id, v from mix.csv order by v'
    id,v
    2,          <- null first
    4,1.5       <- numbers, by value: 1.5 < 7 < 42
    6,7
    7,7.0       <- VInt 7 and VDec 7.0 are EQUAL; input order breaks the tie
    1,42
    5,007       <- strings last, byte order: "007" < "zz"
    3,zz
    -- exit 0

    $ tycho-q 'select id, v from mix.csv order by v desc'
    id,v
    3,zz
    5,007
    1,42
    6,7         <- STILL 6 before 7: the tie did NOT reverse
    7,7.0
    4,1.5
    2,          <- null last
    -- exit 0
    ```

    CSV cannot produce a `VBool` at all (DECISION 1 types by round trip and
    `VBool` is not in it), so the bool rank is tested from JSON, which can:

    ```
    $ tycho-q 'select id, v from mix.json order by v'
    id,v
    2,          <- null
    5,false     <- bool: false < true
    4,true
    6,7         <- number
    1,42
    3,zz        <- string
    -- exit 0

    $ tycho-q 'select id, v from mix.json order by v desc'
    id,v
    3,zz
    1,42
    6,7
    4,true
    5,false
    2,
    -- exit 0
    ```

    Both directions of every rank boundary — null/bool, bool/number,
    number/string — plus false<true, byte order, and value order across
    `VInt`/`VDec`.

    ### The sort, and why it is not `core:sort` (ORDER 2)

    Read against the signatures rather than asserted:
    `corelib/sort/sort.ty@by_key` takes `key: fn($T) -> int`, and this order
    ranks by kind and then by value across `VStr` and `decimal.Decimal` — there
    is no order-preserving injection of that into one `int`, because a string
    does not fit in one. `corelib/sort/sort.ty@asc` and
    `corelib/sort/sort.ty@desc` are `where comparable(T)` over the values and
    take ONE array and ONE direction, so `order by a asc, b desc` — two keys,
    two directions, same rows — fits no signature in the package;
    `corelib/sort/sort.ty@argsort` has the same ceiling. Hence
    `tools/tycho-q/main.ty@merge_sort`: bottom-up, iterative (no slicing
    needed), stable, comparator as a first-class value.

    **Stability is a property of the merge, not of a tiebreak.** The merge
    takes the left run on `<= 0`. A total-by-index comparator would look
    identical on every test above and is deliberately not used: it would
    reverse tie order under `desc`, and then `order by k desc limit 3` on a
    column with ties would return different rows by a rule nobody wrote down.
    Proved on `ord.csv`, which has two duplicate-key pairs — `0.20` (Bo, Eve)
    and `1.50` (Ada, Fox):

    ```
    $ tycho-q 'select name, price from ord.csv order by price'
    name,price
    Bo,0.20        <- tie: input order Bo, Eve
    Eve,0.20
    Ada,1.50       <- tie: input order Ada, Fox
    Fox,1.50
    Di,2.05
    Cy,10.00       <- numeric, NOT string order ("10.00" would sort first)
    -- exit 0

    $ tycho-q 'select name, price from ord.csv order by price desc'
    name,price
    Cy,10.00
    Di,2.05
    Ada,1.50       <- STILL Ada before Fox
    Fox,1.50
    Bo,0.20        <- STILL Bo before Eve
    Eve,0.20
    -- exit 0
    ```

    The Pre-flight's `"10" < "9"` worry is disproved by the last line of the
    first listing: `10.00` sorts after `2.05`, by value.

    **A probe corrected a phase 2 finding.** Before writing the sort, a
    fallible higher-order function was probed:

    ```
    fn msort(xs: [int], f: fn(int, int) -> Result(int, E)) -> Result([int], E)
    ```

    ```
    $ ./pc
    named comparator: 123
    closure comparator: 120
    propagated: 99 does not compare
    ok
    ```

    It compiles, accepts **both** a named function and a lambda in that
    position, and `or_return` propagates a comparator failure out through it.
    So phase 2's wall narrows: it was `corelib/iter/iter.ty@filter`'s
    **signature** pinning the lambda's return type to `int`, **not** a language
    rule against fallible higher-order functions. A caller who declares the
    parameter type gets the fallible version. This is recorded in
    `tools/tycho-q/main.ty`'s ORDER 2 block as a correction, not a new
    complaint — and phase 4's `FRICTION.md` entry for `core:iter` should say
    "the corelib's signature", not "the language".

    This sort does not need it: the order is total by construction, so the
    comparator is `fn(int, int) -> int` and declaring a `Result` would be
    machinery around an `Err` that cannot be built. What can fail is evaluating
    the KEYS, which happens once per row before the sort — also the O(n log n)
    argument, since a key computed inside the comparator is computed a
    logarithmic factor too often and `qty * price` is a `core:decimal` multiply.

    ### Multi-key, mixed directions, and `limit`

    ```
    $ tycho-q 'select name, region, price from ord.csv order by region asc, price desc'
    name,region,price
    Cy,eu,10.00
    Ada,eu,1.50
    Eve,eu,0.20
    Di,us,2.05
    Fox,us,1.50
    Bo,us,0.20
    -- exit 0

    $ tycho-q 'select name, qty from ord.csv order by qty desc, name asc'
    name,qty
    Ada,7
    Cy,7
    Eve,7
    Bo,3
    Fox,3
    Di,1
    -- exit 0

    $ tycho-q 'select name, price from ord.csv order by price desc limit 3'
    name,price
    Cy,10.00
    Di,2.05
    Ada,1.50
    -- exit 0

    $ tycho-q 'select name from ord.csv order by name limit 0'
    name
    -- exit 0                      # header only, and exit 0: an empty answer
                                   # is not an error

    $ tycho-q 'select name from ord.csv limit 2'
    name
    Ada
    Bo
    -- exit 0

    $ tycho-q 'select name from ord.csv limit 99'   # limit > rows: all rows
    name
    Ada
    Bo
    Cy
    Di
    Eve
    Fox
    -- exit 0
    ```

    `limit` applies AFTER the ordering, so `order by x limit 3` is the top
    three by x. `limit 0` stays an `Option(int)` rather than a sentinel 0
    precisely so it can differ from no `limit` at all.

    ### CSV and JSON produce byte-identical output

    `pair.csv` and `pair.json` carry the same four rows, spelled equivalently —
    JSON **numbers** for the numeric column, per DECISION 3's cost note. One
    logical query, both sources, compared with `cmp`:

    ```
    $ tycho-q "select name, qty from pair.csv where region == 'eu' order by qty desc, name asc limit 2" > out.csv
    name,qty
    Ada,12
    Cy,7
    $ tycho-q "select name, qty from pair.json where region == 'eu' order by qty desc, name asc limit 2" > out.json
    name,qty
    Ada,12
    Cy,7
    $ cmp out.csv out.json; echo "cmp exit $?"
    cmp exit 0
    ```

    That query exercises `where`, both `order by` directions, a tie broken by
    the second key, and `limit`, over both readers. `select *` also matches
    byte-for-byte, which additionally proves the JSON header (union of keys, in
    first-appearance order) reproduces the CSV column order:

    ```
    $ tycho-q 'select * from pair.csv' > a.csv
    $ tycho-q 'select * from pair.json' > a.json
    $ cat a.csv
    name,region,qty
    Ada,eu,12
    Bo,us,3
    Cy,eu,7
    Di,eu,7
    $ cmp a.csv a.json; echo "cmp exit $?"
    cmp exit 0
    ```

    ### The JSON row rules, and the failure legs

    A missing key is `VNull`; the header is the union of all keys in
    first-appearance order, built in its own pass **before** any cell is read,
    because a column first seen in the last row is still a column and every
    earlier row needs a null in it:

    ```
    $ cat sparse.json
    [{"a":1,"b":2},{"b":5},{"a":9,"c":"new"}]
    $ tycho-q 'select * from sparse.json'
    a,b,c
    1,2,
    ,5,
    9,,new
    -- exit 0
    ```

    CSV refuses a short row while JSON accepts a sparse object, which looks
    inconsistent and is not: a short CSV row means the **delimiters** are
    wrong, so field k is not missing but MISALIGNED — every value after the gap
    sits under the wrong column. A JSON key is NAMED, so an absent key is
    unambiguous and nothing shifts. Absence is expressible in JSON and is not
    expressible in CSV.

    ```
    $ tycho-q 'select * from nest.json'        # [{"a":[1,2]}]
    tycho-q: nest.json: row 1, key `a`: the value is an array, and a table cell
    cannot hold one -- flattening it would invent a spelling that every later
    query would then depend on (see DECISION 3 at the top of tools/tycho-q/main.ty)
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select * from dupkey.json'      # [{"a":1,"a":2}]
    tycho-q: dupkey.json: row 1 names the key `a` twice
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select * from obj.json'         # {"a":1}
    tycho-q: obj.json: the top level of a JSON source must be an ARRAY of
    objects, one object per row, and this is an object
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select * from emptyarr.json'    # []
    tycho-q: an empty array, so there are no keys and therefore no header:
    emptyarr.json
    -- exit 1, stdout 0 bytes
    ```

    A JSON string is **not** re-classified — `"42"` stays the string `"42"`.
    DECISION 1's round trip exists because CSV LOST the type; JSON did not, and
    re-running it would overrule an author to guess back what they already
    stated. The cost is the CSV/JSON asymmetry above: equivalent files need
    equivalent spelling.

    ### `order by` on a select alias — needed for the plan's own headline query

    The Goal's example is `select name, qty * price as total ... order by total
    desc`, where `total` is an alias and not a column. Without resolution that
    query fails, so the tool would not run its own example.
    `tools/tycho-q/main.ty@resolve_key` resolves a key that is **exactly** a
    bare column reference against the select aliases — never an alias buried
    inside a larger expression, which would make a name resolve one way at the
    top of a key and another way underneath it. A name that is **both** a
    column and an alias is refused rather than settled by a precedence rule:
    either choice is defensible and neither is guessable from the query text.

    ```
    $ tycho-q "select name, qty * price as total from sales.csv where region == 'eu' and qty > 10 order by total desc limit 5"
    name,total
    Di,99.00
    Ada,18.00
    Cy,2.00
    -- exit 0

    $ tycho-q 'select name, qty as name2 from sales.csv order by name2 desc'
    name,name2
    Bo,30
    Cy,20
    Ada,12
    Di,11
    -- exit 0

    $ tycho-q 'select name, qty * 2 as qty from sales.csv order by qty'
    tycho-q: `order by qty` is ambiguous: `qty` is both a column of sales.csv
    and an alias in the select list -- rename the alias
    -- exit 1, stdout 0 bytes
    ```

    ### No regressions in phases 1 and 2

    The loader was refactored (the stat/read split out of `load` so both
    readers share it), so phase 2's strongest check was re-run, along with the
    key ones either side of it:

    ```
    $ tycho-q 'select * from fix.csv' | cmp - fix.csv && echo "byte-identical (cmp exit 0)"
    byte-identical (cmp exit 0)

    $ tycho-q "select 0.1 + 0.2 as sum from fix.csv where name == 'Ada'"
    sum
    0.3

    $ tycho-q 'select name from fix.csv where region == null'
    name
    Di

    $ tycho-q --explain 'select 1 + 2 * 3 from x.csv'
    (query
      (select (+ 1 (* 2 3)))
      (from "x.csv")
    )
    ```

    A key expression that cannot be evaluated fails before any comparison, with
    empty stdout — the "build the whole result before printing" guarantee holds
    through the new stages:

    ```
    $ tycho-q 'select name from fix.csv order by qty / 5'
    tycho-q: `/` is exact-only: 12 / 5 is not a whole number, and core:decimal
    has no div, so there is no rounding policy to apply (see DECISION 2 at the
    top of tools/tycho-q/main.ty)
    -- exit 1, stdout 0 bytes

    $ tycho-q 'select nope from fix.csv order by nope'
    tycho-q: no such column: nope (the header has: name, region, qty, price, code, note)
    -- exit 1, stdout 0 bytes
    ```

    ### Friction found

    **1. `core:json` is unsafe on input it cannot represent, and has no error
    channel to say so.** Measured above: silent truncation, silent corruption
    with exit 0, and unbounded memory on five bytes. `json.parse` returns
    `Json`, so *every* caller that cares must pre-validate the text — which
    means writing most of a second parser. Filed as **phase 21** below; it is a
    corelib change and out of this phase's scope.

    **2. `core:sort` has no comparator-taking sort at all.** Recorded in the
    Pre-flight as a prediction and confirmed against the signatures here. Any
    program ordering by more than one key, or in more than one direction, or
    over a type with no `comparable` instance, writes its own sort. This one is
    ~35 lines.

    **3. Phase 2's `core:iter` finding was too broad, and this phase narrows
    it** — see the probe above. Fallible higher-order functions ARE expressible;
    `corelib/iter/iter.ty@filter`'s signature is what is not. Phase 4 should
    write the narrower claim.

    **Not friction, recorded so it is not re-derived:** a lambda with a
    `Result` return type is accepted; a closure capturing two arrays compiles
    and works (capture is by deep copy at creation, so the key matrix is copied
    once, not per comparison); a qualified enum from another package
    (`json.Json`) works as a parameter type; and the whole of this phase
    compiled **first try** with zero diagnostics — the first phase here to do
    so.

    ### Gates

    Per this phase's brief and `CLAUDE.md`'s gate table: **no `make test`, no
    `make test-fast`, no `make ci`, no `make ar-check`.** This phase edits one
    file under `tools/` and touches no corelib, no fixture and no golden, so
    none of them can redden for it; every fixture above was written to the
    scratchpad, not into the tree — phase 4 owns the tracked ones. Run: the
    compile after every edit, the query set above, and `python3
    scripts/check_citations.py` over the **tree** (not merely this block, which
    is how phase 1's leftovers survived):

    ```
    $ python3 scripts/check_citations.py
    citation check: ok (178 anchored contain the token they name and each names
    one line, ... 201 `path@SYMBOL` definition refs name a symbol still in their
    file)
    ```

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

## Discovered by this plan

Appended by the phase that found it, unchecked, and not part of that phase's
completion.

- [ ] **Phase 20** — `core:decimal` has no `div`, and phase 2 measured what
      that costs a caller rather than only noting it. `tycho-q` now refuses `/`
      whenever the answer is not exact, which means `select total / count` —
      the ordinary averaging query — fails on almost all real data. The
      corelib's own header says division is deferred because it needs a target
      scale and a rounding policy; the fix is therefore not `div(a, b)` but
      `div(a, b, scale, mode)` with **both named by the caller**, plus the
      rounding modes spelled out (at minimum half-up and toward-zero, since
      `corelib/decimal/decimal.ty@rescale` already truncates toward zero and a
      second policy must not silently disagree with it). Scope is
      `corelib/decimal/decimal.ty`, its test under `corelib/test/`, and then
      `tools/tycho-q/main.ty`'s DECISION 2 block, which must be rewritten
      rather than deleted — the reasoning for exact-only is what makes the
      choice of default scale reviewable. This is a corelib change, so it is
      **`make test`**, not the tools gates. Named as out of scope by this
      plan's own "Out of scope" section, which anticipated exactly this filing.

- [ ] **Phase 21** — `core:json` mis-handles input it cannot represent, three
      different ways, and cannot report any of them. Measured by probe in phase
      3, not inferred: `json.parse("1.5")` returns `JNum(1)` at exit 0 (silent
      truncation); `json.parse("[1.5]")` **exhausts memory** — `parse_value`
      consumes nothing at the `.` and `corelib/json/json.ty:81-92` advances only
      on `,` or `]`, so `JNum(0)` is pushed forever, from five bytes of input;
      and `json.parse('[{"a":1.5}]')` returns exit 0 with `.5}]` parsed as the
      next KEY, inventing a column. The last is the dangerous one: it is the
      shape a table reader actually meets and it has no symptom.
      **The root cause is not the missing float path, it is the missing error
      channel:** `corelib/json/json.ty@parse` returns `Json`, not
      `Result(Json, E)`, so no caller can ask whether a parse succeeded, and
      "fails closed to JNull" (`corelib/json/json.ty:12-13`) is not closed when
      the failure is an OOM or a fabricated key. `tycho-q` works around it with
      `tools/tycho-q/main.ty@json_guard`, which validates the raw bytes before
      handing them over — i.e. every caller who cares has to write most of a
      second parser. Scope: at minimum make `parse` fallible and make
      `parse_array` unable to loop without advancing; a float path is a
      separate, larger question that interacts with phase 20 (`decimal.div`),
      since `core:decimal` is the only exact numeric tower here and `JNum` is
      an `int`. This is a corelib change, so it is **`make test`**, plus
      whatever under `corelib/test/` covers `core:json`.

## Out of scope

- **The `ParallelFor` width slot**, `FRICTION.md`'s last hard item. Unchanged
  from the previous plan: still a language change and still its own plan.
- **`decimal.div`.** Phase 2 will meet its absence and record it; adding it is a
  corelib change and becomes a new unchecked phase here if phase 2 wants it.
- **`group by`, aggregates, joins, indexes.** Named in the Pre-flight.
