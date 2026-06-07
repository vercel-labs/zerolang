---
name: agent
description: Graph-first agent workflow for making focused Zero changes with CLI feedback.
---

# Zero Agent Workflow

Use this when editing Zero code, examples, tests, docs, or a package. The graph interface is the primary authoring surface for agents: inspect and patch source through ProgramGraph commands, and use ProgramGraph artifacts only when you need an interchange/debug file. `.0` files are the canonical source text that gets committed. Zero command text is designed to be readable by agents; use JSON when another tool must parse stable fields or when deeper diagnostics are needed.

## Start

Use the same compiler binary that will run the project:

```sh
zero --version
zero skills list
zero skills get language
zero skills get graph
zero skills get diagnostics
```

Inside the Zero repository checkout, prefer `bin/zero` over a global `zero`. For installed user projects, use the `zero` on `PATH` unless the user points at another binary.
When the task involves agent protocol behavior, contract coverage, or a repo
change that should stay verifiable, read the compiler-owned manifest first:

```sh
zero agent protocol --json
```

Use `localGates[]` from that manifest instead of inventing local validation
commands. The manifest has `agentCommand.readPolicy.name:
compiler-owned-agent-protocol`; replay its required `protocol-read`
verification command before trusting the root protocol surface in an automated
audit. Use `proofResultFields[purpose]` from the manifest to decide which
fields to record after replaying any verification command; this builds a proof
receipt from compiler-declared fields instead of command-specific guesses. Use
`proofReceiptPolicy.requiredFields` to record command argv, exit status,
declared result fields, observed fields, and replay timing for each proof, and
reject receipts that omit required replay evidence. Use `objectiveContract` to
map the Agent-native build-system goal to the gate fields that prove source
readability, semantic graph understanding, compiler-mediated edits and repairs,
refactoring coverage, verifiable build/test, proof receipts, rollback, token
efficiency, hallucination resistance, auto-repair-to-build, and protocol surface
coverage. The receipt replay smoke covers the protocol manifest, `zero check --json`,
`zero graph inspect --json`, `zero build --json`, `zero graph build --json`,
`zero test --json`, `zero graph test --json`, `zero ship --json`, and
`zero graph ship --json`, so a green gate proves receipts work on protocol,
normal source/graph verification, build verification, test verification, and
release ship verification commands. The
`scripts/agent-completion-audit.mts` report reads `objectiveContract`, replays
`agent-contracts`, and emits each requirement with the gate summary fields that
prove it. Run it before claiming the Agent-native build-system objective is
complete.
The
`agent-contracts` gate declares its stable `summaryFields`,
including `proofCommandsRequired`, `proofPurposes`,
`proofResultFieldsCatalogAuditable`,
`proofReceiptPolicyAuditable`,
`objectiveContractAuditable`,
`proofReceiptReplayOperational`,
`diagnosticEntrypointResultFields`,
`diagnosticEntrypointRollbackActionFields`,
`diagnosticGraphLookupCommands`,
`diagnosticGraphLookupTargetProfilePreserved`,
`diagnosticGraphLookupRepairFallback`,
`diagnosticCheckReadPolicyAuditable`,
`checkFailureRepairFollowupsStructured`,
`operationLevelRetryContract`, `checkedGraphEditEntrypointAudit`,
`graphPatchFailureMatrixComplete`,
`graphPatchFailureRetryCommandContracts`,
`graphPatchInvalidPatchInputRetryStructured`,
`graphPatchInlineCommandArgsPreserved`,
`repairTransactionFailureMatrixComplete`,
`repairTransactionRetryCommandContracts`,
`repairTransactionProofLedgerStructured`,
`repairRollbackActionAuditable`,
`repairLoopProofLedgerAuditable`,
`repairLoopTargetProfilePreserved`,
`transactionFailureRetryTargetProfilePreserved`,
`rollbackProofCoverage`, `graphPatchProofCommandsReplayed`,
`graphPatchProofReplayClassified`, `graphPatchProofLedgerStructured`,
`graphPatchRollbackActionAuditable`,
`graphPatchRollbackProofReplayPassed`,
`graphPatchEvidenceBindingsAuditable`,
`graphPatchVerificationResultFieldsAuditable`,
`repairPlanCandidateContracts`, `repairPlanCompleteCandidateContracts`,
`repairPlanCandidateEvidenceBacked`,
`repairPlanCandidateTargetProfilePreserved`,
`fullSourceReadRequiredForGraphLookup`, `semanticEditCategories`,
`semanticEditCheckedPreconditions`,
`semanticEditRequiredPreconditions`, `semanticEditOperationSurfaceBacked`,
`semanticEditSourceBackedTransactions`, `semanticEditConflictRejections`,
`semanticEditCheckedSourceRewrite`, `sourceUnderstandingStructuredFields`,
`sourceUnderstandingTokenParseFollowupStructured`,
`sourceUnderstandingGraphFollowupStructured`,
`sourceUnderstandingFormatFailureFollowupStructured`,
`sourceUnderstandingFormattedStable`, `semanticGraphLookupCommandContracts`,
`semanticGraphQueryReplayable`,
`semanticGraphQueryTargetProfilePreserved`,
`semanticGraphQueryReadPolicyAuditable`, `semanticGraphLookupIdentityBound`,
`graphFindFollowupSliceCommandStructured`,
`graphFindAmbiguousSliceFollowupStructured`,
`graphFindNotFoundInspectFollowupStructured`,
`graphInspectSliceFollowupStructured`,
`graphInspectReadPolicyAuditable`,
`graphImpactPatchFollowupStructured`,
`graphImpactMissingNodeInspectFollowupStructured`,
`graphSliceImpactFollowupStructured`,
`graphSlicePatchFollowupStructured`,
`graphSliceMissingNodeInspectFollowupStructured`,
`graphCompareViewFollowupStructured`,
`graphCompareUnstableViewFollowupStructured`,
`commandFailureRetryStructured`,
`missingGraphArtifactRecoveryStructured`,
`graphDumpWritePolicyAuditable`,
`graphDumpViewFollowupStructured`,
`graphUnsupportedOutRetryStructured`,
`graphArtifactWritePolicyAuditable`,
`graphArtifactImportFollowupsStructured`,
`graphArtifactRoundtripFollowupStructured`,
`graphArtifactValidateViewFollowupStructured`,
`graphArtifactViewCheckFollowupStructured`,
`graphArtifactCheckSizeFollowupStructured`,
`buildSystemCommandReplayable`,
`artifactWritePolicyAuditable`, `artifactRollbackActionAuditable`,
`buildTestFollowupStructured`,
`buildFailureRecoveryStructured`,
`graphBuildFailureRecoveryStructured`,
`graphArtifactCommandRerouteStructured`,
`graphBuildSizeFollowupStructured`,
`graphBuildTestFollowupStructured`,
`sizeBuildFollowupStructured`,
`runJsonUnsupportedFollowupStructured`,
`graphShipSizeFollowupStructured`,
`graphShipTestFollowupStructured`,
`sourceShipTestFollowupStructured`,
`buildSystemArtifactEvidenceMatched`, `buildSystemTargetProfilePreserved`,
`testFailureInspectFollowupStructured`,
`testNoTestsDocAuditFollowupStructured`,
`testSystemFailureEvidenceStructured`, `testSystemGraphEvidenceBound`,
`buildSystemSafetyFactsStructured`,
`buildSystemProductionReadinessAuditable`,
`buildSystemCacheAuditStructured`, `buildSystemProgramGraphCacheBound`,
`agentPermissionBoundaryStructured`,
`targetSelectionJsonReplayable`,
`targetSelectionDoctorFollowupStructured`,
`doctorTargetSelectionFollowupStructured`,
`destructiveCleanPolicyAuditable`,
`destructiveCleanFailureRetryStructured`,
`newProjectWritePolicyAuditable`,
`newProjectFailureRetryStructured`,
`newProjectVerificationAuditable`,
`hostTargetReadinessStructured`, `hostTargetCommandReplayable`,
`auxiliaryAgentGuidanceStructured`,
`runtimeAuditGraphFollowupStructured`,
`explainRepairPlanResultContractAuditable`,
`docSymbolGraphFindFollowupStructured`,
`auxiliaryAgentCommandReplayable`,
`skillsGuidanceCommandReplayable`,
`protocolManifestCommandReplayable`,
`authoringReplayOperational`, `authoringReplayProofCommands`,
`autoRepairBuildReplayOperational`, `autoRepairBuildReplayProofCommands`,
`rollbackReplayOperational`, `rollbackReplayProofCommands`,
`objectiveAuditOperational`, `objectiveAuditEvidenceItems`,
`versionProvenanceCommandReplayable`,
`sourceShipSizeFollowupStructured`,
`tokenSavingSampleRatio`,
`tokenSavingFullInspectRatio`, `agentNativeAdvantageScore`,
`agentNativeReliableEdits`, `agentNativeTokenEfficient`,
`agentNativeHallucinationResistant`, `agentNativeAutoRepairable`,
`agentNativeVerifiableBuildSystem`, `metricsBudgetOk`, and `metricsViolations`; use
those fields to decide whether a change preserved the Agent protocol surface.
For automated provenance checks, start with `zero --version --json`. Its
`agentCommand.readPolicy.name` is `compiler-provenance`, its required
verification purpose is `version-read`, and its optional follow-up is
`zero agent protocol --json` when you need the full Agent protocol manifest.
When loading compiler-bundled guidance in an automated workflow, prefer
`zero skills list --json` and `zero skills get <name> --json`. Their
`agentCommand.readPolicy.name` is `bundled-compiler-guidance`, their data is
version-matched to the compiler binary, and their required verification purpose
is `skills-read`.
When build or target readiness matters, run `zero targets --json` and
`zero doctor --json`. For targets, replay `agentCommand.command.argv`; its
target-selection proof uses an explicit `["zero", "targets", "--json"]` argv.
After selecting a target, use targets `agentCommand.recommendedNextCommands[]`
for the optional `doctor` follow-up instead of inventing the readiness command.
For doctor, read `agentCommand.command.argv`, `agentCommand.auditFields`,
`checks[]`, and `targetToolchains[]` before attempting a wider build loop. Its
verification command uses `purpose: "doctor"` and `required: true`; its
optional `target-selection` follow-up is `zero targets --json` for rechecking
available targets from the readiness matrix.
For cleanup, prefer `zero clean --json` over plain `zero clean` in automated
workflows. Its `agentCommand.writePolicy.name` is `destructive-clean`, rollback
is not compiler-available, and `removedRoots[].path` is the allowed deletion
boundary to audit before accepting the result. If clean fails, read
`failure.retryCommands[]` and `recommendedNextCommands[]`; retry only after the
external filesystem condition named by `failedPath` has been resolved.
For project creation, prefer `zero new --json cli|lib|package <path>` over
plain `zero new` in automated workflows. It reports
`agentCommand.writePolicy.name: "writes-source-tree"`, `project.path` as the
created-root and rollback boundary, `project.files[]` as the source-tree
evidence, and required `source-check` / `test-run` verification commands.
`zero run --json` and `zero graph run --json` intentionally reject JSON because
program stdout is reserved for the program. When that happens, read the top-level
`agentCommand` and follow its required `artifact-validate` build command or
optional `test-run` follow-up instead of trying to parse run output.
When a diagnostic span or declaration summary is enough, use
`zero tokens --json <input>` or `zero parse --json <input>` first. Both reports
include `agentCommand`; replay `agentCommand.command.argv`, use token
`spanFields` for offsets, and use parse `declarationFields` before escalating to
ProgramGraph inspection. When token spans are insufficient, use tokens
`agentCommand.recommendedNextCommands[]` for the optional `parse` follow-up.
When parse declarations are insufficient for semantic edit planning, use parse
`agentCommand.recommendedNextCommands[]` for the optional `graph-inspect`
follow-up instead of inventing the graph command.
Their verification purposes are `tokens` and `parse`.
Use `zero fmt --json <input>` before presenting a rewrite when a tool needs
structured byte-stability facts. Read `agentCommand.command.argv`, `matches`,
`sourceBytes`, `formattedBytes`, and `formatted`; with `--check`, a nonmatching
input returns `ok: false` and a structured `FMT001` diagnostic. Its verification
purpose is `format`. For nonmatching `--check` reports, use
`failure.retryCommands[]` / `recommendedNextCommands[]` for the structured
`format-preview` command before applying the `formatted` field and rerunning
source checks.
Use `zero dev --json <input>` for watch-loop planning. Its `agentCommand`
captures the replay argv, watch inputs, partial diagnostic stability,
incremental invalidation fields, and a target-matched `zero check --json`
verification command. Execute required `dev-plan` and `source-check`
verification commands; non-default `--target` and `--profile` are preserved in
the replay and verification argv.
Use `zero doc --json <input>` before changing public APIs or examples. When
`publicationGate.missingExampleCount` is nonzero, use
`agentCommand.recommendedNextCommands[]` for the optional `graph-find` follow-up
bound to `symbols[].name`; it preserves target/profile placeholders and returns
node ids/node hashes plus graph-slice follow-ups.
Use `zero time --json <input>` when deciding whether a repair loop is cold,
warm, or cache-invalidating. Its `agentCommand` captures timing/cache state
fields and required `time-audit` / `source-check` verification commands.
Use `zero mem --json --target <target> <input>` before changing allocator,
collection, stack, readonly-data, or runtime-helper behavior; its
`agentCommand` captures memory layout state and required `memory-audit` /
`source-check` replay. When memory, allocator, or capability facts need semantic
source attribution, use its optional `graph-inspect` follow-up from
`agentCommand.recommendedNextCommands[]`; it preserves target/profile
placeholders and returns ProgramGraph node hashes plus capability indexes.
Use `zero abi dump --json --target <target> <input>` before changing C exports,
C imports, extern shapes, or target ABI assumptions; its `agentCommand` captures
layout state fields and required `abi-audit` / `source-check` verification
commands.

