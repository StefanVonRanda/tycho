# The archived C compiler

The original compiler was `src/tychoc.c`, a single-file C implementation. It is
deleted from the tree. `compiler/` (self-hosted Tycho) is the only maintained
implementation, `bootstrap/tycho_compiler.c` is the generated cold-start
artifact, and no build or test lane compiles the old C source.

The deleted source remains in git history at commit
`c7d46528ac6efcf9dc93359d14d98bedcb7ddbad`:

```sh
git show c7d46528ac6efcf9dc93359d14d98bedcb7ddbad:src/tychoc.c | wc -l
```

## Why citations still point at it

Older design notes, defect records and source comments cite
`src/tychoc.c:LINE` or a `path@symbol` anchor into it. Those are historical
evidence, not live code. `scripts/check_citations.py` resolves them against the
pinned revision above, so every line range and anchored symbol is still checked
without keeping a dead compiler in the tree.

The reject classifier no longer reads the C source either. Its diagnostic-site
index is frozen in `compiler/reject_sites.json`, which records the same
revision, the SYNTAX boundary, and every `die_at` / `die` / `warn_at` /
`diag_push` format string with its C line number. `compiler/run.sh` audits the
committed `compiler/reject_class.tsv` against that index, so the evidence
survives deletion.

## What is not true any more

- The C compiler is not built, tested, shipped, or updated.
- Do not use it as a second opinion. The spec is normative; `compiler/` is the
  reference implementation in practice.
- A citation to `src/tychoc.c` means "the archived revision named above", not a
  file in the working tree.
