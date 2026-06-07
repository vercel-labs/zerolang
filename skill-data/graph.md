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
- Use `zero sync --from-graph <package>` to materialize or refresh `.0`
  projections for human review.
- Use `zero sync --from-source <package>` after a human edits `.0` so the
  reviewed source projection refreshes `zero.graph`.
- Use `zero verify-sync <package>` when graph/source drift must fail
  without writing files.
- Use `.program-graph` artifacts only when another tool needs a standalone
  debug or interchange file.

## Repository Store Encoding

`zero.graph` defaults to the text repository graph store. The compiler also
supports an explicit binary store that is loaded through typed graph tables
without parsing the text wrapper:

```sh
zero init --format binary app
zero sync --from-source --format binary <package>
zero patch --format binary <package> --op 'addMain'
```

Reads auto-detect text and binary `zero.graph` stores. Plain writes preserve an
existing binary store, and `zero status <package>` reports `store format:
text|binary`. Do not make binary the default in prompts; use it when the task is
to test or opt into binary graph storage.

## Graph-First Loop

Create a graph-first package without writing `.0` source:

```sh
zero init app
cd app
zero patch --op 'addMain'
zero query .
zero check .
zero run .
zero sync --from-graph .
```

Build useful program shape through graph operations:

```sh
zero patch \
  --op 'addFunction name="add" ret="i32"' \
  --op 'addParam fn="add" name="left" type="i32"' \
  --op 'addParam fn="add" name="right" type="i32"' \
  --op 'addReturnBinary fn="add" name="+" left="left" right="right" type="i32"' \
  --op 'addTest name="addition works" call="add" arg0="40" arg1="2" expect="42" type="i32"'
zero test .
```

For multi-statement functions, use builder operations instead of hand-authoring
node tables:

```sh
zero patch \
  --op 'addFunction name="add_twice" ret="u32"' \
  --op 'addParam fn="add_twice" name="x" type="u32"' \
  --op 'addParam fn="add_twice" name="y" type="u32"' \
  --op 'addLetBinary fn="add_twice" name="first" type="u32" operator="+" left="x" right="y"' \
  --op 'addLetBinary fn="add_twice" name="total" type="u32" operator="+" left="first" right="y"' \
  --op 'addReturnValue fn="add_twice" value="total" type="u32"'
zero check .
```

For output from a local value:

```sh
zero patch \
  --op 'addLetLiteral fn="main" name="message" type="String" value="hello\n"' \
  --op 'addCheckWriteValue fn="main" value="message" type="String"'
```

For CLI behavior and other multi-statement workflows, use row syntax in a graph
patch file. This keeps the write on `zero.graph`; the `.0` file is only the
human projection you sync later. Do not add or depend on program-specific patch
operations for toy workflows.

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
replaceFunctionBody main
  let name Maybe<String> = std.args.get 1
  if name.has
    check world.out.write "hello "
    check world.out.write name.value
    check world.out.write "\n"
  else
    check world.out.write "hello anonymous\n"
end
```

To replace only one branch or nested body, query block handles and patch the
selected `Block` node instead of rewriting the whole function:

```sh
zero query --find Block <file-or-package>
```

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
replaceBlockBody #block_32cefdd9
  check world.out.write "name: "
  check world.out.write name.value
  check world.out.write "\n"
end
```

Preview repository graph patches without writing:

```sh
zero patch --check-only <package> /tmp/body.patch
zero patch --dry-run --json <package> /tmp/body.patch
```

Inspect an existing package through the graph interface:

```sh
zero query <file-or-package>
zero query --fn main <file-or-package>
zero query --find write <file-or-package>
zero query --calls std <file-or-package>
zero query --refs add <file-or-package>
zero query --node '#expr_2cad38f9' <file-or-package>
zero view <file-or-package>
zero check <file-or-package>
zero status <file-or-package>
```

Stay on normal graph output for agent inspection. Add `--json` only when an
automation tool needs stable fields or a debugging session needs exact machine
facts. Use `zero query --fn <name>` when you know the function you need,
`zero query --calls <name>` to find resolved call targets,
`zero query --refs <name>` to find semantic references, and
`zero query --find <text>` to search names, IDs, types, values, paths, and
node kinds for patchable handles. Use those handles for checked edits such as
`set`, `insert`, `insertEdge`, `replace`, `rename`, or `delete`; `zero
query --node <id>` shows the selected node's parent and child edges without a
full graph dump. Low-level delete compacts ordered graph groups so valid sibling
order is preserved. Use full dumps only when a tool needs every node and edge:

```sh
zero query <file-or-package>
zero query --fn main <file-or-package>
zero query --find parse <file-or-package>
zero query --calls std <file-or-package>
zero query --refs add <file-or-package>
zero query --node '#fn_main' <file-or-package>
zero source-map <file-or-package>
zero inspect <file-or-package>
zero patch <package> --op 'addMain'
```

