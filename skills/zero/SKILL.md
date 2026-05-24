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

Use `zero skills list` to discover additional skills bundled with that Zero version. Use `zero skills get <name>` to load the one relevant to the task. Common inner skills include `agent`, `language`, `diagnostics`, `packages`, `builds`, `testing`, and `stdlib`.

## Common Entry Points

```sh
zero check --json <file-or-package>
zero graph --json <file-or-package>
zero size --json <file-or-package>
zero explain <diagnostic-code>
zero fix --plan --json <file-or-package>
```

In a Zero repository checkout, prefer `bin/zero` when the task is about that checkout rather than the globally installed compiler.
