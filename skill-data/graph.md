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

## Graph-First Loop

Inspect the source through the graph interface:

```sh
zero graph view <file-or-package>
zero graph check <file-or-package>
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
zero graph inspect --json <file-or-package>
zero graph patch --json <file.0> --expect-graph-hash graph:f76987e99677f1b3 --op 'rename node="#ea5ea1ca" expect="main" value="start"'
```

Each node also carries a `pathId`: a structural location such as
`module:hello/function[0]/body[0]/statement[0]` derived from the node's owning
edges. Unlike the content-addressed `id`/`nodeHash`, a `pathId` stays stable when
only a node's contents change, so it is a durable handle for the same logical node
across edits.

## Patches

Graph patches are checked against the input graph hash and node facts:

```sh
zero graph patch \
  <file.0> \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'set node="#610c78bf" field="value" expect="hello\n" value="hello agent\n"'
```

For larger edits, use a patch file:

```text
zero-program-graph-patch v1
expect graphHash "graph:f76987e99677f1b3"
set node="#610c78bf" field="value" expect="hello\n" value="hello agent\n"
rename node="#ea5ea1ca" expect="main" value="start"
delete node="#patch001"
```

Prefer `set`, `rename`, `insert`, and `delete` patches over editing graph artifact text by hand. Always include `expect graphHash` when you are carrying a patch across tool calls.

## Validate Before Persisting

After a source-backed patch, validate the source:

```sh
zero graph check <file.0>
zero check <file.0>
```

For derived graph artifacts, validate the artifact before applying the accepted change to source:

```sh
zero graph validate .zero/agent/app.patched.program-graph
zero graph check .zero/agent/app.patched.program-graph
zero graph view .zero/agent/app.patched.program-graph
```

Do not commit `.program-graph` files unless the user explicitly asks for derived artifacts.

## Packages

For packages, inspect the graph from the package root or manifest. Only write an artifact when another tool needs a file handoff:

```sh
zero graph view <package-dir>
zero graph check <package-dir>
zero graph import --out .zero/agent/package.program-graph <package-dir>
```

Normal build, run, test, and ship commands use canonical source unless the command is explicitly `zero graph ...`.
