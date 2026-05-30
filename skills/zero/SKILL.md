---
name: zero
description: Install Zero and load version-matched workflows with zero skills.
---

# Zero

Zero is the programming language for agents.

Install this skill once in an agent's skill manager. Keep it thin; Zero's own CLI serves the version-matched workflow for each installed compiler.

Install the latest release:

```sh
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

## Version-Matched Skills

This file is a discovery stub. Do not treat it as the full Zero workflow.

Before editing, checking, testing, or repairing Zero code, ask the installed compiler for the skill content that matches that exact binary:

```sh
zero skills list
zero skills get zero
zero skills get zero --full
```

If the user has multiple Zero binaries, use the same binary that will run the project:

```sh
/path/to/zero skills list
/path/to/zero skills get zero --full
```

Use `zero skills list` to discover additional skills bundled with that Zero version. Use `zero skills get <name>` to load the one relevant to the task. Common inner skills include `agent`, `graph`, `language`, `diagnostics`, `packages`, `builds`, `testing`, and `stdlib`.

## Output Formats

Three output formats are available on commands that accept `--json`:

| Flag | Format | Use case |
| --- | --- | --- |
| _(none)_ | **TEXT** | Terminal output (default) |
| `--json` | **JSON** | CI, editors, external tools |
| `--zdn` / `--format zdn` | **ZDN** | AI agents and LLMs |

**ZDN (Zero Data Notation)** is the agent-first format. It reuses Zero's own
row syntax — every line is a self-contained fact, indentation replaces brackets,
and a ZDN document always starts with a typed record name. Use `--zdn` when an
AI agent will read the output; it preserves the same structured fields as JSON
in a more compact, scan-friendly layout.

Use `--json` when an external tool must parse stable fields, compare contracts,
or inspect deeper diagnostic and repair metadata. `zero fix --plan --json` is
intentionally machine-readable.

## Common Entry Points

```sh
# Check and analyze
zero check <file-or-package>
zero check --zdn <file-or-package>     # structured check for agents
zero graph <file-or-package>
zero graph view <file-or-package>
zero graph check <file-or-package>
zero graph dump --json <file-or-package>
zero explain <diagnostic-code>

# ProgramGraph workflow
zero graph dump --out .zero/agent/work.program-graph <file-or-package>
zero graph view .zero/agent/work.program-graph
zero graph check .zero/agent/work.program-graph

# Size and diagnostics
zero size <file-or-package>
zero size --zdn <file-or-package>      # structured size breakdown for agents
zero explain <diagnostic-code>

# Fix plan (machine-readable)
zero fix --plan --json <file-or-package>
zero fix --plan --zdn <file-or-package>  # ZDN variant for agents
```

All commands that accept `--json` also accept `--zdn`. See the [ZDN Format
Reference](/zdn) for the complete specification.

In a Zero repository checkout, prefer `bin/zero` when the task is about that checkout rather than the globally installed compiler.