## Graph-First Edit Loop

1. Read the nearest `zero.json`, source files, tests, and examples enough to understand the package boundary.
2. Inspect the current source through the graph:

```sh
zero graph view <file-or-package>
zero graph check <file-or-package>
zero graph status <file-or-package>
```

3. Use JSON when you need exact node IDs or graph hashes:

```sh
zero graph dump --json <file-or-package>
```

Read `agentQuery.repairLoop` and `agentQuery.checkedEditSurface` before making
checked graph edits. `repairLoop.operationRetry` points at
`operations[].retryCommands[]`; for `GPH005` precondition failures, prefer the
operation-level `graph-impact` retry command when present before rebuilding the
patch. Use `repairLoop.commandContracts.*.argv` for execution; those argv arrays
preserve `--target <same-target> --profile <same-profile>` across check, plan,
inspect, and checked graph patch steps.

4. For precise mechanical edits on canonical `.0`, prefer a checked graph patch that rewrites the source after validation:

```sh
zero graph patch <file.0> --expect-graph-hash graph:a7f7e6899a73f3b4 --op 'rename node="#decl_ad8d9028" expect="main" value="start"'
zero graph check <file.0>
zero check <file.0>
```

5. When a graph artifact is necessary, write it under `.zero/`, patch the artifact, validate it, and then make the accepted source change. Do not commit derived `.program-graph` files unless the user explicitly asks.
6. If `zero graph status <input>` reports repository graph sync as enabled, use `zero graph verify-sync <input>` when graph/source drift must fail the workflow. Use `zero graph sync --from-source <input>` to refresh `zero.graph` from reviewed source changes while preserving unambiguous graph node handles and exact local `.0` projection bytes, or `zero graph sync --from-graph <input>` to refresh stale or missing `.0` source projections from `zero.graph`. When `zero.json` sets `repositoryGraph.compilerInput` to `true`, normal compiler commands validate and compile from `zero.graph`; they report projection state but do not rewrite projections. When combining repository graph stores, use `zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <input>` and then refresh projections explicitly if the merge succeeds.
7. Run a focused source check:

