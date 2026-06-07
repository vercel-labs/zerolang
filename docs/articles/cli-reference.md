## CLI Reference

`zero` checks, formats, runs, tests, builds, inspects, and repairs Zero programs.

Most commands accept the same input forms:

| Input | Meaning |
| --- | --- |
| `file.0` | Canonical Zero source text. |
| `project/` | A package directory containing `zero.json`. |
| `zero.json` | A package manifest. |

## Daily Commands

| Command | Use it for |
| --- | --- |
| `zero check <input>` | Parse, typecheck, and report diagnostics. |
| `zero run <input>` | Build and run a host executable with the selected backend. |
| `zero test <input>` | Run inline `test` blocks. |
| `zero fmt [--json] <input>` | Print formatted source. Add `--check` in CI; JSON reports byte-stability and an `agentCommand`. |
| `zero new [--json] cli|lib|package <path>` | Create a package template. JSON reports created files, a source-tree write policy, rollback path, and verification commands. |
| `zero build <input>` | Emit an executable or object file. |
| `zero ship <input>` | Produce a release preview with checksums and metadata. |
| `zero graph <input>` | Inspect modules, symbols, capabilities, helper use, and ProgramGraph facts. |
| `zero size <input>` | Explain artifact size, retained helpers, and profile budgets. |
| `zero doc <input>` | Emit public API documentation facts. |
| `zero fix --plan/--patch/--apply --json <input>` | Ask for a typed repair plan, reviewable compiler patch, or checked behavior-preserving repair. |
| `zero doctor` | Check host and target readiness. |

Copyable examples:

```sh
zero check examples/hello.0
zero fmt --json examples/hello.0
zero run examples/add.0
zero test conformance/native/pass/test-blocks.0
zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add
zero graph --json examples/systems-package
zero graph dump examples/hello.0
zero graph dump --out .zero/out/hello.program-graph examples/hello.0
zero graph import --out .zero/out/hello.program-graph examples/hello.0
zero graph inspect --json examples/hello.0
zero graph validate .zero/out/hello.program-graph
zero graph view examples/hello.0
zero graph view --out .zero/out/hello.view.0 .zero/out/hello.program-graph
zero graph source-map --json examples/hello.0
zero graph reconcile --json .zero/out/hello.program-graph --source examples/hello.0
zero graph status --json .
zero graph check --json .zero/out/hello.program-graph
zero graph size --json .zero/out/hello.program-graph
zero graph build --json --emit obj --target linux-musl-x64 --out .zero/out/hello.o .zero/out/hello.program-graph
zero graph run .zero/out/hello.program-graph
zero graph test --json .zero/out/hello.program-graph
zero graph patch examples/hello.0 --expect-graph-hash graph:a7f7e6899a73f3b4 --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello patched\n"'
zero graph ship --json --target linux-musl-x64 --out .zero/ship/hello .zero/out/hello.program-graph
zero graph patch .zero/out/hello.view.0 --expect-graph-hash graph:f76987e99677f1b3 --op 'set node="#610c78bf" field="value" expect="hello from zero\n" value="hello patched\n"'
zero graph roundtrip examples/hello.0
zero graph roundtrip .zero/out/hello.program-graph
zero size --json examples/point.0
zero ship --json --target linux-musl-x64 examples/hello.0 --out .zero/ship/hello
zero doctor --json
```

## Run

`zero run` builds a host executable, runs it, passes through program
stdout/stderr, and exits with the program status. Direct output is the default;
pass `--backend llvm` for explicit LLVM host execution when `clang` is ready.

Pass program arguments after `--`:

```sh
zero run examples/cli-file.0 -- input.txt
zero graph run .zero/out/cli-file.program-graph -- input.txt
```

## JSON Output

Normal command output is designed to be readable by agents. Use `--json` when
another tool needs stable fields.

