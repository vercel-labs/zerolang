---
name: agent
description: Graph-first agent workflow for making focused Zero changes with CLI feedback.
---

# Zero Agent Workflow

Use this when editing Zero code, examples, tests, docs, or a package. `zero.graph` is the package compiler input; `.0` files are the human-readable source projection. Command text is designed to be readable by agents. Use JSON only when another tool must parse stable fields or a debugging session needs the full machine contract.

Inside the Zero repository checkout, prefer `bin/zero` over a global `zero`. Otherwise use the same compiler binary that will run the project; start with `zero --version` and `zero skills`, and load `language`, `graph`, or `diagnostics` only as the task needs them. Fetch each skill topic once and rely on it: the content is fixed for a given compiler binary, so re-running `zero skills get` for a topic you already loaded returns the same text. The `zero` stub topic lists every topic with its approximate size.

## Read, Edit, Verify

The core loop for changing an existing package:

```sh
zero query <name>        # locate: bare name searches the graph, prints handles and spans
zero view --fn <name>    # read: one function's canonical source
# edit: zero patch for structured edits, or edit the .0 source text directly
zero check               # validate; refreshes a stale zero.graph from edited source
zero run . -- <args>     # execute the changed code
```

Never read a whole `.0` file to find one function. Locate the definition with `zero query <name>` (or `zero query --find <text>`), then read only that function with `zero view --fn <name>`; a missing name fails with close matches. `zero view --fn` prints source. `zero query --fn main` prints graph facts and patch handles; use it when you are about to patch, not to read code.

For a new agent-authored package: `zero init`, then `zero patch --op 'addMain'`.

## Locate

- `zero query <name>`: a bare name that is not an existing path runs `--find` against the current package.
- `zero query --find <text>`: search names, IDs, types, values, and node kinds for patchable handles.
- `zero query --calls <name>` / `zero query --refs <name>`: resolved calls and semantic references.
- `zero query --node <id>`: one node's span, parents, and children; add `--depth <n>` for a deeper subtree or `--full` for the whole-module report.

Reserve unfiltered `zero query` dumps for tools that need every node and edge.

## Edit

Both edit surfaces write the same `zero.graph` store:

- `zero patch --op '...'` for structured edits; `zero patch --op help` lists operation shapes. Body rows in `replaceFunctionBody` and `replaceBlockBody` accept canonical projection syntax, the same text `zero view` prints. A successful patch has already validated and saved the graph; do not run `zero check` just to confirm it.
- Direct `.0` text edits. `zero check`, `zero run`, `zero test`, and `zero build` refresh a stale `zero.graph` from the edited package source automatically and note it on stderr; set `ZERO_STALE=fail` to turn staleness into an RGP008 error instead. `zero import` refreshes the store explicitly and accepts the package root or any source path inside it; never delete `zero.graph` to force a reimport.

If `zero import` reports ambiguous identity (RGP007), follow its help: split the text edit into smaller passes or make the change with `zero patch`.

Export `.0` projections with `zero export` only when a human asks to review them; `zero verify-projection` fails on drift without writing files.

## Verify Before Done

After a fix works on the path you changed, exercise the paths you did not. Zero inserts runtime checks (array and span indexing is bounds-checked, for example), so code that passes `zero check` and one probe run can still trap on another input; a trap exits with a signal status such as 133 and no output.

```sh
zero run . -- <typical input>
zero run . -- <empty or boundary input>
zero test
```

If behavior changed, add or update a `test` block. When a diagnostic appears, run `zero explain <code>` before broad refactors; `zero fix --plan` lists candidate repairs for graph-backed inputs.

## Rules

- Treat effects as capabilities, not ambient globals: `World`, `std.fs`, `std.args`, `std.env`.
- Use `Maybe<T>`, explicit `raises` / `raises [...]`, and `check` / `rescue` instead of hidden failure.
- Prefer explicit types at public boundaries and when inference is unclear.
- Do not invent syntax or CLI fields. Load `language` when unsure; run the command with `--json` when automation needs fields.
