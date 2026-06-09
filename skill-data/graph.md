---
name: graph
description: Use ProgramGraph commands as the primary agent authoring and inspection surface.
---

# Zero Graph Authoring

Use this when an agent needs to create, inspect, patch, validate, import, or
export Zero programs through the graph interface. The graph interface is the primary agent authoring surface. `zero.graph` is the repository graph store for
packages, `.0` files are the human-readable source projection, and
`.program-graph` files are derived debug/interchange artifacts.

## Source Boundary

- For packages, patch the package or active manifest (`zero.toml`
  takes precedence over `zero.json`); the patch writes
  `zero.graph` after validation. Normal `zero check`, `zero run`, `zero test`,
  `zero build`, `zero size`, and `zero mem` commands compile from
  `zero.graph`.
- Use `zero export [package]` only when the user asks to materialize or refresh
  `.0` projections for human review.
- Use `zero import [package]` after a human edits `.0` so the
  reviewed source projection refreshes `zero.graph`.
- Use `zero verify-projection [package]` when projection drift must fail
  without writing files.
- Use `.program-graph` artifacts only when another tool needs a standalone
  debug or interchange file.

## Repository Store Encoding

`zero.graph` defaults to the binary repository graph store. The compiler loads
that store through typed graph tables without parsing a text wrapper. Text
stores remain available only when explicitly requested for debugging or
inspection:

```sh
zero init
zero init app
zero init --format text app-debug
zero import --format text
zero patch --format text --op 'addMain'
zero validate --format binary --out /tmp/app.graph
```

Reads auto-detect text and binary `zero.graph` stores and binary graph
artifacts. Plain package writes preserve an existing text or binary store, and
`zero status [package]` reports `store format: text|binary`. Prefer the binary
default for agent-authored packages. Use `--format text` only when the task
needs a readable repository store artifact. The standard library also uses
binary `std/*.graph` stores for the compile path, while `std/*.0` files remain
human-readable projections for review.

## Diffing Graph Stores

Use `zero diff [graph-input]` when a human wants a readable Git diff for
`.graph` files. It prints the canonical source projection on stdout for Git
textconv drivers. It does not write `.0` files, and it is not the semantic
inspection, merge, or repair surface. Agents should still use `zero query`,
`zero inspect`, and `zero patch` for graph work.

This repository marks graph stores with:

```gitattributes
*.graph diff=zero-graph
```

Each clone also needs a Git textconv command. In a Zero checkout:

```sh
git config diff.zero-graph.textconv 'bin/zero diff'
```

For an installed compiler outside the Zero repository:

```sh
git config --global diff.zero-graph.textconv 'zero diff'
```

`zero.graph` remains the authoring and repository compiler-input store.
Repository graph build, run, test, size, and mem commands, plus
standalone `.program-graph` build, run, and size commands, may additionally write
`.zero/cache/native/mir-*.zmir`, a derived final-MIR cache. The compiler
memory-maps and verifies that cache before codegen; stale caches are rejected
by compiler version, graph hash, target, emit kind, and backend request.
Agents should not patch `.zmir` files. JSON outputs expose this path as a
`mappedFinalMir` compiler cache entry; `hit: true` means the cache was reused,
`written: true` means the current command generated it before mapping it, and
`borrowedStorage: true` means codegen is reading stable strings/readonly data
from the mapped cache instead of copied source text. For warm repository
`zero.graph` build/run hits and warm standalone `.program-graph` build/run hits,
`codegenImmediate: true` and `programReconstructed: false` mean codegen started
from mapped final MIR without reconstructing checked Program state. Graph-backed
`zero size` also reports `programReconstructed: false`; it derives helper and
capability summaries from graph/IR facts instead of checked Program state.

## Graph-First Loop

Create a graph-first package without writing `.0` source:

```sh
zero init
zero patch --op 'addMain'
```

`zero init` defaults to the current directory. Use `zero init app` only when the
user asks for a new subdirectory. If the user explicitly asks for starter
files, keep the creation surface under init with
`zero init --template cli|lib|package [app]`.

Build useful program shape through graph operations:

```sh
zero patch \
  --op 'addFunction name="add" ret="i32"' \
  --op 'addParam fn="add" name="left" type="i32"' \
  --op 'addParam fn="add" name="right" type="i32"' \
  --op 'addReturnBinary fn="add" name="+" left="left" right="right" type="i32"' \
  --op 'addTest name="addition works" call="add" arg0="40" arg1="2" expect="42" type="i32"'
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
```

For output from a local value:

```sh
zero patch \
  --op 'addLetLiteral fn="main" name="message" type="String" value="hello\n"' \
  --op 'addCheckWriteValue fn="main" value="message" type="String"'
```

