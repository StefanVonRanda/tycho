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

```
tychoc program.ty -g -o program
```

Then debug the binary as if it were hand-written C, but stepping lands on
`.ty` lines:

**Linux (gdb)** — DWARF is embedded in the executable directly:

```
gdb ./program
(gdb) break program.ty:7      # break on a Tycho source line
(gdb) run
(gdb) step                    # steps through program.ty, not the generated C
```

**macOS (lldb)** — the toolchain leaves DWARF in the intermediate objects, and
a one-step compile+link discards them, so generate a `.dSYM` first:

```
tychoc program.ty -g --emit-c -o program      # keep the .c
cc -O0 -g -fwrapv -pthread program.c -o program -lm
dsymutil program                               # writes program.dSYM
lldb ./program
(lldb) breakpoint set -f program.ty -l 7
```

(`-g` alone still produces a runnable binary on macOS; the `.dSYM` step is only
needed for source-level stepping.)

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

## Limits

- **Single-file only.** `-g` emits line info for a single-file compile. A
  package build merges several `.ty` files into one unit and drops per-file
  identity, so `-g` is skipped there (with a note on stderr) rather than
  emitting wrong file/line pairs. Debug a package by reducing the failing path
  to a single file, or debug at the generated-C level without `-g`.
- **Synthesized glue is unmapped.** The program entry point, arena bootstrap,
  and other compiler-generated scaffolding have no `.ty` line; a `#line`
  directive only anchors real statements, so stepping through the glue shows the
  nearest preceding source line. This is cosmetic.
- **Relative paths resolve against the compile directory.** If you compiled with
  a relative path (`tychoc src/program.ty -g`), debug from the same working
  directory so the debugger finds the source.
