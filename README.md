# zerolang

Experimental programming language for agent workflows.

Built for reliable autonomous software generation.

Optimized for:

- Token efficiency
- Low memory usage
- Fast startup
- Fast builds
- Low runtime latency
- Zero dependencies

> **Safety status**
>
> Security vulnerabilities should be expected. zerolang is not ready for production systems, sensitive data, or trusted infrastructure. Run and develop it in isolated, disposable environments.

## Agent Workflow Interfaces

A small program shows function definitions, return rows, prefix calls, fallibility, and indentation:

```zero
fn answer i32
  ret + 40 2

pub fn main Void world World !
  if == answer() 42
    check world.out.write "math works\n"
```

The compiler exposes the workflow through CLI commands with stable structured output.

### Load Version-Matched Rules

The compiler ships skill text that matches the binary being used:

```bash
zero skills list
zero skills get language
zero skills get diagnostics
zero skills get stdlib
```

Print the language guide bundled with the compiler:

```bash
zero skills get language
```

### Inspect Compiler Facts

Compiler state is exposed through structured command output instead of prose-only output. The important contract is the stable fields and repair identifiers; today the CLI exposes those fields with `--json`:

```bash
zero tokens --json examples/hello.0
zero parse --json examples/hello.0
zero check --json examples/hello.0
zero graph --json examples/systems-package
zero size --json examples/point.0
```

The JSON contracts include diagnostic codes and spans, public symbols, import edges, target readiness, compile-time sandbox facts, retained helpers, and size retention reasons.

### Compiler-Native Contracts

Most language ecosystems expose some of these facts through separate tools, editor protocols, or library APIs. zerolang keeps the agent-facing inspection and repair path in the compiler CLI.

The inspection and repair surfaces are compiler commands, not editor-only features or a separate analysis service:

| Command | Contract |
| --- | --- |
| `zero skills get language` | Version-matched language rules bundled with the compiler binary. |
| `zero check --json` | Diagnostics with code, span, expected/actual fields, fix safety, repair metadata, compile-time sandbox facts, and target readiness. |
| `zero parse --json` | A stable parse summary with declarations, function signatures, and body node kinds. |
| `zero graph --json` | Modules, imports, public symbols, capabilities, effects, ownership facts, helper use, and interface fingerprints. |
| `zero fix --plan --json` | Typed repair plans that describe proposed fixes without editing files. |
| `zero size --json` | Retained helpers, size reasons, profile policy, backend facts, and artifact budget data. |

### Repair With Diagnostics

A failing fixture reports a diagnostic with stable fields:

```bash
zero check --json conformance/check/fail/unknown-name.0
```

Today that output includes fields like:

```json
{
  "code": "NAM003",
  "message": "unknown identifier 'message'",
  "expected": "visible local, parameter, function, or builtin",
  "actual": "no matching visible symbol",
  "repair": {
    "id": "declare-missing-symbol"
  }
}
```

Diagnostics can be explained and turned into typed fix plans:

```bash
zero explain --json TYP009
zero fix --plan --json examples/agent-repair-demo/broken.0
```

Run the repair demo:

```bash
pnpm run agent:demo
```

See `examples/agent-repair-demo/` for the broken fixture, suggested edit, fixed fixture, and scripted check-explain-plan-rerun flow.

### Compatibility Policy

zerolang is intentionally unstable before 1.0. The repo prefers one current syntax and one formatted style over compatibility layers:

```bash
zero fmt --check examples/hello.0
zero check --json examples/hello.0
```

Before 1.0, the project may make breaking changes to simplify the language, standard library, diagnostics, or inspection APIs for agent use.

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
