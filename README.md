# zerolang

zerolang is an experimental graph-first programming language where agents work with semantic program structure instead of raw source text.

For graph-first packages, `zero.graph` is the repository graph store and normal compiler input. `.0` files are human-readable projections: reviewable and bidirectional for humans, but not the normal agent authoring surface.

The current model:

- Human-readable `.0` projections stay reviewable, auditable, and durable.
- `zero.graph` is the checked repository graph store for opted-in packages.
- Agents inspect graph facts such as node IDs, graph hashes, types, effects, ownership facts, capabilities, helper use, and module edges.
- Agents submit checked graph edits instead of patching source text ranges.
- Humans can edit `.0` when needed, then import the reviewed projection back to the graph.

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

The ProgramGraph is zerolang's compiler-owned structure for that work. It is meant to give agents a map they can navigate in slices: start from a symbol, diagnostic, call, capability, module, or node ID, then ask for the surrounding semantic facts instead of loading unrelated source. That keeps context gathering focused while leaving `.0` projections as the durable artifact humans review.

The edit loop is also different. A graph edit can target `node #expr_653eeb6e`
instead of a line range, require the inspected `graphHash`, require an expected
field value, and let the compiler validate, lower, write the graph store,
export projections when needed, and check the result as one path. Refactors can
be expressed as semantic operations such as renaming a function node or
replacing a resolved callee, rather than search-and-replace over text followed
by separate cleanup commands.

## Source Text

`.0` source is intentionally regular. In graph-first packages it is a
human-readable projection that behaves like durable data: easy to index,
compare, format, audit, regenerate, and import back to the graph after a human
edit.

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

For graph-first packages, `zero.graph` is the stored compiler input. ProgramGraph artifacts are optional derived inspection and interchange data, not primary project files.

## ProgramGraph

Agents do not have to infer every fact from text. The compiler can expose the checked structure of a program directly:

```bash
zero dump examples/hello.graph
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

For graph-first packages, graph artifacts, and projections with graph sidecars,
`zero patch` applies checked edits to the graph and rewrites the target only
after validation. The command is intended to collapse the normal agent loop of
edit, validate, export projections when needed, check, and fix into a
compiler-mediated operation:

```bash
zero patch examples/hello.graph \
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
zero check --json examples/hello.graph
zero dump examples/hello.graph
zero inspect --json examples/systems-package
zero size --json examples/point.graph
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
| `zero inspect --json` | Modules, imports, public symbols, capabilities, effects, ownership facts, safety facts, helper use, and interface fingerprints. |
| `zero dump` | Deterministic ProgramGraph text with graph hashes, node IDs, nodes, and edges. |
| `zero patch` | Checked graph edits with graph-hash and field-value preconditions. |
| `zero fix --plan --json` | Typed repair plans that describe proposed fixes without editing files. |
| `zero size --json` | Retained helpers, size reasons, profile policy, safety facts, backend facts, and artifact budget data. |

### Repair With Diagnostics

A graph-backed failing fixture reports a diagnostic with stable fields:

```bash
zero check --json --target linux-musl-x64 conformance/common/fail/unsupported-target-feature.graph
```

Today that output includes fields like:

```json
{
  "code": "TAR002",
  "message": "target does not provide hosted filesystem capability",
  "expected": "target with capability fs",
  "actual": "linux-musl-x64",
  "repair": {
    "id": "choose-target-with-required-capability"
  }
}
```

Diagnostics can be explained, and invalid human projections can be imported for
repair metadata before the fixed graph is checked:

```bash
zero explain --json TYP009
pnpm run agent:demo
```

See `examples/agent-repair-demo/` for the broken projection, suggested edit,
fixed graph, and scripted import-explain-repair-check flow.

### Compatibility Policy

zerolang is experimental and intentionally unstable. The repo prefers one current syntax and one formatted style over compatibility layers:

```bash
zero fmt --check examples/hello.0
zero check --json examples/hello.graph
```

The project may make breaking changes to simplify the language, standard library, diagnostics, graph APIs, or inspection surfaces for agent use.

## Quick Start

Install the latest release:

```bash
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

Check a program:

```bash
zero check examples/hello.graph
```

Run a small executable:

```bash
zero run examples/add.graph
```

Expected output:

```text
math works
```

## Common Commands

```bash
zero check examples/hello.graph
zero run examples/add.graph
zero build --emit exe --target linux-musl-x64 examples/add.graph --out .zero/out/add
zero inspect --json examples/systems-package
zero size --json examples/point.graph
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

For local iteration, append runner options to the local commands:

```bash
pnpm run conformance:local -- --list
pnpm run conformance:local -- --shard 1/4
pnpm run command-contracts:local
```

`pnpm run conformance` runs in the sandbox with four isolated conformance check
workers. `conformance:local` stays serial by default; set
`ZERO_CONFORMANCE_CHECK_JOBS=<n>` only when measuring that path locally, because
compiler child-process contention can be slower than the serial default on some
machines.
The validation scripts prefer `.zero/bin/zero` after the native build phase to
avoid paying the `bin/zero` wrapper cost on every compiler invocation. Set
`ZERO_BIN=<path>` only when you intentionally want to compare another binary.

Benchmarks run locally by default:

```bash
pnpm run bench
```