| Command | Useful JSON fields |
| --- | --- |
| `zero check --json` | Diagnostics with code, span, expected/actual details, help, repair metadata, `graph` identity for canonical source, `targetReadiness`, and `safetyFacts` for the selected target/emit kind. |
| `zero graph --json` | Modules, public symbols, capabilities, static facts, safety facts, helper use, and nested `programGraph`. |
| `zero graph dump --json` | The bare deterministic ProgramGraph with `moduleIdentity`, `graphHash`, validation, counts, nodes, and edges. Use `--out <program-graph-artifact>` to also write a derived graph artifact. |
| `zero graph import --json` | Source-to-ProgramGraph import with graph identity and validation. With `--out <program-graph-artifact>`, writes a derived graph artifact and reports `saved.path`. |
| `zero graph validate --json` | A derived ProgramGraph artifact readback check with `moduleIdentity`, `graphHash`, counts, validation state, and optional normalized artifact output path. |
| `zero graph view --json` | Canonical source text rendered from source or a ProgramGraph artifact with `moduleIdentity`, `graphHash`, and optional output path. |
| `zero graph source-map --json` | Graph node IDs mapped to source ranges with node hashes, symbol/type/effect IDs, and file hash facts. |
| `zero graph reconcile --json` | Identity decisions when edited source is compared with a prior graph, including ambiguous-match diagnostics and simple graph patch text when available. |
| `zero graph status --json` | Repository graph sync facts, the expected `zero.graph` path, no-write status, store validity, and whether graph/source sync is enabled. |
| `zero graph verify-sync --json` | A no-write graph/source sync check that compares a valid repository graph store with the current source graph and reports repair commands on drift. |
| `zero graph merge --json` | Three-way repository graph store merge with base/left/right stores, durable-node conflict diagnostics, changed-path reporting, storage facts, and scale counts. |
| `zero graph check --json` | Typecheck source or a ProgramGraph artifact through direct graph lowering with graph identity, target, `check.lowering: "direct-program-graph"`, target readiness, safety facts, and graph-mapped diagnostics. |
| `zero graph size --json` | Size, helper, runtime, profile, safety, and backend facts for a ProgramGraph artifact lowered through typed graph MIR, with graph identity. |
| `zero graph build --json` | Build a ProgramGraph artifact through typed graph MIR when supported, including graph identity, selected `emit` kind, target, artifact path and size, safety facts, compiler cache facts, and graph-aware incremental invalidation. |
| `zero graph patch --json` | Checked graph edits with graph-hash preconditions, per-operation node/field results, the changed graph hash, and the saved source or artifact path. |
| `zero graph roundtrip --json` | Source or ProgramGraph artifact stability through direct graph lowering with `semanticStable`, lowering mode, original and roundtripped graph hashes, raw counts, normalized semantic counts, and optional ProgramGraph output. |
| `zero dev --json` | A watch plan for changed source, manifest, package-lock, and generated-binding inputs. |
| `zero dev --json --trace` | Adds phase timing, cache hit/miss facts, diagnostics passthrough, and `interfaceFingerprints`. |
| `zero time --json` | Compiler phase timing plus `interfaceFingerprints` and incremental invalidation facts. |
| `zero build --json` | Artifact path, size, selected `toolchain`, target triple, linker flavor, sysroot status, `graph` identity, `safetyFacts`, and runtime provider facts when a helper such as hosted HTTP is linked. |
| `zero size --json` | `graph` identity, `profileSemantics`, `profileCatalog`, `profileBudget`, `safetyFacts`, `backendProfile`, `backendComparison`, `sizeBreakdown`, `retentionReasons`, and `optimizationHints`. |
| `zero ship --json` | A release preview with artifact names, hashes, graph identity, safety facts, a checksum file, debug-symbol metadata, size report, and SBOM placeholder. |
| `zero test --json` | Graph identity for canonical source, test discovery mode, selected fixtures, result counts, output, and per-test locations/failures. |
| `zero doctor --json` | Host checks plus `targetToolchains`, the per-target readiness matrix. |
| `zero check --json` | Diagnostics with code, span, expected/actual details, help, repair metadata, `targetReadiness`, `safetyFacts`, and read-only `agentCommand.readPolicy.name: "semantic-diagnostic-read"` for the selected target/emit kind. |
| `zero agent protocol --json` | Compiler-owned Agent protocol manifest with read-only `agentCommand.readPolicy.name: "compiler-owned-agent-protocol"`, required `protocol-read` verification, entrypoints, proof purposes, permission model, and local gate command argv. |
| `zero fmt --json` | `agentCommand` with canonical argv, formatted source in `formatted`, byte counts, `matches`, and required `format` verification; with `--check`, `ok` is false when the input is not byte-stable after formatting and the report includes `failure.retryCommands[]` / `recommendedNextCommands[]` for a structured `format-preview` retry. |
| `zero graph --json` | Modules, public symbols, capabilities, static facts, safety facts, helper use, nested `programGraph`, and `agentQuery` for token-efficient graph lookup. |
| `zero graph dump --json` | The bare deterministic ProgramGraph with `moduleIdentity`, `graphHash`, validation, counts, nodes, and edges. Use `--out <program-graph-artifact>` to also write a derived graph artifact; both forms declare an optional `graph-view` follow-up in `agentCommand.recommendedNextCommands[]` for token-bounded artifact viewing. |
| `zero graph import --json` | `agentCommand` with canonical argv, state fields, saved artifact fields, and required `graph-import` / `artifact-validate` replay commands; source-to-ProgramGraph import with graph identity and validation. With `--out <program-graph-artifact>`, writes a derived graph artifact and reports `saved.path`. |
| `zero graph validate --json` | `agentCommand` with canonical argv, state fields, saved artifact fields, optional `graph-view` follow-up, and required `artifact-validate` verification command; a derived ProgramGraph artifact readback check with `moduleIdentity`, `graphHash`, counts, validation state, and optional normalized artifact output path. |
| `zero graph view --json` | `agentCommand` with canonical argv, state fields, view/saved artifact fields, optional `graph-check` follow-up, and required `graph-view` verification command; canonical source text rendered from source or a ProgramGraph artifact with `moduleIdentity`, `graphHash`, and optional output path. |
| `zero graph check --json` | `agentCommand` with canonical argv, state fields, diagnostic fields, optional `graph-size` follow-up after a passing check, and `graph-check` verification command; typecheck source or a ProgramGraph artifact through direct graph lowering with graph identity, target, `check.lowering: "direct-program-graph"`, target readiness, safety facts, and graph-mapped diagnostics. |
| `zero graph size --json` | `agentCommand` with canonical graph argv, graph hash state fields, artifact `verificationFields` for `artifactHash` and `artifactBytes` when `--out` writes an artifact, and required `graph-check` / `graph-size` verification commands; size, helper, runtime, profile, safety, and backend facts for a ProgramGraph artifact lowered through `direct-program-graph`, with graph identity. |
| `zero graph build --json` | `agentCommand` with canonical graph argv, graph hash state fields, optional `graph-size` / `graph-test` follow-ups, artifact `verificationFields` for `artifactHash` and `artifactBytes`, and required `graph-check` / `graph-size` / `artifact-validate` verification commands; build a ProgramGraph artifact through direct graph lowering, including graph identity, selected `emit` kind, target, artifact path, size, hash, safety facts, compiler cache facts, and graph-aware incremental invalidation. |
| `zero graph ship --json` | `agentCommand` with canonical graph argv, graph hash state fields, optional `graph-size` / `graph-test` follow-ups, artifact `verificationFields` for `checksum`, `artifactBytes`, and emitted `artifacts`, and required `graph-check` / `graph-size` / `artifact-validate` verification commands; produce a release preview from a ProgramGraph artifact with direct graph lowering, graph-aware caches, release artifacts, checksum, and provenance facts. |
| `zero graph test --json` | `agentCommand` with canonical graph argv, graph hash/test-discovery state fields, failure span fields, and required `graph-check` / `graph-test` verification commands; run test blocks from a ProgramGraph artifact through direct graph lowering with per-test locations and failures mapped back to graph artifact source labels. |
| graph missing artifact failures | Graph command JSON failures with `failure.class: "missing-graph-artifact"` include a `graph-artifact-create` retry command using `zero graph dump --json --out <missing-artifact> <source-or-package>`. |
| graph unsupported `--out` failures | Graph JSON failures for non-output subcommands such as `inspect`, `check`, `test`, or `compare` include `agentCommand.failure.retryCommands[]`; use the reported `correct-command-usage` argv, which removes unsupported `--out` and preserves target/profile context. |
| source command with ProgramGraph artifact | Source-level `build`, `size`, `ship`, and `test` JSON failures for `.program-graph` inputs use `failure.class: "graph-artifact-command-mismatch"` and include a required `use-graph-artifact-command` retry in the `zero graph <command>` namespace, preserving target/profile/output flags. |
| `zero test --json` | `agentCommand` with canonical source argv, test-discovery state fields, failure span fields, optional `graph-inspect` failure follow-up, optional `doc-audit` follow-up when `selectedTests == 0`, and required `source-check` / `test-run` verification commands. |
| `zero --version --json` | Compiler provenance with `version`, `commit`, `host`, `backend`, targets, target compiler readiness, cross-compilation readiness, and a read-only `agentCommand.readPolicy.name: "compiler-provenance"` with required `version-read` verification and an optional `zero agent protocol --json` follow-up. |
| `zero skills list|get --json` | `agentCommand` with canonical argv, read-only `bundled-compiler-guidance` policy, version-matched skill data fields, and required `skills-read` verification command. |
| `zero new --json` | `agentCommand.writePolicy.name: "writes-source-tree"` with `createsRootField` / `rollbackField: "project.path"`, created `project.files`, required `source-check` / `test-run` verification commands for the generated package, and structured `failure.retryCommands[]` for unknown templates or existing project paths. |
| `zero graph patch --json` | Checked graph edits with graph-hash preconditions, `agentTransaction` audit metadata, per-operation node/field results, the changed graph hash, rollback facts, verification commands, and the saved source or artifact path. |
| `zero graph compare --json` | Semantic comparison for two source or ProgramGraph inputs with `semanticStable`, left/right graph hashes, comparison fields, required `graph-compare` replay argv, and optional `graph-view` follow-ups for both stable and unstable comparisons. |
| `zero graph roundtrip --json` | Source or ProgramGraph artifact stability through direct graph lowering with `semanticStable`, lowering mode, original and roundtripped graph hashes, raw counts, normalized semantic counts, optional ProgramGraph output, and an `agentCommand` contract with required `graph-roundtrip` replay argv, stability state fields, and saved/view artifact fields. |
| `zero dev --json` | `agentCommand` with canonical argv, watch/action/partial-diagnostic state fields, and required `dev-plan` / `source-check` verification commands; a watch plan for changed source, manifest, package-lock, generated-binding inputs, cache facts, incremental invalidation, local runtime, and capability state. |
| `zero dev --json --trace` | Preserves the `agentCommand` replay argv and adds phase timing, cache hit/miss facts, diagnostics passthrough, and `interfaceFingerprints`. |
| `zero time --json` | `agentCommand` with canonical argv, timing/cache/invalidation state fields, and required `time-audit` / `source-check` verification commands; compiler phase timing plus `interfaceFingerprints`, cache summary, package cache, capability, and incremental invalidation facts. |
| `zero abi check|dump --json` | `agentCommand` with canonical argv, ABI layout/import/export state fields, and required `abi-audit` / `source-check` verification commands; target pointer size, object format, calling convention, primitive layouts, extern shapes, C imports/exports, generated header, and diagnostics. |
| `zero mem --json` | `agentCommand` with canonical argv, memory/layout state fields, and required `memory-audit` / `source-check` verification commands; direct backend stack/static memory facts, budgets, regions, allocator and collection facts, MIR validity, object backend, safety facts, and incremental invalidation. |
| `zero doc --json` | `agentCommand` with canonical argv, public API audit fields, `symbols`, capability requirements, `publicationGate`, package metadata, and required `doc-audit` / `source-check` verification commands. |
| `zero build --json` | `agentCommand` with canonical argv, audit fields, artifact `verificationFields` for `artifactHash` and `artifactBytes`, and required `source-check` / `size-analysis` / `artifact-validate` verification commands; buildability failures expose `agent-build-failure-command-contract` with required `source-check` and optional `target-selection` / `doctor` / `graph-inspect` recovery commands; ProgramGraph buildability failures expose `agent-graph-build-failure-command-contract` with required `graph-check`; artifact path, size, `artifactHash`, selected `toolchain`, target triple, linker flavor, sysroot status, `safetyFacts`, and runtime provider facts when a helper such as hosted HTTP is linked. |
| `zero size --json` | `agentCommand` with canonical argv, audit fields, artifact `verificationFields` for `artifactHash` and `artifactBytes` when `--out` writes an artifact, required `source-check` / `size-analysis` verification commands, and optional `artifact-validate` follow-up to `zero build --json`; `profileSemantics`, `profileCatalog`, `profileBudget`, `safetyFacts`, `sizeBreakdown`, `retentionReasons`, and `optimizationHints`. |
| `zero ship --json` | `agentCommand` with canonical argv, artifact/checksum mapping, artifact `verificationFields` for `checksum`, `artifactBytes`, and emitted `artifacts`, audit fields, required `source-check` / `size-analysis` / `artifact-validate` verification commands, and optional `size-analysis` / `test-run` follow-ups; a release preview with artifact names, hashes, safety facts, a checksum file, debug-symbol metadata, size report, and SBOM placeholder. |
| `zero doctor --json` | `agentCommand` with canonical argv, audit/readiness fields, required `doctor` verification command, and optional `target-selection` follow-up to `zero targets --json`; host checks plus `targetToolchains`, the per-target readiness matrix. |