Create a derived graph artifact only when you need to carry a graph between
tools:

```sh
zero dump --out .zero/agent/app.program-graph <file-or-package>
```

Inspect it with normal readable output:

```sh
zero view .zero/agent/app.program-graph
zero check .zero/agent/app.program-graph
zero roundtrip .zero/agent/app.program-graph
```

Use source maps when a tool needs to connect graph nodes back to source ranges:

```sh
zero source-map <file-or-package>
```

When a human has edited source after an agent captured a derived graph, reconcile
the prior graph with the edited source before relying on old node IDs:

```sh
zero dump --out .zero/agent/app.before.program-graph <file-or-package>
zero reconcile .zero/agent/app.before.program-graph --source <file-or-package>
```

`zero reconcile` reports unchanged, edited, inserted, deleted, ambiguous,
and identity-changed nodes. Ambiguous identity matches fail instead of silently
assigning a stale node handle.

## Patches

Graph patches against an opted-in package write `zero.graph` after loading the
store, applying operations, validating graph readiness, and rechecking compiler
input:

```sh
zero patch \
  --op 'addMain' \
  --op 'addCheckWrite fn="main" text="hello from graph\n"'
zero check .
zero run .
```

List supported patch operation shapes without loading or writing a graph:

```sh
zero patch --op help
```

Supported graph patch operations:

```text
expect graphHash "graph:a7f7e6899a73f3b4"
set node="#id" field="value" expect="old" value="new"
insert node="#id" kind="Literal" parent="#parent" edge="arg" order="0" type="String" value="text"
insertEdge from="#from" to="#to" edge="arg" target="node" order="0"
replace node="#id" expect="nodehash:abc123" kind="Literal" type="String" value="text"
delete node="#id" expect="nodehash:abc123"
rename node="#id" expect="old" value="new"
addMain
addCheckWrite fn="main" text="hello\n"
addFunction name="add" ret="i32"
addParam fn="add" name="left" type="i32"
addReturnBinary fn="add" name="+" left="left" right="right" type="i32"
addLetLiteral fn="main" name="count" type="u32" value="0"
addLetBinary fn="add" name="sum" type="i32" operator="+" left="left" right="right"
addReturnValue fn="identity" value="input" type="i32"
addCheckWriteValue fn="main" value="message" type="String"
addTest name="addition works" call="add" arg0="40" arg1="2" expect="42" type="i32"
replaceFunctionBody main
  let name Maybe<String> = std.args.get 1
  if name.has
    check world.out.write "hello "
    check world.out.write name.value
    check world.out.write "\n"
  else
    check world.out.write "hello anonymous\n"
end
replaceBlockBody #block_id
  check world.out.write "updated\n"
end
```

`insert` and `insertEdge` default `order` to `0` when it is omitted, which is
usually right for singular edges like `expr`, `left`, and `declaredType`.

For precise existing-node edits, use graph hashes and node facts:

```sh
zero patch \
  <package> \
  --expect-graph-hash graph:a7f7e6899a73f3b4 \
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello agent\n"'
```

For larger edits, use a patch file:

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello agent\n"
insert node="#patch001" kind="Literal" parent="#expr_c403020c" edge="arg" order="1" type="String" value="again\n"
rename node="#decl_ad8d9028" expect="main" value="start"
delete node="#patch001"
```

Prefer structured operations over editing graph artifact text by hand. Use
`--patch-text` or write patch files under `/tmp` when an edit spans many lines.
If the user requested graph authoring, do not hand-edit `.0` source or create a
temporary `.0` program as a fallback. Always include `expect graphHash` when you
are carrying a patch across tool calls.

## Validate And Sync

After a graph-first patch, validate with the normal compiler commands:

```sh
zero check <package>
zero test <package>
```

When human-readable source projections are needed, sync explicitly:

```sh
zero sync --from-graph <package>
zero verify-sync <package>
```

When a human edits `.0`, bring the graph store back in sync:

```sh
zero status <package>
zero sync --from-source <package>
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
rewrite `.0` files. Use `zero verify-sync` when graph/source drift must
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
zero validate .zero/agent/app.patched.program-graph
zero check .zero/agent/app.patched.program-graph
zero view .zero/agent/app.patched.program-graph
```

Do not commit `.program-graph` files unless the user explicitly asks for derived
artifacts.

## Packages

For packages, inspect and patch from the package root or manifest. Only write an
artifact when another tool needs a file transfer:

```sh
zero query <package-dir>
zero view <package-dir>
zero check <package-dir>
zero patch <package-dir> --op 'addMain'
```

If `zero.json` sets `repositoryGraph.compilerInput` to `true`, those commands
use the checked-in `zero.graph` store and report source projection state without
rewriting `.0` files. Otherwise, normal build, run, test, and ship commands use
checked-in `.0` source unless the package opts into repository graph input.
