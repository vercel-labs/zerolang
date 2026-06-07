---
name: diagnostics
description: Read Zero diagnostics, explanations, and typed fix plans.
---

# Zero Diagnostics

Use this when Zero code fails to parse, typecheck, build, test, or target-check. Zero diagnostics are intended for agents: start with the readable command output, then use JSON when you need stable fields, spans, or repair metadata.

## Commands

```sh
zero check <input>
zero explain <diagnostic-code>
```

Use machine-readable output when you need exact fields:

```sh
zero tokens --json <input>
zero parse --json <input>
zero check --json <input>
zero explain --json <diagnostic-code>
zero fix --plan --json <input>
zero fix --patch --json <input>
zero fix --apply --json <input>
```

`zero tokens --json` reports `agentCommand` plus token span fields (`line`,
`column`, `offset`, `length`) for precise diagnostic lookup. `zero parse --json`
reports `agentCommand` plus declaration summaries; use it when a parse-level
fact is enough and a full ProgramGraph query would waste context. Their
verification commands use `purpose: "tokens"` and `purpose: "parse"` with
`required: true`. `zero tokens --json` declares an optional `parse` follow-up
in `agentCommand.recommendedNextCommands[]` for the point where lexical spans
are no longer enough. `zero parse --json` also declares an optional
`graph-inspect` follow-up for the point where declaration facts are no longer
enough for semantic edit planning.
`zero explain --json` reports `agentCommand`; use `repairFields` to read the
stable repair id/summary and `recommendedNextCommands` to continue with check,
fix planning, or graph inspection without inventing command strings. Each
recommended command includes `purpose`, `required`, and `argv`; start with the
required `source-check`, then choose optional `repair-plan` or `graph-inspect`
based on the diagnostic. The optional `repair-plan` command also declares
`resultContract: "agentTransaction"` and `resultFields` for the repair proof
ledger, rollback actions, rollback verification commands, transaction
verification commands, and graph patch candidates.
`zero check --json` reports command-level `recommendedNextCommands` for failing
diagnostics: optional `repair-plan` points at `zero fix --plan --json` and
declares `resultContract: "agentTransaction"`, while optional `graph-inspect`
points at the semantic graph lookup path. Use these command-level follow-ups
when routing a failed check, then use per-diagnostic `repair.agentRepair` when a
stable repair id is available. Use its structured `commands` instead of
inventing `zero fix` invocations: required `repair-plan` is the safe default,
while optional `repair-patch` and `repair-apply` enter the same
compiler-mediated `agentTransaction` contract after review. Its `resultFields`
advertise the repair transaction proof ledger, rollback action fields, rollback
proof commands, verification commands, retry commands, graph patch candidate
fields, and diagnostic graph lookup fields before the repair command runs.

