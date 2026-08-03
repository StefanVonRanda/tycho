# What the next program says the language needs

> This plan is a fresh clone, 2026-08-03: the completed tycho-scheme plan lives
> at `docs/internals/plan-tycho-scheme-DONE.md`. The rule from that plan holds
> here: *does anything that is not the program written to want it need this?* A
> finding becomes a phase only when a second, independent caller exists.

## The program

**A bytecode compiler for the Scheme interpreter** -- `tools/tycho-scheme`
grows a `compile` front end that lowers Scheme to tycho-vm bytecode, so
`tycho-scheme` and `tycho-vm` converge. It is the first real customer for
tycho-vm's instruction encoding and const pools outside the tool's own
fixtures, and it re-exercises the interpreter's reader (reused verbatim),
symbol interning (the const pool), and scalar-match dispatch at a new scale.
The differential gate: compile the same four programs, run them on the VM,
and the output must be byte-identical to the interpreter's golden.

**Design, settled before a line of code:**

- The VM's `CALL` takes a STATIC pc and its `Val` is int-or-string only, so
  first-class functions and lists cannot be expressed in the current ISA. The
  VM grows the minimum the compiler needs, all appended (existing opcode
  numbers and goldens untouched): `CONS`/`CAR`/`CDR`/`SET-CAR` over a bounded
  pair heap (lists AND environment chains), `NIL`/`ISNIL` (Scheme truth:
  everything but `#f`/`()` is true), `EQV` (generic equality: int/nil/string,
  pairs never equal -- matching the interpreter's `eq?`), `PRINTS` (print a
  value as its Scheme form, no newline -- the compiled `display`), `DIE k`
  (die with a const-pool message -- the compiled "attempt to call a
  non-function"). `Val` gains `isnil`/`ispair` flags (pairs live in the heap;
  `i` holds the cell index).
- **Defunctionalization** (Reynolds): every lambda is compiled to one VM
  function with a static signature `(params..., env)` -- params in slots
  0..k-1, the captured-env chain in slot k. A closure VALUE is the pair
  `(tag . env)` where `tag` is a compile-time int and `env` is a list of the
  captured free vars' values. A call through a function value dispatches on
  the tag (a chain of `EQ`/`JNZ`); a call to a statically-known global
  function compiles to a direct `CALL`.
- **Globals** are frame-0 slot 0, a pair-chain built by the entry code in
  define order; a global reference walks the chain at a compile-known depth
  (two-pass: collect all top-level defines first, then compile with the
  depths). Frame 0 keeps 16 slots -- the VM's fixed limit stays untouched.
  Primitives compile to direct ops (`+` -> `ADD` chains, `=` -> `EQ`, ...),
  never an env lookup.
- **Mutable captures** (`set!` on a captured var -- the counter in
  closures.scm): a local that an inner lambda captures is BOXED at its
  binding -- one `CONS` cell; reads become `CAR`, `set!` becomes `SET-CAR`;
  the closure captures the cell. Uncaptured locals stay plain slots.
- **Compile-time checks that die loudly, never silently wrong**: unbound
  variables, arity errors, bad defines, malformed lambdas, and primitives the
  compiler does not lower yet (`pair?`, `number?`, `symbol?`, `string?`,
  `boolean?`) die at compile time with "not compilable". Documented gaps that
  stay runtime divergences: a bare `#t` displayed as `1` (the VM has no bool
  Val), `zero?`/`eq?` on a pair, and passing a primitive as a value.

## Findings

**The compiler works -- the differential is byte-identical.** All six programs
(fib, closures, ho, sort, shadow, eqsym) compile to tycho-vm bytecode and the
VM's run matches the interpreter's golden byte-for-byte, including the two
counter closures staying independent (`1 2 1`). The gate now asserts that
differential on every run. What the build found, all tool-level (none becomes
a phase -- nothing outside the compiler wants it):

- **The VM's `CALL` is static and `Val` is int-or-string, so the compiler
  needed pairs and a truthiness test.** The VM grew ten ops, all appended
  (existing opcode numbers untouched): CONS/CAR/CDR/SET-CAR over a bounded
  pair heap (lists AND environment chains), NIL/ISNIL, EQV (generic equality;
  pairs never equal -- the interpreter's `eq?`), PRINTS (a value's Scheme
  form, no newline -- the compiled `display`), DIE (die with a const-pool
  message), TRUTHY (false = nil OR int 0). TRUTHY exists because the compiled
  comparisons return the VM's int 0/1, and a comparison result used as a
  cond must be false when it is 0 -- the VM's own comparison semantics did
  not change, so tycho-vm's goldens did not move.
- **Cross-file declaration order matters.** A multi-file package compiles its
  files in lexical order and types do not forward-reference across files, so
  the compiler (which uses main.ty's `Val`) must sort AFTER main.ty. It is
  named `main_compiler.ty` for exactly that reason.
- **A real compiler bug, found by this build and FIXED: returning a map
  PARAMETER emits the wrong C copy.** `src/tychoc.c`'s return branch
  hardcoded `tycho_map_%s_copy(map_fn)`; `map_fn` defaults to `"si"` for
  every map that is not sf/ii/if, so `[string: Struct]` and `[string:
  string]` returned from a parameter emitted `tycho_map_si_copy` over a
  `TychoMapC0` and the generated C did not compile. Returning a LOCAL dodged
  it via the return-slot no-op move; only the param-copy path reached the
  wrong name. Fixed by routing the copy through `copy_into` (which already
  picks `tycho_mapc%d_copy` for composite maps); the compiler's earlier
  `scope_copy` workaround was then reverted, so the compiler itself now
  exercises the fixed path. Regression fixture: `tests/map_param_return.ty`
  (struct-valued and string-valued param returns).
- **Divergences from the interpreter, each documented in the code and
  avoided by the programs** (the gate's differential would catch one): a bare
  `#t` displays as `1` (the VM has no bool Val); symbols ARE strings, so
  `eq?` cannot tell a symbol from a string; the compiled truthiness treats
  int 0 as false (the interpreter's `is_true` does not); a top-level display
  between defines runs after all defines (globals are a chain walked at
  compile-known depth, so defines are emitted first -- the interpreter
  resolves by name); a closure call evaluates its arguments before the
  operator (the dispatch needs the operator on top). All four fail loudly or
  are invisible in the programs; none is silent.

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