For CLI behavior and other multi-statement workflows, use row syntax in a graph
patch file. This keeps the write on `zero.graph`; the `.0` file is only the
human projection you export later. Do not add or depend on program-specific patch
operations for toy workflows.

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
replaceFunctionBody main
  let name Maybe<String> = std.args.get 1
  let excited Bool = std.args.has 2 || !name.has
  if name.has && !excited
    check world.out.write "hello "
    check world.out.write name.value
    check world.out.write "\n"
  else if name.has
    check world.out.write "hello! "
    check world.out.write name.value
    check world.out.write "\n"
    return
  else
    check world.out.write "hello anonymous\n"
end
```

To replace only one branch or nested body, query block handles and patch the
selected `Block` node instead of rewriting the whole function:

```sh
zero query --find Block
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
zero patch --check-only /tmp/body.patch
zero patch --dry-run --json /tmp/body.patch
```

Inspect an existing package through the graph interface:

```sh
zero query
zero query --fn main
zero query --find write
zero query --calls std
zero query --refs add
zero query --node '#expr_2cad38f9'
zero view
zero status
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
zero query
zero query --fn main
zero query --find parse
zero query --calls std
zero query --refs add
zero query --node '#fn_main'
zero source-map
zero inspect
zero patch --op 'addMain'
```

Create a derived graph artifact only when you need to carry a graph between
tools:

```sh
zero dump --out .zero/agent/app.program-graph
```

Inspect it with normal readable output:

```sh
zero view .zero/agent/app.program-graph
zero check .zero/agent/app.program-graph
zero roundtrip .zero/agent/app.program-graph
```

Use source maps when a tool needs to connect graph nodes back to source ranges:

```sh
zero source-map
```

When a human has edited source after an agent captured a derived graph, reconcile
the prior graph with the edited source before relying on old node IDs:

```sh
zero dump --out .zero/agent/app.before.program-graph [graph-input]
zero reconcile .zero/agent/app.before.program-graph --source <projection-or-package>
```

`zero reconcile` reports unchanged, edited, inserted, deleted, ambiguous,
and identity-changed nodes. Ambiguous identity matches fail instead of silently
assigning a stale node handle.

## Patches

Graph patches against a package write `zero.graph` after loading the
store, applying operations, validating graph readiness, and saving the result.
Plain success output includes the saved path, new graph hash, functions, and
tests, so agents do not need a follow-up `zero query` just to refresh context:

```sh
zero patch \
  --op 'addMain' \
  --op 'addCheckWrite fn="main" text="hello from graph\n"'
zero run
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

## Validate And Projections

Do not run `zero check` just to validate a successful patch. Run focused
compiler commands only when they verify behavior that patch validation cannot,
such as tests or execution:

```sh
zero test
zero run
```

When human-readable source projections are requested by the user, export
explicitly:

```sh
zero export
zero verify-projection
```

When a human edits `.0`, import the reviewed projection into the graph store:

```sh
zero status
zero import
```

`import` updates `zero.graph` from source text, preserves existing
graph node handles where the source edit is unambiguous, and stores exact
checked-in `.0` projection bytes for tracked local files. Ambiguous identity
changes fail instead of guessing. `export` updates stale or missing
checked-in `.0` projections from `zero.graph`, and `verify-projection` checks the
store against checked-in source projection bytes without rebuilding a source
graph.

For package inputs, normal compiler commands validate and compile from the graph
store, including target and package metadata, so graph packages can still be
checked, built, run, tested, sized, and inspected when `.0`
projections are missing. Commands report whether the source projection is
clean, missing, stale, conflicting, or unavailable, but do not rewrite `.0`
files. Use `zero verify-projection` when projection drift must fail the
workflow.

`merge` writes only the target `zero.graph` when independent node-hash edits can
be combined; it does not rewrite `.0` projections. Run `export` only when a
human-readable projection needs to be refreshed.

In the Zero repository, `pnpm run repository-graph:check` verifies checked-in
`zero.graph` stores for CI with the pinned `linux-musl-x64` graph target.

For derived graph artifacts, validate the artifact before applying any accepted
change to a package graph store or `.graph` sidecar. Export `.0` separately when
humans need to review the projection:

```sh
zero validate .zero/agent/app.patched.program-graph
zero check .zero/agent/app.patched.program-graph
zero view .zero/agent/app.patched.program-graph
```

Do not commit `.program-graph` files unless the user explicitly asks for derived
artifacts.

## Packages

For packages, inspect and patch from the package root or manifest. Commands
default to the current directory, so omit the input when already inside the
package. Only write an artifact when another tool needs a file transfer:

```sh
zero query
zero view
zero patch --op 'addMain'
```

Those commands use the checked-in `zero.graph` store and report source
projection state without rewriting `.0` files.
