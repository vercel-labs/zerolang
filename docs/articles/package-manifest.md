## Package And Manifest Reference

`zero.json` is the package manifest. The current compiler supports local
packages and executable targets.

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

`zero graph --json <package>` reports:

- `package.dependencies`
- `package.lockfile`
- `package.resolver`
- `packageCache.cacheKeyInputs`

The resolver writes a deterministic dependency fingerprint file under
`.zero/package-locks/*.lock.json`. Cache keys include the compiler version,
target, package version, manifest hash, dependency graph hash, and lockfile hash.

Package graph failures use stable diagnostics:

- `PKG001`: a local dependency path does not contain `zero.json`
- `PKG002`: package dependencies form a cycle
- `PKG003`: the graph resolves one package name to conflicting versions
- `PKG004`: the selected target is not listed in a dependency's target metadata

`zero doc --json <package>` exposes registry-oriented publication metadata.
Public package APIs should carry docs/examples metadata before publication. The
current compiler reports this as `publicationGate`.
