---
name: graph
description: Use ProgramGraph commands as the primary agent authoring and inspection surface.
---

# Zero Graph Authoring

Use this when an agent needs to inspect, plan, patch, or validate Zero changes through the graph interface. The graph interface is the primary agent authoring surface. `.0` files are canonical source text; `.program-graph` artifacts are derived debug and interchange files.

## Source Boundary

- Commit `.0` source text for durable program changes.
- Use `zero graph view <input>` to render canonical source text from a source file or ProgramGraph artifact.
- Use `zero graph patch <file.0> ...` for source-backed graph edits that rewrite the canonical source after validation.
- Write explicit graph artifacts only when you need an interchange/debug file, using non-source paths such as `.zero/agent/app.program-graph`.
- Use `zero graph status <input>` to inspect repository graph sync readiness. `zero graph sync --from-source <input>` writes the repository `zero.graph` store from current `.0` source; `zero graph sync --from-graph <input>` rewrites stale `.0` source projections from that store; `zero graph verify-sync <input>` checks graph/source sync without writing; `zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <input>` combines independent repository graph store edits and reports graph-addressed conflicts.
When using `zero graph view --json`, read `agentCommand.artifact.viewField`
for inline source text and `agentCommand.artifact.pathField` /
`byteStableField` when `--out` writes canonical `.0` text.
Replay `agentCommand.command.argv` for view audits; the contract preserves the
selected `--target` and `--profile` context.

## Graph-First Loop

Inspect the source through the graph interface:

```sh
zero graph view <file-or-package>
zero graph check <file-or-package>
zero graph status <file-or-package>
zero graph dump --json <file-or-package>
```

Create a derived graph artifact when you need to carry a graph between tools:

```sh
zero graph dump --out .zero/agent/app.program-graph <file-or-package>
```

When importing source into a derived graph artifact, prefer
`zero graph import --json --out <artifact> <input>` and read `agentCommand`.
Use `agentCommand.stateFields` for the source file, graph hash, counts, and
validation state, and `agentCommand.verificationCommands` to replay import and
validate the saved artifact. When `saved.path` is present, use
`agentCommand.recommendedNextCommands[]` for optional `graph-view` and
`graph-roundtrip` follow-ups bound to that saved path.
Replay those commands directly; they preserve the selected `--target` and
`--profile` context. Import verification uses `purpose: "graph-import"` for
the source-to-graph transaction and `purpose: "artifact-validate"` for the
saved artifact proof.
For `graph import/view/roundtrip/validate --json --out`, read
`agentCommand.writePolicy`: it declares `writes-artifact`, maps the artifact and
rollback path to `saved.path`, and requires replaying
`agentCommand.verificationCommands`. Without `--out`, these commands report
`writePolicy.name: "preview-only"` so agents can audit without treating inline
views as filesystem writes.
`zero graph dump --json --out` follows the same artifact-write contract:
read `saved.path`, `agentCommand.writePolicy.rollbackField`, and
`agentCommand.verificationCommands` instead of inferring cleanup from the
`--out` argument. Plain `zero graph dump --json` reports
`writePolicy.name: "preview-only"`.
Both graph dump forms declare an optional `graph-view` follow-up in
`agentCommand.recommendedNextCommands[]`; use its argv when a token-bounded
view of the dump or saved ProgramGraph path is needed instead of inventing a
`zero graph view --json` command from prose.
If a graph command fails because a `.program-graph` artifact is missing or
unreadable, read `agentCommand.failure.retryCommands[]`. The recovery purpose is
`graph-artifact-create`, with argv
`zero graph dump --json --out <missing-artifact> <source-or-package>`.
If a source-level `build`, `size`, `ship`, or `test` command receives a
`.program-graph` input, follow the reported `use-graph-artifact-command` retry
instead of repeating the source command; it reroutes to `zero graph <command>`
and preserves target/profile/output flags.
If a graph subcommand that does not write files is invoked with `--out`, use
`agentCommand.failure.retryCommands[]` from the JSON failure. The
`correct-command-usage` retry removes unsupported `--out` while preserving
target/profile context.
For `zero graph --json` / `zero graph inspect --json`, the primary verification
command includes `purpose: "graph-inspect"` and `required: true`; for
`zero graph dump --json`, use `purpose: "graph-dump"`; for `zero graph compare
--json --against <right> <left>`, use `purpose: "graph-compare"`; for `zero
graph check --json`, use `purpose: "graph-check"`.
Without `--out`, `zero graph import --json <input>` returns the same import
report with `saved: null`; use it for preview/audit before choosing an artifact
path.

