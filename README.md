# zerolang

zerolang is an experimental graph-first programming language where agents work with semantic program structure instead of raw source text.

Source code is still the source of truth. The graph is the compiler-derived interface agents use to inspect and change programs with less guessing.

The current model:

- Human-readable `.0` source stays reviewable, auditable, and durable.
- The compiler derives a checked ProgramGraph from source.
- Agents inspect graph facts such as node IDs, graph hashes, types, effects, ownership facts, capabilities, helper use, and module edges.
- Agents can submit checked graph edits instead of only patching text ranges.
- Where source rewriting is supported, the compiler validates the edit before writing source.

Design goals:

- Token efficiency
- Low memory usage
- Fast startup
- Fast builds
- Low runtime latency
- Zero dependencies

> **Safety status**
>
> Security vulnerabilities should be expected. zerolang is not ready for production systems, sensitive data, or trusted infrastructure. Run and develop it in isolated, disposable environments.

## Why Graph

Agents can edit source text, but source text is a lossy interface for program understanding. A text patch has to guess which references are related, whether a range is stale, whether a call resolves to the intended function, and whether an edit preserved ownership, fallibility, effects, imports, and target constraints.

The ProgramGraph is zerolang's compiler-owned structure for that work. It is meant to give agents a map they can navigate in slices: start from a symbol, diagnostic, call, capability, module, or node ID, then ask for the surrounding semantic facts instead of loading unrelated source. That keeps context gathering focused while leaving source code as the durable artifact humans review.

The edit loop is also different. A graph edit can target `node #expr_653eeb6e` instead of a line range, require the inspected `graphHash`, require an expected field value, and let the compiler validate, lower, write, format, reparse, and check the result as one path. Refactors can be expressed as semantic operations such as renaming a function node or replacing a resolved callee, rather than search-and-replace over text followed by separate cleanup commands.

## Source Text

`.0` source is intentionally regular. The goal is source that behaves like durable data: easy to index, compare, format, audit, and regenerate, while still reading like normal code.

A small program shows typed signatures, infix expressions, fallibility, and explicit capability passing:

```zero
fn answer() -> i32 {
    return 40 + 2
}

pub fn main(world: World) -> Void raises {
    if answer() == 42 {
        check world.out.write("math works\n")
    }
}
```

Source code remains the stored representation. ProgramGraph artifacts are derived inspection and interchange data, not the primary project files.

## ProgramGraph

Agents do not have to infer every fact from text. The compiler can expose the checked structure of a program directly:

```bash
zero graph dump examples/hello.0
```

Example output:

```text
zero-graph v1
origin source-text
module "hello"
hash "graph:a7f7e6899a73f3b4"

node #decl_ad8d9028 Function name:"main" type:"Void" public:true fallible:true
node #param_4610ae76 Param name:"world" type:"World"
node #expr_c403020c MethodCall name:"write" type:"Void"
node #expr_653eeb6e Literal type:"String" value:"hello from zero\n"
edge #expr_c403020c arg #expr_653eeb6e order:0
edge #decl_ad8d9028 body #block_29d1811d
```

The graph gives agents explicit handles such as node IDs, graph hashes, resolved types, effects, ownership facts, capability facts, helper use, and module edges. The hash is a stale-context check; node IDs are edit targets for the graph that was inspected.

## Checked Graph Edits

For supported canonical `.0` source, `zero graph patch` applies checked edits to the graph and rewrites source only after validation. The command is intended to collapse the normal agent loop of edit, format, reparse, check, and fix into a compiler-mediated operation:

```bash
zero graph patch examples/hello.0 \
  --expect-graph-hash graph:a7f7e6899a73f3b4 \
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello graph\n"'
```

This is different from a source-text patch. The operation targets a checked semantic node and field. The graph hash rejects stale context, and `expect` rejects the edit if the current field value is different from what the agent inspected.

## Agent Workflow Interfaces

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
zero graph dump examples/hello.0
zero graph --json examples/systems-package
zero size --json examples/point.0
```

The structured contracts include diagnostic codes and spans, public symbols, import edges, target readiness, compile-time sandbox facts, retained helpers, graph hashes, node IDs, and size retention reasons.

### Compiler-Native Contracts

Most language ecosystems expose some of these facts through separate tools, editor protocols, or library APIs. zerolang keeps the agent-facing inspection and repair path in the compiler CLI.

The inspection and repair surfaces are compiler commands, not editor-only features or a separate analysis service:

| Command | Contract |
| --- | --- |
| `zero skills get language` | Version-matched language rules bundled with the compiler binary. |
| `zero check --json` | Diagnostics with code, span, expected/actual fields, fix safety, repair metadata, compile-time sandbox facts, target readiness, and safety facts. |
| `zero parse --json` | A stable parse summary with declarations, function signatures, and body node kinds. |
| `zero graph --json` | Modules, imports, public symbols, capabilities, effects, ownership facts, safety facts, helper use, and interface fingerprints. |
| `zero graph dump` | Deterministic ProgramGraph text with graph hashes, node IDs, nodes, and edges. |
| `zero graph patch` | Checked graph edits with graph-hash and field-value preconditions. |
| `zero fix --plan --json` | Typed repair plans that describe proposed fixes without editing files. |
| `zero size --json` | Retained helpers, size reasons, profile policy, safety facts, backend facts, and artifact budget data. |

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

zerolang is experimental and intentionally unstable. The repo prefers one current syntax and one formatted style over compatibility layers:

```bash
zero fmt --check examples/hello.0
zero check --json examples/hello.0
```

The project may make breaking changes to simplify the language, standard library, diagnostics, graph APIs, or inspection surfaces for agent use.

## Quick Start

### Homebrew (macOS/Linux)

```bash
brew install zerolang
```

### Install Script

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
