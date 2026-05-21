# Zero for Zed

Language support for Zero `.0` files in Zed.

This extension provides language detection, Tree-sitter syntax highlighting, editor brackets/comments, outline queries, text objects, snippets, and Zero Language Server (`zls`) integration when opened inside a Zero repository checkout.

## Requirements

- Node.js 22+ on your PATH (used to launch `zls`)
- A built Zero compiler at `bin/zero` in the opened worktree, or `zero` on your PATH via [zerolang.ai/install.sh](https://zerolang.ai/install.sh)
- **rustup** with the `wasm32-wasip2` target (required for the LSP host; grammar-only is not enough once `[language_servers]` is enabled)

```sh
brew install rustup
export PATH="$(brew --prefix rustup)/bin:$HOME/.cargo/bin:$PATH"
rustup target add wasm32-wasip2
```

Zed compiles the Rust extension when you install the dev extension. If Homebrew's standalone `rust` formula is ahead of rustup on `PATH`, compilation fails even when rustup is installed. Put rustup first, then restart Zed (or launch it from a terminal where that PATH is set).

Check prerequisites:

```sh
pnpm --filter=./extensions/zed run check:rust
```

## Local Development

Prepare a local dev-extension copy:

```sh
pnpm run extension:zed:prepare
```

Then run `zed: install dev extension` and select `.zero/zed/zero`.

The checked-in manifest keeps a portable `file://tree-sitter-zero` grammar URL. The prepare script rewrites that to the absolute local grammar path Zed expects for dev-extension installs. Zed fetches grammars as Git repositories, so the prepare script also creates a temporary standalone grammar repo under `.zero/zed/tree-sitter-zero`.

The WASM extension host is built by Zed when installing the dev extension. Install rustup and the `wasm32-wasip2` target before installing (see Requirements above).

## Troubleshooting

If Zed reports `failed to compile Rust extension`:

1. Run `pnpm --filter=./extensions/zed run check:rust`
2. Install the missing target: `rustup target add wasm32-wasip2`
3. Ensure rustup's `cargo`/`rustc` come before Homebrew's `rust` on `PATH`
4. Quit and reopen Zed, then retry **Zed: Install Dev Extension**
5. For details, open **Zed: Open Log** and search for `cargo` errors

## Known Limitations

- `zls` writes files to disk on open/change so `zero check` can run against real paths.
- Code actions from `zls` may appear in the UI before apply support is complete.
- VS Code-style `// #region` / `// #endregion` folding is not supported. Zed does not yet expose marker-based folding or `folds.scm` for extensions. Track [zed-industries/zed#22703](https://github.com/zed-industries/zed/issues/22703).

## Repository Layout Decision

See [REPO-DECISION.md](./REPO-DECISION.md) for guidance on keeping the extension in the monorepo versus publishing it separately.
