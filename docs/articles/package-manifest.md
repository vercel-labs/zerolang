## Package And Manifest Reference

Zero packages normally use `zero.toml` as the package manifest plus a checked-in
`zero.graph` repository graph store as compiler input. `zero.json` is accepted
only as a compatibility manifest format; examples and fixtures use TOML. Use
one manifest per package in normal projects. If both files exist, `zero.toml`
takes precedence for directory inputs; an explicit `zero.json` path still checks
that compatibility manifest directly.

The current compiler supports local packages and executable targets.

TOML is the preferred graph-first manifest format:

```toml
[package]
name = "hello"
version = "0.1.0"
license = "MIT"

[targets.cli]
kind = "exe"
main = "src/main.0"

[dependencies.local-tools]
path = "../local-tools"
version = "0.1.0"
```

The equivalent JSON shape is accepted for compatibility, but new agent-authored
packages should not start from it:

```json
{
  "package": { "name": "hello", "version": "0.1.0", "license": "MIT" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } },
  "dependencies": {
    "local-tools": { "path": "../local-tools", "version": "0.1.0" },
    "registry-tools": "1.2.3"
  },
  "profiles": {
    "dev": { "inherits": "dev" },
    "release": { "inherits": "release" }
  }
}
```

Package-local imports resolve from `src/`:

- `src/foo.0` defines module `foo`
- `src/foo/mod.0` defines directory module `foo`

Import cycles and duplicate public exports are diagnosed before build output.

Local path dependencies are accepted by the resolver. Exact versioned registry
references are recorded as metadata without remote fetches.

Packages use a checked-in `zero.graph` store as the compiler input for normal
check, build, run, test, size, ship, and mem commands. Those commands read and
validate the graph store directly, report whether source projections are clean,
missing, stale, conflicting, or unavailable, and do not rewrite `.0` files. A
graph-first package can be created with:

```sh
zero init --manifest toml app
cd app
zero patch --op 'addMain'
zero check .
```

Use `zero export` to materialize or refresh `.0` projections
for human review. Use `zero import` after humans edit `.0` so
the graph store reflects the reviewed source projection. Use
`zero verify-projection` when CI or review needs the no-write projection drift
gate. A package without `zero.graph` is missing its compiler input; run
`zero import` after reviewing the projection or create it with `zero init`.

`zero inspect --json <package>` reports:

- `package.dependencies`
- `package.lockfile`
- `package.resolver`
- `packageCache.cacheKeyInputs`

The resolver writes a deterministic dependency fingerprint file under
`.zero/package-locks/*.lock.json`. Cache keys include the compiler version,
target, package version, manifest hash, dependency graph hash, and lockfile hash.

Package graph failures use stable diagnostics:

- `PKG001`: a local dependency path does not contain `zero.toml` or a
  compatibility `zero.json`
- `PKG002`: package dependencies form a cycle
- `PKG003`: the graph resolves one package name to conflicting versions
- `PKG004`: the selected target is not listed in a dependency's target metadata

`zero doc --json <package>` exposes registry-oriented publication metadata.
Public package APIs should carry docs/examples metadata before publication. The
current compiler reports this as `publicationGate`.
