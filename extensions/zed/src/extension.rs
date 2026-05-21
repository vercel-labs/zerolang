use std::path::{Path, PathBuf};
use zed_extension_api as zed;

struct ZeroExtension;

impl ZeroExtension {
    fn locate_zero_root(worktree: &zed::Worktree) -> Result<String, String> {
        if worktree.read_text_file("scripts/zls.mts").is_ok() {
            return Ok(worktree.root_path());
        }

        let mut path = PathBuf::from(worktree.root_path());
        for _ in 0..8 {
            if path.join("scripts").join("zls.mts").is_file() && path.join("bin").join("zero").is_file() {
                return Ok(path.to_string_lossy().into_owned());
            }
            if !path.pop() {
                break;
            }
        }

        Err("could not locate Zero repository root containing scripts/zls.mts and bin/zero".into())
    }

    fn zls_script_path(zero_root: &str) -> String {
        Path::new(zero_root)
            .join("scripts")
            .join("zls.mts")
            .to_string_lossy()
            .into_owned()
    }
}

impl zed::Extension for ZeroExtension {
    fn new() -> Self {
        Self
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &zed::LanguageServerId,
        worktree: &zed::Worktree,
    ) -> zed::Result<zed::Command> {
        let zero_root = Self::locate_zero_root(worktree)?;
        let zls_script = Self::zls_script_path(&zero_root);
        let node = worktree.which("node").unwrap_or_else(|| "node".to_string());

        let mut env = match zed::current_platform().0 {
            zed::Os::Mac | zed::Os::Linux => worktree.shell_env(),
            zed::Os::Windows => Vec::new(),
        };
        env.push(("ZERO_ROOT".to_string(), zero_root));

        Ok(zed::Command {
            command: node,
            args: vec![
                "--experimental-strip-types".to_string(),
                "--disable-warning=ExperimentalWarning".to_string(),
                zls_script,
            ],
            env,
        })
    }
}

zed::register_extension!(ZeroExtension);
