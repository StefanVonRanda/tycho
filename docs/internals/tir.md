# The typed IR (`compiler/tir`)

What the checker knows about a program, written down for a back end that is not
the C emitter. `tychoc --emit-tir FILE` prints it. Urani's bytecode compiler is
the first consumer.

## How it is made

`types.lower(p)` checks the program, then lowers every non-generic function
**through the checker itself**: each function is entered as `check_on2` enters it,
each local declared with `_vpush` as `_tstmt` declares it, each expression typed
by `_ex2` with the type its context expects. There is no second copy of the type
rules, so the IR cannot disagree with the checker. Re-typing a checked function
must raise nothing; if it does, lowering reports `tir: internal: ...` instead of
an IR.

## What it holds

- Every expression node carries its type, in the checker's encoding (`int`,
  `arr(int)`, `@Name` for a struct). Statements have none.
- Every local is a slot; `for x in xs` is the loop it means, with hidden `$c` and
  `$i` locals, the collection read once.
- Every call names its callee's mangled symbol (`CALL`), or a builtin by name
  (`BUILTIN`); `s.split(",")` is `split(s, ",")`.
- Constants are inlined as their literals.
- Each implicit conversion the checker allowed is explicit: an int literal where a
  float is expected is a float literal; anything else is a `CONV` node.
- A three-clause `for` is `BLOCK { init; LOOP(cond, body, post) }`; `continue`
  runs the post block.

The node kinds and their fields are listed in `compiler/tir/tir.ty`.

## The script subset (v1)

int, float, bool and string scalars; structs; arrays; functions, including
`inout` parameters; `if`/`elif`/`else`, `for cond:`, three-clause `for`,
`for x in array`, `break`, `continue`, `return`; calls to user functions and to
the builtins.

Anything else is **refused by name** (`tir: `match` is not in the script subset`),
never lowered wrongly. A function's first refusal is its answer: past it the
scope is no longer the checker's, so later lines would be a cascade.

## Tests

`tests/tir.sh` (in `make test`): golden dumps, refusals by name, and a sweep of
every accepting program in `tests/*.ty`, each of which must lower or be refused
by name, with no re-typing difference.