Inspect it with normal readable output:

```sh
zero graph view .zero/agent/app.program-graph
zero graph check .zero/agent/app.program-graph
zero graph roundtrip .zero/agent/app.program-graph
```

Use `zero graph roundtrip --json <input>` when a rewrite or interchange artifact
needs a stability proof. Read `agentCommand.stateFields` for the original and
roundtripped graph hashes, module identities, lowering mode, and
`comparison.ok`; `agentCommand.artifact` maps `saved` and inline `view` fields
when `--out` is used or when source text is returned for review. After a stable
roundtrip, prefer `agentCommand.recommendedNextCommands[]` for the optional
`graph-view` follow-up bound to `saved.path` or the original graph input.
For package or manifest inputs, replay `agentCommand.command.argv`; `sourceFile`
may name the resolved entry `.0`, while the command argv preserves package
context.
Roundtrip and validate replay commands also preserve target/profile context, so
do not reconstruct them from `sourceFile` or `artifact`. Roundtrip verification
uses `purpose: "graph-roundtrip"`; validate verification uses
`purpose: "artifact-validate"`; view verification uses `purpose: "graph-view"`.
After `zero graph validate --json`, prefer its optional `graph-view` follow-up
when the validated artifact needs a token-bounded source view; use the declared
`saved.path or command.argv input` placeholder instead of reconstructing argv
from `saved` or the original artifact path.
After `zero graph view --json`, prefer its optional `graph-check` follow-up when
the reviewed source view needs target-aware validation; keep the command bound
to the original ProgramGraph input rather than the rendered `.0` view file.
After `zero graph check --json` succeeds, prefer its optional `graph-size`
follow-up when size or build budget review is needed before emitting artifacts.
Use `zero graph compare --json --against <right> <left>` after a refactor or
source-backed graph edit when you need an independent semantic equality proof
between before and after ProgramGraph states. When it reports
`semanticStable: true`, read `agentCommand.recommendedNextCommands[]`; the
optional `graph-view` follow-up declares result fields for `graphHash`,
`moduleIdentity`, `view`, and `agentCommand.verificationCommands`.
When it reports `semanticStable: false`, use the additional optional
`graph-view` follow-up bound to the left or right input to inspect either side
from a compiler-declared argv before rebuilding the patch or retrying the
refactor.
For artifact build and size work, `zero graph size --json` and
`zero graph build --json` include graph-specific `agentCommand` contracts. Use
their canonical graph argv for replay, state fields for graph hash and lowering
audits, artifact fields for `artifactPath`/`artifactBytes`, and verification
commands to re-run graph check/size without falling back to source-level build
commands.
After `zero graph size --json`, prefer `agentCommand.recommendedNextCommands[]`
for the optional `artifact-validate` graph-build follow-up when a runnable
artifact is required; it preserves target/profile placeholders and returns
artifact path, size, hash, write policy, and verification commands.
After `zero graph build --json`, prefer `agentCommand.recommendedNextCommands[]`
for the optional `graph-size` budget follow-up and optional `graph-test`
behavioral follow-up; both preserve target/profile with `<same-target>` and
`<same-profile>` placeholders. `graph-size` reports `profileBudget`/
`sizeBreakdown` evidence, and `graph-test` reports `passedTests`,
`failedTests`, `unexpectedPasses`, `results[]`, and verification commands.
For release previews from a graph artifact, use `zero graph ship --json`. Its
`agentCommand` keeps replay and verification on graph commands, and maps
release artifact plus checksum fields for audit/provenance records.
After `zero graph ship --json`, prefer `agentCommand.recommendedNextCommands[]`
for the optional `graph-size` release-budget follow-up and optional `graph-test`
behavioral follow-up instead of reconstructing the target/profile argv from the
release preview.
For artifact test work, `zero graph test --json` also includes a graph-specific
`agentCommand`. Use its failure fields for test location and failure spans, and
its verification commands to rerun graph check plus the same filtered graph test
against the current ProgramGraph artifact.

Use JSON when you need exact node IDs, graph hashes, spans, or operation fields:

```sh
zero graph dump --json <file-or-package>
zero graph source-map --json <file-or-package>
zero graph inspect --json <file-or-package>
zero graph patch --json <file.0> --expect-graph-hash graph:a7f7e6899a73f3b4 --op 'rename node="#decl_ad8d9028" expect="main" value="start"'
```

