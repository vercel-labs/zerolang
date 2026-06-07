---
name: packages
description: Create, inspect, and repair Zero packages and manifests.
---

# Zero Packages

Use this when working with `zero.toml`, `zero.json`, package-local modules, package tests, or multi-file Zero projects.

## Create

For agent-authored packages, start graph-first:

```sh
zero init hello
cd hello
zero patch --op 'addMain'
zero check .
zero sync --from-graph .
```

`zero.graph` is the package graph store and compiler input. `.0` files are the
human-readable projection; sync them after graph checks pass. Use `zero new`
only when the user explicitly asks for a source-text package template.
Use `zero init --manifest toml <package>` when the user wants TOML metadata.

## Manifest

Minimal executable package in TOML:

```toml
[package]
name = "hello"
version = "0.1.0"

[targets.cli]
kind = "exe"
main = "src/main.0"

[repositoryGraph]
compilerInput = false
```

JSON is also accepted:

```json
{
  "package": { "name": "hello", "version": "0.1.0" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } },
  "repositoryGraph": { "compilerInput": false }
}
```

If both `zero.toml` and `zero.json` exist in the same package root, Zero uses
`zero.toml`. Keep one manifest checked in unless the task is specifically
testing precedence.

Pass either the package directory or manifest to commands:

```sh
zero check .
zero check zero.toml
zero check zero.json
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
zero check <package>
zero inspect <package>
```

## Dependencies

Current packages support local path dependencies and registry metadata. Local dependencies must point at a directory containing `zero.toml` or `zero.json`; `zero.toml` takes precedence.

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

Set `repositoryGraph.compilerInput` to `true` only for packages that check in a
valid `zero.graph` store. Normal compiler commands validate and compile from
that store, including target and package metadata, and can operate when `.0`
source projections are missing. Commands report projection state and never
rewrite `.0` files. Use `zero verify-sync` when drift must fail the
workflow, and `zero sync --from-graph` to regenerate projections. Leave
the field unset or `false` for source-text packages.

## Inspect

```sh
zero inspect <package>
zero doc <package>
zero dev <package>
```

Use `--json` when a tool needs exact graph, doc, or dev fields. Useful `graph` facts include modules, source paths, import edges, public and private symbol counts, function effects, required capabilities, target facts, dependency facts, and package cache key inputs.

## Graph Authoring

For agent-authored packages, prefer the repository graph surface:

```sh
zero init <package>
cd <package>
zero patch --op 'addMain'
zero check .
zero run .
```

Inspect and patch existing packages through the graph. Create an artifact under
`.zero/` only when another tool needs a file artifact:

```sh
zero view <package>
zero check <package>
zero patch <package> --op 'addMain'
```

When `repositoryGraph.compilerInput` is true, package-level patches write
`zero.graph`; use `zero sync --from-graph <package>` to materialize `.0`
for human review and `zero sync --from-source <package>` after humans edit
that projection. Keep derived graph artifacts out of the package source unless
the user explicitly asks for them.

Repository graph stores are text by default. Use `zero init --format binary` or
`zero sync --from-source --format binary <package>` only when the package is
intentionally opting into binary `zero.graph` storage. Normal reads auto-detect
both encodings, and normal writes preserve an existing binary store.

## Common Repairs

- `IMP001`: create the imported module, fix its path, or adjust `use`.
- `IMP002`: break a direct import cycle.
- `PKG001`: fix a local dependency path so it contains `zero.toml` or `zero.json`.
- `PKG002`: break a package dependency cycle.
- `PKG003`: avoid resolving one package name to multiple versions.
- `PKG004`: update target metadata or choose a supported target.

Prefer a package-local fix over moving unrelated files.
