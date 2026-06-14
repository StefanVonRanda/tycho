// VS Code extension entry point for Hier. Syntax highlighting is contributed
// declaratively (package.json grammar); this file just launches the language
// server (hier-lsp) over stdio for live diagnostics. Plain JS -- no build step.
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

function activate(context) {
  const cfg = vscode.workspace.getConfiguration("hier");
  if (!cfg.get("enableServer", true)) {
    return; // highlighting-only mode
  }
  const lspPath = cfg.get("lspPath", "hier-lsp");
  const compilerPath = cfg.get("compilerPath", "hierc");

  const serverOptions = {
    command: lspPath,
    transport: TransportKind.stdio,
    options: { env: Object.assign({}, process.env, { HIERC: compilerPath }) },
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "hier" }],
    synchronize: { configurationSection: "hier" },
  };

  client = new LanguageClient(
    "hier",
    "Hier Language Server",
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
