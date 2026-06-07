# zerolang

zerolang is an experimental graph-first programming language where agents work with semantic program structure instead of raw source text.

[中文说明](README.zh-CN.md)

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
  --expect-graph-hash graph:b8a019041020df03 \
  --op 'set node="#610c78bf" field="value" expect="hello from zero\n" value="hello graph\n"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'renameSymbol node="#48a1ed3f" expect="helper" value="compute"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'renameParam node="#5c6d1a27" expect="value" value="input"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'renameLocal node="#23d497a1" expect="count" value="totalCount"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'renameField node="#3b9f241e" expect="x" value="left"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'replaceCallee node="#77e12235" expect="oldHelper" value="newHelper"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'addImport name="std.mem"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'addFunction name="answer" type="i32" value="42"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'addFunction name="answerWithInput" type="i32" params="arg0:i32" value="42"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'addParam node="#48a1ed3f" expect="helper" name="bias" type="i32" value="0"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'removeParam node="#5c6d1a27" expect="unused"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'removeFunction node="#8f0c2b91" expect="unusedHelper"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'removeImport name="std.mem" expect="std.mem"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'replaceImport name="std.mem" expect="std.mem" value="std.ascii"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'renameImportAlias name="std.mem" expect="memory" value="buffer"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'changeReturnType node="#48a1ed3f" expect="i32" value="i64"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'changeParamType node="#4c21b7e2" expect="i32" value="i64"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'changeLocalType node="#23d497a1" expect="i32" value="i64"'
zero graph patch examples/app.0 \
  --expect-graph-hash graph:f76987e99677f1b3 \
  --op 'changeFieldType node="#3b9f241e" expect="i32" value="i64"'
```

This is different from a source-text patch. The operation targets a checked semantic node and field. The graph hash rejects stale context, and `expect` rejects the edit if the current field value is different from what the agent inspected.
With `--json`, the graph patch transaction includes a `patchContract` so agents
can interpret `operations[]`, required inputs, preconditions, and result fields
without scraping terminal text. Successful saves also expose `saved.graphHash`
and `saveProof`, so an agent can audit that the saved source or artifact is the
validated patched graph rather than an inferred filesystem side effect. The
transaction also includes a `proofLedger` with ordered inspect, patch, validate,
save, verify, and rollback phases, each naming the fields and proof purposes an
agent must audit.

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
| `zero agent protocol --json` | Machine-readable Agent programming protocol manifest with a read-only `agentCommand.readPolicy`, required `protocol-read` replay, entrypoints, ProgramGraph query contract, checked edit surface, proof purposes, `proofResultFields` and `proofReceiptPolicy` for purpose-indexed proof receipts, rollback policy, risk model, and local gate command. |
| `zero --version --json` | Compiler provenance and target readiness with a read-only `agentCommand.readPolicy` and required `version-read` replay command. |
| `zero skills list|get --json` | Version-matched compiler-bundled guidance with a read-only `agentCommand.readPolicy` and required `skills-read` replay command. |
| `zero check --json` | Diagnostics with code, span, expected/actual fields, fix safety, repair metadata, read-only diagnostic policy, command-level repair/graph-inspect follow-ups, compile-time sandbox facts, target readiness, and safety facts. |
| `zero parse --json` | A stable parse summary with an `agentCommand` replay contract, declarations, function signatures, and body node kinds. |
| `zero graph --json` | Modules, imports, public symbols, capabilities, effects, ownership facts, safety facts, helper use, and interface fingerprints. |
| `zero graph find --json --symbol <symbol-or-name> <input>` | Token-bounded symbol-to-node lookup with read-only `semantic-graph-query-read` policy, graph hash, node hashes, replayable proof command, structured follow-up slice command, optional ambiguous-candidate slice follow-up, and not-found graph-inspect recovery. |
| `zero graph impact --json --node <node-id> <input>` | Token-bounded edit impact facts with read-only `semantic-graph-query-read` policy, call sites, name references, pre-patch edit guards, structured graph-patch transaction follow-up, and stale-node graph-inspect recovery. |
| `zero graph slice --json --node <node-id> <input>` | Token-bounded one-hop ProgramGraph fact pack with read-only `semantic-graph-query-read` policy for a symbol, diagnostic target, or edit target, plus structured graph-impact/graph-patch follow-ups and stale-node graph-inspect recovery. |
| `zero graph dump` | Deterministic ProgramGraph text with graph hashes, node IDs, nodes, and edges. |
| `zero graph patch` | Checked graph edits with graph-hash and field-value preconditions. |
| `zero fix --plan/--patch/--apply --json` | Typed repair plans, reviewable compiler patches, and checked behavior-preserving repairs. |
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
zero fix --patch --json examples/agent-repair-demo/broken.0
zero fix --apply --json examples/agent-repair-demo/broken.0
```

