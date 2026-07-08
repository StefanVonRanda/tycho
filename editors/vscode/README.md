# Tycho for VS Code

Syntax highlighting + live diagnostics for [Tycho](../../README.md).

- **Highlighting** is declarative (a TextMate grammar) — works with zero setup.
- **Diagnostics** (red squiggles on compile errors, cleared as you fix them) come
  from `tycho-lsp`, the language server, over LSP.

## Install (development / local)

1. Build the toolchain from the repo root so `tycho-lsp` and `tychoc` exist:

   ```sh
   make tools          # builds tychoc, tycho, tychofmt, tycho-lsp
   ```

2. Make `tycho-lsp` and `tychoc` reachable — either put the repo root on your
   `PATH`, or set absolute paths in VS Code settings:

   ```jsonc
   // settings.json
   "tycho.lspPath": "/abs/path/to/tycho/tycho-lsp",
   "tycho.compilerPath": "/abs/path/to/tycho/tychoc"
   ```

3. Install the extension's runtime dep and launch it:

   ```sh
   cd editors/vscode
   npm install
   ```

   Then press **F5** in VS Code (Extension Development Host), or package it:

   ```sh
   npx @vscode/vsce package      # produces tycho-0.0.1.vsix
   code --install-extension tycho-0.0.1.vsix
   ```

Open any `.ty` file: keywords/strings/comments are colored, and saving or editing
shows compile errors inline.

## Settings

| setting | default | meaning |
|---|---|---|
| `tycho.lspPath` | `tycho-lsp` | path to the language-server binary |
| `tycho.compilerPath` | `tychoc` | transpiler the server runs for diagnostics (passed as `TYCHOC`) |
| `tycho.enableServer` | `true` | set `false` for highlighting only |

If your code imports `core:` packages, set `TYCHO_CORELIB` in your environment so
the server can resolve them (see [corelib](../../docs/corelib.md)) — this also
powers completion and hover on imported members (`strings.trim`), which the
server reads by running the transpiler on the file in its real package directory.

## Status

Diagnostics are line-level (the transpiler reports `file:line: error: msg`). Hover
types and go-to-definition aren't here yet — I'll add them as the server grows.
