# Bootstrap compiler

`tycho_compiler.c` is generated: it is `compiler/main.ty` emitted as C by the
last known-good self-hosted compiler. `make` compiles it with `cc` to
`bootstrap-tychoc`, uses that to build `compiler/main.ty`, and then uses the
result to rebuild itself.

`surface_tables.txt` is a frozen extraction of the archived compiler's keyword
and builtin tables, read only by `scripts/surface_lock.py`; it is never compiled.

It is not a second compiler source. Do not edit it by hand. Regenerate it only
when the compiler source stops being expressible in the existing bootstrap
dialect, then run `make bootstrap-check` and commit the new file:

```text
./tychoc1 --emit-c compiler/main.ty -o bootstrap/tycho_compiler
```

The historical C compiler is `src/tychoc.c`. It is deleted from the tree; its
source remains in git history at commit
`c7d46528ac6efcf9dc93359d14d98bedcb7ddbad`, and historical records cite it
through [`docs/internals/archived-c-compiler.md`](../docs/internals/archived-c-compiler.md).
