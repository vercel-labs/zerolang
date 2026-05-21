# zerolang

zerolang is an experiment in building an agent-first programming language.

The project is exploring what changes when agents are primary users from day one: a language that can be learned on the fly, tooling that exposes structured facts for debugging and repair, and a standard library broad enough that most programs do not start with a dependency search.

zerolang is pre-1 and intentionally unstable. The project will make breaking changes while it searches for the language, library, and tooling patterns that work best for agents. Treat today's syntax and APIs as something to explore, not something to memorize. If that sounds useful, try it with us: run examples, inspect the structured output, and send feedback about what helps agents work better.

Security vulnerabilities should be expected. zerolang is not ready for production systems, sensitive data, or trusted infrastructure. If you plan to run or develop zerolang, do so in an isolated, disposable environment.

## What zerolang Is Aiming For

- Agent-first learnability: a small, regular language surface that agents can pick up quickly from examples, docs, and compiler feedback.
- Standard-library depth: common capabilities should live in documented, coherent library APIs instead of scattered dependency stacks.
- Deterministic tooling: diagnostics, graph facts, size reports, explanations, and fix plans should be structured enough for agents to inspect and act on.
- Direct developer experience: checking, running, formatting, inspecting, and repairing code should be fast, copyable, and scriptable.
- Regularity over syntax: prefer one obvious way to express most things, even when that makes code more explicit than a human might choose in another language.

## Quick Start

Install the latest release:

```bash
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

Check a program:

```bash
zero check examples/hello.0
```

Run a small executable:

```bash
zero run examples/add.0
```

Expected output:

```text
math works
```

## Common Commands

```bash
zero check examples/hello.0
zero run examples/add.0
zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add
zero graph --json examples/systems-package
zero size --json examples/point.0
zero skills get zero --full
zero doctor --json
```

## Validation

```bash
pnpm run docs:test
pnpm run conformance
pnpm run native:test
pnpm run command-contracts
```

Benchmarks run locally by default:

```bash
pnpm run bench
```
