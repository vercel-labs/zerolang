---
name: graph
description: Use ProgramGraph commands as the primary agent authoring and inspection surface.
---

# Zero Graph Authoring

Use this when creating, inspecting, patching, importing, or exporting Zero programs through the graph interface, the primary agent authoring surface. `zero.graph` is the repository graph store for packages, `.0` files are the human-readable source projection, and `.program-graph` files are derived debug/interchange artifacts.

## Source Boundary

- Normal `zero check`, `zero run`, `zero test`, `zero build`, `zero size`, and `zero mem` compile packages from `zero.graph` (`zero.toml` takes precedence over `zero.json`). When the `.0` projection was edited, those commands refresh the stale store from source first and note it on stderr; `ZERO_STALE=fail` turns that refresh into an RGP008 error instead.
- `zero import` refreshes `zero.graph` from edited source explicitly. It accepts the package root, manifest, or any source path inside the package, updates an existing store in place, and preserves node handles where the edit is unambiguous. When several edited nodes could claim one handle, import picks the structurally closest match and notes it on stderr; only genuinely tied matches fail (RGP007) with a split-the-edit strategy. Never delete the store to force a reimport, and omit `--out` for package imports.
- `zero export [package]` materializes `.0` projections for human review; compiler commands report projection state but never rewrite `.0` files. `zero verify-projection [package]` fails on drift without writing anything.
- `zero.graph` is binary by default. Reads auto-detect text and binary stores, writes preserve the existing encoding, and `zero status` reports `store format: text|binary`. Use `--format text` only for a deliberately readable debug store. Stdlib `std/*.graph` stores are binary; sibling `std/*.0` files are projections, not the stdlib compile source.

## Create

```sh
zero init
zero patch --op 'addMain'
```

`zero init` defaults to the current directory and that folder's name. Use `zero init app` for a new subdirectory, `--manifest json` only for explicit compatibility, and `--template cli|lib|package` only when the user asks for starter files.

## Inspect

```sh
zero query userTotals      # bare name that is not a path = --find in the current package
zero query --fn main       # one function's signature and call summary
zero query --fn main --handles   # adds stmt/param patch handles; use before patching
zero query --calls std     # resolved call targets
zero query --refs add      # semantic references
zero query --node '#expr_2cad38f9' --depth 2   # node-scoped: span, parents, children
zero view --fn main        # one function's canonical source
zero view --fn main --handles   # the same source with a trailing // #handle per statement
zero view --fn main --around minLength   # only the enclosing block containing the text
zero view --outline src/main.0           # signatures plus one-line docs, no bodies
zero status                # store format and projection state
```

`--node` defaults to depth 1; add `--full` for the whole-module report. Use handles from `zero view --fn <name> --handles`, `--find`, or `--fn <name> --handles` for checked edits (`set`, `insert`, `insertEdge`, `replace`, `replaceExpr`, `rename`, `delete`); delete compacts ordered graph groups so valid sibling order is preserved. Handles accept short forms: the id segment (`#55ae541c`), any unique prefix (`#55ae`), or `#head..tail` for ids with long shared prefixes; `--handles` views print the shortest form that resolves, and a missing handle fails with the nearest existing one. Reserve unfiltered `zero query` dumps for tools that need every node and edge.

## Patches

Edit through the graph: `zero patch` covers everything from one-line changes (`addCheckWrite`, `rename`, `set`) to whole function bodies (`--replace-fn <fn> --body-file -` with a heredoc). Direct `.0` text edits are a last resort for changes no patch op expresses; the compiler will refresh the graph from edited source, but patch keeps the loop faster (0.2s validation, no reconcile pass) and preserves node identity for queries.

A successful patch loads, applies, validates, and saves `zero.graph`, and prints the saved path, new graph hash, functions, and tests. Do not run `zero check` or `zero query` just to confirm that the patch applied.

```sh
zero patch \
  --op 'addFunction name="add" ret="i32"' \
  --op 'addParam fn="add" name="left" type="i32"' \
  --op 'addParam fn="add" name="right" type="i32"' \
  --op 'addReturnBinary fn="add" name="+" left="left" right="right" type="i32"' \
  --op 'addTest name="addition works" call="add" arg0="40" arg1="2" expect="42" type="i32"'
```

For sub-line edits, think in graph: take a handle from `zero view --fn <name> --handles` and change exactly one thing. `set` edits one field (a literal `value`, a declared `type`, a `name`/operator); `replaceExpr` swaps any expression subtree, and aimed at a statement handle it replaces that statement's expression (initializer, condition, return value). Repeat `--op` to batch several micro-ops into one patch with a single revalidation:

```sh
zero patch . \
  --op 'set node="#a647" field="value" expect="1" value="8"' \
  --op 'replaceExpr node="#5f15" with="i < k + 1"'
```

To express one cross-cutting transformation instead of editing N sites, use structural rewrite by example. `--rewrite '<pattern>' --to '<template>'` matches canonical projection expressions structurally; `$A`, `$B` bind arbitrary subtrees and the same metavariable twice must match equal subtrees. The default is a dry run that lists every site as `path fn:handle` with rendered before/after; `--apply` rewrites all sites in one batch with one revalidation, and `--fn <name>` scopes matching to one function. Patterns are expression-level only; unsupported subtree kinds are skipped and counted.

