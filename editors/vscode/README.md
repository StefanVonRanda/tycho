# Hier for VS Code

Syntax highlighting + live diagnostics for [Hier](../../README.md).

- **Highlighting** is declarative (a TextMate grammar) — works with zero setup.
- **Diagnostics** (red squiggles on compile errors, cleared as you fix them) come
  from `hier-lsp`, the language server, over LSP.

## Install (development / local)

1. Build the toolchain from the repo root so `hier-lsp` and `hierc` exist:

   ```sh
   make tools          # builds hierc, hier, hierfmt, hier-lsp
   ```

2. Make `hier-lsp` and `hierc` reachable — either put the repo root on your
   `PATH`, or set absolute paths in VS Code settings:

   ```jsonc
   // settings.json
   "hier.lspPath": "/abs/path/to/hier/hier-lsp",
   "hier.compilerPath": "/abs/path/to/hier/hierc"
   ```

3. Install the extension's runtime dep and launch it:

   ```sh
   cd editors/vscode
   npm install
   ```

   Then press **F5** in VS Code (Extension Development Host), or package it:

   ```sh
   npx @vscode/vsce package      # produces hier-0.0.1.vsix
   code --install-extension hier-0.0.1.vsix
   ```

Open any `.hi` file: keywords/strings/comments are colored, and saving or editing
shows compile errors inline.

## Settings

| setting | default | meaning |
|---|---|---|
| `hier.lspPath` | `hier-lsp` | path to the language-server binary |
| `hier.compilerPath` | `hierc` | compiler the server runs for diagnostics (passed as `HIERC`) |
| `hier.enableServer` | `true` | set `false` for highlighting only |

## Status

Diagnostics are line-level (the compiler reports `file:line: error: msg`). Hover
types and go-to-definition are planned as the server grows.
