# Zero for Zed

Language support for Zero `.0` files in Zed.

This extension currently provides language detection, Tree-sitter syntax highlighting, editor brackets/comments, outline queries, text objects, and snippets. It does not package a language server yet.

## Known Limitations

- No language server integration yet. Use the `zero` CLI and `pnpm run zls` separately for diagnostics, hover, and completion.
- VS Code-style `// #region` / `// #endregion` folding is not supported. Zed does not yet expose marker-based folding or `folds.scm` for extensions.

## Local Development

Prepare a local dev-extension copy:

```sh
pnpm run extension:zed:prepare
```

Then run `zed: install dev extension` and select `.zero/zed/zero`.

The checked-in manifest keeps a portable `file://tree-sitter-zero` grammar URL. The prepare script rewrites that to the absolute local grammar path Zed expects for dev-extension installs.
Zed fetches grammars as Git repositories, so the prepare script also creates a temporary standalone grammar repo under `.zero/zed/tree-sitter-zero`.
