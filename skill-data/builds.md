---
name: builds
description: Build, run, ship, target, and profile Zero programs.
---

# Zero Builds

Use this when an agent needs to run, build, cross-build, inspect artifacts, or explain target support for a Zero program.

## Inputs

Most build commands accept one of these:

- a single `.0` file
- a package directory containing `zero.json`
- a direct path to `zero.json`

For packages with `repositoryGraph.compilerInput: true`, normal check, build,
run, test, size, ship, and mem commands compile from the checked-in
`zero.graph` store. Source projections may be clean, missing, stale, or in
conflict; commands report that state and do not rewrite `.0` files. Use
`zero graph verify-sync` when CI or review needs graph/source drift to fail, and
`zero graph sync --from-graph` to regenerate projections from the store. Other
packages compile from `.0` source text.

## Run

Use `zero run` for the host development loop:

```sh
zero run examples/hello.0
zero run examples/cli-file.0 -- input.txt
```

Arguments after `--` are passed to the Zero program.

`zero run --json` and `zero graph run --json` are rejected because program
stdout and stderr are passed through unchanged. The JSON rejection still includes
`agentCommand`; use `agentCommand.recommendedNextCommands[]` to run the required
`artifact-validate` build audit or the optional structured `test-run` follow-up.

## Clean

Use `zero clean --json` when an agent needs to remove compiler artifacts. Its
`agentCommand.writePolicy.name` is `destructive-clean`: it deletes artifact
roots, does not provide compiler rollback, and requires external restore policy
if deleted outputs matter. Audit `removedRoots[].path`,
`removedRoots[].existedBefore`, `removedRoots[].existsAfter`, and
`agentCommand.verificationCommands` instead of inferring deletion scope from
prose output.

## Build

Use direct emitters. The removed generated-C backend is not a fallback path.

```sh
zero build --emit exe examples/hello.0 --out .zero/out/hello
zero build --emit obj examples/hello.0 --out .zero/out/hello.o
```

Use LLVM only when the request is explicit. LLVM is experimental: it is not the
default backend, not release eligible, and not accepted by `zero ship`. Textual
IR is inspectable with `--emit llvm-ir`; host executable builds require a ready
clang toolchain. LLVM currently lowers scalar code, direct calls, branches,
loops, primitive fixed arrays, byte views, readonly strings, and primitive
`std.mem` helpers:

```sh
zero build --backend llvm --emit llvm-ir examples/hello.0 --out .zero/out/hello.ll
zero build --backend llvm --emit exe examples/hello.0 --out .zero/out/hello-llvm
zero run --backend llvm examples/hello.0
```

Use `--json` when a tool will read exact build fields:

```sh
zero build --json --target linux-musl-x64 examples/memory-package
```

