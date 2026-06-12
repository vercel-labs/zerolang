---
name: agent
description: Graph-first agent workflow for making focused Zero changes with CLI feedback.
---

# Zero Agent Workflow

Use this when editing Zero code, examples, tests, docs, or a package. `zero.graph` is the package compiler input; `.0` files are the human-readable projection. Command text output is written for agents. Use JSON only when another tool must parse stable fields.

## Think In Graph

Edit by graph address instead of retyping lines:

```sh
zero view --fn <name> --handles   # 1. addresses: each statement ends in // #handle
zero patch . --op '<micro-op>'    # 2. one batched edit by handle
zero check                        # 3. validate after direct .0 edits
```

1. Addresses. `zero view --fn <name> --handles` prints one function with a `// #handle` margin comment per statement; compound headers show `#stmt #block`, and else/arm lines show the clause block (a `replaceBlockBody` target). For cross-function work `zero query --refs <name> --handles` adds `stmt:#...` to every reference row; `zero query --fn <name> --handles` lists stmt and param handles.
2. One batched edit. Micro-ops change one thing precisely; repeat `--op` to batch edits into one patch with a single revalidation. Change a value:

```sh
zero patch . --op 'set node="#a647" field="value" expect="1" value="8"'
```

`set` edits one field (`value`, `type`, `name`/operator). `replaceExpr node="#h" with="<expr>"` swaps any expression subtree; aimed at a statement handle it replaces that statement's expression (initializer, condition, return value). Express a cross-cutting change once as a structural rewrite (`$A`, `$B` bind subtrees; dry run by default):

```sh
zero patch . --rewrite 'bnCmp($A, $B) == 0' --to 'bnEq($A, $B)' --apply
```

3. Validate. A successful patch has already validated and saved the graph; do not run `zero check` to confirm it. Then `zero run . -- <args>` / `zero test`.

Scoped reads; never read a whole `.0` file for one function:

- `zero view --fn <name>`: one function's source; misses fail with close matches.
- `zero view --fn <name> --around <text>`: only the enclosing block containing the text.
- `zero view --outline <module-or-file>`: signatures plus one-line docs, no bodies.

For a new agent-authored package: `zero init`, then `zero patch --op 'addMain'`.

## zero query

```text
zero query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>]
           [--node <id>] [--depth <n>] [--full] [--handles] [graph-input|name]
```

- bare name that is not an existing path: runs `--find` against the current package
- `--find <text>`: search names, ids, types, values, and node kinds; prints matches with spans
- `--calls <name>` / `--refs <name>`: resolved calls and semantic references
- `--node <id>`: one node's span, parents, and children; short handles resolve here too

Whole bodies and in-function text edits (`--replace-fn`, `--replace-in-fn`) plus import/export, identity recovery, and merge live in the `graph` topic. Direct `.0` edits are a last resort; never delete `zero.graph`.

## Verify Before Done

After a fix works on the path you changed, exercise the paths you did not. Zero inserts runtime checks (indexing is bounds-checked), so code that passes `zero check` and one probe run can still trap on another input; a trap exits with a signal status and no output.

```sh
zero run . -- <typical input>
zero run . -- <empty or boundary input>
zero test
```

If behavior changed, add or update a `test` block. On a diagnostic, run `zero explain <code>` before broad refactors.

## Rules

- Treat effects as capabilities, not ambient globals: `World`, `std.fs`, `std.args`, `std.env`.
- Use `Maybe<T>`, explicit `raises` / `raises [...]`, and `check` / `rescue` instead of hidden failure.
- Do not invent syntax or CLI fields; load `language` when unsure.
- Do not hand-write parsing or validation before checking the `stdlib` topic: it ships validators such as `std.time` (RFC 3339), `std.inet`, `std.regex`, and `std.unicode`. Fetch one module's signatures with `zero skills get stdlib --topic std.time`.
