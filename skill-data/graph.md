---
name: graph
description: Use ProgramGraph commands as the primary agent authoring and inspection surface.
---

# Zero Graph Authoring

Use this when an agent needs to create, inspect, patch, validate, or sync Zero
programs through the graph interface. The graph interface is the primary agent authoring surface. `zero.graph` is the repository graph store for opted-in
packages, `.0` files are the human-readable source projection, and
`.program-graph` files are derived debug/interchange artifacts.

## Source Boundary

- For graph-first packages, patch the package or `zero.json`; the patch writes
  `zero.graph` after validation. Normal `zero check`, `zero run`, `zero test`,
  `zero build`, `zero size`, `zero ship`, and `zero mem` commands compile from
  `zero.graph` when `repositoryGraph.compilerInput` is true.
- Use `zero graph sync --from-graph <package>` to materialize or refresh `.0`
  projections for human review.
- Use `zero graph sync --from-source <package>` after a human edits `.0` so the
  reviewed source projection refreshes `zero.graph`.
- Use `zero graph verify-sync <package>` when graph/source drift must fail
  without writing files.
- Use `.program-graph` artifacts only when another tool needs a standalone
  debug or interchange file.

## Graph-First Loop

Create a graph-first package without writing `.0` source:

```sh
zero graph init app
cd app
zero graph patch --op 'addMain'
zero graph query .
zero check .
zero run .
zero graph sync --from-graph .
```

Build useful program shape through graph operations:

```sh
zero graph patch \
  --op 'addFunction name="add" ret="i32"' \
  --op 'addParam fn="add" name="left" type="i32"' \
  --op 'addParam fn="add" name="right" type="i32"' \
  --op 'addReturnBinary fn="add" name="+" left="left" right="right" type="i32"' \
  --op 'addTest name="addition works" call="add" arg0="40" arg1="2" expect="42" type="i32"'
zero test .
```

Create a simple CLI that parses two `u32` command-line arguments, adds them,
and writes the result:

```sh
zero graph patch --op 'setMainArgsAddCli fn="add_u32"'
zero run . -- 40 2
```

Inspect an existing package through the graph interface:

```sh
zero graph query <file-or-package>
zero graph query --fn main <file-or-package>
zero graph query --find write <file-or-package>
zero graph query --calls std <file-or-package>
zero graph query --refs add <file-or-package>
zero graph query --node '#expr_2cad38f9' <file-or-package>
zero graph view <file-or-package>
zero graph check <file-or-package>
zero graph status <file-or-package>
```

Stay on normal graph output for agent inspection. Add `--json` only when an
automation tool needs stable fields or a debugging session needs exact machine
facts. Use `zero graph query --fn <name>` when you know the function you need,
`zero graph query --calls <name>` to find resolved call targets,
`zero graph query --refs <name>` to find semantic references, and
`zero graph query --find <text>` to search names, IDs, types, values, paths, and
node kinds for patchable handles. Use those handles for checked edits such as
`set`, `rename`, or `delete`; `zero graph query --node <id>` shows the selected
node's parent and child edges without a full graph dump. Low-level delete
compacts ordered graph groups so valid sibling order is preserved. Use full
dumps only when a tool needs every node and edge:

```sh
zero graph query <file-or-package>
zero graph query --fn main <file-or-package>
zero graph query --find parse <file-or-package>
zero graph query --calls std <file-or-package>
zero graph query --refs add <file-or-package>
zero graph query --node '#fn_main' <file-or-package>
zero graph source-map <file-or-package>
zero graph inspect <file-or-package>
zero graph patch <package> --op 'addMain'
```

Create a derived graph artifact only when you need to carry a graph between
tools:

```sh
zero graph dump --out .zero/agent/app.program-graph <file-or-package>
```

Inspect it with normal readable output:

```sh
zero graph view .zero/agent/app.program-graph
zero graph check .zero/agent/app.program-graph
zero graph roundtrip .zero/agent/app.program-graph
```

