---
name: zero
description: Install Zero and load version-matched workflows with zero skills.
---

# Zero

Zero is an agent-first programming language where the graph is the program
database and `.0` files are human-readable projections.

Install this skill once in an agent's skill manager. Keep it thin; Zero's own CLI serves the version-matched workflow for each installed compiler.

Use the zero already on PATH or at `~/.zero/bin/zero` when one exists; environments often pin a specific compiler, and replacing it silently breaks version-matched stores and workflows. Install only when zero is missing:

```sh
command -v zero >/dev/null 2>&1 || { curl -fsSL https://zerolang.ai/install.sh | bash; }
export PATH="$PATH:$HOME/.zero/bin"
zero --version
```

## Version-Matched Skills

This file is only a discovery stub. Do not treat it as the full Zero workflow
or as command reference.

Before editing, checking, testing, or repairing Zero code, ask the installed compiler for the skill content that matches that exact binary:

```sh
zero skills
zero skills get zero
zero skills get zero --full
zero skills get stdlib --topic std.time   # one section instead of the whole topic
```

If the user has multiple Zero binaries, use the same binary that will run the project:

```sh
/path/to/zero skills
/path/to/zero skills get zero --full
```

Use `zero skills get <name>` to load only what the task needs, and fetch each
topic at most once per session: the content is fixed for a given compiler
binary, so refetching a loaded topic returns the same text. Topics and
approximate served sizes:

- `zero` (~2 KB): this discovery stub
- `agent` (~4 KB): read-edit-verify loop, zero query usage, edit surfaces, verification
- `language` (~6 KB): syntax, types, effects, control flow, generics
- `graph` (~8 KB): zero.graph store, query/view, patch operations, import/export/merge
- `diagnostics` (~4 KB): reading diagnostics, zero explain, typed fix plans
- `packages` (~5 KB): manifests, package layout, creation and repair
- `builds` (~5 KB): build/run, targets, profiles, emitted artifacts
- `testing` (~3 KB): test blocks, filters, runtime checks
- `stdlib` (~39 KB): full signature reference, including ready-made validators: `std.time` (RFC 3339 incl. the exact leap-second rule), `std.inet` (IPv4/IPv6/hostname), `std.regex` (ECMA subset), `std.unicode` (strict UTF-8). Check here before hand-writing any parsing or validation logic. Fetch one module's section (~1 KB) with `zero skills get stdlib --topic <prefix>`, e.g. `--topic std.time`.

Agents author through graph patches or direct `.0` source edits; package commands refresh `zero.graph` from edited source automatically. Read one function with `zero view --fn <name>` instead of whole files. Prefer concise text output during interactive agent work; use `--json` only for automation, exact spans, contracts, or machine-readable diagnostics.

## Common Entry Points

```sh
zero query [graph-or-package]
zero patch [graph-or-package] --op '<operation>'
zero check [graph-or-package]
zero test [graph-or-package]
zero run [graph-or-package] -- <args>
zero diff [graph-or-package]
zero explain <diagnostic-code>
zero fix --plan [graph-or-package]
```

In a Zero repository checkout, prefer `bin/zero` when the task is about that checkout rather than the globally installed compiler.
