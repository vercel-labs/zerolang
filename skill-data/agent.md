---
name: agent
description: Graph-first agent workflow for making focused Zero changes with CLI feedback.
---

# Zero Agent Workflow

Use this when editing Zero code, examples, tests, docs, or a package. The graph
interface is the primary authoring surface for agents: inspect and patch
packages through ProgramGraph commands. For graph-first packages, agents write
`zero.graph`; `.0` files are the human-readable source projection. ProgramGraph
artifacts are only for interchange/debug files. Zero command text is designed to
be readable by agents. Use JSON only when another tool must parse stable fields
or a debugging session needs the full machine contract.

## Start

Use the same compiler binary that will run the project:

```sh
zero --version
zero skills
zero skills get language
zero skills get graph
zero skills get diagnostics
```

Inside the Zero repository checkout, prefer `bin/zero` over a global `zero`. For installed user projects, use the `zero` on `PATH` unless the user points at another binary.

## Graph-First Edit Loop

1. Read the nearest package manifest, graph status, tests, and examples enough
to understand the package boundary. If both `zero.toml` and `zero.json` are in
the package root, `zero.toml` is the active manifest.
2. For a new agent-authored package, start from the graph:

```sh
zero init
zero patch --op 'addMain'
```

`zero init` defaults to the current directory and uses that directory's folder
name as the package name. Use `zero init app` when the user asks to create
a new subdirectory. `zero init` writes TOML metadata by default; use
`zero init --manifest json [package]` only for explicit compatibility cases.
If the user explicitly asks for starter files, use
`zero init --template cli|lib|package [package]`.

3. Inspect the current program through the graph:

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

4. Stay on normal graph output for agent inspection. Add `--json` only when an
automation tool needs stable fields or a debugging session needs exact machine
facts. Use `zero query --fn <name>` for function-local context and
`zero query --calls <name>` for resolved calls, `zero query --refs <name>` for
semantic references, and `zero query --find <text>` to search
patchable node handles for `set`, `insert`, `insertEdge`, `replace`, `rename`,
or `delete` operations. Use `zero query --node <id>` for one node's parent
and child edges. Delete patches preserve valid sibling order for ordered graph
groups. Use full dumps only when a tool needs the complete node/edge table:

```sh
zero query
zero query --fn main
zero query --find parse
zero query --calls std
zero query --refs add
zero query --node '#fn_main'
```

5. For graph-first packages, patch the package graph store. A successful patch
has already loaded, applied, validated, and saved the graph; plain output
includes the saved path, new graph hash, functions, and tests. Do not run
`zero check` or `zero query` just to confirm that the patch applied.

```sh
zero patch --op 'addCheckWrite fn="main" text="hello\n"'
```

For multi-statement functions, compose graph builder operations instead of
writing `.0` source by hand:

```sh
zero patch \
  --op 'addLetLiteral fn="main" name="message" type="String" value="hello\n"' \
  --op 'addCheckWriteValue fn="main" value="message" type="String"'
```

For custom behavior such as CLI argument flow, write row syntax inside
`replaceFunctionBody` for a whole function or `replaceBlockBody` for a selected
`Block` node. Avoid program-specific patch shortcuts; row patches are the
reusable graph surface agents should learn.

```text
zero-program-graph-patch v1
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

For branch-local changes, query block handles and replace only the selected
block:

```sh
zero query --find Block
```

```text
zero-program-graph-patch v1
replaceBlockBody #block_id
  check world.out.write "updated\n"
end
```

Preview repository graph patches without writing:

```sh
zero patch --check-only /tmp/body.patch
```

When you need patch operation shapes, ask the compiler without loading or
writing a graph:

```sh
zero patch --op help
```

If the user asks you to read or write through the graph, do not hand-edit `.0`
source or create temporary `.0` programs as a fallback. Use `zero patch`
with `--op`, `--patch-text`, or a `zero-program-graph-patch v1` file under
`/tmp`. If the graph surface cannot express the change, say which graph
operation is missing instead of silently switching to source text.

6. Only touch `.0` projections when the user asks for human-readable review or
after a human edited the projection. Agents should not export as a routine
post-patch step.

```sh
zero export
zero verify-projection
```

7. If a human edits `.0`, import the reviewed projection back to the graph store:

```sh
zero status
zero import
```

8. When a graph artifact is necessary, write it under `.zero/`, patch the
artifact, validate it, and then apply the accepted change to the package graph
store or `.graph` sidecar. Export `.0` separately when humans need to review the
projection. Do not commit derived `.program-graph` files unless the user
explicitly asks.
9. Use `zero verify-projection <project|zero.toml|zero.json|file.0>` when projection drift must fail the
workflow. For package inputs, normal compiler commands validate and compile
from `zero.graph`; they report projection state but do not rewrite projections.
When combining repository graph stores, use
`zero merge --base <base-zero.graph> --left <left-zero.graph> --right
<right-zero.graph> <project|zero.toml|zero.json|file.0>` and then refresh projections explicitly if the merge
succeeds.
`zero.graph` is binary by default. Use `--format text` only when explicitly
requesting a readable repository graph store; reads auto-detect either format,
and plain package writes preserve an existing text or binary store. `--format
binary` is still useful for explicit graph artifact outputs. Stdlib
`std/*.graph` stores are binary by design; sibling `.0` files are human
projections, not the stdlib compiler source.
10. Run a focused compiler command only when it verifies behavior the patch
cannot prove, such as executing changed code or running tests:

```sh
zero test
zero run
```

11. When the compiler reports a diagnostic, explain the code first. If an
automation tool needs stable fields or a repair plan, rerun with JSON:

```sh
zero explain <diagnostic-code>
zero check --json
zero fix --plan --json
```

12. If behavior changes, add or update a `test` block or conformance fixture.
13. Validate with the narrowest command that covers the changed surface.

## Agent Rules

- Treat effects as capabilities, not ambient globals. Use `World`, `std.fs`, `std.args`, `std.env`, and similar APIs only where the target supports them.
- Keep examples copyable and runnable from the repository or package root.
- Prefer explicit types at public boundaries and when inference is unclear.
- Use `Maybe<T>`, explicit `raises` / `raises [...]`, and `check` instead of hidden failure.
- Prefer package-level graph inspection and graph-store patches for agent
  planning and mechanical edits.
- Treat `.0` edits as human edits. Agents may export `.0` from the graph for
  review, but should not use source text as the implementation path when the
  requested workflow is graph-first.
- Do not invent syntax. Load `language` when unsure.
- Do not invent CLI fields. If automation needs fields, run the command with `--json` and read the data.

## Useful Focused Commands

```sh
zero check
zero inspect
zero query
zero view
zero status
zero test
zero size
zero doctor
```

For CLI behavior, JSON contracts, or editor/tool integrations in the Zero repo, use `--json` and the repository scripts listed by `AGENTS.md` or the project documentation.
