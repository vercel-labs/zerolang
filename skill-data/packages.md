---
name: packages
description: Create, inspect, and repair Zero packages and manifests.
---

# Zero Packages

Use this when working with `zero.json`, package-local modules, package tests, or multi-file Zero projects.

## Create

```sh
zero new cli hello
zero new lib math-tools
zero new package app
```

Check the generated files before changing structure.

## Manifest

Minimal executable package:

```json
{
  "package": { "name": "hello", "version": "0.1.0" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } }
}
```

Pass either the package directory or manifest to commands:

```sh
zero check .
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
zero check --json <package>
zero graph --json <package>
```

## Dependencies

Current packages support local path dependencies and registry metadata. Local dependencies must point at a directory containing `zero.json`.

```json
{
  "dependencies": {
    "local-tools": { "path": "../local-tools", "version": "0.1.0" }
  }
}
```

The resolver is declarative; it records deterministic lock facts under `.zero/package-locks/` and does not fetch remote package code.

## Inspect

```sh
zero graph --json <package>
zero doc --json <package>
zero dev --json --trace <package>
```

Useful `graph` facts include modules, source paths, import edges, public and private symbol counts, function effects, required capabilities, target facts, dependency facts, and package cache key inputs.

## Common Repairs

- `IMP001`: create the imported module, fix its path, or adjust `use`.
- `IMP002`: break a direct import cycle.
- `PKG001`: fix a local dependency path so it contains `zero.json`.
- `PKG002`: break a package dependency cycle.
- `PKG003`: avoid resolving one package name to multiple versions.
- `PKG004`: update target metadata or choose a supported target.

Prefer a package-local fix over moving unrelated files.