Useful JSON fields include `agentCommand`, `artifactPath`, `artifactBytes`,
`toolchain`, `releaseTargetContract`, selected target facts, linker flavor, and
sysroot status. Use `agentCommand.command.argv` as the canonical build command
for audit logs, `agentCommand.auditFields` to locate stable build facts, and
`agentCommand.verificationCommands` to rerun source checking and size analysis
after the build. Replay those verification commands directly; source build,
size, test, and ship contracts preserve the selected `--target` and
`--profile` in their check/size verification argv. Each verification command
has `purpose`, `required`, and `argv`; execute every required `source-check`
and `size-analysis` proof before accepting build, size, or ship results.
For commands that write artifacts, read `agentCommand.writePolicy`; it declares
`writes-artifact`, the artifact path field, required verification field, and the
agent-enforced rollback policy for deleting or replacing the artifact. Use
`agentCommand.writePolicy.rollbackActions[]` rather than inferring cleanup from
file extensions or output paths.
After artifact validation, use build `agentCommand.recommendedNextCommands[]`
for the optional `test-run` follow-up when behavioral confidence is required.
The follow-up preserves target/profile placeholders and returns structured
`passedTests`, `failedTests`, `unexpectedPasses`, `results[]`, and
`agentCommand.verificationCommands` fields.
When `zero build --json` fails with
`agentCommand.kind: "agent-build-failure-command-contract"`, treat the top-level
`agentCommand` as the recovery contract. Run the required `source-check`
follow-up first, then use the optional `target-selection`, `doctor`, and
`graph-inspect` commands when target capability, host readiness, or semantic
source attribution is needed. Do not infer those commands from diagnostic prose;
read `diagnostics[].backendBlocker` and the declared argv/result fields.
When `zero graph build --json` fails with
`agentCommand.kind: "agent-graph-build-failure-command-contract"`, keep the
workflow graph-native. Replay `agentCommand.command.argv`, run the required
`graph-check` follow-up, and use the same optional `target-selection`, `doctor`,
and `graph-inspect` recovery commands from the contract.
After source size analysis, use size `agentCommand.recommendedNextCommands[]`
for the optional `artifact-validate` build follow-up when a runnable artifact is
required. It preserves target/profile placeholders and returns `artifactPath`,
`artifactBytes`, `artifactHash`, `agentCommand.writePolicy`, and
`agentCommand.verificationCommands`.
After source release packaging, use ship `agentCommand.recommendedNextCommands[]`
for the optional `size-analysis` follow-up when release preview or artifact
audit needs a fresh budget view, and the optional `test-run` follow-up when
behavioral confidence is required before accepting the release preview. Both
preserve target/profile placeholders. `size-analysis` returns `sizeBreakdown`,
`profileBudget`, `profileSemantics`, and verification commands; `test-run`
returns `passedTests`, `failedTests`, `unexpectedPasses`, `results[]`, and
verification commands.
For package or manifest inputs, keep using `agentCommand.command.argv`; `sourceFile`
may name the resolved entry `.0` file, but replay should preserve the package
or manifest path that carried package context.

## Graph Inputs

When an agent is authoring through ProgramGraph, inspect and patch the canonical `.0` source directly. Use graph build and run only when you intentionally need to validate a derived interchange artifact:

```sh
zero graph build --out .zero/out/app .zero/agent/app.program-graph
zero graph ship --json --out .zero/ship/app .zero/agent/app.program-graph
zero graph run .zero/agent/app.program-graph
```

Use normal `zero build` and `zero run` after persisting the accepted change to
canonical `.0` source text. If the package opts into repository graph compiler
input, run `zero graph sync --from-source <package>` after reviewed source
changes so normal commands can compile from the refreshed `zero.graph` store.
For ProgramGraph artifact build audits, prefer JSON:
`zero graph size --json <artifact-or-package>` and
`zero graph build --json <artifact-or-package>`. Read `agentCommand.command.argv`
for the canonical graph replay command, `agentCommand.stateFields` for graph hash
and lowering state, `agentCommand.artifact` for `artifactPath`/`artifactBytes`,
`agentCommand.writePolicy` for artifact write boundaries, and
`agentCommand.verificationCommands` to rerun `zero graph check --json` plus
`zero graph size --json` against the same artifact. Graph build, size, and ship
verification commands use `purpose: "graph-check"` for semantic validity and
`purpose: "graph-size"` for artifact/profile facts; both are required.
Use `zero graph ship --json <artifact-or-package>` when the derived graph itself
is the release input. Its `agentCommand` replays `zero graph ship`, maps
`checksum` and release artifact fields, and verifies with graph check/size.
For ProgramGraph buildability failures, require
`agent-graph-build-failure-command-contract`; its first recovery command must be
`graph-check`, not source `check`.

Use normal `zero build` and `zero run` after persisting the accepted change to canonical `.0` source text.

## Memory

Use `zero mem --json --target <target> <input>` when an agent needs to audit
direct backend memory behavior. The report includes `agentCommand`, stack and
readonly data estimates, memory budgets, regions, allocator and collection
facts, capability facts, used stdlib helpers, runtime shims, object backend
facts, MIR validity, and cache/invalidation facts. Replay
`agentCommand.command.argv` and then run its `verificationCommands` before
accepting a memory-sensitive refactor or release change. Execute required
`memory-audit` and `source-check` proofs. When allocator, capability, or
hidden-memory facts need source attribution, use
`agentCommand.recommendedNextCommands[]` for the optional `graph-inspect`
follow-up; it preserves target/profile placeholders and returns ProgramGraph
node hashes plus capability indexes.

