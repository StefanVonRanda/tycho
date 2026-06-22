// VS Code extension entry point for Tycho. Syntax highlighting is contributed
// declaratively (package.json grammar); this file just launches the language
// server (tycho-lsp) over stdio for live diagnostics. Plain JS -- no build step.
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

function activate(context) {
  const cfg = vscode.workspace.getConfiguration("tycho");
  if (!cfg.get("enableServer", true)) {
    return; // highlighting-only mode
  }
  const lspPath = cfg.get("lspPath", "tycho-lsp");
  const compilerPath = cfg.get("compilerPath", "tychoc");

  const serverOptions = {
    command: lspPath,
    transport: TransportKind.stdio,
    options: { env: Object.assign({}, process.env, { TYCHOC: compilerPath }) },
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "tycho" }],
    synchronize: { configurationSection: "tycho" },
  };

  client = new LanguageClient(
    "tycho",
    "Tycho Language Server",
    serverOptions,
    clientOptions
  );
  client.start();
  context.subscriptions.push({ dispose: () => client && client.stop() });
}

function deactivate() {
  return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
