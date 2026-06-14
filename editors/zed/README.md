# Hier for Zed

Syntax highlighting (tree-sitter) + live diagnostics (via `hier-lsp`) for
[Hier](../../README.md).

## What's here

- `grammars/hier/` — a **flat** tree-sitter grammar (token-level: keywords,
  types, builtins, literals, identifiers, operators). It registers the language
  and drives highlighting; it does **not** model block nesting (hier is
  indentation-significant; full structure would need a C external scanner). The
  generated parser (`src/parser.c`) is committed, so no tree-sitter CLI is needed
  to build it. **Verified: parses 321/321 valid `.hi` files with zero errors.**
- `languages/hier/` — Zed language config + `highlights.scm` queries.
- `extension.toml`, `Cargo.toml`, `src/lib.rs` — the Zed extension; the Rust code
  just launches `hier-lsp` (passing `HIERC`) for diagnostics.

## Install (dev)

1. Build the toolchain so `hier-lsp` + `hierc` exist and are on your `PATH`:

   ```sh
   make tools
   export PATH="$PWD:$PATH"     # or copy hier-lsp/hierc somewhere on PATH
   ```

2. **Publish the grammar** (Zed fetches grammars from a git repo, even for dev
   extensions — there is no local-grammar path). Push `editors/zed/grammars/hier`
   as its own repo (e.g. `tree-sitter-hier`), then in `extension.toml` set:

   ```toml
   [grammars.hier]
   repository = "https://github.com/<you>/tree-sitter-hier"
   rev = "<commit sha of that repo>"
   ```

3. In Zed: **Extensions → Install Dev Extension** → choose this `editors/zed`
   directory. Zed clones + builds the grammar and compiles `src/lib.rs` to WASM.

Open a `.hi` file: tokens are colored and compile errors show inline.

## Caveats (honest)

- **Not GUI-verified here** — `zed` isn't installed in the dev environment. The
  grammar + highlight queries are verified with the tree-sitter CLI; the Zed
  extension wiring (`extension.toml`, `src/lib.rs`) is written to the docs but
  tested only by you in Zed.
- `zed_extension_api`'s trait shape changes across Zed versions. If the WASM build
  fails, bump the version in `Cargo.toml` to match your Zed and adjust
  `src/lib.rs` accordingly.
- Highlighting is token-level (flat grammar). Upgrading to a structural grammar
  (folding, structural nav) means adding a C external scanner for INDENT/DEDENT.
