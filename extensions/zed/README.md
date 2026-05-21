# Zero for Zed

Language support for Zero `.0` files in Zed.

This extension provides language detection, Tree-sitter syntax highlighting, editor brackets/comments, outline queries, text objects, snippets, and Zero Language Server (`zls`) integration when opened inside a Zero repository checkout.

## Requirements

- Node.js 22+ on your PATH (used to launch `zls`)
- A built Zero compiler at `bin/zero` in the opened worktree, or `zero` on your PATH via [zerolang.ai/install.sh](https://zerolang.ai/install.sh)

## Local Development

Prepare a local dev-extension copy:

```sh
pnpm run extension:zed:prepare
```

Then run `zed: install dev extension` and select `.zero/zed/zero`.

The checked-in manifest keeps a portable `file://tree-sitter-zero` grammar URL. The prepare script rewrites that to the absolute local grammar path Zed expects for dev-extension installs. Zed fetches grammars as Git repositories, so the prepare script also creates a temporary standalone grammar repo under `.zero/zed/tree-sitter-zero`.

The WASM extension host is built by Zed when installing the dev extension. Install Rust via [rustup](https://www.rust-lang.org/tools/install) before installing the dev extension.

## Known Limitations

- `zls` writes files to disk on open/change so `zero check` can run against real paths.
- Code actions from `zls` may appear in the UI before apply support is complete.
- VS Code-style `// #region` / `// #endregion` folding is not supported. Zed does not yet expose marker-based folding or `folds.scm` for extensions. Track [zed-industries/zed#22703](https://github.com/zed-industries/zed/issues/22703).

## Repository Layout Decision

See [REPO-DECISION.md](./REPO-DECISION.md) for guidance on keeping the extension in the monorepo versus publishing it separately.