Canonical `.0` source JSON for `zero check`, `zero build`, `zero size`,
`zero ship`, `zero mem`, and `zero test` reports a top-level `graph` object
with `artifact`, `canonicalSource`, `moduleIdentity`, `graphHash`, and
`lowering`. Their compiler cache and incremental invalidation facts use
`sourceKind: "program-graph"` and include the graph input that keyed the
compile. Planning and introspection commands such as `zero dev`, `zero time`,
`zero doc`, and `zero abi` continue to report canonical source cache facts.
Derived ProgramGraph artifact commands report the same identity fields for the
artifact being inspected or built.

`zero check --json` and `zero graph --json` also include `compileTime`.
That object records bounded `meta` evaluation, sandbox denials, cache key
inputs, typed reflection facts, and integer/Bool/enum static values.

`zero graph inspect --json` and `zero graph --json` include `agentQuery`.
This is the machine-readable contract for Agent-side graph lookup: stable keys
include `programGraph.graphHash`, node `id`, `nodeHash`, `symbolId`, and edge
`from`/`to` links. Agents should resolve a symbol to a node, inspect only the
incoming and outgoing edge neighborhood needed for the task, and carry
`graphHash`, node ids, and expected field values into checked edits. The
contract explicitly marks full source reads as unnecessary for ordinary graph
inspection and points edits at `zero graph patch --json`.
Rich graph query reports also include `agentCommand`, whose canonical
`command.argv` normalizes the query to `zero graph inspect --json` with explicit
target/profile flags, whose read-only `readPolicy.name: "semantic-graph-read"`
points token audits at `agentQuery.tokenStrategy`, and whose `stateFields`
identify the graph facts an Agent must preserve across inspect, plan, and patch
steps.
Token-bounded `zero graph find --json`, `zero graph slice --json`, and
`zero graph impact --json` reports also carry
`agentCommand.readPolicy.name: "semantic-graph-query-read"` so agents can audit
that these follow-up queries read semantic graph facts without writing source or
artifacts. Ambiguous `zero graph find --json` reports declare an optional
`graph-slice` follow-up with `when: "resolution.ambiguous"` and
`inputField: "matches[].id"` so agents can inspect candidate neighborhoods
before selecting an edit target. Not-found `zero graph find --json` reports
declare an optional `graph-inspect` follow-up so agents can return to
compiler-reported symbol ids and node hashes before trying another lookup.
`zero graph slice --json` reports also declare
an optional `graph-impact` follow-up bound to `center.id`, so agents can audit
call sites, name references, and `editGuards[]` before constructing a patch.
Failed `zero graph impact --json` and `zero graph slice --json` reports for
stale or invalid node ids declare an optional command-level `graph-inspect`
follow-up with the same target/profile context, so agents can refresh node ids
and node hashes before retrying.
JSON command-usage failures expose `agentCommand.kind:
"agent-command-failure-contract"` with
`agentCommand.failure.retryCommands[]`, so agents can repair missing arguments
such as `--against`, `--symbol`, or `--node` from structured argv arrays instead
of parsing prose.
`agentQuery.lookupSurfaces` declares the stable command/field pairs for common
Agent lookups: symbol-to-node and node-neighborhood queries use
`zero graph inspect --json`, capability queries use `zero graph --json`
fields such as `requiresCapabilities` and
`functions[].requiresCapabilities`. `agentLookupIndexes.capabilities` gives a
compact capability-to-functions/helper index for the current program so agents
can locate the effect surface without scanning every graph node. Diagnostic-
driven work starts from `zero check --json` with `zero fix --plan --json` as the
typed repair planner.
`agentQuery.repairLoop` makes that loop explicit for agents: start with
diagnostics, plan a repair, inspect the graph, submit a checked graph patch, run
`agentTransaction.verificationCommands[]`, and use
`agentTransaction.failure.retryCommands[]` for rejected transactions.
It also includes `commandContracts.*.argv` so tools can execute the loop without
splitting command strings, and `stateFields` for carrying graph hashes and
patched-transaction hashes across the loop.
When `zero graph inspect --json` or `zero graph dump --json` fails after parsing
but before typecheck succeeds, the failure JSON can include `agentRecovery` and
`recoverableProgramGraph`. `agentRecovery.graphCompleteness` is `parse-level`,
so agents may use the graph for diagnostic span lookup and checked patch
planning, but must rerun the listed verification commands before treating the
program as valid. Recovery verification commands include `purpose`, `required`,
and `argv`, so agents can run required `source-check` and `graph-check` proofs
without parsing strings.
`agentQuery.checkedEditSurface.supportedOperations` lists the compiler-owned
edit verbs available to tools, including semantic refactor operations such as
`replaceCallee`. `operationContracts` gives the required and optional patch
attributes for each verb plus the meaning of `expect`, so agents can generate
checked edits without scraping command examples. `transactionContract` describes
the `zero graph patch --json` result before an agent submits an edit: transaction
phases, stable result fields, failure classes, retry commands, rollback
`savedKind`, and the verification command families for source-backed rewrites
versus derived graph artifacts.

