# Tycho for Zed

Syntax highlighting (tree-sitter) + live diagnostics (via `tycho-lsp`) for
[Tycho](../../README.md).

## What's here

- `grammars/tycho/` — a flat tree-sitter grammar (keywords, types, builtins,
  literals, identifiers, operators). It drives highlighting; it does not model
  block nesting, because Tycho is indentation-significant and structure would
  need a C external scanner. The generated parser is committed, so no
  tree-sitter CLI is needed to build it.
- `languages/tycho/` — Zed language config + `highlights.scm` queries.
- `extension.toml`, `Cargo.toml`, `src/lib.rs` — the Zed extension; the Rust code
  launches `tycho-lsp` (passing `TYCHOC`) for diagnostics.

## Install (dev)

1. Build the toolchain so `tycho-lsp` + `tychoc` exist and are on your `PATH`:

   ```sh
   make tools
   export PATH="$PWD:$PATH"     # or copy tycho-lsp/tychoc somewhere on PATH
   ```

2. **Publish the grammar** (Zed fetches grammars from a git repo, even for dev
   extensions — there is no local-grammar path). Push `editors/zed/grammars/tycho`
   as its own repo (e.g. `tree-sitter-tycho`), then in `extension.toml` set:

   ```toml
   [grammars.tycho]
   repository = "https://github.com/<you>/tree-sitter-tycho"
   rev = "<commit sha of that repo>"
   ```

3. In Zed: **Extensions → Install Dev Extension** → choose this `editors/zed`
   directory. Zed clones + builds the grammar and compiles `src/lib.rs` to WASM.

Open a `.ty` file: tokens are colored and compile errors show inline.

## Caveats

- Highlighting is token-level (a flat grammar): the grammar and highlight queries
  are validated with the tree-sitter CLI. Structural features (folding,
  structural navigation) would need a C external scanner for INDENT/DEDENT, since
  tycho is indentation-significant.
- `zed_extension_api`'s trait shape changes across Zed versions. If the WASM build
  fails, bump the version in `Cargo.toml` to match your Zed and adjust
  `src/lib.rs` to suit.
- If your code imports `core:` packages, set `TYCHO_CORELIB` so the server can
  resolve them (see [corelib](../../docs/guides/corelib.md)) — this also powers
  completion and hover on imported members (`strings.trim`), which the server
  reads by running the transpiler on the file in its real package directory.

## Working on the grammar

`grammars/tycho/src/parser.c` is generated at ABI 15 and committed. To
regenerate after editing `grammar.js`:

```sh
npx tree-sitter-cli@0.25 generate --abi 15
```

`scripts/editors_check.sh` runs `tree-sitter parse -q` over
every tracked `.ty` file and requires an `ERROR` node on nothing outside a
known-bad set —
`tests/reject/hex_escape_one_digit.ty` and
`tests/reject/rawstring_unterminated.ty`, both fixtures that are supposed not to
parse. `$T` generics and backtick raw literals must parse cleanly.
