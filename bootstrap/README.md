# Bootstrap compiler

`tycho_compiler.c` is generated: it is `compiler/main.ty` emitted as C by the
last known-good self-hosted compiler. `make` compiles it with `cc` to
`bootstrap-tychoc`, uses that to build `compiler/main.ty`, and then uses the
result to rebuild itself.

It is not a second compiler source. Do not edit it by hand. Regenerate it only
when the compiler source stops being expressible in the existing bootstrap
dialect, then run `make bootstrap-check` and commit the new file:

```text
./tychoc1 --emit-c compiler/main.ty -o bootstrap/tycho_compiler
```

The historical C compiler is `src/tychoc.c`. It is archived: not built, not
tested, and not updated. It remains in the tree only because earlier design and
defect records cite it.
