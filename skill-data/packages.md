---
name: packages
description: Create, inspect, and repair Zero packages and manifests.
---

# Zero Packages

Use this when working with `zero.toml`, compatibility `zero.json`, package-local modules, package tests, or multi-file Zero projects.

## Create

For agent-authored packages, start graph-first:

```sh
zero init
zero patch --op 'addMain'
```

`zero.graph` is the package graph store and compiler input. `.0` files are the
human-readable projection; export them only when a human asks to review or edit
the projection. `zero init` defaults to the current directory and uses that
directory's folder name as the package name. Use `zero init app` when the
user asks for a new subdirectory. It writes TOML metadata by default; use
`zero init --manifest json [package]` only for explicit compatibility cases.
Use `zero init --template cli|lib|package [package]` only when the user
explicitly asks for starter files.

## Manifest

Minimal executable package in TOML:

```toml
[package]
name = "hello"
version = "0.1.0"

[targets.cli]
kind = "exe"
main = "src/main.0"
```

The target `main` path names the human-readable projection for source maps,
export/import, and review. Normal package commands compile from `zero.graph`.

JSON is also accepted for compatibility, but new agent-authored packages should
use TOML:

```json
{
  "package": { "name": "hello", "version": "0.1.0" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } }
}
```

If both `zero.toml` and `zero.json` exist in the same package root, Zero uses
`zero.toml`. Keep one manifest checked in unless the task is specifically
testing precedence.

Pass either the package directory or manifest to commands:

```sh
zero check
zero check zero.toml
zero run examples/systems-package
```

## Module Imports

Package-local imports resolve from `src/`:

- `use helpers` resolves `src/helpers.0`
- `use config.parser` resolves `src/config/parser.0`
- `use config.parser` may also resolve `src/config/parser/mod.0`

Standard library imports use the same `use` form:

```zero
use std.mem
use std.parse
```

Avoid implicit files. If an import is unknown, run:

```sh
zero check
zero inspect
```

## Dependencies

Current packages support local path dependencies and registry metadata. Local
dependencies must point at a directory containing `zero.toml` or compatibility
`zero.json`; `zero.toml` takes precedence.

TOML dependency metadata:

```toml
[dependencies.local-tools]
path = "../local-tools"
version = "0.1.0"
```

```json
{
  "dependencies": {
    "local-tools": { "path": "../local-tools", "version": "0.1.0" }
  }
}
```

The resolver is declarative; it records deterministic lock facts under `.zero/package-locks/` and does not fetch remote package code.

Package compiler commands validate and compile from a checked-in `zero.graph` store, including target and package metadata, and can operate when `.0` source projections are missing. When the projection was edited, commands that consume the store (`zero check`, `zero build`, `zero run`, `zero test`, `zero query`, `zero view`, `zero diff`, and friends) refresh the stale store from source and note it on stderr (`ZERO_STALE=fail` makes that an error); they never rewrite `.0` files. When `zero.graph` is the newer side, for example right after `zero patch`, they keep using the graph and note that too; when both sides changed independently they fail with `RGP006` and offer `zero import` or `zero export` as repairs. Drift is classified by content (each store write records a source projection hash), never by file timestamps, so staged or cloned workspaces behave identically. Use `zero verify-projection` when drift must fail the workflow, and `zero export` only when a human-readable projection needs regeneration.

## Inspect

```sh
zero inspect
zero doc
zero dev
```

Use `--json` when a tool needs exact graph, doc, or dev fields. Useful `graph`
facts include modules, source paths, import edges, public and private symbol
counts, function effects, required capabilities, target facts, dependency
facts, and package cache key inputs.

## Graph Authoring

For agent-authored packages, prefer the repository graph surface:

```sh
zero init
zero patch --op 'addMain'
```

Inspect and patch existing packages through the graph. Create an artifact under
`.zero/` only when another tool needs a file artifact:

```sh
zero view
zero patch --op 'addMain'
```

Package-level patches write `zero.graph`; successful patch output includes the new graph hash and top-level symbols. Use `zero export` only to materialize `.0` for human review, and `zero import` after humans edit that projection; import accepts the package root or any source path inside it and updates the existing store in place. Keep derived graph artifacts out of the package source unless the user explicitly asks for them.

Repository graph stores are binary by default. Use `zero init --format text` or
`zero import --format text [package]` only when the package
intentionally needs a readable debug store. Normal reads auto-detect both
encodings, and normal writes preserve an existing text or binary store. Stdlib
`std/*.graph` stores are binary graph stores; `std/*.0` siblings are human
projections and are not used as the stdlib compile source.

## Common Repairs

- `IMP001`: create the imported module, fix its path, or adjust `use`.
- `IMP002`: break a direct import cycle.
- `PKG001`: fix a local dependency path so it contains `zero.toml` or a compatibility `zero.json`.
- `PKG002`: break a package dependency cycle.
- `PKG003`: avoid resolving one package name to multiple versions.
- `PKG004`: update target metadata or choose a supported target.

Prefer a package-local fix over moving unrelated files.