`zero check --json --target <target> --emit <kind>` keeps language validity
separate from target buildability. Top-level `ok` and `diagnostics` describe
parse/typecheck results; `targetReadiness.ok`, `buildable`, and nested
diagnostics describe predictable backend blockers without writing artifacts.

Build and ship JSON include `releaseTargetContract`. It records artifact kind,
object format, direct linker flavor, target libc mode, sysroot requirements,
emitter readiness, target capability facts, and the repeat-build hash policy.
Host builds that retain runtime-backed helpers can also include `objectBackend`
linking facts such as retained runtime objects, provider libraries, and
`httpRuntime` TLS/provider metadata.
`zero ship --json` nests the same contract under
`releasePreview.targetContract`.

`.0` files are source text. ProgramGraph commands that write graph artifacts
must use a non-source output path, such as `.zero/out/app.program-graph`.
Agents can inspect source through ProgramGraph commands and can patch canonical
`.0` source through `zero graph patch`. ProgramGraph artifacts remain optional
debug and interchange files.

`zero graph status`, `zero graph verify-sync`, and `zero graph sync
--from-source|--from-graph` define the repository graph sync surface. Today
`zero graph sync --from-source` writes a deterministic `zero.graph` repository
store from current `.0` source, preserves existing graph node handles where the
source edit is unambiguous, and stores exact checked-in source projection bytes
for tracked local files. Ambiguous identity changes fail instead of guessing.
`zero graph sync --from-graph` rewrites stale `.0` source projections from that
store, and `zero graph verify-sync` checks the store against the current source
graph and source projection without writing files. Packages can opt normal
check, build, run, test, size, ship, and mem commands into the checked-in store
with `repositoryGraph.compilerInput: true` in `zero.json`. Normal compiler
commands validate and compile from the graph store, including target and package
metadata, so source-free graph packages can still be checked, built, run,
tested, sized, shipped, and inspected. Commands report source projection state
and do not rewrite `.0` files; run `zero graph verify-sync` when graph and
source projection drift must fail the workflow. Packages without that marker
still use checked-in `.0` source text as their compiler input.
`zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right
<right-zero.graph> <input>` combines independent repository graph store edits by
durable node ID and node hash, writes the target `zero.graph` on success, and
reports conflicts by graph node, source projection, semantic object, and field.
It does not rewrite `.0` projections; run `zero graph sync --from-graph` after a
successful merge when the checked-in source projection should be refreshed.
In this repository, `pnpm run repository-graph:check` verifies checked-in
`zero.graph` stores for CI with the pinned `linux-musl-x64` graph target.

