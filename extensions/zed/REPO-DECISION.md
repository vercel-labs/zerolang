# Zero Zed Extension Repository Decision

This checklist helps decide whether the Zed extension should remain in the `zerolang` monorepo or move to a dedicated repository.

## Stay in the monorepo (current default)

Choose this when:

- You want one PR flow for compiler, `zls`, grammar, and extension changes
- The extension can assume access to `scripts/zls.mts` and `bin/zero` inside the same checkout
- Publishing to the Zed extension registry is not urgent

Current install path:

```sh
pnpm run extension:zed:prepare
# zed: install dev extension → .zero/zed/zero
```

## Split to a dedicated repository

Choose this when:

- You need an independent release cadence from the compiler
- You want a public extension repo even if upstream compiler PRs stall
- You are ready to publish through [zed-industries/extensions](https://github.com/zed-industries/extensions)

### What would move

- `extensions/zed/` (manifest, queries, snippets, Tree-sitter grammar, WASM host)
- Extension-specific CI (grammar tests, WASM build)

### What should stay in `zerolang`

- `scripts/zls.mts` and `bin/zero`
- VS Code extension (unless you split that too)
- Conformance fixtures and compiler sources

### Split checklist

1. Create a public repo (for example `zero-zed`) with an accepted license at the repo root
2. Point `extension.toml` `repository` at the new repo
3. Publish the Tree-sitter grammar as its own git repository (Zed fetches grammars by URL)
4. Keep snippet sync either via submodule, npm package, or duplicated JSON with CI parity tests
5. Document how the extension finds `zero` and `zls` when the monorepo is not present (`ZERO_ROOT`, PATH, or user settings)
6. Add CI: `tree-sitter test`, extension smoke tests, `cargo build --target wasm32-wasip2 --release`
7. Open a PR to `zed-industries/extensions` with a submodule entry

## Recommendation

Defer a split until ZLS integration is validated in daily use. The WASM language-server host and grammar hardening are easier to iterate on while the extension still lives beside `zls` and `bin/zero`.