```sh
zero check <file-or-package>
```

8. When the compiler reports a diagnostic, explain the code first. If you need stable fields or a repair plan, rerun with JSON:

```sh
zero explain <diagnostic-code>
zero check --json <file-or-package>
zero fix --plan --json <file-or-package>
```

9. If behavior changes, add or update a `test` block or conformance fixture.
10. Validate with the narrowest command that covers the changed surface.
For failed `zero check --json`, prefer `agentCommand.recommendedNextCommands[]`
to choose `repair-plan` or `graph-inspect`; for failed `zero graph check --json`,
prefer its `graph-inspect` follow-up before rebuilding graph command argv by
hand.

8. If behavior changes, add or update a `test` block or conformance fixture.
9. Validate with the narrowest command that covers the changed surface.

## Agent Rules

- Treat effects as capabilities, not ambient globals. Use `World`, `std.fs`, `std.args`, `std.env`, and similar APIs only where the target supports them.
- Keep examples copyable and runnable from the repository or package root.
- Prefer explicit types at public boundaries and when inference is unclear.
- Use `Maybe<T>`, explicit `raises` / `raises [...]`, and `check` instead of hidden failure.
- Prefer graph inspection and source-backed graph patches for agent planning and mechanical edits.
- Do not invent syntax. Load `language` when unsure.
- Do not invent CLI fields. If you need fields, run the command with `--json` and read the data.

## Useful Focused Commands

```sh
zero check <input>
zero graph <input>
zero graph view <input>
zero graph check <input>
zero graph status <input>
zero graph dump --json <input>
zero test <input>
zero size <input>
zero doctor --json
```

For CLI behavior, JSON contracts, or editor/tool integrations in the Zero repo, use `--json` and the repository scripts listed by `AGENTS.md` or the project documentation.