## ProgramGraph Patches

`zero graph patch` applies checked edits to a graph. When the input is
canonical `.0` source without comments, the command rewrites that source after
lowering, formatting, re-parsing, and semantic graph comparison succeeds. For
small edits, pass one or more operations inline:

```sh
zero graph patch \
  examples/hello.0 \
  --expect-graph-hash graph:a7f7e6899a73f3b4 \
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello patched\n"'
```

For larger edits, patch files are line-oriented text:

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello patched\n"
insert node="#patch001" kind="Literal" parent="#expr_c403020c" edge="arg" order="1" type="String" value="again\n"
rename node="#decl_ad8d9028" expect="main" value="start"
expect graphHash "graph:f76987e99677f1b3"
set node="#610c78bf" field="value" expect="hello from zero\n" value="hello patched\n"
insert node="#patch001" kind="Literal" parent="#421a4d4b" edge="arg" order="1" type="String" value="again\n"
rename node="#ea5ea1ca" expect="main" value="start"
renameSymbol node="#48a1ed3f" expect="helper" value="compute"
renameParam node="#5c6d1a27" expect="value" value="input"
renameLocal node="#23d497a1" expect="count" value="totalCount"
renameField node="#3b9f241e" expect="x" value="left"
replaceCallee node="#77e12235" expect="oldHelper" value="newHelper"
addImport name="std.mem"
addFunction name="answer" type="i32" value="42"
addFunction name="answerWithInput" type="i32" params="arg0:i32" value="42"
addParam node="#48a1ed3f" expect="helper" name="bias" type="i32" value="0"
removeParam node="#5c6d1a27" expect="unused"
removeFunction node="#8f0c2b91" expect="unusedHelper"
removeImport name="std.mem" expect="std.mem"
replaceImport name="std.mem" expect="std.mem" value="std.ascii"
renameImportAlias name="std.mem" expect="memory" value="buffer"
changeReturnType node="#48a1ed3f" expect="i32" value="i64"
changeParamType node="#4c21b7e2" expect="i32" value="i64"
changeLocalType node="#23d497a1" expect="i32" value="i64"
changeFieldType node="#3b9f241e" expect="i32" value="i64"
delete node="#patch001"
```

Use `--patch-text <text>` when a tool already has a complete patch document in
memory and should not create a temporary file.

The header is required. `expect graphHash` is optional but recommended; it
rejects edits against a different artifact. `set` requires `node`, `field`, and
`value`; `expect` is optional and rejects the operation when the current field
value differs.

With `--json`, `agentTransaction.failure` classifies rejected edits. A stale
graph hash reports `class: "stale-graph"` and tells the agent to inspect the
current graph before retrying. Invalid patch syntax reports
`class: "invalid-patch"` at phase `parse`; invalid typed values report
`class: "invalid-patch-value"` at phase `patch`. A failed node or field
precondition reports `class: "precondition-failed"` and tells the agent to
reinspect the target node or field before rebuilding the patch.
`agentTransaction.applied` is true only after a source or artifact was written,
and `failure.phase` names the failed transaction phase (`inspect`, `patch`,
`validate`, or `parse`).
`failure.retryCommands` gives structured commands, such as `zero graph dump
--json` for stale graph hashes or `zero graph inspect --json` for parse, value,
target, and precondition failures, so agents do not have to translate prose
retry guidance.
Use `failure.retryable` and the `failureClasses[].retryable` table before
starting an automatic retry; `GPH006` validation failures are marked
non-retryable because the patched graph failed compiler validation.
`agentTransaction.patchContract` describes the executed patch document as
`program-graph-patch-v1`, names `operations` as the audited item list, records
required operation inputs, and lists result fields such as
`operations[].actual` and `operations[].value` that agents should use for review
or retry planning. `agentTransaction.resultFields` is included on each graph
patch transaction and names stable audit fields such as `patchContract`,
`command.argv`, `originalGraphHash`, `patchedGraphHash`, `operationCount`,
`rollback.savedPath`, `rollback.applied`, `rollback.verificationCommands`,
verification commands,
`verificationCommands[].purpose`, `verificationCommands[].required`, and failure
code/class/retry commands, including `failure.retryCommands[].purpose` and
`failure.retryCommands[].required`. `agentTransaction.failureClasses` repeats the stable `GPH001`
through `GPH006` failure table on graph patch results, so an agent can classify
a rejected edit without fetching a separate inspect contract.
Input validation, graph loading, patch validation, and save failures are also
reported with `agentTransaction`, so graph patch JSON clients can keep one
transaction parser for both accepted and rejected edits.
`agentTransaction.phaseAudit` records the inspect, patch,
validate, save, and verify scheduling state for the transaction, so agents can
resume from the failed phase instead of inferring progress from prose.
`agentTransaction.rollback.savedKind` distinguishes source-backed `.0` rewrites
from derived ProgramGraph artifacts, and `verificationCommands` uses
`zero check --json` plus `zero graph check --json` for source rewrites, or
`zero graph validate --json` plus `zero graph check --json` for artifact writes.
Each verification command includes `purpose`, `required`, and `argv`, so agents
can distinguish source checks, graph checks, and artifact validation without
parsing command strings.
The inspect-time transaction contract also exposes structured
`rollback.sourceVerificationCommands` and
`rollback.artifactVerificationCommands` with the same command object shape.
Rollback also has its own `agentTransaction.rollback.verificationCommands`; run
those required commands after restoring a saved source or artifact before
continuing the transaction loop.
Rejected graph patch transactions use the same shape for retry commands:
`graph-dump` refreshes stale graph state, while `graph-inspect` rebuilds a patch
from current node facts.
When a repair planner returns `fixes[].graphPatchCandidates[]`, each candidate
also includes `evidence.diagnosticSpan`, `evidence.targetNode`, and
`evidence.typedValues` so an agent can audit why that graph edit was proposed
before submitting it as a checked patch. Candidate verification commands use
the same `purpose` / `required` / `argv` shape as transaction verification.

Supported operations are `set`, `insert`, `insertEdge`, `replace`, `delete`,
`rename`, `renameSymbol`, `replaceCallee`, `addImport`, `addFunction`, `addParam`, `removeParam`, `removeFunction`, `removeImport`,
`replaceImport`, `renameImportAlias`, `changeReturnType`, `changeParamType`,
`changeLocalType`, `changeFieldType`, `renameParam`, `renameLocal`, and `renameField`. `insert` creates a node and connects it to a
parent node with an ordered node edge. `insertEdge` connects existing graph facts
across `node`, `symbol`, `type`, or `effect` target domains. `replace` updates a
node in place and can require the current node hash through `expect`. `delete`
removes an owned subtree and rejects external references into that subtree.
`rename` updates a node name with an optional current-name precondition.
`renameSymbol` is a higher-level refactor operation for Function nodes: it
renames the declaration and direct call sites after checking owner-module name
conflicts.
`renameParam` targets a Param node and updates the declaration plus matching
Identifier uses inside the owning function body. It rejects duplicate parameter
names in the same function and checks the current parameter name with `expect`.
`renameLocal` targets a Let node and updates the declaration plus matching
Identifier uses in the owning function body when the compiler can prove a single
local binding boundary. It rejects parameter/local name conflicts and unproven
shadowing boundaries instead of applying an ambiguous rename.
`renameField` targets a Shape Field node and updates the declaration plus
matching `FieldAccess` nodes whose receiver type matches the owning shape and
matching `FieldInit` nodes in literals for that shape. It rejects duplicate field
names in the owning shape and checks the current field name with `expect`.
`replaceCallee` targets a `Call` or `MethodCall` node and updates the proven
callee leaf after checking the current callee name.
`addImport` adds an Import node to a Module; the compiler chooses the next
import edge order and a stable import node id unless `parent`, `order`, or
`node` is supplied. Use `name` for the module path and optional `value` for an
alias. Source-backed `std.*` imports are rewritten, reparsed, graph-compared,
and verified as `.0` source transactions. Package-local multi-module import
patches require a graph that already contains the target Module node; full
package source writeback for newly imported modules is deliberately stricter and
should not be guessed from a single-file graph.
`addFunction` adds a Function node to a Module with optional Param children from
`params="name:Type,..."`, a `returnType` TypeRef, and body Block. `Void`
functions get an empty body; non-`Void` functions require `value` as a return
literal and get a Return plus Literal child. It rejects duplicate function names
in the target Module and checks the parent Module node id with `expect`.
`addParam` targets a Function node and adds a Param plus `type` TypeRef at the
next parameter order unless `order` is supplied. If direct call sites exist,
`value` is required and the compiler adds a matching Literal argument to those
Call nodes at the same order; without direct calls it can add the parameter
alone. It rejects duplicate parameter names and occupied parameter or argument
orders, and checks the current function name with `expect`.
`removeParam` targets a Param node and removes that Param, its TypeRef, and the
same-order direct call arguments. It rejects parameters still referenced in the
function body, missing direct call arguments, and external references into the
removed parameter or argument subtrees. Its `expect` precondition checks the
current parameter name.
`removeFunction` targets a Function node and removes the owned function subtree
from its Module. It rejects direct call sites and external references into the
removed subtree, decrements later Module function edge orders, and checks the
current function name with `expect`.
`removeImport` removes an Import node by `node` or by `name` plus optional
`parent` and alias `value`. Its `expect` precondition checks the current import
module name, not the node hash.
`replaceImport` updates an Import node's module path by `node` or by `name`;
`value` is the new module path. It preserves any alias, rejects duplicate imports
in the same Module, and checks the current import module name with `expect`.
`renameImportAlias` updates an Import node's alias by `node` or by `name`;
`value` is the new alias and may be empty to clear it. It rejects duplicate
module+alias bindings and checks the current alias with `expect`.
`changeReturnType` targets a Function node and updates both the function return
type fact and its `returnType` TypeRef child. Its `expect` precondition checks
the current function return type.
`changeParamType` targets a Param node and updates both the parameter type fact
and its `type` TypeRef child. It also updates matching parameter Identifier
type facts inside the owning function body so multi-operation signature edits
can roundtrip through canonical source. Its `expect` precondition checks the
current parameter type.
`changeLocalType` targets a Let node and updates both the local binding type
fact and its `declaredType` TypeRef child. It also updates matching initializer
expression type facts and local Identifier type facts after the binding when
they still match the previous local type. Its `expect` precondition checks the
current local binding type.
`changeFieldType` targets a Shape Field node and updates both the field type
fact and its `type` TypeRef child. It also updates matching `FieldAccess` type
facts for the owning shape, field default expression type facts, and matching
shape-literal `FieldInit` value type facts so field type changes can roundtrip
through canonical source. Its `expect` precondition checks the current field
type.

Editable scalar fields are `name`, `type`, `value`, `public`, `mutable`,
`static`, `fallible`, and `exportC`. Boolean fields accept only `true` or
`false`. `name` values must be identifier paths or supported operator tokens.
`type` values must be valid Zero type syntax. Strings support `\\`, `\"`,
`\n`, `\r`, `\t`, and `\u00XX` escapes for non-NUL bytes. NUL bytes are not
valid ProgramGraph patch text.