## ABI

Use `zero abi dump --json --target <target> <input>` when an agent needs to
audit a C interop or FFI boundary. The report includes `agentCommand`,
target pointer size, object format, calling convention, primitive layouts,
extern shapes, C imports/exports, and generated header text. Replay
`agentCommand.command.argv` for the ABI view and run the listed
`verificationCommands` before accepting a build or release change that crosses
the C boundary. Non-default `--profile` is preserved in ABI replay and
verification argv. Execute required `abi-audit` and `source-check` proofs.

## Targets

Inspect target names and capability facts before cross-building:

```sh
zero targets --json
zero check --target linux-musl-x64 examples/memory-package
zero graph --target linux-musl-x64 examples/memory-package
```

`zero targets --json` returns JSON with `agentCommand`. Use
`agentCommand.command.argv` for audit logs and `selectionFields` to choose a
target from stable fields such as `targets[].name`, `capabilities`, and
`directBackend.*` support before building or shipping. Its verification command
uses `purpose: "target-selection"`, `required: true`, and an explicit
`["zero", "targets", "--json"]` replay argv.
Unknown target failures (`TAR001`) also use that required `target-selection`
retry before exposing a `correct-command-usage` template with
`<supported-target>`, so do not replay the invalid target value. The retry
preserves repair modes and graph patch inputs, so keep the reported argv intact.
Before a non-host build, follow targets `agentCommand.recommendedNextCommands[]`
for the optional `doctor` readiness audit. The follow-up replays
`["zero", "doctor", "--json"]` and returns structured `checks[]` plus
`targetToolchains[]` fields for the selected-target readiness decision.

Hosted APIs such as process args, environment, filesystem, net, and proc are target-gated. A non-host target may reject code that checks on the host.

## Profiles

Common profile names are `debug`, `dev`, `release-fast`, `release-small`, `tiny`, and `audit`.

```sh
zero build --profile release-small examples/hello.0
zero size --profile tiny examples/hello.0
```

Use `zero size` to explain retained functions, sections, literals, runtime shims, imports, debug metadata, and optimization hints. Add `--json` when a tool needs exact fields.
Use `zero size --backend llvm` when the question is specifically about the explicit LLVM backend; the report includes LLVM target triple, optimization level, retained runtime/helper facts, toolchain readiness, and direct-vs-LLVM comparison rows.
Use `zero size` to explain retained functions, sections, literals, runtime shims, imports, debug metadata, and optimization hints. Add `--json` when a tool needs exact fields. In JSON, use `agentCommand.command.argv` for audit logs, `agentCommand.auditFields` to locate `sizeBreakdown`, `retentionReasons`, `optimizationHints`, `profileBudget`, and safety facts, and `agentCommand.verificationCommands` to rerun required `source-check` and `size-analysis` proofs in the same target/profile context.

## Ship

`zero ship` produces a release preview:

```sh
zero ship --target linux-musl-x64 examples/hello.0 \
  --out .zero/ship/hello
```

The preview includes artifact names, sizes, hashes, checksum file metadata, size report data, debug-symbol metadata, and target contract facts.
In JSON, use `agentCommand.command.argv` for the release audit, `agentCommand.artifact` to locate the binary and checksum fields, `agentCommand.auditFields` to review `releasePreview`, `artifacts`, `releaseTargetContract`, package cache, and safety facts, and `agentCommand.verificationCommands` to rerun required `source-check` and `size-analysis` proofs in the same target/profile context.

## Troubleshooting

- `zero doctor` checks host and target readiness.
- `zero doctor --json` reports `llvmToolchain` readiness for explicit LLVM host builds.
- LLVM JSON facts include `backendLifecycle` so tools can distinguish explicit experimental readiness from release support.
- `BLD003` means an old backend flag was requested; remove it.
- `BLD004` with `backendBlocker.backend: "llvm"` means the selected LLVM artifact, target, command, MIR subset, or clang toolchain is not ready.
- Missing sysroot facts identify the required `ZERO_SYSROOT_*` variable.
- Unsupported targets fail explicitly instead of silently choosing another backend.