`zero fix --plan` reports candidate repairs without editing files. `zero fix --patch`
returns compiler-generated, reviewable source patch lines for supported repairs.
`zero fix --apply` applies only supported behavior-preserving repairs and reports
an `agentTransaction` with failure and rollback metadata when the compiler cannot
complete the transaction.
On failure, prefer `agentTransaction.failure.retryCommands` over prose. For an
unsupported source patch, retry with the reported plan command or inspect the
ProgramGraph before choosing a checked graph edit.
Check `agentTransaction.failure.retryable` and
`agentTransaction.failureClasses[].retryable` before starting automated source
repair retries.
For `source-unavailable`, fix the path or source availability, then rerun the
reported `zero check --json <input>` inspect retry command.
For `edit-not-found`, rerun the reported `zero check --json <input>` and rebuild
the repair from current diagnostic or graph facts instead of applying a no-op.
For `write-failed`, inspect the path or file permissions, then rerun the reported
`zero fix --apply --json <input>` retry command.
Read `agentTransaction.failure.phase` to see whether a rejected source repair
failed during inspect, patch, or rewrite before choosing a retry.
Use `agentTransaction.resultFields` and `agentTransaction.failureClasses` as the
stable source repair transaction schema instead of inferring fields from samples.
For source repairs, `resultFields` declares `input`, executed `command.argv`,
`diagnosticCode`, `repairId`, `proofLedger`, rollback fields, verification
commands, `verificationCommands[].purpose`, `verificationCommands[].required`,
and failure class, phase, retryable state, retry commands, retry command
`purpose` / `required` fields, `retryCommandContracts`, and applied state. Use
`agentTransaction.retryCommandContracts` as the executable retry template for
declared failure classes instead of parsing `failureClasses[].retryCommand`;
those argv templates preserve `--target <same-target> --profile <same-profile>`.
Read `agentTransaction.proofLedger` before executing or auditing a repair
transaction. Its phase order is inspect, plan, patch, validate, rewrite, verify,
rollback; each phase gives a `status`, `evidenceFields`, and where relevant
`requiredPurposes`. Use it to decide which fields prove that a source patch was
planned, validated, rewritten, verified, or rolled back.
When `zero fix --json` is run with a non-default `--target` or `--profile`, use
`agentTransaction.command.argv`, `agentTransaction.verificationCommands`, and
`agentTransaction.failure.retryCommands` directly; they preserve that
target/profile context for replay and repair retries.
On rejected source repairs, each retry command includes `purpose`, `required`,
and `argv`; use `source-check` to refresh diagnostics, `repair-plan` to request
a new plan, `graph-inspect` before graph edits, and `repair-apply` only after
checking the rejected rewrite cause.
For `fixes[].graphPatchCandidates[]`, use the candidate's `command.argv` and
`verificationCommands` directly as well; candidate graph patch commands preserve
the same target/profile context as the parent fix plan. Candidate verification
commands include `purpose`, `required`, and `argv`; execute each required
`source-check` and `graph-check` after the graph patch transaction succeeds.
Read `agentTransaction.writePolicy` before running a repair command:
`preview-only` does not write source, while `writes-source` does.
Use `agentTransaction.patchContract` to interpret `patches[]`. Use each
`fixes[].repairContract` to check required diagnostic inputs, whether the repair
id supports compiler-generated source patches, preconditions, and verification
commands before applying or delegating a fix. `repairContract.verification[]`
uses `purpose`, `required`, and `argv` for the required source and graph checks.
For source repair rollback, use `agentTransaction.rollback.actions[]` to choose
the rollback action and `agentTransaction.rollback.restoreFields` to locate the
patch path, line, and old text instead of inferring rollback data from prose.
After restoring source, run every required command in
`agentTransaction.rollback.verificationCommands`; rollback proofs use
`rollback-source-check` and `rollback-graph-check` purposes.
For successful source repairs, run the listed `zero check --json` and
`zero graph check --json` commands before continuing the repair loop.
Each repair transaction verification command includes `purpose`, `required`,
and `argv`; execute every required command and record whether the proof is a
`source-check` or `graph-check`.
`zero graph check --json` includes `agentCommand`; record its canonical argv,
`stateFields`, and `diagnosticFields` as the semantic verification proof for the
repair. If graph check reports diagnostics, use its `recommendedNextCommands[]`
`graph-inspect` follow-up to inspect the failing ProgramGraph artifact without
reconstructing target/profile argv.
For derived graph artifacts, `zero graph validate --json` also includes
`agentCommand`; record its `stateFields` and saved artifact fields before
running graph check.
Use `agentTransaction.phaseAudit` to see whether inspect/plan completed, whether
a source patch is ready or unsupported, whether preconditions validated, whether
rewrite was applied, and whether verification commands were scheduled.
When present, prefer `fixes[].graphPatchCandidates[]` for Agent-owned edits.
Each candidate gives a `zero graph patch --json` command shape, graph hash,
node id, expected value, patch text, preconditions, evidence, and verification
commands. Inspect `evidence.diagnosticSpan`, `evidence.targetNode`, and
`evidence.typedValues` before applying the patch; these fields explain which
diagnostic span, semantic node, and inferred values the compiler used to build
the candidate. Write the patch text to a temporary patch file, submit it through
graph patch, then rerun the listed verification commands.
Read `candidateContract` on each graph patch candidate before execution. It
names the patch kind, structured command field, patch text field, state fields,
audit fields, and the graph patch transaction result contract.
If a candidate is stale, rely on the rejected graph patch transaction's
`agentTransaction.failure.retryCommands` instead of reusing the old patch.
Current graph patch candidates cover supported mutability, parameter annotation,
binding annotation, field default annotation, return annotation, and missing-symbol
repairs such as `make-binding-mutable`, `match-binding-annotation`, a narrow
`TYP001` parameter annotation candidate, a narrow `TYP003` return annotation
candidate, and `NAM003` callee repair candidates. For unresolved typed calls,
prefer `replaceCallee` when the graph proves exactly one existing function has
the required return type and parameter types. Otherwise, typed calls such as
`let value: i32 = missing()`, `let value: i32 = missing(input)`, or
`return missing(input)` inside a typed function may use `addFunction`; it
includes `params` only when call argument types are inferable from graph facts,
literals, same-function parameters, or prior same-function `let` bindings, and
intentionally does not guess signatures for non-inferable arguments.

## Diagnostic Shape

Important fields from `zero check --json`:

- `code`: stable diagnostic code such as `NAM003` or `TAR002`
- `message`: short human summary
- `path`, `line`, `column`, `length`: source span
- `expected` and `actual`: structured mismatch facts when available
- `help`: concise next action
- `fixSafety`: safety label for an agent repair
- `repair`: optional repair id and summary
- `graphLookup`: ProgramGraph lookup commands and span/node fields for mapping
  the diagnostic to semantic graph facts
- `related`: extra spans or facts

When diagnostics include `graphLookup.commands`, replay those `argv` values
directly. They preserve non-default `--target` and `--profile` context from the
failing check or fix command. Each lookup command also includes `purpose` and
`required`; use `graph-inspect` for semantic lookup and `graph-check` for
verification without parsing command strings.

Successful `zero check --json` reports also include `agentCommand`, the stable
command contract for replaying the same check. Use `agentCommand.command.argv`
instead of reconstructing flags, review `auditFields` and `diagnosticFields` for
the machine fields this contract commits to, and rerun the listed
`verificationCommands` after a repair. The check verification command includes
`purpose: "source-check"` and `required: true`.
When `ok: false`, prefer `agentCommand.recommendedNextCommands[]` to choose the
next compiler-owned step. `repair-plan` advertises the transaction proof ledger,
rollback actions, retry commands, graph patch candidates, and diagnostic graph
lookup result fields; `graph-inspect` advertises graph hash, node id, node hash,
and follow-up command fields.

Do not scrape terminal prose for automation. Use JSON when a script or tool needs stable fields.

## Fix Safety

`zero fix --plan --json` reports `safetyLevels` and per-fix `safety`:

- `format-only`: formatting or trivia only
- `behavior-preserving`: intended not to change runtime behavior
- `api-changing`: signatures, exports, or call sites may change
- `target-changing`: target support or capability use may change
- `requires-human-review`: the compiler cannot prove the edit is safe

Apply only the edit you can justify from the source and fix plan. Treat `requires-human-review` as a planning hint, not an automatic patch.

## Common Codes

- `NAM003`: unknown name; declare it, import it, or fix spelling. When the
  graph proves a typed unresolved call has exactly one existing same-signature
  replacement, prefer `replaceCallee`; when no replacement is proven but result
  and argument types are inferable, prefer `addFunction` over a line patch.
- `TYP001`: call argument type mismatch. When the graph proves a direct call
  resolves to one function, the argument type matches diagnostic `actual`, and
  the corresponding parameter type matches `expected`, a `changeParamType` graph
  patch candidate may be available. Argument type proof may come from typed
  literals, same-function parameters, or prior same-function `let` bindings;
  review it as a signature/API change.
- `TYP003`: return expression type does not match the function return annotation.
  When the graph proves the returned expression type matches the diagnostic
  `actual`, a `changeReturnType` graph patch candidate may be available. Return
  expression type proof may come from typed literals, same-function parameters,
  prior same-function `let` bindings, or known stdlib helper return signatures;
  review it as a signature/API change.
- `TYP002`: local binding or field default type mismatch. When the graph proves
  the initializer/default expression type matches diagnostic `actual`, prefer
  `changeLocalType` or `changeFieldType` graph patch candidates over line patches.
  Local initializer proof may come from typed literals, same-function parameters,
  prior same-function `let` bindings, or known stdlib helper return signatures.
- `IMP001`: unknown package-local import.
- `IMP002`: package-local import cycle.
- `PKG001`: local dependency path lacks `zero.json`.
- `PKG002`: package dependency cycle.
- `PKG003`: one package name resolves to conflicting versions.
- `PKG004`: selected target is not supported by a dependency.
- `TAR001`: unknown target; JSON command failures require `zero targets --json`
  before retrying with a `<supported-target>` value. Preserve reported repair
  modes and graph patch inputs from the retry argv.
- `TAR002`: capability unavailable for selected target.
- `BLD003`: removed backend flag; use direct emitters.
- `STD002`: unknown standard-library helper; use a documented `std.<module>.<helper>` name.
- `STD003`: standard-library capability or contract mismatch; inspect the helper signature and required capability.
- `TYP009`: immutable value used where a mutable destination is required; make the binding `var` or pass mutable storage.

## Agent Triage

1. Run the failing command normally first.
2. If the readable output is not enough, rerun with `--json` and use
   `diagnostics[].graphLookup` to inspect the matching ProgramGraph span or
   one-hop node neighborhood before reading broader source.
3. Run `zero explain <code>` before broad refactors.
4. If multiple diagnostics share a root cause, fix the earliest source issue.
5. Re-run the same command after the patch.