## Build Outputs

| Emit mode | Command |
| --- | --- |
| Native executable | `zero build --emit exe --target linux-musl-x64 <input>` |
| Native object | `zero build --emit obj --target linux-musl-x64 <input>` |
| LLVM IR | `zero build --emit llvm-ir --backend llvm --target linux-musl-x64 <input>` |
| LLVM host executable | `zero build --backend llvm --emit exe --target host <input>` |

Removed backend flags report `BLD003`. Use direct emitters; the removed C
backend is not a compatibility path.

`direct` is the default backend family. `llvm` is an explicit experimental
backend family. It is not default eligible, release eligible, or accepted by
`zero ship`; direct emitters remain the supported release path. Use
`--backend llvm --emit llvm-ir` to write a `.ll` artifact. On a supported host
with `clang`, `zero build --backend llvm --emit exe` and
`zero run --backend llvm` compile that IR into a native executable through an
external LLVM toolchain plan. LLVM lowering currently supports scalar code,
direct calls, branches, loops, primitive fixed arrays, byte views, readonly
strings, and primitive `std.mem` helpers. Native LLVM object output,
unsupported targets, unsupported MIR constructs, and `zero ship --backend llvm`
report `BLD004` with `backendBlocker.backend: "llvm"` and do not fall back to
direct emitters. If the LLVM artifact references Zero runtime helpers, the JSON
build report lists the required runtime object in `objectBackend`.
`zero size --json --backend llvm` reports LLVM size/profile metadata, including
target triple, optimization level, retained runtime/helper facts, toolchain
readiness, and direct-vs-LLVM comparison rows without writing a native artifact.