Use source maps when a tool needs to connect graph nodes back to source ranges:

```sh
zero graph source-map --json <file-or-package>
```

When a human has edited source after an agent captured a graph, reconcile the prior graph with the edited source before relying on old node IDs:

```sh
zero graph dump --out .zero/agent/app.before.program-graph <file-or-package>
zero graph reconcile --json .zero/agent/app.before.program-graph --source <file-or-package>
```

`zero graph reconcile` reports unchanged, edited, inserted, deleted, ambiguous, and identity-changed nodes. Ambiguous identity matches fail instead of silently assigning a stale node handle.
Read `agentQuery.lookupSurfaces` from `zero graph inspect --json` before doing a
token-sensitive lookup. It names the stable command and JSON fields for
symbol-to-node lookup through
`zero graph find --json --symbol <symbol-or-name> <input>`,
edit-impact lookup through `zero graph impact --json --node <node-id> <input>`,
compiler-produced one-hop node neighborhoods through `zero graph slice --json
--node <node-id> <input>`, capability lookup through `zero graph --json`, and
diagnostic lookup through `zero check --json` plus `zero fix --plan --json`.
Use `agentQuery.lookupCommandContracts.*.argv` for executable lookups; every
lookup argv contract carries `--target <same-target> --profile <same-profile>`
so target-aware inspect results do not lead to reconstructed bare lookup
commands.
`agentCommand.recommendedNextCommands[]` also declares the optional
`graph-slice` follow-up bound to `programGraph.nodes[].id`; replay that argv
directly after selecting a node from span, symbol, diagnostic, or test-failure
evidence.
Rich graph query reports also include `agentCommand`. Use
`agentCommand.command.argv` as the canonical replay command for the query, and
audit `agentCommand.readPolicy.name: "semantic-graph-query-read"` plus
`tokenStrategy` before using bounded graph JSON instead of full source. Use
`agentCommand.stateFields` to locate graph hashes, node ids, node hashes, and
symbol ids that must be carried into checked edits. For `zero graph find --json`,
read `agentCommand.recommendedNextCommands[]`; the optional `graph-slice`
follow-up declares `when: "resolution.requiresFollowupSlice"`, binds its
`--node` argument to `matches[].id`, and advertises result fields such as
`center.nodeHash` and `agentCommand.verificationCommands`. For ambiguous
results, use the additional `graph-slice` follow-up with
`when: "resolution.ambiguous"` and `inputField: "matches[].id"` to inspect
candidates before choosing an edit target. For not-found results, use the
additional `graph-inspect` follow-up with `when: "resolution.status ==
not-found"` to return to compiler-reported symbol ids and node hashes before
trying another lookup.
When starting from `zero parse --json`, use parse
`agentCommand.recommendedNextCommands[]` for the optional `graph-inspect`
follow-up once declaration summaries are insufficient. That keeps token-cheap
source understanding connected to ProgramGraph inspection without rebuilding
argv by hand.
For `zero graph impact --json`, read `agentCommand.recommendedNextCommands[]`
after checking `editGuards[]`; the optional `graph-patch` follow-up declares
`resultContract: "agentTransaction"` and result fields for the proof ledger,
rollback actions, transaction retry commands, operation retry commands, and
verification commands. Its argv preserves the query target/profile with
`--target <same-target> --profile <same-profile>`, so do not rebuild a bare
patch command after a target-aware impact query. If `zero graph impact --json` fails because the node id
was not found, use the optional command-level `graph-inspect` follow-up to
refresh graph hashes, node ids, symbol ids, and node hashes before retrying the
impact query.
For `zero graph slice --json`, read `agentCommand.recommendedNextCommands[]`
after reviewing the one-hop neighborhood. Use the optional `graph-impact`
follow-up bound to `center.id` before patching when call sites, references, or
`editGuards[]` need an independent audit. The optional `graph-patch` follow-up
then binds its edit target to `center.id`, declares the `"agentTransaction"`
result contract, and advertises proof ledger, rollback, retry, and verification
fields. Its argv also preserves `--target <same-target> --profile
<same-profile>` for the checked graph patch transaction. If `zero graph slice --json` fails because the node id was not found,
use the optional command-level `graph-inspect` follow-up instead of guessing a
replacement node id from source text.
Read `agentQuery.repairLoop` for the compiler-owned minimal repair loop:
diagnostics, repair plan, graph inspect, checked graph patch, transaction
verification commands, and transaction retry commands.
Use `repairLoop.commandContracts.*.argv` instead of parsing command strings, and
carry `repairLoop.stateFields` such as graph hashes across tool calls. The
repair loop argv contracts preserve `--target <same-target> --profile
<same-profile>` across check, plan, inspect, and checked graph patch steps.
For diagnostic-driven graph lookup, use `diagnostics[].graphLookup.commands`
and match `spanMatch` against `programGraph.nodes[].path/line/column`. Each
lookup command includes `purpose`, `required`, and `argv`; use `graph-inspect`
for node neighborhood facts and `graph-check` for semantic verification.
Failed `zero check --json` also exposes command-level
`agentCommand.recommendedNextCommands[]` entries for `repair-plan` and
`graph-inspect`, so route from the check report before reconstructing any graph
or repair command. Failed `zero graph check --json` exposes a `graph-inspect`
follow-up for the same target/profile graph artifact.
If `zero graph inspect --json` or `zero graph dump --json` fails after parsing
but before typecheck succeeds, read `agentRecovery` and
`recoverableProgramGraph`. That graph is marked `graphCompleteness:
parse-level`; use it for diagnostic span lookup and patch planning, then verify
with `agentRecovery.verificationCommands` after the edit. Replay those
verification commands directly; they preserve non-default `--target` and
`--profile` context from the graph command that produced the recoverable graph.
Use their `purpose` and `required` fields to execute all required `source-check`
and `graph-check` proofs.
For capability work, read `agentLookupIndexes.capabilities[]` from
`zero graph --json`; each item maps a capability to the program functions and
stdlib helpers that require it.