The JSON includes an `agentTransaction` for the whole repair attempt and a
`repairContract` on each `fixes[]` item so agents can tell whether a repair is
compiler-patchable, which diagnostic fields are required, and how to verify the
result.
Failed `zero check --json` also reports command-level `repair-plan` and
`graph-inspect` follow-ups; failed `zero graph check --json` reports a
graph-specific `graph-inspect` follow-up for the same target/profile context.
`zero explain --json <code>` also participates in the Agent proof ledger:
diagnostic explanations expose stable repair ids, recommended next commands,
examples, and a replayable `explain` verification command.

Run the repair demo:

```bash
pnpm run agent:demo
```

Run the focused Agent protocol smoke suite:

```bash
pnpm run agent:manifest
pnpm run agent:protocol
```

Run the local Agent protocol eval baseline:

```bash
pnpm run agent:eval
```

Run the full local Agent protocol contract gate:

```bash
pnpm run agent:contracts
```

The gate uses `ZERO_BIN` when set, otherwise it looks for the local native
compiler at `.zero/bin/zero`. Build it first with `make -C native/zero-c` when
working from a fresh checkout.
Agents can run `node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/agent-contracts-gate.mts --summary-only`
to suppress child command logs and read only the final structured gate report.
For a goal-level completion report, run `node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/agent-completion-audit.mts`;
it reads the compiler-owned `objectiveContract`, replays the local gate, and
reports each requirement with the gate summary fields that prove it.