## Tests

`zero test --json` is shaped for CI, editors, and Agent repair loops. It reports:

- `agentCommand`: canonical test argv, stable audit fields, failure fields, and
  verification commands
- discovery: `testDiscovery`, `fixtures`, `snapshotKey`
- counts: `discoveredTests`, `selectedTests`, `passedTests`, `failedTests`
- expected failures: `expectedFailures`, `unexpectedPasses`
- execution: `targetFacts`, `results`, `durationMs`, `stdout`, `stderr`

When `selectedTests == 0`, use the optional `doc-audit` recommended command to
audit public symbols and missing example/test coverage before treating the test
run as behavioral evidence.

Expected-fail tests use `xfail:`, `expected fail:`, or `[xfail]` in the test
name. A test marked this way must fail; an unexpected pass fails the command.

## Skills

`zero skills` serves bundled skill content for agents:

```sh
zero skills list
zero skills get zero
zero skills get language
zero skills get zero --full
zero skills list --json
zero skills get language --json
```

Add `--json` for automation. Skill content is bundled with the compiler so
agents can load the workflow that matches the Zero binary they are using.
JSON skills reports include `agentCommand.readPolicy.name:
"bundled-compiler-guidance"` and a required `skills-read` replay command.

## Language Server Smoke

Run the editor smoke path with:

```sh
pnpm run zls -- --self-test
```

The smoke covers diagnostics, hover docs, completions, go-to definition,
document symbols, and quick-fix code actions surfaced from `zero fix` for
`TAR002`, `TYP009`, `ERR002`, `ERR003`, and `PUB001`.

## Utility Commands

```sh
zero --version [--json]
zero new [--json] cli|lib|package <path>
zero doctor [--json]
zero check [--json] [--target <target>] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] <input>
zero dev [--json] [--trace] [--target <target>] <input>
zero run [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile dev|release] [--out <file>] <input> [-- args...]
zero build [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile dev|release] [--out <file>] <input>
zero ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <input>
zero test [--json] [--filter <name>] [--target <target>] [--cc <path>] [--out <file>] <input>
zero fmt [--check] <input>
zero graph [dump|import|inspect|validate|view|source-map|reconcile|status|verify-sync|sync|merge|check|size|build|run|test|patch|roundtrip] [--json] [--target <target>] <input> [patch]
zero fmt [--json] [--check] <input>
zero graph [dump|import|inspect|validate|view|check|size|build|run|ship|test|patch|roundtrip] [--json] [--target <target>] <input> [patch]
zero graph [dump|import|validate|roundtrip] [--json] --out <program-graph-artifact> <input>
zero graph view [--json] [--out <file.0>] <program-graph-or-source>
zero graph source-map --json <program-graph-or-source>
zero graph reconcile [--json] <base-program-graph-or-source> --source <edited-file.0|project|zero.json>
zero graph status|verify-sync [--json] <project|zero.json|file.0>
zero graph sync (--from-source|--from-graph) [--json] <project|zero.json|file.0>
zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> [--json] <project|zero.json|file.0>
zero graph size [--json] [--target <target>] --out <artifact> <program-graph-or-package>
zero graph patch [--json] [--out <program-graph-artifact>] <program-graph-or-source> (<patch-file>|--op <operation>)
zero graph build [--json] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package>
zero graph run [--target <host-target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package> [-- args...]
zero graph ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <program-graph-or-package>
zero graph test [--json] [--filter <name>] [--target <target>] <program-graph-or-package>
zero doc [--json] [--target <target>] <input>
zero size [--json] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--out <artifact>] <input>
zero explain [--json] <diagnostic-code>
zero fix --plan|--patch|--apply --json [--target <target>] <input>
zero targets [--json]
zero clean [--json] [--all]
zero mem [--json] [--target <target>] <input>
zero time --json [--target <target>] <input>
zero abi check|dump [--json] [--target <target>] <input>
zero tokens --json <input>
zero parse --json <input>
```

`zero tokens --json` and `zero parse --json` include `agentCommand` so agents
can audit and replay syntax-level inspection before graph lookup or repair
planning. Use token `spanFields` for stable diagnostic offsets and parse
`declarationFields` for declaration summaries without reading the full source.
Their verification commands use required `tokens` and `parse` purposes. Tokens
reports also declare an optional `parse` follow-up, and parse reports declare an
optional `graph-inspect` follow-up.
`zero clean --json` includes `agentCommand.writePolicy.name:
"destructive-clean"`, `removedRoots[]`, and `removed[]` so agents can audit
artifact deletion boundaries. Rollback is external; do not infer restore steps
from the removed paths. Clean failures expose `failure.retryCommands[]` and
`recommendedNextCommands[]` for replaying `zero clean --json` after external
filesystem locks or permissions are resolved.
`zero explain --json` also includes `agentCommand`; use its `repairFields` and
`recommendedNextCommands` to connect diagnostic explanation to check, fix-plan,
and graph-inspect steps. Each recommended command carries `purpose`, `required`,
and `argv`, so agents can route required source checks separately from optional
repair planning or graph inspection.

`zero targets --json` includes `agentCommand` for target selection audits. Use
`agentCommand.command.argv` for explicit JSON replay and
`agentCommand.selectionFields` with
`targets[].name`, `targets[].capabilities`, and `targets[].directBackend.*`
before choosing a build, size, or ship target. Its verification command uses
required `target-selection` with `["zero", "targets", "--json"]`.
Unknown target diagnostics (`TAR001`) use the same required `target-selection`
retry first, then expose a `correct-command-usage` retry template with
`<supported-target>`. The retry preserves repair modes and graph patch inputs so
agents keep the original transaction semantics.