## Patches

Graph patches are checked against the input graph hash and node facts:

```sh
zero graph patch \
  <file.0> \
  --expect-graph-hash graph:a7f7e6899a73f3b4 \
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello agent\n"'
```

For larger edits, use a patch file:

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello agent\n"
rename node="#decl_ad8d9028" expect="main" value="start"
expect graphHash "graph:f76987e99677f1b3"
set node="#610c78bf" field="value" expect="hello\n" value="hello agent\n"
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
removeImport name="std.mem" expect="std.mem"
replaceImport name="std.mem" expect="std.mem" value="std.ascii"
renameImportAlias name="std.mem" expect="memory" value="buffer"
changeReturnType node="#48a1ed3f" expect="i32" value="i64"
changeParamType node="#4c21b7e2" expect="i32" value="i64"
changeLocalType node="#23d497a1" expect="i32" value="i64"
changeFieldType node="#3b9f241e" expect="i32" value="i64"
delete node="#patch001"
```

Prefer `set`, `renameSymbol`, `renameParam`, `renameLocal`, `renameField`, `rename`, `replaceCallee`, `changeReturnType`, `changeParamType`, `changeLocalType`, `changeFieldType`, `addImport`, `addFunction`, `addParam`, `removeParam`, `removeImport`, `replaceImport`, `renameImportAlias`, `insert`, and `delete` patches over editing graph artifact text by hand. Always include `expect graphHash` when you are carrying a patch across tool calls.
`renameParam` updates a Param declaration and matching Identifier uses inside
the owning function body.
`renameLocal` updates a Let declaration and matching Identifier uses inside the
owning function body when the compiler can prove there is no parameter/local
name conflict or ambiguous shadowing boundary.
`renameField` updates a Shape Field declaration, matching FieldAccess nodes
whose receiver type matches the owning shape, and matching FieldInit nodes in
shape literals for the owning shape.
`addFunction` creates a Function node, optional Param plus TypeRef children from
`params="name:Type,..."`, returnType TypeRef, body Block, and for non-Void
functions a Return plus Literal child. Use it for compiler-mediated missing
implementation stubs before broadening signatures.
For a `NAM003` typed missing-function call, `zero fix --plan --json` may emit an
`addFunction` candidate with the module parent, expected graph hash, expected
module id, return type from a typed `let` or return context, and conservative
default literal. For typed calls with arguments proven from literals,
same-function parameters, or prior same-function `let` bindings, the candidate
includes `params`, such as `params="arg0:i32"` for `missing(41)` or
`missing(input)` when `input` is known to be `i32`.
`addParam` creates a Param plus TypeRef on a Function and can update direct Call
arg lists with a supplied literal `value`, keeping signature changes transactional.
`removeParam` removes an unused Param plus TypeRef and same-order direct Call
args. It rejects removal when the parameter is still referenced in the function
body.
`changeParamType` updates the Param `type`, its `type` TypeRef child, and
matching parameter Identifier type facts inside the owning function body.
`changeLocalType` updates a Let `type`, its `declaredType` TypeRef child,
matching initializer expression type facts, and matching local Identifier type
facts after the binding.
`changeFieldType` updates a Shape Field `type`, its `type` TypeRef child,
matching FieldAccess type facts for the owning shape, field default expression
type facts, and matching FieldInit value type facts in shape literals for the
owning shape.
`addImport name="std.mem"` and other `std.*` imports can be source-backed
transactions on canonical `.0` files: the compiler rewrites, reparses, compares
the graph, and returns source rollback plus verification commands. Package-local
multi-module imports require the target Module to already be present in the
patched graph; do not guess a single-file package import transaction.
In `zero graph patch --json`, read `agentTransaction.patchContract` before
interpreting the result. It identifies `operations` as the audited patch item
list, gives required inputs, repeats graph/node/expect preconditions, and names
the result fields agents should use for review and retry planning.
`zero graph patch --json` can apply source-backed checked edits to parseable
programs that still have typecheck diagnostics. The patch transaction verifies
the graph hash, node facts, canonical rewrite, reparse, and graph stability;
then use `agentTransaction.verificationCommands` to continue the repair loop.
Before patching, read `agentQuery.checkedEditSurface.transactionContract` from
`zero graph inspect --json`. It declares graph patch transaction phases, stable
`agentTransaction` result fields, failure classes, `failure.retryCommands`,
rollback `savedKind`, rollback `actions[]`, and the source/artifact
verification command families.
Its rollback contract includes structured `sourceVerificationCommands` and
`artifactVerificationCommands`; use those command arrays instead of inventing
rollback proof commands.
Each `zero graph patch --json` result also includes
`agentTransaction.resultFields`; use it to locate stable audit fields such as
`command.argv`, `originalGraphHash`, `patchedGraphHash`, `operationCount`,
`patchContract`, `rollback.savedPath`, `rollback.applied`,
`rollback.actions[]`, `rollback.verificationCommands`, `verificationCommands`,
`verificationCommands[].purpose`, `verificationCommands[].required`,
`verificationCommands[].resultFields`,
`rollback.verificationCommands[].resultFields`,
`saved.graphHash`, `saveProof.semanticStable`, `saveProof.comparedGraphHash`,
`evidenceBindings[]`, `evidenceBindings[].sourceFields`, `failure.code`,
`failure.class`, `failure.retryCommands`,
`failure.retryCommands[].purpose`, and `failure.retryCommands[].required`
without scraping samples.
Use `evidenceBindings[]` to audit why each operation was accepted: each binding
ties `operationIndex`, `op`, `graphHash`, `node`, `field`, `expect`, `actual`,
and `value` back to compiler-produced source fields such as
`originalGraphHash` and `operations[].actual`. Prefer these bindings over
re-reading source text when checking that a patch was based on graph facts
rather than guessed text edits.
Input validation, graph loading, patch validation, and save failures all return
`agentTransaction`; do not fall back to diagnostics-only retry handling for
`zero graph patch --json`.
Graph patch results also include `agentTransaction.failureClasses`, matching the
inspect contract's `GPH001` through `GPH006` classes, so classify failures from
that table before choosing a retry. For executable retry templates, use
`agentTransaction.retryCommandContracts` or
`agentQuery.checkedEditSurface.transactionContract.retryCommandContracts`
instead of parsing `failureClasses[].retryCommand`; the retry argv contracts
preserve `--target <same-target> --profile <same-profile>` where a graph
inspect or graph dump retry is needed.
After patching, inspect `agentTransaction.phaseAudit` to see which transaction
phases completed. `verify.status` is `scheduled` only when verification commands
were returned for a saved source or artifact.
Each returned verification command includes `purpose`, `required`, and `argv`;
use `purpose` to distinguish `source-check`, `graph-check`, and
`artifact-validate`, and execute every command where `required` is true. Use
`verificationCommands[].resultFields` to decide which proof fields to collect:
source checks report `ok` and `diagnostics`, graph checks also report
`graphHash`, and artifact validation reports `ok`, `graphHash`, and
`validation.ok`.
If you roll back a saved source or artifact, choose the action from
`agentTransaction.rollback.actions[]`, then run every required command in
`agentTransaction.rollback.verificationCommands`; rollback purposes distinguish
source checks, graph checks, and artifact validation after restoration. Rollback
verification commands declare the same `resultFields` arrays as the forward
verification commands.
Before any patch is applied, the same command families are available from
`agentQuery.checkedEditSurface.transactionContract.rollback`.
On failure, prefer the structured retry command over prose: stale graph hashes
retry with `zero graph dump --json <input>`, while invalid patch input,
including unreadable or syntactically invalid patch files, invalid typed values,
or missing targets retry with
`zero graph inspect --json <input>`. Failed preconditions (`GPH005`) may also
carry operation-level `operations[].retryCommands`; when present, run those
first. They use `graph-impact` to refresh the failed node's call sites,
references, and edit guards before rebuilding the patch.
Retry commands also include `purpose`, `required`, and `argv`; use `graph-dump`
to refresh stale graph state, `graph-inspect` to rebuild a checked patch from
current node facts, and `graph-impact` to explain operation-level precondition
failures without rereading the full source.
Check `failure.retryable` before retrying; `GPH006` validate failures are not
auto-retryable and require compiler/tooling repair or human review.
When `zero graph patch --json` is run with a non-default `--target` or
`--profile`, replay `agentTransaction.command.argv`,
`agentTransaction.verificationCommands`, and
`agentTransaction.failure.retryCommands` directly; they preserve the target and
profile context for checked graph edits.

## Validate Before Persisting

After a source-backed patch, validate the source:

```sh
zero graph check <file.0>
zero check <file.0>
```

When `zero graph status <input>` reports repository graph sync as enabled,
verify graph/source sync when drift must fail the workflow:

```sh
zero graph verify-sync <file-or-package>
zero graph sync --from-source <file-or-package>
zero graph sync --from-graph <file-or-package>
zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <file-or-package>
```

In the current compiler, `sync --from-source` updates `zero.graph` from source
text, preserves existing graph node handles where the source edit is
unambiguous, and stores exact checked-in `.0` projection bytes for tracked local
files. Ambiguous identity changes fail instead of guessing. `sync --from-graph`
updates stale checked-in `.0` projections from `zero.graph`, and `verify-sync`
compares the store with the current source graph and source projection.
When a package manifest sets `repositoryGraph.compilerInput` to `true`,
normal compiler commands validate and compile from the graph store, including
target and package metadata, so source-free graph packages can still be checked,
built, run, tested, sized, shipped, and inspected. Commands report whether the
source projection is clean, missing, stale, conflicting, or unavailable, but do
not rewrite `.0` files. Use `zero graph verify-sync` when graph/source drift
must fail the workflow. Without that marker, normal commands use checked-in
`.0` source text.
`merge` writes only the target `zero.graph` when independent node-hash edits can
be combined; it does not rewrite `.0` projections, so run `sync --from-graph`
after a successful merge when source projections should be refreshed.
In the Zero repository, `pnpm run repository-graph:check` verifies checked-in
`zero.graph` stores for CI with the pinned `linux-musl-x64` graph target.
When validating through JSON, read `zero graph check --json` `agentCommand`.
Use `agentCommand.command.argv` for audit logs, `stateFields` for `graphHash`
and direct-graph lowering facts, and `diagnosticFields` if verification fails.

For derived graph artifacts, validate the artifact before applying the accepted change to source:

```sh
zero graph validate .zero/agent/app.patched.program-graph
zero graph check .zero/agent/app.patched.program-graph
zero graph view .zero/agent/app.patched.program-graph
```

For artifact validation JSON, read `zero graph validate --json` `agentCommand`.
Use `stateFields` to audit `graphHash`, counts, and validation state, and
`agentCommand.artifact` to find `saved.path` and `saved.byteStable` when
normalizing a derived graph artifact.

Do not commit `.program-graph` files unless the user explicitly asks for derived artifacts.

## Packages

For packages, inspect the graph from the package root or manifest. Only write an artifact when another tool needs a file transfer:

```sh
zero graph view <package-dir>
zero graph check <package-dir>
zero graph import --out .zero/agent/package.program-graph <package-dir>
```

If `zero.json` sets `repositoryGraph.compilerInput` to `true`, those commands
use the checked-in `zero.graph` store and report source projection state without
rewriting `.0` files. Otherwise, normal build, run, test, and ship commands use
canonical source unless the command is explicitly `zero graph ...`.
