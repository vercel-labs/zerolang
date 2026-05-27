---
name: graph
description: Use ProgramGraph artifacts as the primary agent authoring and inspection surface.
---

# Zero Graph Authoring

Use this when an agent needs to inspect, plan, patch, or validate Zero changes through the graph interface. ProgramGraph artifacts are the primary agent authoring surface. `.0` and `.row` files are canonical source text; `.program-graph` artifacts and `.zero` previews are derived.

## Source Boundary

- Commit `.0` or `.row` source text for durable program changes.
- Write graph artifacts to non-source paths such as `.zero/agent/app.program-graph`.
- Use `.zero` only for generated human-readable graph previews from `zero graph view`.
- Do not write graph output to `.0` or `.row`; those extensions are source text.

## Graph-First Loop

Create a derived graph artifact:

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
zero graph patch --json --out .zero/agent/app.patched.program-graph .zero/agent/app.program-graph --expect-graph-hash graph:f76987e99677f1b3 --op 'rename node="#ea5ea1ca" expect="main" value="start"'
```

## Patches

Graph patches are checked against the input graph hash and node facts:

```sh
zero graph patch \
  --out .zero/agent/app.patched.program-graph \
  .zero/agent/app.program-graph \
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

After a patch, validate the graph artifact first:

```sh
zero graph validate .zero/agent/app.patched.program-graph
zero graph check .zero/agent/app.patched.program-graph
zero graph view .zero/agent/app.patched.program-graph
```

Then persist the accepted change in the canonical `.0` or `.row` file and run the source command:

```sh
zero check <file-or-package>
zero test <file-or-package>
```

Do not commit `.program-graph` or generated `.zero` files unless the user explicitly asks for derived artifacts.

## Packages

For packages, derive the graph from the package root or manifest:

```sh
zero graph import --out .zero/agent/package.program-graph <package-dir>
zero graph test .zero/agent/package.program-graph
```

If a package manifest has `targets.cli.graph`, graph subcommands may use that artifact. Normal build, run, test, and ship commands use canonical source unless the command is explicitly `zero graph ...`.
