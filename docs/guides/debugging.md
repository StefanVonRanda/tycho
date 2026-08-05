# Debugging generated C

> Tycho transpiles to C and hands the `.c` to your system `cc`, so DWARF debug
> info flows through for free — the only missing piece is a map from the
> generated C back to your `.ty` source. The `-g` flag adds that map.

## `tychoc -g`

`-g` does two things:

1. Emits `#line N "src.ty"` directives into the generated C, one before each
   statement, so the C compiler's line table attributes the code to your Tycho
   source instead of the `.c` file.
2. Builds with `-O0 -g` (unoptimized + DWARF) instead of the default `-O3`, so
   stepping is faithful and locals aren't optimized away.

```sh
tychoc program.ty -g -o program
```

Then debug the binary as if it were hand-written C, but stepping lands on
`.ty` lines:

**Linux (gdb)** — DWARF is embedded in the executable directly:

```sh
gdb ./program
(gdb) break program.ty:7      # break on a Tycho source line
(gdb) run
(gdb) step                    # steps through program.ty, not the generated C
```

**macOS (lldb)** — the toolchain leaves DWARF in the intermediate objects, and
a one-step compile+link discards them, so generate a `.dSYM` first:

```sh
tychoc program.ty -g --emit-c -o program      # keep the .c
cc -O0 -g -fwrapv -pthread program.c -o program -lm
dsymutil program                               # writes program.dSYM
lldb ./program
(lldb) breakpoint set -f program.ty -l 7
```

(`-g` alone still produces a runnable binary on macOS; the `.dSYM` step is only
needed for source-level stepping.)

## `tycho debug` — the gdb adapter

`tycho debug` (the `tycho-debug` tool in `tools/tycho-debug/`) wraps the raw
gdb workflow: it compiles the program with `-g` itself, starts gdb, and drives
an interactive session that breaks by **source line** and shows locals under
their Tycho names (the `h_` prefix is stripped; arena machinery is hidden):

```sh
tycho debug program.ty [args...]
tycho debug -b 7 -b 12 program.ty     # pre-set breakpoints
```

```text
tycho-debug: debugging program.ty  (q quits, h for help)
tycho-debug> b 7
breakpoint 1 at program.ty:7
tycho-debug> r
[breakpoint 1 hit]
  at program.ty:7  (main)
tycho-debug> loc
  x = 41
  y = 42
tycho-debug> p h_x         # raw C names for expressions
41
tycho-debug> n
[stepped]
  at program.ty:8  (main)
```

Commands: `b <line>` break, `r` run, `n` next, `s` step, `c` continue,
`p <expr>` print a C expression, `loc` locals, `bt` backtrace, `l` source
around the current line, `$ <gdb cmd>` raw gdb passthrough, `q` quit. Ctrl-C
interrupts the running program (the tool forwards SIGINT to the inferior);
Ctrl-D quits.

It is a gdb adapter, not a re-implementation: expressions are the emitted C
names (`h_x`, `*h_x` for an `inout`), and `$` hands anything gdb understands
straight through. It needs `gdb` on `PATH`; `TYCHOC` selects the compiler and
`TYCHODEBUG` the debugger binary, same as `TYCHOFMT` does for the formatter.
The debugger is itself a Tycho program (`make tycho-debug`), dogfooding
core:os + core:strings over a small fork/pipe/signal shim (`debug_shim.c`);
it is gated by `make debug-check`.

## What the frames look like

The emitted C is faithful but not idiomatic, so a few things read differently
in a debugger:

- **Every function takes a hidden first argument `Arena *_parent`** — the arena
  that owns its return value. You'll see it on every frame; ignore it unless
  you're debugging allocation.
- **`inout` parameters are passed by pointer.** A `inout int x` is a `long *h_x` in
  C; print `*h_x`, not `h_x`. A heap `inout` also carries an `Arena *_ina_x`
  just before it.
- **Locals are prefixed `h_`** (`h_x` for Tycho `x`) to avoid clashing with C
  keywords and runtime symbols.
- **Statement temporaries live in per-statement `_t` arenas** freed at the end
  of each statement; block/loop bodies get their own `_b`/`_scope` arenas. These
  are the arena-management lines you'll step over between your statements.

## Where the memory lives: `TYCHO_ARENA_STATS`

Set the env var on any Tycho binary (built by either compiler — no rebuild, no
flag) and a residency summary prints to stderr at exit:

```text
$ TYCHO_ARENA_STATS=1 ./tychoc0 < compiler/tychoc0.ty > /dev/null

[tycho arena stats]
  peak live:   75.9 MiB   (working-set high-water)
  bump-alloc:  315.6 MiB over 11177285 allocations
  OS reserved: 79.1 MiB over 902 blocks
  block reuse: 777462 of 778364 requests from pool (99.9%)
  arenas:      8790511 created, 8790511 freed

  by function (20 of 139 shown, by peak):
    main                       75.6 MiB peak    75.8 MiB bump  1646059 allocs
    parse_program               7.3 MiB peak    10.9 MiB bump  455484 allocs
    gfix_program                4.0 MiB peak    12.2 MiB bump  518127 allocs
    ...
    ... 119 more (TYCHO_ARENA_STATS=full lists every function)
```

Reading it:

- **peak live** is the working-set high-water — the number that decides whether
  the program fits in memory. **bump-alloc** is everything ever handed out;
  a large gap between the two is arenas doing their job.
- **A function's row counts the memory its arena OWNS**, which is the honest
  attribution: a value returned up into the caller's arena is the *caller's*
  residency, not the callee's. So a `main` that holds the whole program value
  dominates, and per-pass rows show each pass's transient working set.
- `(unlabeled)` collects allocations from arenas with no owning proc — task
  roots, channel cells, and per-statement `_t` temporaries (~1% of bump on the
  self-compile).
- Counters are only touched when the variable is set; a normal run pays one
  never-taken branch per allocation.

## Limits

- **Single-file only.** `-g` emits line info for a single-file compile. A
  package build merges several `.ty` files into one unit and drops per-file
  identity, so `-g` is skipped there (with a note on stderr) rather than
  emitting wrong file/line pairs. Debug a package by reducing the failing path
  to a single file, or debug at the generated-C level without `-g`. `tycho
  debug` refuses a `package` build up front with this same explanation.
- **Synthesized glue is unmapped.** The program entry point, arena bootstrap,
  and other compiler-generated scaffolding have no `.ty` line; a `#line`
  directive only anchors real statements, so stepping through the glue shows the
  nearest preceding source line. This is cosmetic.
- **Relative paths resolve against the compile directory.** If you compiled with
  a relative path (`tychoc src/program.ty -g`), debug from the same working
  directory so the debugger finds the source.
