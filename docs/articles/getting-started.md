## Getting Started

Zero is the programming language for agents. The normal first workflow is not
"write a `.0` file"; it is "ask an agent to create a graph-first package, let
the compiler own the graph, and materialize `.0` only as the human-readable
projection."

## Install The Compiler

```sh
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

The installer downloads the latest matching binary from the GitHub release and
writes it to `$HOME/.zero/bin/zero`.

## Ask An Agent For Hello World

Start by telling the agent to use Zero's bundled skills and to write through the
graph:

```text
Check out the agent, graph, and language skills for Zero:
zero skills list
zero skills get agent
zero skills get graph
zero skills get language

Create a hello world Zero program. Use the graph to read and write. `.0` files
are for humans only; sync them from the graph after the graph checks and runs.
```

A typical agent conversation should look like this:

```text
You:
  Create a hello world Zero program. Use the graph to read and write.
  `.0` files are for humans only.

Agent:
  I will initialize a graph-first package, patch zero.graph, validate it, run it,
  then sync the `.0` projection for review.

Agent runs:
  zero init hello
  cd hello
  zero patch --op 'addMain' --op 'addCheckWrite fn="main" text="hello from zero\n"'
  zero check .
  zero run .
  zero sync --from-graph .
  zero verify-sync .

Agent reports:
  The package compiles from zero.graph, zero run . prints "hello from zero",
  and src/main.0 was regenerated from the graph for human review.
```

Expected output from the run:

```text
hello from zero
```

## What The Agent Created

The graph-first package has these important files:

```text
hello/
  zero.toml or zero.json
  zero.graph
  src/
    main.0
```

`zero.graph` is the repository graph store and the normal compiler input for
this package. Agents should usually inspect and patch that graph with
`zero query` and `zero patch`. Its default encoding is text; binary
`zero.graph` storage is available with `--format binary` when you specifically
want to test or opt into direct binary graph loading. Binary graph artifacts are
also supported for explicit graph outputs. The standard library already uses
binary `std/*.graph` stores for compilation while keeping `std/*.0` projections
for human review.

`src/main.0` is the human-readable projection. It is deliberately readable and
bidirectional: humans can review it, and humans may edit it directly when that
is the right workflow. After a human edit, run `zero sync --from-source .`
to refresh `zero.graph`. Agents should not normally hand-write `.0` files.

After `zero sync --from-graph .`, the projection should look like this:

```zero
pub fn main(world: World) -> Void raises {
    check world.out.write("hello from zero\n")
}
```

That file is useful for human trust. The graph remains the normal agent
read/write surface.

## Check, Run, And Build

From inside the graph-first package:

```sh
zero check .
zero run .
zero build --target linux-musl-x64 --out .zero/out/hello .
```

Normal compiler commands compile from `zero.graph` when
`repositoryGraph.compilerInput: true` is set in `zero.toml` or `zero.json`.
They report source
projection state, but they do not rewrite `.0` files. Sync explicitly when the
human projection needs to be refreshed.

## Ask For The Next Change

Keep the same graph-first instruction in the prompt:

```text
Add a function add(x, y) that returns x + y, and add a test for it.
Use the graph to read and write. Do not hand-edit `.0`; sync the projection
from the graph after checks pass.
```

The agent should use `zero patch` operations, validate with `zero check .` and
`zero test .`, then run `zero sync --from-graph .` only for the human
projection.

## Learn The Core Syntax

The examples are still useful for humans learning how Zero reads:

```sh
zero check examples/hello.0
zero check examples/hello-let.0
zero check examples/functions.0
zero check examples/branch.0
zero check examples/point.0
zero check examples/result-choice.0
```

They cover:

- entry points and output
- `let` bindings
- functions and return values
- conditionals
- `type` data declarations
- `enum`, `choice`, and `match`

For agent-authored programs, use those files as projections and examples, not
as the default write path.

## Inspect A Package

Use graph query to inspect a package in slices:

```sh
zero query .
zero query --fn main .
zero query --find write .
```

The manifest records that the graph store is the compiler input:

```json
{
  "package": { "name": "hello", "version": "0.1.0", "license": "MIT" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } },
  "repositoryGraph": { "compilerInput": true }
}
```

After the first graph-backed build or run, Zero may also create
`.zero/cache/native/mir-*.zmir`. That file is not an authoring surface. It is a
derived final-MIR cache that the compiler memory-maps and verifies before
codegen so later graph builds can stay closer to the compiler.

## Next Steps

- Load `zero skills get agent`, `zero skills get graph`, and
  `zero skills get stdlib` before asking an agent for larger changes.
- Use the examples index to pick the next concept by projection.
- Use the CLI reference for `zero patch`, `zero query`, and sync commands.
- Use Building From Source when you want to validate a local checkout.
