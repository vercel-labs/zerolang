# Zero Language Extension

Syntax highlighting, snippets, and Language Server Protocol support for Zero `.0` files in VS Code and Cursor.

The extension starts `scripts/zls.mts` from the nearest Zero repository root (or from `zero.zeroRoot` / `ZERO_ROOT` when installed outside the monorepo). The repository also ships `pnpm run zls -- --self-test`, which exercises diagnostics, hovers, completions, symbols, fix plans, and editor metadata from `zero graph`, `zero size`, `zero mem`, and `zero doc`.

Install into Cursor from the repository root:

```sh
pnpm install
pnpm run extension:install:cursor
```

When the extension is installed outside the monorepo, set `zero.zeroRoot` to a checkout that contains `bin/zero` and `scripts/zls.mts`.