The eval reports diagnostic repair entrypoint coverage, checked graph repair
loop success, repair-plan semantic candidate coverage, proof-command coverage,
rollback proof coverage, whether graph lookup avoids requiring full source
reads, and token-saving ratios for a node-neighborhood fact pack versus source
and full inspect JSON. It also audits source-understanding replay through
`zero tokens`, `zero parse`, and `zero fmt --check`, so lexical spans,
declaration summaries, and canonical formatting are part of the same proof
ledger; format-check failures expose structured `format-preview` follow-up
commands instead of asking Agents to infer the formatter retry. `zero graph inspect --json`, `zero graph dump --json`, and
`zero graph compare --json --against <right> <left>` replay `graph-inspect`,
`graph-dump`, and `graph-compare` directly, so the primary ProgramGraph query,
snapshot, and semantic comparison surfaces are passed proofs, not only retry
commands from repair failures. `zero graph compare --json` also declares
`graph-view` follow-ups for both stable and unstable comparisons, so agents can
inspect either side from compiler-reported argv instead of inventing a view
command after a failed refactor proof. A default build emits a verifiable artifact with replayable
source-check, size-analysis, and artifact-validate proof commands plus stable
`artifactHash`/`artifactBytes` identity fields for artifact audits; `zero graph
dump --json --out`, `zero graph import --json --out`, `zero graph view --json`,
and `zero graph roundtrip --json` prove the source-to-ProgramGraph artifact lifecycle through
`graph-import`, `artifact-validate`, `graph-view`, and `graph-roundtrip`
commands with compiler-declared `agentCommand.writePolicy` rollback paths.
Buildability failures from `zero build --json` now include a command-level
`agent-build-failure-command-contract`, so Agents can run the required
`source-check` recovery and optional `target-selection`, `doctor`, or
`graph-inspect` follow-ups from declared argv arrays instead of inferring them
from diagnostics. ProgramGraph build failures use the graph-native
`agent-graph-build-failure-command-contract`, whose required recovery command is
`graph-check`.
Plain `zero graph dump --json` remains `preview-only`; with `--out` it reports
`saved.path` for artifact cleanup and verification replay. Both forms now
declare an optional `graph-view` follow-up in
`agentCommand.recommendedNextCommands[]`, so agents can move from a full graph
dump or saved ProgramGraph path to a token-bounded `zero graph view --json`
inspection without inventing argv or result fields. `zero graph size
--json --out` and `zero graph build --json` apply the same artifact replay to
ProgramGraph size/build artifacts, and `zero ship
--json` / `zero graph ship --json` apply the pattern to release checksums,
artifact bytes, and emitted artifact lists. ProgramGraph test replay also
covers both passing tests and expected-failure regressions through `graph-check`
/ `graph-test` proof commands, with structured failure/location fields and
ProgramGraph graph-hash binding. Missing `.program-graph` artifacts report a
`missing-graph-artifact` failure with a `graph-artifact-create` retry command to
recreate the artifact via `zero graph dump --json --out`. If an Agent passes a
ProgramGraph artifact to source-level `build`, `size`, `ship`, or `test`, the
compiler reports `graph-artifact-command-mismatch` and returns a
`use-graph-artifact-command` retry argv in the `zero graph <command>` namespace,
preserving target/profile/output choices. Graph subcommands that do not support
`--out` return command-level JSON failure contracts with a
`correct-command-usage` retry argv that removes unsupported output flags while
preserving target/profile context. Target/backend selection is also audited through
the `zero targets --json` `target-selection` proof command, including structured
capability, direct backend, toolchain, libc, and host-target facts. Its
compiler-declared `doctor` follow-up links target selection to host/toolchain
readiness without requiring Agents to infer the next command. Unknown target
failures (`TAR001`) require the same `zero targets --json` retry before exposing
a `<supported-target>` command template, so Agents do not replay stale target
names; repair modes and graph patch inputs are preserved in that retry argv so
Agents do not accidentally change a write transaction into a different command.
`zero new --json`
failure reports also carry structured retry commands for unknown templates and
existing project paths, so project scaffolding failures can be retried from argv
arrays instead of prose. Cleanup is audited
too: `zero clean --json` reports a `destructive-clean` write policy, allowed
`removedRoots[]`, removed path evidence, and an explicit external-restore
rollback boundary; clean failures include structured retry commands for
re-running the clean audit after external filesystem locks or permissions are
resolved. Project creation is audited as well: `zero new --json`
reports a `writes-source-tree` policy, `project.path` as the created-root and
rollback boundary, `project.files[]` evidence, and required `source-check` /
`test-run` verification commands. Runtime/build audit
commands participate too: `zero abi check --json`, `zero mem --json`, and
`zero time --json` replay `abi-audit`, `memory-audit`, `time-audit`, and
`source-check` proofs so Agent build decisions can cite target ABI, memory
layout, MIR validity, safety facts, production readiness risks, package cache
keys, compiler cache keys, ProgramGraph cache binding, and incremental state.
Documentation and development planning are covered as first-class proofs:
`zero doc --json` and `zero dev --json` replay `doc-audit`, `dev-plan`, and
`source-check` commands for public API publication gates, watch/restart plans,
affected modules, and stable partial diagnostics.
Host and target readiness is audited through `zero doctor --json` as a
`doctor` proof, with structured check and target-toolchain readiness fields, so
Agents can cite environment/toolchain state before trusting build, test, or
release results. Doctor also declares an optional `target-selection` follow-up
to `zero targets --json`, closing the loop between readiness and target choice.
The full gate hard-fails if
required proof purposes, diagnostic graph lookup command coverage,
proof result field catalog coverage,
proof receipt policy coverage,
compiler-declared objective contract coverage,
proof receipt replay coverage across protocol, source-check, graph-inspect, and source/ProgramGraph build, test, and release ship commands,
Agent authoring replay from token-bounded graph find/impact/slice through checked graph patch, rollback availability, source rewrite, test verification, and artifact build verification,
diagnostic-driven auto-repair replay from compiler repair plan through graph patch candidate, rollback availability, source rewrite, source-check, and artifact build verification,
real rollback replay from checked graph patch through source restoration, rollback source-check, rollback graph-check, and semantic identity restoration,
objective-level audit evidence that maps the Agent-native build-system goal to machine-checked source, graph, edit, repair, refactor, build, audit, rollback, token-efficiency, hallucination-resistance, and protocol-surface requirements,
the Agent-native advantage score and its reliable-edit, token-efficiency,
hallucination-resistance, auto-repair, and verifiable-build dimensions,
diagnostic repair entrypoint rollback action fields,
checked graph edit entrypoint audit fields, rollback proof coverage, graph patch
and repair transaction failure matrices, graph patch invalid-input retry
structure, graph patch proof replay, graph patch proof-ledger structure, graph patch rollback action auditability,
repair transaction proof-ledger structure,
repair rollback action auditability, repair loop proof-ledger audit fields,
repair-plan candidate operation/contract/evidence coverage,
semantic edit operation/category coverage, checked preconditions, source-backed
checked rewrites, conflict rejections, token/parse/format structured
source-understanding fields, token-parse follow-up structure, semantic graph query replayability,
target/profile preservation, query read-policy auditability, symbol lookup
identity/disambiguation binding,
graph-find follow-up slice command structure,
graph-find ambiguous candidate slice follow-up structure,
graph-find not-found graph-inspect recovery structure,
graph-impact graph-patch follow-up transaction structure,
graph-slice graph-impact pre-patch follow-up structure,
graph-slice graph-patch follow-up transaction structure,
graph-compare graph-view follow-up structure,
command failure retry-command structure,
graph-dump graph-view follow-up structure,
graph-artifact write-policy auditability,
graph-import artifact view/roundtrip follow-up structure,
graph-roundtrip artifact view follow-up structure,
graph-validate artifact view follow-up structure,
graph-view graph-check follow-up structure,
graph-check graph-size follow-up structure,
graph-inspect graph-slice follow-up structure,
build test-run follow-up structure,
graph-build graph-size follow-up structure,
graph-build graph-test follow-up structure,
size build follow-up structure,
graph-ship graph-size follow-up structure,
graph-ship graph-test follow-up structure,
source-ship size-analysis follow-up structure,
source-ship test-run follow-up structure,
test-failure graph-inspect follow-up structure,
test no-tests doc-audit follow-up structure,
host/target readiness structure and replayability,
target-selection doctor follow-up structure,
build/test/size/ship command replayability, artifact write-policy and rollback-action auditability,
artifact/test evidence matching, test failure evidence structure, ProgramGraph
test evidence binding, build/size/ship safety fact structure,
production readiness risk auditability, package/cache/incremental audit
structure, ProgramGraph cache binding, structured Agent permission boundaries,
explicit JSON replay for target selection,
explain/dev/doc/runtime guidance structure,
explain repair-plan result contract auditability,
auxiliary command replayability, full-source graph lookup avoidance,
source/full-inspect token-saving bounds, or compiler metrics budgets regress.

See `examples/agent-repair-demo/` for the broken fixture, compiler-mediated
repair transactions, checked ProgramGraph edit, and scripted verification flow.

### Compatibility Policy

zerolang is experimental and intentionally unstable. The repo prefers one current syntax and one formatted style over compatibility layers:

```bash
zero fmt --check examples/hello.0
zero check --json examples/hello.0
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

`zero run` keeps stdout and stderr for the program. For machine-readable audit
before running, use `zero build --json`; if `zero run --json` is attempted, the
JSON diagnostic includes `agentCommand.recommendedNextCommands[]` for the
required artifact validation path.

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
pnpm run agent:contracts
```

Benchmarks run locally by default:

```bash
pnpm run bench
```
