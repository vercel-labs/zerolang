## Use Commands By Workflow

The Zerolang CLI is organized around graph-first agent work. Humans ask for a task;
agents inspect and patch the graph; projections are exported only for review or
manual edits.

Most commands default to the current directory:

```sh
zero status
zero query
zero patch --op help
zero check
zero run -- <args>
```

Pass an explicit graph input or package when you are outside the project:

```sh
zero check examples/hello.graph
zero query examples/crm-api
zero run examples/json-api-router.graph -- $'GET /health\n\n'
```

## Create A Project

Use `zero init` for all project creation.

```sh
zero init
zero init --template cli crm-tool
zero init --manifest toml --format binary --template package api-server
```

If no path is given, `zero init` creates the package in the current directory.
For `.` or an omitted path, the package name comes from the directory name.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "start a cli here"
    },
    {
      "role": "assistant",
      "text": "I’ll initialize this directory and add the starting CLI shape."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero init --template cli",
          "output": "graph project init ok\nwrote: ./zero.toml\nwrote: ./zero.graph"
        },
        {
          "command": "zero patch --op 'addMain'",
          "output": "program graph patch ok"
        }
      ]
    }
  ]
}
```

## Inspect Before Editing

Agents should query for the exact thing they need instead of dumping the whole
program.

```sh
zero status
zero query --fn main
zero query --find customer
zero query --refs handle
zero query --calls write
zero inspect --json
zero size --json
zero mem --json
```

Use plain text first. Use `--json` when a tool needs exact fields such as node
ids, graph hashes, `interfaceFingerprints`, `targetToolchains`,
`usedStdlibHelpers`, `memoryBudgets`, or `releaseTargetContract`.

## Patch The Graph

Patch commands are checked graph edits:

```sh
zero patch --op 'addFunction name="add" ret="i32"'
zero patch --op 'addParam fn="add" name="x" type="i32"'
zero patch --op 'addParam fn="add" name="y" type="i32"'
zero patch --op 'addReturnBinary fn="add" name="+" left="x" right="y" type="i32"'
```

For larger edits, use a patch file under `/tmp`:

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
replaceFunctionBody main
  check world.out.write "hello\n"
end
```

Dry-run a repository graph patch without writing:

```sh
zero patch --check-only /tmp/main.patch
zero patch --dry-run --json /tmp/main.patch
```

Apply it:

```sh
zero patch /tmp/main.patch
```

To replace one function body without patch syntax, pass only the new body rows
(exactly what `zero view --fn <name>` prints between the signature braces).
`--body-file -` reads them from stdin, so a heredoc does the whole edit in one
call:

```sh
zero patch --replace-fn main --body-file - <<'EOF'
  check world.out.write("hello agent\n")
EOF
```

A file path works as the alternative:

```sh
zero patch --replace-fn main --body-file /tmp/main.body
```

To change a few characters inside a function without retyping the body,
`--replace-in-fn` replaces one unique literal occurrence of `--old` in the
function's canonical body text (what `zero view --fn <name>` prints) with
`--new`, then revalidates exactly like `--replace-fn`:

```sh
zero patch --replace-in-fn main --old 'limit + 1' --new 'limit + 2'
```

A missing or non-unique `--old` fails with the occurrence count. Inline
`--old`/`--new` accept `\n` escapes for multi-line text; `--old-file` and
`--new-file <file|->` read the text from a file or stdin.

The patch step validates graph shape and repository metadata. A stale graph
hash, missing required edge, sparse ordered child group, or invalid row body
fails before the package store is updated.

## Validate Only What You Need

Do not run every command after every patch. `zero patch` already reports whether
the edit applied. Run the next command that proves the user-visible behavior.

```sh
zero check
zero test
zero test --json --filter add
zero run -- add 40 2
```

Use `zero check --json` when an editor, CI job, or agent needs stable
diagnostic fields. Test JSON includes `expectedFailures`, `fixtures`,
`snapshotKey`, and per-test results.

## Run And Build

Use `zero run` for local behavior:

```sh
zero run -- help
zero run examples/hello.graph
```

Use `zero build` for artifacts:

```sh
zero build --emit exe --target linux-musl-x64 --out .zero/out/app
zero build --emit obj --target darwin-arm64 examples/direct-call-add.graph --out .zero/out/add.o
zero build --emit llvm-ir examples/hello.graph --out .zero/out/hello.ll
```

Build JSON reports profile and target readiness:

```sh
zero build --json --profile tiny --target linux-musl-x64 examples/hello.graph --out .zero/out/hello
```

Important fields include `profileSemantics`, `profileBudget`,
`releaseTargetContract`, `targetToolchains`, `compileTime`, and repeat-build
hash policy data for artifact determinism.

## Review Projections

Projection commands are for humans:

```sh
zero export
zero verify-projection
zero import
zero diff
zero view
```

Use `zero export` when a human wants the current `.0` projection. Use
`zero import` after a human intentionally edits projection text. Use
`zero verify-projection` to catch drift without writing.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "show me the projection before we keep going"
    },
    {
      "role": "assistant",
      "text": "I’ll export the current projection and verify it matches the graph."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero export",
          "output": "repository graph export ok\nwrote: ./src/main.0"
        },
        {
          "command": "zero verify-projection",
          "output": "repository graph verify-projection ok"
        }
      ]
    }
  ]
}
```

## Diagnose And Repair

```sh
zero explain NAM003
zero fix --plan --json
zero doctor
zero dev --json
zero dev --json --trace
```

`zero dev --json` is the editor-facing snapshot. It includes diagnostics,
document symbols, hover data, completions, definition targets, and
`interfaceFingerprints`.

## Command Groups

| Workflow | Commands |
| --- | --- |
| create | `init` |
| inspect | `status`, `query`, `inspect`, `size`, `mem`, `doc`, `source-map` |
| edit graph | `patch`, `reconcile`, `merge` |
| validate | `check`, `test`, `verify-projection`, `validate`, `roundtrip` |
| run/build | `run`, `build`, `targets`, `abi` |
| projection review | `export`, `import`, `view`, `diff`, `fmt`, `tokens`, `parse` |
| support | `skills`, `explain`, `fix`, `doctor`, `clean`, `dev`, `time` |

## Input Forms

| Input | Meaning |
| --- | --- |
| `project/` | A package directory. Normal package commands compile from `zero.graph`. |
| `zero.toml` | Preferred package manifest. Takes precedence over `zero.json` for directory inputs. |
| `zero.json` | Compatibility manifest. Prefer `zero.toml` for new packages. |
| `file.graph` | Binary or text graph store/artifact. |
| `file.0` | Human-readable projection for formatting, import/export, and review workflows. It is not the normal compiler input. |

## JSON Rule

Humans and interactive agents should start with concise text output. Use JSON
when a program needs exact structured data:

```sh
zero check --json
zero test --json
zero inspect --json
zero size --json
zero doctor --json
```

JSON is a contract for tools, not the default reading experience for humans.
