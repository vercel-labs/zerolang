---
name: agent
description: Graph-first agent workflow for making focused Zero changes with CLI feedback.
---

# Zero Agent Workflow

Use this when editing Zero code, examples, tests, docs, or a package. The graph interface is the primary authoring surface for agents: inspect and patch source through ProgramGraph commands, and use ProgramGraph artifacts only when you need an interchange/debug file. `.0` files are the canonical source text that gets committed. Zero command text is designed to be readable by agents; use JSON when another tool must parse stable fields or when deeper diagnostics are needed.

## Start

Use the same compiler binary that will run the project:

```sh
zero --version
zero skills list
zero skills get language
zero skills get graph
zero skills get diagnostics
```

Inside the Zero repository checkout, prefer `bin/zero` over a global `zero`. For installed user projects, use the `zero` on `PATH` unless the user points at another binary.

## Graph-First Edit Loop

1. Read the nearest `zero.json`, source files, tests, and examples enough to understand the package boundary.
2. Inspect the current source through the graph:

```sh
zero graph view <file-or-package>
zero graph check <file-or-package>
zero graph status <file-or-package>
```

3. Use JSON when you need exact node IDs or graph hashes:

```sh
zero graph dump --json <file-or-package>
```

4. For precise mechanical edits on canonical `.0`, prefer a checked graph patch that rewrites the source after validation:

```sh
zero graph patch <file.0> --expect-graph-hash graph:a7f7e6899a73f3b4 --op 'rename node="#decl_ad8d9028" expect="main" value="start"'
zero graph check <file.0>
zero check <file.0>
```

5. When a graph artifact is necessary, write it under `.zero/`, patch the artifact, validate it, and then make the accepted source change. Do not commit derived `.program-graph` files unless the user explicitly asks.
6. If `zero graph status <input>` reports repository graph sync as enabled, run `zero graph verify-sync <input>` before build/test gates. Use `zero graph sync --from-source <input>` to refresh `zero.graph` from reviewed source changes while preserving unambiguous graph node handles and exact local `.0` projection bytes, or `zero graph sync --from-graph <input>` to refresh stale `.0` source projections from `zero.graph`. When `zero.json` sets `repositoryGraph.compilerInput` to `true`, normal check/build/run/test commands compile from `zero.graph` after this sync check. When combining repository graph stores, use `zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <input>` and then refresh projections explicitly if the merge succeeds.
7. Run a focused source check:

```sh
zero check <file-or-package>
```

8. When the compiler reports a diagnostic, explain the code first. If you need stable fields or a repair plan, rerun with JSON:

```sh
zero explain <diagnostic-code>
zero check --json <file-or-package>
zero fix --plan --json <file-or-package>
```

9. If behavior changes, add or update a `test` block or conformance fixture.
10. Validate with the narrowest command that covers the changed surface.

## Agent Rules

- Treat effects as capabilities, not ambient globals. Use `World`, `std.fs`, `std.args`, `std.env`, and similar APIs only where the target supports them.
- Keep examples copyable and runnable from the repository or package root.
- Prefer explicit types at public boundaries and when inference is unclear.
- Use `Maybe<T>`, explicit `raises` / `raises [...]`, and `check` instead of hidden failure.
- Prefer graph inspection and source-backed graph patches for agent planning and mechanical edits.
- Do not invent syntax. Load `language` when unsure.
- Do not invent CLI fields. If you need fields, run the command with `--json` and read the data.

## Useful Focused Commands

```sh
zero check <input>
zero graph <input>
zero graph view <input>
zero graph check <input>
zero graph status <input>
zero graph dump --json <input>
zero test <input>
zero size <input>
zero doctor
```

For CLI behavior, JSON contracts, or editor/tool integrations in the Zero repo, use `--json` and the repository scripts listed by `AGENTS.md` or the project documentation.
