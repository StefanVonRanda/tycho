// Zed extension for Tycho. Highlighting comes from the tree-sitter grammar +
// languages/tycho/highlights.scm (declarative); this code only tells Zed how to
// launch the language server (tycho-lsp) so .ty files get live diagnostics.
//
// Compiled to WASM by Zed. If the build fails on the API, bump
// `zed_extension_api` in Cargo.toml to match your Zed version (the trait shape
// has changed across versions); see README.md.
use zed_extension_api as zed;

struct TychoExtension;

impl zed::Extension for TychoExtension {
    fn new() -> Self {
        TychoExtension
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &zed::LanguageServerId,
        worktree: &zed::Worktree,
    ) -> zed::Result<zed::Command> {
        // Find the server (and the compiler it shells out to) on the worktree PATH.
        let server = worktree
            .which("tycho-lsp")
            .ok_or_else(|| "tycho-lsp not found on PATH (build it with `make tycho-lsp`)".to_string())?;

        let mut env = Vec::new();
        if let Some(tychoc) = worktree.which("tychoc") {
            env.push(("TYCHOC".to_string(), tychoc));
        }

        Ok(zed::Command {
            command: server,
            args: Vec::new(),
            env,
        })
    }
}

zed::register_extension!(TychoExtension);