```sh
zero patch . --rewrite 'bnCmp($A, $B) == 0' --to 'bnEq($A, $B)'          # dry run
zero patch . --rewrite 'bnCmp($A, $B) == 0' --to 'bnEq($A, $B)' --apply  # rewrite every site
```

For multi-statement bodies, use `replaceFunctionBody` for a whole function or `replaceBlockBody` for one selected `Block` node. Body rows accept canonical projection syntax, the same text `zero view` prints:

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
replaceFunctionBody main
  let name: Maybe<String> = std.args.get(1)
  if name.has {
    check world.out.write("hello ")
    check world.out.write(name.value)
    check world.out.write("\n")
  } else {
    check world.out.write("hello anonymous\n")
  }
end
```

To replace one function body without patch syntax or shell quoting, use `--replace-fn` with `--body-file`. `--body-file -` reads the body rows from stdin, so a heredoc does the whole edit in one call:

```sh
zero patch --replace-fn main --body-file - <<'EOF'
  let name: Maybe<String> = std.args.get(1)
  if name.has {
    check world.out.write("hello ")
    check world.out.write(name.value)
    check world.out.write("\n")
  } else {
    check world.out.write("hello anonymous\n")
  }
EOF
```

The body holds only the new rows, exactly what `zero view --fn <name>` prints between the signature braces (no header, no `end`). Quotes, `$variables`, and backslashes pass through a quoted heredoc untouched. The alternative is a file path: `zero patch --replace-fn <name> --body-file /tmp/main.body`.

To change a few characters inside a large function, do not retype the body: `--replace-in-fn` replaces one unique literal occurrence of `--old` in the function's canonical body text with `--new` (Edit semantics), then revalidates exactly like `--replace-fn`:

```sh
zero patch --replace-in-fn handleLine --old 'limit + 1' --new 'limit + 2'
```

A missing or non-unique `--old` fails with the occurrence count; extend `--old` with surrounding lines from `zero view --fn <name>` until it matches once. Inline `--old`/`--new` accept `\n` escapes; `--old-file`/`--new-file <file|->` read multi-line text from a file or stdin.

To patch one branch instead of rewriting the whole function, find the block handle first:

```sh
zero query --find Block
```

```text
zero-program-graph-patch v1
replaceBlockBody #block_32cefdd9
  check world.out.write("updated\n")
end
```

Preview without writing, and list operation shapes without loading a graph:

```sh
zero patch --check-only /tmp/body.patch
zero patch --dry-run --json /tmp/body.patch
zero patch --op help
```

Supported graph patch operations (authoring ops first; node-handle ops are the advanced surface):

```text
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
expect graphHash "graph:a7f7e6899a73f3b4"
set node="#id" field="value" expect="old" value="new"
insert node="#id" kind="Literal" parent="#parent" edge="arg" order="0" type="String" value="text"
insertEdge from="#from" to="#to" edge="arg" target="node" order="0"
replace node="#id" expect="nodehash:abc123" kind="Literal" type="String" value="text"
replaceExpr node="#id" with="left + 1"
delete node="#id" expect="nodehash:abc123"
rename node="#id" expect="old" value="new"
```

`insert` and `insertEdge` default `order` to `0`, which fits singular edges like `expr`, `left`, and `declaredType`. For precise existing-node edits, pin the graph hash and node facts:

```sh
zero patch \
  --expect-graph-hash graph:a7f7e6899a73f3b4 \
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello agent\n"'
```

For larger edits, write a patch file under `/tmp` or pass `--patch-text`. Always include `expect graphHash` when a patch is carried across tool calls.

## Artifacts, Reconcile, And Diff

Create a derived artifact only to carry a graph between tools, and validate it before applying accepted changes back to a package store:

```sh
zero dump --out .zero/agent/app.program-graph
zero validate .zero/agent/app.program-graph
zero view .zero/agent/app.program-graph
```

Do not commit `.program-graph` files unless the user explicitly asks. `zero source-map` connects graph nodes to source ranges. When a human edited source after a graph was captured, reconcile before relying on old node IDs:

```sh
zero reconcile .zero/agent/app.before.program-graph --source <projection-or-package>
```

`zero reconcile` reports unchanged, edited, inserted, deleted, ambiguous, and identity-changed nodes; ambiguous matches fail instead of assigning stale handles.

For readable Git diffs of `.graph` files, mark them with `*.graph diff=zero-graph` in `.gitattributes` and set `git config diff.zero-graph.textconv 'zero diff'` (`bin/zero diff` inside a Zero checkout). `zero diff` prints canonical review text for textconv, and `zero diff --fn <name>` scopes it to one function; keep using `zero query`, `zero inspect`, and `zero patch` for graph work.

`zero merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <package>` combines independent node-hash edits and writes only the target store; run `export` separately if a human needs the refreshed projection. Build and run commands may also write a derived final-MIR cache under `.zero/cache/native/`; agents should not patch `.zmir` files.
