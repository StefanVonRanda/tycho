// Zed extension for Hier. Highlighting comes from the tree-sitter grammar +
// languages/hier/highlights.scm (declarative); this code only tells Zed how to
// launch the language server (hier-lsp) so .hi files get live diagnostics.
//
// Compiled to WASM by Zed. If the build fails on the API, bump
// `zed_extension_api` in Cargo.toml to match your Zed version (the trait shape
// has changed across versions); see README.md.
use zed_extension_api as zed;

struct HierExtension;

impl zed::Extension for HierExtension {
    fn new() -> Self {
        HierExtension
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &zed::LanguageServerId,
        worktree: &zed::Worktree,
    ) -> zed::Result<zed::Command> {
        // Find the server (and the compiler it shells out to) on the worktree PATH.
        let server = worktree
            .which("hier-lsp")
            .ok_or_else(|| "hier-lsp not found on PATH (build it with `make hier-lsp`)".to_string())?;

        let mut env = Vec::new();
        if let Some(hierc) = worktree.which("hierc") {
            env.push(("HIERC".to_string(), hierc));
        }

        Ok(zed::Command {
            command: server,
            args: Vec::new(),
            env,
        })
    }
}

zed::register_extension!(HierExtension);
