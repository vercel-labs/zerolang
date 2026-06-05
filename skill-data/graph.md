---
name: graph
description: Use ProgramGraph commands as the primary agent authoring and inspection surface.
---

# Zero Graph Authoring

Use this when an agent needs to inspect, plan, patch, or validate Zero changes through the graph interface. The graph interface is the primary agent authoring surface. `.0` files are canonical source text; `.program-graph` artifacts are derived debug and interchange files.

## Source Boundary

- Commit `.0` source text for durable program changes.
- Use `zero graph view <input>` to render canonical source text from a source file or ProgramGraph artifact.
- Use `zero graph patch <file.0> ...` for source-backed graph edits that rewrite the canonical source after validation.
- Write explicit graph artifacts only when you need an interchange/debug file, using non-source paths such as `.zero/agent/app.program-graph`.
- Use `zero graph status <input>` to inspect repository graph sync readiness. `zero graph sync --from-source <input>` writes the repository `zero.graph` store from current `.0` source; `zero graph sync --from-graph <input>` rewrites stale `.0` source projections from that store; `zero graph verify-sync <input>` checks graph/source sync without writing; `zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <input>` combines independent repository graph store edits and reports graph-addressed conflicts.

## Graph-First Loop

Inspect the source through the graph interface:

```sh
zero graph view <file-or-package>
zero graph check <file-or-package>
zero graph status <file-or-package>
zero graph dump --json <file-or-package>
```

Create a derived graph artifact when you need to carry a graph between tools:

```sh
zero graph dump --out .zero/agent/app.program-graph <file-or-package>
```

Inspect it with normal readable output:

```sh
zero graph view .zero/agent/app.program-graph
zero graph check .zero/agent/app.program-graph
zero graph roundtrip .zero/agent/app.program-graph
```

Use JSON when you need exact node IDs, graph hashes, spans, or operation fields:

```sh
zero graph dump --json <file-or-package>
zero graph source-map --json <file-or-package>
zero graph inspect --json <file-or-package>
zero graph patch --json <file.0> --expect-graph-hash graph:a7f7e6899a73f3b4 --op 'rename node="#decl_ad8d9028" expect="main" value="start"'
```

Use source maps when a tool needs to connect graph nodes back to source ranges:

```sh
zero graph source-map --json <file-or-package>
```

When a human has edited source after an agent captured a graph, reconcile the prior graph with the edited source before relying on old node IDs:

```sh
zero graph dump --out .zero/agent/app.before.program-graph <file-or-package>
zero graph reconcile --json .zero/agent/app.before.program-graph --source <file-or-package>
```

`zero graph reconcile` reports unchanged, edited, inserted, deleted, ambiguous, and identity-changed nodes. Ambiguous identity matches fail instead of silently assigning a stale node handle.

## Patches

Graph patches are checked against the input graph hash and node facts:

```sh
zero graph patch \
  <file.0> \
  --expect-graph-hash graph:a7f7e6899a73f3b4 \
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello agent\n"'
```

For larger edits, use a patch file:

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello agent\n"
rename node="#decl_ad8d9028" expect="main" value="start"
delete node="#patch001"
```

Prefer `set`, `rename`, `insert`, and `delete` patches over editing graph artifact text by hand. Always include `expect graphHash` when you are carrying a patch across tool calls.

## Validate Before Persisting

After a source-backed patch, validate the source:

```sh
zero graph check <file.0>
zero check <file.0>
```

When `zero graph status <input>` reports repository graph sync as enabled,
verify graph/source sync before build/test gates:

```sh
zero graph verify-sync <file-or-package>
zero graph sync --from-source <file-or-package>
zero graph sync --from-graph <file-or-package>
zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <file-or-package>
```

In the current compiler, `sync --from-source` updates `zero.graph` from source
text, preserves existing graph node handles where the source edit is
unambiguous, and stores exact checked-in `.0` projection bytes for tracked local
files. Ambiguous identity changes fail instead of guessing. `sync --from-graph`
updates stale checked-in `.0` projections from `zero.graph`, and `verify-sync`
compares the store with the current source graph and source projection.
When a package manifest sets `repositoryGraph.compilerInput` to `true`,
`zero check` validates the graph store directly, including target and package
metadata, so source-free graph packages can still be checked. Use
`zero graph verify-sync` when graph/source drift must fail the workflow. Build,
run, test, size, ship, and mem still verify sync before compiling from
`zero.graph`. Without that marker, normal commands use checked-in `.0` source
text.
`merge` writes only the target `zero.graph` when independent node-hash edits can
be combined; it does not rewrite `.0` projections, so run `sync --from-graph`
after a successful merge when source projections should be refreshed.
In the Zero repository, `pnpm run repository-graph:check` verifies checked-in
`zero.graph` stores for CI with the pinned `linux-musl-x64` graph target.

For derived graph artifacts, validate the artifact before applying the accepted change to source:

```sh
zero graph validate .zero/agent/app.patched.program-graph
zero graph check .zero/agent/app.patched.program-graph
zero graph view .zero/agent/app.patched.program-graph
```

Do not commit `.program-graph` files unless the user explicitly asks for derived artifacts.

## Packages

For packages, inspect the graph from the package root or manifest. Only write an artifact when another tool needs a file transfer:

```sh
zero graph view <package-dir>
zero graph check <package-dir>
zero graph import --out .zero/agent/package.program-graph <package-dir>
```

Normal build, run, test, and ship commands use canonical source unless the command is explicitly `zero graph ...`.
If `zero.json` sets `repositoryGraph.compilerInput` to `true`, those commands
use the checked-in `zero.graph` store after a no-write sync check. `zero check`
validates the store directly and reports target/package facts without requiring
the checked-in source projection.