Use source maps when a tool needs to connect graph nodes back to source ranges:

```sh
zero graph source-map <file-or-package>
```

When a human has edited source after an agent captured a derived graph, reconcile
the prior graph with the edited source before relying on old node IDs:

```sh
zero graph dump --out .zero/agent/app.before.program-graph <file-or-package>
zero graph reconcile .zero/agent/app.before.program-graph --source <file-or-package>
```

`zero graph reconcile` reports unchanged, edited, inserted, deleted, ambiguous,
and identity-changed nodes. Ambiguous identity matches fail instead of silently
assigning a stale node handle.

## Patches

Graph patches against an opted-in package write `zero.graph` after loading the
store, applying operations, validating graph readiness, and rechecking compiler
input:

```sh
zero graph patch \
  --op 'addMain' \
  --op 'addCheckWrite fn="main" text="hello from graph\n"'
zero check .
zero run .
```

For common CLI scaffolding, prefer the structured operation:

```sh
zero graph patch --op 'setMainArgsAddCli fn="add_u32"'
zero run . -- 40 2
```

List supported patch operation shapes without loading or writing a graph:

```sh
zero graph patch --op help
```

For precise existing-node edits, use graph hashes and node facts:

```sh
zero graph patch \
  <package> \
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

Prefer structured operations over editing graph artifact text by hand. Always
include `expect graphHash` when you are carrying a patch across tool calls.

## Validate And Sync

After a graph-first patch, validate with the normal compiler commands:

```sh
zero graph check <package>
zero check <package>
zero test <package>
```

When human-readable source projections are needed, sync explicitly:

```sh
zero graph sync --from-graph <package>
zero graph verify-sync <package>
```

When a human edits `.0`, bring the graph store back in sync:

```sh
zero graph status <package>
zero graph sync --from-source <package>
zero check <package>
```

`sync --from-source` updates `zero.graph` from source text, preserves existing
graph node handles where the source edit is unambiguous, and stores exact
checked-in `.0` projection bytes for tracked local files. Ambiguous identity
changes fail instead of guessing. `sync --from-graph` updates stale or missing
checked-in `.0` projections from `zero.graph`, and `verify-sync` checks the
store against the current source graph and source projection.

When a package manifest sets `repositoryGraph.compilerInput` to `true`, normal
compiler commands validate and compile from the graph store, including target
and package metadata, so source-free graph packages can still be checked, built,
run, tested, sized, shipped, and inspected. Commands report whether the source
projection is clean, missing, stale, conflicting, or unavailable, but do not
rewrite `.0` files. Use `zero graph verify-sync` when graph/source drift must
fail the workflow. Without that marker, normal commands use checked-in `.0`
source text.

`merge` writes only the target `zero.graph` when independent node-hash edits can
be combined; it does not rewrite `.0` projections, so run `sync --from-graph`
after a successful merge when source projections should be refreshed.

In the Zero repository, `pnpm run repository-graph:check` verifies checked-in
`zero.graph` stores for CI with the pinned `linux-musl-x64` graph target.

For derived graph artifacts, validate the artifact before applying any accepted
change to a package graph store or source projection:

```sh
zero graph validate .zero/agent/app.patched.program-graph
zero graph check .zero/agent/app.patched.program-graph
zero graph view .zero/agent/app.patched.program-graph
```

Do not commit `.program-graph` files unless the user explicitly asks for derived
artifacts.

## Packages

For packages, inspect and patch from the package root or manifest. Only write an
artifact when another tool needs a file transfer:

```sh
zero graph query <package-dir>
zero graph view <package-dir>
zero graph check <package-dir>
zero graph patch <package-dir> --op 'addMain'
```

If `zero.json` sets `repositoryGraph.compilerInput` to `true`, those commands
use the checked-in `zero.graph` store and report source projection state without
rewriting `.0` files. Otherwise, normal build, run, test, and ship commands use
checked-in `.0` source unless the command is explicitly `zero graph ...`.
