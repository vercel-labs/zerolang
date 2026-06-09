## Packages Are Graph-Backed

In Zerolang, a human usually starts with the package they want, not the manifest fields:

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "start a small crm api package here"
    },
    {
      "role": "assistant",
      "text": "I’ll start the package with zero.toml and add the first API route."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero init --template package --manifest toml --format binary",
          "output": "graph project init ok\nwrote: ./zero.toml\nwrote: ./zero.graph"
        },
        {
          "command": "zero patch /tmp/crm-routes.patch",
          "output": "program graph patch ok"
        },
        {
          "command": "zero run -- $'GET /health\\n\\n'",
          "output": "HTTP/1.1 200 OK\ncontent-type: application/json\nconnection: close"
        }
      ]
    }
  ]
}
```

## What This Means

A Zero package has a manifest, a graph store, and review projections:

```text
zero.toml
zero.graph
src/main.0
```

The manifest tells Zero where the human-readable target projection lives. The
graph store is the normal compiler input. The projection is for spans, review,
and rare manual edits.

## Preferred Manifest

Use `zero.toml` for new projects:

```toml
[package]
name = "crm-api"
version = "0.1.0"
license = "MIT"

[targets.cli]
kind = "exe"
main = "src/main.0"

[dependencies.local-tools]
path = "../local-tools"
version = "0.1.0"
```

`zero.json` is still accepted as a compatibility manifest format. Use one
manifest in normal projects. If both exist for a directory input, `zero.toml`
takes precedence.

## What `main` Means

The `main` path names the human-readable projection associated with a target.
It does not make the projection the normal package compile input.

Normal package commands compile from the checked-in `zero.graph` store:

```sh
zero check
zero test
zero run -- help
zero build --out .zero/out/app
zero size --json
```

These commands report projection state, but they do not rewrite `.0` files.

## Projection Import And Export

Use projection commands only when humans need them:

```sh
zero export
zero verify-projection
zero import
```

`zero export` refreshes `.0` files from the graph. `zero import` rebuilds the
graph from projection text after a human edit. `zero verify-projection` is the
no-write drift gate.

## Dependencies

Package-local imports resolve from `src/` projection paths for stable human
review:

- `src/foo.0` defines module `foo`
- `src/foo/mod.0` defines directory module `foo`

Local path dependencies are accepted. Exact versioned registry references are
recorded as metadata without remote fetches in the current compiler slice.

Package inspection and docs JSON may include `dependencies`, `package.lockfile`,
`packageCache.cacheKeyInputs`, and `publicationGate` facts. Diagnostics such as
`PKG001` and `PKG004` explain invalid manifests, dependency shape issues, and
package resolution failures.

## Profiles

Profiles are declared in the manifest and reported by build/size JSON. Use
`zero build --json` and `zero size --json` to inspect `profileSemantics`,
`profileBudget`, retained helpers, and target readiness.
