---
name: zero-agent
description: Agent workflow for making focused Zero changes with CLI feedback.
---

# Zero Agent Workflow

Use this when editing Zero code, examples, tests, docs, or a package. Zero is designed for explicit, machine-readable feedback; prefer the CLI JSON surfaces over guessing from prose.

## Start

Use the same compiler binary that will run the project:

```sh
zero --version
zero skills list
zero skills get zero-language
zero skills get zero-diagnostics
```

Inside the Zero repository checkout, prefer `bin/zero` over a global `zero`. For installed user projects, use the `zero` on `PATH` unless the user points at another binary.

## Edit Loop

1. Read the nearest `.0`, `zero.json`, tests, and examples before editing.
2. Make the smallest source change that satisfies the request.
3. Run a focused JSON check:

```sh
zero check --json <file-or-package>
```

4. If `ok` is true, inspect `runSupport` before running or promising an
   executable:

```sh
zero check --json <file-or-package> | jq '.runSupport'
```

Use `runSupport.runnableOnHost` for `zero run` decisions. Treat
`runSupport.supportLevel: "check-only"` as checker coverage only, then inspect
`targetReadiness.diagnostics` for backend blockers such as `CGEN004`.

5. When the compiler reports a diagnostic, inspect structured fields first:

```sh
zero explain <diagnostic-code>
zero fix --plan --json <file-or-package>
```

6. If behavior changes, add or update a `test` block or conformance fixture.
7. Validate with the narrowest command that covers the changed surface.

## Agent Rules

- Treat effects as capabilities, not ambient globals. Use `World`, `std.fs`, `std.args`, `std.env`, and similar APIs only where the target supports them.
- Keep examples copyable and runnable from the repository or package root.
- Prefer explicit types at public boundaries and when inference is unclear.
- Use `Maybe<T>`, explicit `raises`, and `check` instead of hidden failure.
- Do not invent syntax. Load `zero-language` when unsure.
- Do not invent CLI fields. Run the command with `--json` and read the data.

## Useful Focused Commands

```sh
zero check --json <input>
zero graph --json <input>
zero test --json <input>
zero size --json <input>
zero doctor --json
```

For CLI behavior or JSON contracts in the Zero repo, use the repository scripts listed by `AGENTS.md` or the project documentation.
