# tree-sitter-zero (vendored)

A tree-sitter grammar for the Zero programming language. Vendored here so the
Zed extension under `extensions/zed/` can reference it directly during
development.

Adapted from [`kennyg/tree-sitter-zero`](https://github.com/kennyg/tree-sitter-zero)
under the MIT License (Copyright 2026 Kenny Gatdula). The upstream license is
preserved at `LICENSE.upstream`.

## Local change

This vendored copy adds the `[value; count]` array-repeat literal to
`array_literal` so the full Zero corpus parses without error. See `grammar.js`.

## Regenerate the parser

```sh
tree-sitter generate
```

## Coverage check

```sh
ZERO_REPO=../../.. node scripts/check-against-upstream.mjs
```

Last measured: 235 / 235 files across `examples/` and
`conformance/native/pass/`.

## Authoritative parser

The Zero compiler's hand-rolled parser at `native/zero-c/src/parser.c` is the
source of truth. This grammar is a parallel artifact and is expected to drift;
the upstream coverage script is the safety net.
