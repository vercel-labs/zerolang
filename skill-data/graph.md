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
- Use `zero graph status <input>` to inspect repository graph sync readiness. Repository graph sync is currently contract-only: `verify-sync` and `sync` report the disabled state and do not write files, even when `zero.graph` is present.

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
verify graph/source projection sync before build/test gates:

```sh
zero graph verify-sync <file-or-package>
zero graph sync --from-source <file-or-package>
zero graph sync --from-graph <file-or-package>
```

In the current compiler, repository graph sync is not enabled yet. These sync
commands report the disabled state and leave files unchanged even when a
`zero.graph` file is present.

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
