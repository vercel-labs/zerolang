---
name: agent
description: Graph-first agent workflow for making focused Zero changes with CLI feedback.
---

# Zero Agent Workflow

Use this when editing Zero code, examples, tests, docs, or a package. `zero.graph` is the package compiler input; `.0` files are the human-readable source projection. Command text output is written for agents. Use JSON only when another tool must parse stable fields.

## Read, Edit, Verify

The core loop for changing an existing package:

```sh
zero query <name>        # locate: bare name searches the graph, prints matches with spans
zero view --fn <name>    # read: one function's canonical source
# edit: zero patch for structured edits, or edit the .0 source text directly
zero check               # validate; refreshes a stale zero.graph from edited source
zero run . -- <args>     # execute the changed code
```

Never read a whole `.0` file to find one function. Scoped reads:

- `zero view --fn <name>`: one function's source; a missing name fails with close matches.
- `zero view --fn <name> --around <text>`: only the enclosing block containing the text.
- `zero view --outline <module-or-file>`: signatures plus one-line docs, no bodies.
- `zero query --fn <name> --handles`: stmt and param patch handles, only when about to patch.

For a new agent-authored package: `zero init`, then `zero patch --op 'addMain'`.

## zero query

```text
zero query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>]
           [--node <id>] [--depth <n>] [--full] [--handles] [graph-input|name]
```

- bare name that is not an existing path: runs `--find` against the current package
- `--find <text>`: search names, ids, types, values, and node kinds; prints matches with spans
- `--calls <name>` / `--refs <name>`: resolved calls and semantic references
- `--node <id>`: one node's span, parents, and children; `--depth <n>` for a deeper subtree
- no arguments: package overview with modules and function signatures

## Edit

Both edit surfaces write the same `zero.graph` store:

- `zero patch --op '...'` for structured edits; `zero patch --op help` lists operation shapes. A successful patch has already validated and saved the graph; do not run `zero check` just to confirm it.
- Direct `.0` text edits: `zero check`, `zero run`, `zero test`, and `zero build` refresh a stale `zero.graph` automatically; `zero import` refreshes it explicitly. Never delete `zero.graph` to force a reimport.

Load the `graph` topic for patch operations, import/export, identity (RGP007) recovery, and merge.

## Verify Before Done

After a fix works on the path you changed, exercise the paths you did not. Zero inserts runtime checks (array and span indexing is bounds-checked, for example), so code that passes `zero check` and one probe run can still trap on another input; a trap exits with a signal status and no output.

```sh
zero run . -- <typical input>
zero run . -- <empty or boundary input>
zero test
```

If behavior changed, add or update a `test` block. When a diagnostic appears, run `zero explain <code>` before broad refactors.

## Rules

- Treat effects as capabilities, not ambient globals: `World`, `std.fs`, `std.args`, `std.env`.
- Use `Maybe<T>`, explicit `raises` / `raises [...]`, and `check` / `rescue` instead of hidden failure.
- Do not invent syntax or CLI fields; load `language` when unsure.
- Do not hand-write parsing or validation logic before checking the `stdlib` topic: it ships ready-made validators such as `std.time` (RFC 3339 incl. the exact leap-second rule), `std.inet` (IPv4/IPv6/hostname), `std.regex` (ECMA subset), and `std.unicode` (strict UTF-8). Fetch one module's signatures with `zero skills get stdlib --topic std.time`.
