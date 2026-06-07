import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const outDir = join(".zero", "agent-repair-transaction-smoke", String(process.pid));
const executedProofs = [];

async function zeroJson(args: string[], options: { allowFailure?: boolean } = {}) {
  const result = await execFileAsync(zero, args, { maxBuffer: 4 * 1024 * 1024 }).catch((error) => error);
  if (!options.allowFailure && result.code) throw result;
  return JSON.parse(result.stdout);
}

function assertCommand(command, purpose: string, argv: string[], required = true) {
  assert.equal(command.purpose, purpose);
  assert.equal(command.required, required);
  assert.deepEqual(command.argv, argv);
}

function ledgerPhase(transaction, phase: string) {
  const found = transaction.proofLedger?.phases?.find((item) => item.phase === phase);
  assert(found, `missing proofLedger phase ${phase}`);
  return found;
}

function purposeSet(commands) {
  return commands.map((command) => command.purpose).sort();
}

function assertRepairRollbackAction(transaction) {
  assert(transaction.resultFields.includes("agentTransaction.rollback.actions[]"));
  assert(transaction.resultFields.includes("agentTransaction.rollback.actions[].kind"));
  assert(transaction.resultFields.includes("agentTransaction.rollback.actions[].pathField"));
  assert(transaction.resultFields.includes("agentTransaction.rollback.actions[].oldTextField"));
  const action = transaction.rollback.actions[0];
  assert.equal(action.kind, "restore-line");
  assert.equal(action.pathField, "patches[].path");
  assert.equal(action.lineField, "patches[].line");
  assert.equal(action.oldTextField, "patches[].old");
  assert.equal(action.condition, "before-retry-or-after-failed-apply");
  assert.equal(action.requiresVerification, true);
  return action;
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const results = [];
  for (const command of commands) {
    assert.equal(typeof command.purpose, "string");
    assert(command.purpose.length > 0);
    assert.equal(typeof command.required, "boolean");
    assert(Array.isArray(command.argv));
    if (!command.required) continue;
    assert.equal(command.argv[0], "zero");
    const body = await zeroJson(command.argv.slice(1), { allowFailure: true });
    const proof = { purpose: command.purpose, ok: body.ok ?? true, diagnostics: body.diagnostics ?? [] };
    results.push(proof);
    executedProofs.push(proof);
  }
  assert.equal(results.length, commands.filter((command) => command.required).length);
  return results;
}

await rm(outDir, { recursive: true, force: true });
await mkdir(outDir, { recursive: true });

const workFile = join(outDir, "main.0");
await writeFile(workFile, await readFile("examples/agent-repair-demo/broken.0", "utf8"));

const checkDiagnostic = (await zeroJson(["check", "--json", workFile], { allowFailure: true })).diagnostics[0];
assert.equal(checkDiagnostic.repair.agentRepair.kind, "diagnostic-repair-entrypoint");
assert.equal(checkDiagnostic.repair.agentRepair.safeDefault, "plan");
assert.equal(checkDiagnostic.repair.agentRepair.transactionContract, "agentTransaction");
assertCommand(checkDiagnostic.repair.agentRepair.commands[0], "repair-plan", ["zero", "fix", "--plan", "--json", workFile]);
assertCommand(checkDiagnostic.repair.agentRepair.commands[1], "repair-patch", ["zero", "fix", "--patch", "--json", workFile], false);
assertCommand(checkDiagnostic.repair.agentRepair.commands[2], "repair-apply", ["zero", "fix", "--apply", "--json", workFile], false);
assert(checkDiagnostic.repair.agentRepair.resultFields.includes("agentTransaction.verificationCommands"));
assert(checkDiagnostic.repair.agentRepair.resultFields.includes("agentTransaction.failure.retryCommands"));
assert(checkDiagnostic.repair.agentRepair.resultFields.includes("agentTransaction.proofLedger"));
assert(checkDiagnostic.repair.agentRepair.resultFields.includes("agentTransaction.proofLedger.phases[].phase"));
assert(checkDiagnostic.repair.agentRepair.resultFields.includes("agentTransaction.proofLedger.phases[].status"));

const firstPlan = await zeroJson(["fix", "--plan", "--json", workFile]);
assert.equal(firstPlan.ok, false);
assert.equal(firstPlan.agentTransaction.kind, "compiler-mediated-repair-transaction");
assert.equal(firstPlan.agentTransaction.mode, "plan");
assert.equal(firstPlan.agentTransaction.ok, true);
assert.equal(firstPlan.agentTransaction.checkedEditSurface, "plan-only");
assert.equal(firstPlan.agentTransaction.writePolicy, "preview-only");
assert.equal(firstPlan.agentTransaction.failure, null);
assert.equal(firstPlan.agentTransaction.repairId, "make-binding-mutable");
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.input"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.command.argv"));
assert.deepEqual(firstPlan.agentTransaction.command.argv, ["zero", "fix", "--plan", "--json", workFile]);
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.diagnosticCode"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.repairId"));
assert.deepEqual(firstPlan.agentTransaction.phases, ["inspect", "plan", "patch", "validate", "rewrite", "verify"]);
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].phase"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].status"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].evidenceFields"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].requiredPurposes"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.failure.class"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.failure.retryCommands"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.failure.retryCommands[].purpose"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.failure.retryCommands[].required"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.retryCommandContracts"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.failure.applied"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.rollback.restoreFields"));
assert(firstPlan.agentTransaction.resultFields.includes("agentTransaction.writePolicy"));
const firstPlanRollbackAction = assertRepairRollbackAction(firstPlan.agentTransaction);
assert(firstPlan.agentTransaction.failureClasses.some((item) => item.class === "unsupported-repair" && item.phase === "patch" && item.retryCommand === "zero fix --plan --json <input>"));
assert(firstPlan.agentTransaction.failureClasses.some((item) => item.class === "write-failed" && item.phase === "rewrite" && item.retryCommand === "zero fix --apply --json <input>"));
assert.deepEqual(firstPlan.agentTransaction.retryCommandContracts["source-unavailable"].argv, ["zero", "check", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(firstPlan.agentTransaction.retryCommandContracts["edit-not-found"].argv, ["zero", "check", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(firstPlan.agentTransaction.retryCommandContracts["unsupported-repair"].commands[0].argv, ["zero", "fix", "--plan", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(firstPlan.agentTransaction.retryCommandContracts["unsupported-repair"].commands[1].argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(firstPlan.agentTransaction.retryCommandContracts["write-failed"].argv, ["zero", "fix", "--apply", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.equal(firstPlan.agentTransaction.phaseAudit.inspect.ok, true);
assert.equal(firstPlan.agentTransaction.phaseAudit.plan.ok, true);
assert.equal(firstPlan.agentTransaction.phaseAudit.patch.status, "not-scheduled");
assert.equal(firstPlan.agentTransaction.phaseAudit.validate.status, "not-scheduled");
assert.equal(firstPlan.agentTransaction.phaseAudit.verify.status, "scheduled");
assert.equal(firstPlan.agentTransaction.proofLedger.schemaVersion, 1);
assert.equal(firstPlan.agentTransaction.proofLedger.kind, "repair-transaction-proof-ledger");
assert.deepEqual(firstPlan.agentTransaction.proofLedger.phaseOrder, ["inspect", "plan", "patch", "validate", "rewrite", "verify", "rollback"]);
assert.equal(ledgerPhase(firstPlan.agentTransaction, "inspect").status, "passed");
assert.equal(ledgerPhase(firstPlan.agentTransaction, "plan").status, "passed");
assert.equal(ledgerPhase(firstPlan.agentTransaction, "patch").status, "not-scheduled");
assert.equal(ledgerPhase(firstPlan.agentTransaction, "validate").status, "not-scheduled");
assert.equal(ledgerPhase(firstPlan.agentTransaction, "rewrite").status, "not-scheduled");
assert.equal(ledgerPhase(firstPlan.agentTransaction, "verify").status, "scheduled");
assert.deepEqual(ledgerPhase(firstPlan.agentTransaction, "verify").requiredPurposes.sort(), ["graph-check", "source-check"]);
assert.equal(ledgerPhase(firstPlan.agentTransaction, "rollback").status, "not-available");
assert.deepEqual(ledgerPhase(firstPlan.agentTransaction, "verify").requiredPurposes.sort(), purposeSet(firstPlan.agentTransaction.verificationCommands));
assert.equal(firstPlan.agentTransaction.proofLedger.verifiedBy, "agentTransaction.verificationCommands");
assert.equal(firstPlan.agentTransaction.patchContract.kind, "single-line-source-replacement");
assert.deepEqual(firstPlan.agentTransaction.patchContract.required, ["path", "line", "old", "new"]);
assertCommand(firstPlan.agentTransaction.verificationCommands[0], "source-check", ["zero", "check", "--json", workFile]);
assertCommand(firstPlan.agentTransaction.verificationCommands[1], "graph-check", ["zero", "graph", "check", "--json", workFile]);
assert.deepEqual((await runRequiredCommands(firstPlan.agentTransaction.verificationCommands)).map((item) => item.purpose), ["source-check", "graph-check"]);
assert.equal(firstPlan.fixes[0].appliesEdits, false);
assert.equal(firstPlan.fixes[0].repairContract.kind, "compiler-mediated-repair-contract");
assert.equal(firstPlan.fixes[0].repairContract.autoPatchSupported, true);
assert.deepEqual(firstPlan.fixes[0].repairContract.requiredInputs.slice(0, 3), ["diagnostic.code", "diagnostic.path", "diagnostic.line"]);
assert.equal(firstPlan.fixes[0].repairContract.verification[0].purpose, "source-check");
assert.equal(firstPlan.fixes[0].repairContract.verification[0].required, true);
assert.equal(firstPlan.fixes[0].repairContract.verification[1].purpose, "graph-check");
assert.equal(firstPlan.fixes[0].repairContract.verification[1].required, true);
assert.deepEqual(firstPlan.fixes[0].repairContract.verification[1].argv, ["zero", "graph", "check", "--json", "<input>"]);
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].kind, "checked-graph-patch-candidate");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].repairId, "make-binding-mutable");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].candidateContract.kind, "checked-graph-patch-candidate-contract");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].candidateContract.patchKind, "program-graph-patch-v1");
assert.deepEqual(firstPlan.fixes[0].graphPatchCandidates[0].candidateContract.commandField, "command.argv");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].candidateContract.patchTextField, "patch.text");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].candidateContract.requiresPatchFile, true);
assert(firstPlan.fixes[0].graphPatchCandidates[0].candidateContract.stateFields.includes("patch.graphHash"));
assert(firstPlan.fixes[0].graphPatchCandidates[0].candidateContract.auditFields.includes("verificationCommands"));
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].candidateContract.resultContract, "agentQuery.checkedEditSurface.transactionContract");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].evidence.diagnosticSpan.path, workFile);
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].evidence.targetNode.kind, "Let");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].evidence.typedValues.field, "mutable");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].evidence.typedValues.expected, "false");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].evidence.typedValues.actual, "true");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].patch.operations[0].op, "set");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].patch.operations[0].field, "mutable");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].patch.operations[0].expect, "false");
assert.equal(firstPlan.fixes[0].graphPatchCandidates[0].patch.operations[0].value, "true");
const firstTargetPlan = await zeroJson(["fix", "--plan", "--json", "--target", "linux-musl-x64", "--profile", "dev", workFile]);
assert.deepEqual(firstTargetPlan.fixes[0].graphPatchCandidates[0].command.argv, ["zero", "graph", "patch", "--json", "--target", "linux-musl-x64", "--profile", "dev", workFile, "<patch-file>"]);
assert.equal(firstTargetPlan.fixes[0].graphPatchCandidates[0].verificationCommands[0].purpose, "source-check");
assert.equal(firstTargetPlan.fixes[0].graphPatchCandidates[0].verificationCommands[0].required, true);
assert.deepEqual(firstTargetPlan.fixes[0].graphPatchCandidates[0].verificationCommands[0].argv, ["zero", "check", "--json", "--target", "linux-musl-x64", "--profile", "dev", workFile]);
assert.equal(firstTargetPlan.fixes[0].graphPatchCandidates[0].verificationCommands[1].purpose, "graph-check");
assert.equal(firstTargetPlan.fixes[0].graphPatchCandidates[0].verificationCommands[1].required, true);
assert.deepEqual(firstTargetPlan.fixes[0].graphPatchCandidates[0].verificationCommands[1].argv, ["zero", "graph", "check", "--json", "--target", "linux-musl-x64", "--profile", "dev", workFile]);

const missingSourceFile = join(outDir, "missing-source.0");
const missingSource = await zeroJson(["fix", "--patch", "--json", missingSourceFile], { allowFailure: true });
assert.equal(missingSource.ok, false);
assert.equal(missingSource.agentTransaction.ok, false);
assert.equal(missingSource.agentTransaction.failure.class, "source-unavailable");
assert.equal(missingSource.agentTransaction.failure.phase, "inspect");
assert.equal(missingSource.agentTransaction.failure.retryCommands[0].purpose, "source-check");
assert.equal(missingSource.agentTransaction.failure.retryCommands[0].required, true);
assert.deepEqual(missingSource.agentTransaction.failure.retryCommands[0].argv, ["zero", "check", "--json", missingSourceFile]);
assert.deepEqual((await runRequiredCommands(missingSource.agentTransaction.failure.retryCommands)).map((item) => item.purpose), ["source-check"]);
assert.equal(missingSource.agentTransaction.phaseAudit.inspect.ok, false);
assert.equal(missingSource.agentTransaction.phaseAudit.verify.status, "not-scheduled");
assert.equal(missingSource.agentTransaction.proofLedger.failureClass, "source-unavailable");
assert.equal(ledgerPhase(missingSource.agentTransaction, "inspect").status, "failed");
assert.equal(ledgerPhase(missingSource.agentTransaction, "verify").status, "not-scheduled");
assert.deepEqual(ledgerPhase(missingSource.agentTransaction, "verify").requiredPurposes, []);

const editNotFoundFile = join(outDir, "edit-not-found.0");
await writeFile(editNotFoundFile, "pub fn main() -> Void {\n    let value: Bool = 1 == true\n}\n");
const editNotFound = await zeroJson(["fix", "--patch", "--json", editNotFoundFile], { allowFailure: true });
assert.equal(editNotFound.ok, false);
assert.equal(editNotFound.agentTransaction.ok, false);
assert.equal(editNotFound.agentTransaction.failure.class, "edit-not-found");
assert.equal(editNotFound.agentTransaction.failure.phase, "patch");
assert.equal(editNotFound.agentTransaction.failure.retryCommands[0].purpose, "source-check");
assert.equal(editNotFound.agentTransaction.failure.retryCommands[0].required, true);
assert.deepEqual(editNotFound.agentTransaction.failure.retryCommands[0].argv, ["zero", "check", "--json", editNotFoundFile]);
assert.deepEqual((await runRequiredCommands(editNotFound.agentTransaction.failure.retryCommands)).map((item) => item.purpose), ["source-check"]);
assert.equal(editNotFound.agentTransaction.phaseAudit.patch.status, "not-scheduled");
assert.deepEqual(editNotFound.patches, []);

const staleCandidateFile = join(outDir, "stale-candidate.0");
const staleCandidatePatch = join(outDir, "stale-candidate.program-graph.patch");
await writeFile(staleCandidateFile, await readFile("examples/agent-repair-demo/broken.0", "utf8"));
const staleCandidatePlan = await zeroJson(["fix", "--plan", "--json", staleCandidateFile]);
const staleCandidate = staleCandidatePlan.fixes[0].graphPatchCandidates[0];
assert.equal(staleCandidate.candidateContract.resultContract, "agentQuery.checkedEditSurface.transactionContract");
await writeFile(staleCandidatePatch, `${staleCandidate.patch.text.join("\n")}\n`);
await writeFile(staleCandidateFile, (await readFile(staleCandidateFile, "utf8")).replace("    let dst:", "    var dst:"));
const staleCandidateRejected = await zeroJson(["graph", "patch", "--json", staleCandidateFile, staleCandidatePatch], { allowFailure: true });
assert.equal(staleCandidateRejected.ok, false);
assert.equal(staleCandidateRejected.agentTransaction.applied, false);
assert.equal(staleCandidateRejected.agentTransaction.failure.class, "stale-graph");
assert.equal(staleCandidateRejected.agentTransaction.failure.phase, "inspect");
assert.equal(staleCandidateRejected.agentTransaction.failure.retryCommands[0].purpose, "graph-dump");
assert.equal(staleCandidateRejected.agentTransaction.failure.retryCommands[0].required, true);
assert.deepEqual(staleCandidateRejected.agentTransaction.failure.retryCommands[0].argv, ["zero", "graph", "dump", "--json", staleCandidateFile]);
assert.deepEqual((await runRequiredCommands(staleCandidateRejected.agentTransaction.failure.retryCommands)).map((item) => item.purpose), ["graph-dump"]);

const graphRepairFile = join(outDir, "graph-repair.0");
const graphRepairPatch = join(outDir, "graph-repair.program-graph.patch");
await writeFile(graphRepairFile, await readFile("examples/agent-repair-demo/broken.0", "utf8"));
const graphRepairPlan = await zeroJson(["fix", "--plan", "--json", graphRepairFile]);
const graphPatchCandidate = graphRepairPlan.fixes[0].graphPatchCandidates[0];
assert.match(graphPatchCandidate.patch.graphHash, /^graph:/);
assert.match(graphPatchCandidate.patch.operations[0].node, /^#/);
await writeFile(graphRepairPatch, `${graphPatchCandidate.patch.text.join("\n")}\n`);
const graphPatchRepair = await zeroJson(["graph", "patch", "--json", graphRepairFile, graphRepairPatch]);
assert.equal(graphPatchRepair.ok, true);
assert.equal(graphPatchRepair.agentTransaction.applied, true);
assert.equal(graphPatchRepair.saved.kind, "source");
assert.equal(graphPatchRepair.saved.graphHash, graphPatchRepair.patchedGraphHash);
assert.equal(graphPatchRepair.saveProof.semanticStable, true);
assert.equal(graphPatchRepair.saveProof.savedGraphHash, graphPatchRepair.patchedGraphHash);
assertCommand(graphPatchRepair.agentTransaction.verificationCommands[0], "source-check", ["zero", "check", "--json", graphRepairFile]);
assertCommand(graphPatchRepair.agentTransaction.verificationCommands[1], "graph-check", ["zero", "graph", "check", "--json", graphRepairFile]);
assert.deepEqual((await runRequiredCommands(graphPatchRepair.agentTransaction.verificationCommands)).map((item) => item.purpose), ["source-check", "graph-check"]);
const graphRepairNextCheck = await zeroJson(["check", "--json", graphRepairFile], { allowFailure: true });
assert.equal(graphRepairNextCheck.ok, false);
assert.equal(graphRepairNextCheck.diagnostics[0].code, "TYP002");
const graphRepairSecondPlan = await zeroJson(["fix", "--plan", "--json", graphRepairFile]);
const graphPatchCandidate2 = graphRepairSecondPlan.fixes[0].graphPatchCandidates[0];
assert.equal(graphPatchCandidate2.repairId, "match-binding-annotation");
assert.equal(graphPatchCandidate2.patch.operations[0].op, "changeLocalType");
assert.equal(graphPatchCandidate2.patch.operations[0].expect, "i32");
assert.equal(graphPatchCandidate2.patch.operations[0].value, "usize");
assert.match(graphPatchCandidate2.patch.text[2], /^changeLocalType node="#/);
await writeFile(graphRepairPatch, `${graphPatchCandidate2.patch.text.join("\n")}\n`);
const graphPatchRepair2 = await zeroJson(["graph", "patch", "--json", graphRepairFile, graphRepairPatch]);
assert.equal(graphPatchRepair2.ok, true);
assert.equal(graphPatchRepair2.agentTransaction.applied, true);
const graphRepairFixed = await zeroJson(["check", "--json", graphRepairFile]);
assert.equal(graphRepairFixed.ok, true);
assert.equal(graphRepairFixed.diagnostics.length, 0);

const missingFunctionFile = join(outDir, "missing-function.0");
const missingFunctionPatch = join(outDir, "missing-function.program-graph.patch");
await writeFile(missingFunctionFile, "pub fn main() -> Void {\n    let value: i32 = missing()\n}\n");
const missingFunctionPlan = await zeroJson(["fix", "--plan", "--json", missingFunctionFile]);
assert.equal(missingFunctionPlan.ok, false);
assert.equal(missingFunctionPlan.diagnostics[0].code, "NAM003");
const missingFunctionCandidate = missingFunctionPlan.fixes[0].graphPatchCandidates[0];
assert.equal(missingFunctionCandidate.repairId, "declare-missing-symbol");
assert.equal(missingFunctionCandidate.candidateContract.patchKind, "program-graph-patch-v1");
assert(missingFunctionCandidate.candidateContract.stateFields.includes("evidence.targetNode.id"));
assert.equal(missingFunctionCandidate.evidence.targetNode.kind, "Module");
assert.equal(missingFunctionCandidate.evidence.typedValues.field, "returnType");
assert.equal(missingFunctionCandidate.evidence.typedValues.expected, "i32");
assert.equal(missingFunctionCandidate.patch.operations[0].op, "addFunction");
assert.equal(missingFunctionCandidate.patch.operations[0].targetKind, "Module");
assert.equal(missingFunctionCandidate.patch.operations[0].name, "missing");
assert.equal(missingFunctionCandidate.patch.operations[0].type, "i32");
assert.equal(missingFunctionCandidate.patch.operations[0].value, "0");
assert.match(missingFunctionCandidate.patch.text[2], /^addFunction parent="#/);
await writeFile(missingFunctionPatch, `${missingFunctionCandidate.patch.text.join("\n")}\n`);
const missingFunctionRepair = await zeroJson(["graph", "patch", "--json", missingFunctionFile, missingFunctionPatch]);
assert.equal(missingFunctionRepair.ok, true);
assert.equal(missingFunctionRepair.agentTransaction.applied, true);
assert.equal(missingFunctionRepair.operations[0].op, "addFunction");
assert.equal(missingFunctionRepair.operations[0].actual, missingFunctionCandidate.patch.operations[0].parent);
const missingFunctionFixed = await zeroJson(["check", "--json", missingFunctionFile]);
assert.equal(missingFunctionFixed.ok, true);

const replaceCalleeFile = join(outDir, "replace-callee.0");
const replaceCalleePatch = join(outDir, "replace-callee.program-graph.patch");
await writeFile(replaceCalleeFile, `fn existing(value: i32) -> i32 {
    return value
}

pub fn main() -> Void {
    let value: i32 = exsting(41)
}
`);
const replaceCalleePlan = await zeroJson(["fix", "--plan", "--json", replaceCalleeFile]);
assert.equal(replaceCalleePlan.diagnostics[0].code, "NAM003");
const replaceCalleeCandidate = replaceCalleePlan.fixes[0].graphPatchCandidates[0];
assert.equal(replaceCalleeCandidate.repairId, "declare-missing-symbol");
assert.equal(replaceCalleeCandidate.patch.operations[0].op, "replaceCallee");
assert.equal(replaceCalleeCandidate.patch.operations[0].targetKind, "Call");
assert.equal(replaceCalleeCandidate.patch.operations[0].expect, "exsting");
assert.equal(replaceCalleeCandidate.patch.operations[0].value, "existing");
assert.match(replaceCalleeCandidate.patch.text[2], /^replaceCallee node="#/);
await writeFile(replaceCalleePatch, `${replaceCalleeCandidate.patch.text.join("\n")}\n`);
const replaceCalleeRepair = await zeroJson(["graph", "patch", "--json", replaceCalleeFile, replaceCalleePatch]);
assert.equal(replaceCalleeRepair.ok, true);
assert.equal(replaceCalleeRepair.operations[0].op, "replaceCallee");
assert.equal(replaceCalleeRepair.operations[0].actual, "exsting");
assert.equal(replaceCalleeRepair.operations[0].value, "existing");
assert.match(await readFile(replaceCalleeFile, "utf8"), /let value: i32 = existing\(41\)/);
assert.equal((await zeroJson(["check", "--json", replaceCalleeFile])).ok, true);

const duplicateFunctionFile = join(outDir, "duplicate-function.0");
const duplicateFunctionPatch = join(outDir, "duplicate-function.program-graph.patch");
await writeFile(duplicateFunctionFile, `fn duplicate() -> i32 {
    return 1
}

fn duplicate() -> i32 {
    return 2
}

pub fn main() -> Void {
}
`);
const duplicateFunctionPlan = await zeroJson(["fix", "--plan", "--json", duplicateFunctionFile]);
assert.equal(duplicateFunctionPlan.diagnostics[0].code, "NAM004");
const duplicateFunctionCandidate = duplicateFunctionPlan.fixes[0].graphPatchCandidates[0];
assert.equal(duplicateFunctionCandidate.repairId, "remove-duplicate-declaration");
assert.equal(duplicateFunctionCandidate.patch.operations[0].op, "removeFunction");
assert.equal(duplicateFunctionCandidate.patch.operations[0].targetKind, "Function");
assert.equal(duplicateFunctionCandidate.patch.operations[0].expect, "duplicate");
assert.equal(duplicateFunctionCandidate.evidence.targetNode.kind, "Function");
assert.equal(duplicateFunctionCandidate.evidence.typedValues.field, "name");
assert(duplicateFunctionCandidate.preconditions.includes("removeFunction rejects direct call sites"));
assert.match(duplicateFunctionCandidate.patch.text[2], /^removeFunction node="#/);
await writeFile(duplicateFunctionPatch, `${duplicateFunctionCandidate.patch.text.join("\n")}\n`);
const duplicateFunctionRepair = await zeroJson(["graph", "patch", "--json", duplicateFunctionFile, duplicateFunctionPatch]);
assert.equal(duplicateFunctionRepair.ok, true);
assert.equal(duplicateFunctionRepair.operations[0].op, "removeFunction");
assert.equal(duplicateFunctionRepair.operations[0].actual, "duplicate");
assert.equal((await readFile(duplicateFunctionFile, "utf8")).match(/fn duplicate/g)?.length, 1);
assert.equal((await zeroJson(["check", "--json", duplicateFunctionFile])).ok, true);

const missingFunctionWithArgFile = join(outDir, "missing-function-with-arg.0");
const missingFunctionWithArgPatch = join(outDir, "missing-function-with-arg.program-graph.patch");
await writeFile(missingFunctionWithArgFile, "pub fn main() -> Void {\n    let value: i32 = missing(41)\n}\n");
const missingFunctionWithArgPlan = await zeroJson(["fix", "--plan", "--json", missingFunctionWithArgFile]);
assert.equal(missingFunctionWithArgPlan.diagnostics[0].code, "NAM003");
const missingFunctionWithArgCandidate = missingFunctionWithArgPlan.fixes[0].graphPatchCandidates[0];
assert.equal(missingFunctionWithArgCandidate.patch.operations[0].op, "addFunction");
assert.equal(missingFunctionWithArgCandidate.patch.operations[0].name, "missing");
assert.equal(missingFunctionWithArgCandidate.patch.operations[0].type, "i32");
assert.equal(missingFunctionWithArgCandidate.patch.operations[0].params, "arg0:i32");
assert.equal(missingFunctionWithArgCandidate.patch.operations[0].value, "0");
assert.match(missingFunctionWithArgCandidate.patch.text[2], /params="arg0:i32"/);
await writeFile(missingFunctionWithArgPatch, `${missingFunctionWithArgCandidate.patch.text.join("\n")}\n`);
const missingFunctionWithArgRepair = await zeroJson(["graph", "patch", "--json", missingFunctionWithArgFile, missingFunctionWithArgPatch]);
assert.equal(missingFunctionWithArgRepair.ok, true);
assert.equal(missingFunctionWithArgRepair.operations[0].params, "arg0:i32");
const missingFunctionWithArgFixed = await zeroJson(["check", "--json", missingFunctionWithArgFile]);
assert.equal(missingFunctionWithArgFixed.ok, true);

const missingFunctionWithTwoArgsFile = join(outDir, "missing-function-with-two-args.0");
const missingFunctionWithTwoArgsPatch = join(outDir, "missing-function-with-two-args.program-graph.patch");
await writeFile(missingFunctionWithTwoArgsFile, "pub fn main() -> Void {\n    let value: i32 = missing(41, true)\n}\n");
const missingFunctionWithTwoArgsPlan = await zeroJson(["fix", "--plan", "--json", missingFunctionWithTwoArgsFile]);
assert.equal(missingFunctionWithTwoArgsPlan.diagnostics[0].code, "NAM003");
const missingFunctionWithTwoArgsCandidate = missingFunctionWithTwoArgsPlan.fixes[0].graphPatchCandidates[0];
assert.equal(missingFunctionWithTwoArgsCandidate.patch.operations[0].op, "addFunction");
assert.equal(missingFunctionWithTwoArgsCandidate.patch.operations[0].name, "missing");
assert.equal(missingFunctionWithTwoArgsCandidate.patch.operations[0].type, "i32");
assert.equal(missingFunctionWithTwoArgsCandidate.patch.operations[0].params, "arg0:i32,arg1:Bool");
assert.equal(missingFunctionWithTwoArgsCandidate.patch.operations[0].value, "0");
assert.match(missingFunctionWithTwoArgsCandidate.patch.text[2], /params="arg0:i32,arg1:Bool"/);
await writeFile(missingFunctionWithTwoArgsPatch, `${missingFunctionWithTwoArgsCandidate.patch.text.join("\n")}\n`);
const missingFunctionWithTwoArgsRepair = await zeroJson(["graph", "patch", "--json", missingFunctionWithTwoArgsFile, missingFunctionWithTwoArgsPatch]);
assert.equal(missingFunctionWithTwoArgsRepair.ok, true);
assert.equal(missingFunctionWithTwoArgsRepair.operations[0].params, "arg0:i32,arg1:Bool");
const missingFunctionWithTwoArgsText = await readFile(missingFunctionWithTwoArgsFile, "utf8");
assert.match(missingFunctionWithTwoArgsText, /fn missing\(arg0: i32, arg1: Bool\) -> i32/);
const missingFunctionWithTwoArgsFixed = await zeroJson(["check", "--json", missingFunctionWithTwoArgsFile]);
assert.equal(missingFunctionWithTwoArgsFixed.ok, true);

const missingFunctionWithLocalArgFile = join(outDir, "missing-function-with-local-arg.0");
const missingFunctionWithLocalArgPatch = join(outDir, "missing-function-with-local-arg.program-graph.patch");
await writeFile(missingFunctionWithLocalArgFile, "pub fn main() -> Void {\n    let input: i32 = 41\n    let value: i32 = missing(input)\n}\n");
const missingFunctionWithLocalArgPlan = await zeroJson(["fix", "--plan", "--json", missingFunctionWithLocalArgFile]);
assert.equal(missingFunctionWithLocalArgPlan.diagnostics[0].code, "NAM003");
const missingFunctionWithLocalArgCandidate = missingFunctionWithLocalArgPlan.fixes[0].graphPatchCandidates[0];
assert.equal(missingFunctionWithLocalArgCandidate.patch.operations[0].op, "addFunction");
assert.equal(missingFunctionWithLocalArgCandidate.patch.operations[0].params, "arg0:i32");
assert.match(missingFunctionWithLocalArgCandidate.patch.text[2], /params="arg0:i32"/);
assert(missingFunctionWithLocalArgCandidate.preconditions.includes("typed result context still has the expected type"));
await writeFile(missingFunctionWithLocalArgPatch, `${missingFunctionWithLocalArgCandidate.patch.text.join("\n")}\n`);
const missingFunctionWithLocalArgRepair = await zeroJson(["graph", "patch", "--json", missingFunctionWithLocalArgFile, missingFunctionWithLocalArgPatch]);
assert.equal(missingFunctionWithLocalArgRepair.ok, true);
assert.equal(missingFunctionWithLocalArgRepair.operations[0].params, "arg0:i32");
assert.match(await readFile(missingFunctionWithLocalArgFile, "utf8"), /fn missing\(arg0: i32\) -> i32/);
assert.equal((await zeroJson(["check", "--json", missingFunctionWithLocalArgFile])).ok, true);

const missingFunctionWithParamArgFile = join(outDir, "missing-function-with-param-arg.0");
const missingFunctionWithParamArgPatch = join(outDir, "missing-function-with-param-arg.program-graph.patch");
await writeFile(missingFunctionWithParamArgFile, "fn run(input: i32) -> i32 {\n    return missing(input)\n}\n\npub fn main() -> Void {\n}\n");
const missingFunctionWithParamArgPlan = await zeroJson(["fix", "--plan", "--json", missingFunctionWithParamArgFile]);
assert.equal(missingFunctionWithParamArgPlan.diagnostics[0].code, "NAM003");
const missingFunctionWithParamArgCandidate = missingFunctionWithParamArgPlan.fixes[0].graphPatchCandidates[0];
assert.equal(missingFunctionWithParamArgCandidate.patch.operations[0].op, "addFunction");
assert.equal(missingFunctionWithParamArgCandidate.patch.operations[0].type, "i32");
assert.equal(missingFunctionWithParamArgCandidate.patch.operations[0].params, "arg0:i32");
await writeFile(missingFunctionWithParamArgPatch, `${missingFunctionWithParamArgCandidate.patch.text.join("\n")}\n`);
const missingFunctionWithParamArgRepair = await zeroJson(["graph", "patch", "--json", missingFunctionWithParamArgFile, missingFunctionWithParamArgPatch]);
assert.equal(missingFunctionWithParamArgRepair.ok, true);
assert.equal(missingFunctionWithParamArgRepair.operations[0].params, "arg0:i32");
assert.match(await readFile(missingFunctionWithParamArgFile, "utf8"), /fn missing\(arg0: i32\) -> i32/);
assert.equal((await zeroJson(["check", "--json", missingFunctionWithParamArgFile])).ok, true);

const returnMismatchFile = join(outDir, "return-mismatch.0");
const returnMismatchPatch = join(outDir, "return-mismatch.program-graph.patch");
await writeFile(returnMismatchFile, "fn value() -> i32 {\n    return \"text\"\n}\n\npub fn main() -> Void {\n}\n");
const returnMismatchPlan = await zeroJson(["fix", "--plan", "--json", returnMismatchFile]);
assert.equal(returnMismatchPlan.diagnostics[0].code, "TYP003");
const returnMismatchCandidate = returnMismatchPlan.fixes[0].graphPatchCandidates[0];
assert.equal(returnMismatchCandidate.repairId, "match-return-type");
assert.equal(returnMismatchCandidate.evidence.targetNode.kind, "Function");
assert.equal(returnMismatchCandidate.evidence.targetNode.name, "value");
assert.equal(returnMismatchCandidate.evidence.typedValues.field, "type");
assert.equal(returnMismatchCandidate.evidence.typedValues.expected, "i32");
assert.equal(returnMismatchCandidate.evidence.typedValues.actual, "String");
assert.equal(returnMismatchCandidate.patch.operations[0].op, "changeReturnType");
assert.equal(returnMismatchCandidate.patch.operations[0].targetKind, "Function");
assert.equal(returnMismatchCandidate.patch.operations[0].expect, "i32");
assert.equal(returnMismatchCandidate.patch.operations[0].value, "String");
assert.match(returnMismatchCandidate.patch.text[2], /^changeReturnType node="#/);
await writeFile(returnMismatchPatch, `${returnMismatchCandidate.patch.text.join("\n")}\n`);
const returnMismatchRepair = await zeroJson(["graph", "patch", "--json", returnMismatchFile, returnMismatchPatch]);
assert.equal(returnMismatchRepair.ok, true);
assert.equal(returnMismatchRepair.operations[0].op, "changeReturnType");
assert.equal(returnMismatchRepair.operations[0].actual, "i32");
assert.equal(returnMismatchRepair.operations[0].value, "String");
assert.match(await readFile(returnMismatchFile, "utf8"), /^fn value\(\) -> String/m);
assert.equal((await zeroJson(["check", "--json", returnMismatchFile])).ok, true);

const returnIdentifierMismatchFile = join(outDir, "return-identifier-mismatch.0");
const returnIdentifierMismatchPatch = join(outDir, "return-identifier-mismatch.program-graph.patch");
await writeFile(returnIdentifierMismatchFile, `fn value(input: i32) -> String {
    return input
}

pub fn main() -> Void {
}
`);
const returnIdentifierMismatchPlan = await zeroJson(["fix", "--plan", "--json", returnIdentifierMismatchFile]);
assert.equal(returnIdentifierMismatchPlan.diagnostics[0].code, "TYP003");
const returnIdentifierMismatchCandidate = returnIdentifierMismatchPlan.fixes[0].graphPatchCandidates[0];
assert.equal(returnIdentifierMismatchCandidate.repairId, "match-return-type");
assert.equal(returnIdentifierMismatchCandidate.patch.operations[0].op, "changeReturnType");
assert.equal(returnIdentifierMismatchCandidate.patch.operations[0].targetKind, "Function");
assert.equal(returnIdentifierMismatchCandidate.patch.operations[0].expect, "String");
assert.equal(returnIdentifierMismatchCandidate.patch.operations[0].value, "i32");
assert.match(returnIdentifierMismatchCandidate.patch.text[2], /^changeReturnType node="#/);
await writeFile(returnIdentifierMismatchPatch, `${returnIdentifierMismatchCandidate.patch.text.join("\n")}\n`);
const returnIdentifierMismatchRepair = await zeroJson(["graph", "patch", "--json", returnIdentifierMismatchFile, returnIdentifierMismatchPatch]);
assert.equal(returnIdentifierMismatchRepair.ok, true);
assert.equal(returnIdentifierMismatchRepair.operations[0].op, "changeReturnType");
assert.equal(returnIdentifierMismatchRepair.operations[0].actual, "String");
assert.equal(returnIdentifierMismatchRepair.operations[0].value, "i32");
assert.match(await readFile(returnIdentifierMismatchFile, "utf8"), /^fn value\(input: i32\) -> i32/m);
assert.equal((await zeroJson(["check", "--json", returnIdentifierMismatchFile])).ok, true);

const returnStdHelperMismatchFile = join(outDir, "return-std-helper-mismatch.0");
const returnStdHelperMismatchPatch = join(outDir, "return-std-helper-mismatch.program-graph.patch");
await writeFile(returnStdHelperMismatchFile, `use std.mem

fn value() -> i32 {
    return std.mem.len("zero")
}

pub fn main() -> Void {
}
`);
const returnStdHelperMismatchPlan = await zeroJson(["fix", "--plan", "--json", returnStdHelperMismatchFile]);
assert.equal(returnStdHelperMismatchPlan.diagnostics[0].code, "TYP003");
const returnStdHelperMismatchCandidate = returnStdHelperMismatchPlan.fixes[0].graphPatchCandidates[0];
assert.equal(returnStdHelperMismatchCandidate.repairId, "match-return-type");
assert.equal(returnStdHelperMismatchCandidate.patch.operations[0].op, "changeReturnType");
assert.equal(returnStdHelperMismatchCandidate.patch.operations[0].targetKind, "Function");
assert.equal(returnStdHelperMismatchCandidate.patch.operations[0].expect, "i32");
assert.equal(returnStdHelperMismatchCandidate.patch.operations[0].value, "usize");
assert.match(returnStdHelperMismatchCandidate.patch.text[2], /^changeReturnType node="#/);
await writeFile(returnStdHelperMismatchPatch, `${returnStdHelperMismatchCandidate.patch.text.join("\n")}\n`);
const returnStdHelperMismatchRepair = await zeroJson(["graph", "patch", "--json", returnStdHelperMismatchFile, returnStdHelperMismatchPatch]);
assert.equal(returnStdHelperMismatchRepair.ok, true);
assert.equal(returnStdHelperMismatchRepair.operations[0].op, "changeReturnType");
assert.equal(returnStdHelperMismatchRepair.operations[0].actual, "i32");
assert.equal(returnStdHelperMismatchRepair.operations[0].value, "usize");
assert.match(await readFile(returnStdHelperMismatchFile, "utf8"), /^fn value\(\) -> usize/m);
assert.equal((await zeroJson(["check", "--json", returnStdHelperMismatchFile])).ok, true);

const paramMismatchFile = join(outDir, "param-mismatch.0");
const paramMismatchPatch = join(outDir, "param-mismatch.program-graph.patch");
await writeFile(paramMismatchFile, `fn echo(value: i32) -> String {
    return "ok"
}

pub fn main() -> Void {
    let text: String = echo("hi")
}
`);
const paramMismatchPlan = await zeroJson(["fix", "--plan", "--json", paramMismatchFile]);
assert.equal(paramMismatchPlan.diagnostics[0].code, "TYP001");
const paramMismatchCandidate = paramMismatchPlan.fixes[0].graphPatchCandidates[0];
assert.equal(paramMismatchCandidate.patch.operations[0].op, "changeParamType");
assert.equal(paramMismatchCandidate.patch.operations[0].targetKind, "Param");
assert.equal(paramMismatchCandidate.patch.operations[0].expect, "i32");
assert.equal(paramMismatchCandidate.patch.operations[0].value, "String");
assert.match(paramMismatchCandidate.patch.text[2], /^changeParamType node="#/);
await writeFile(paramMismatchPatch, `${paramMismatchCandidate.patch.text.join("\n")}\n`);
const paramMismatchRepair = await zeroJson(["graph", "patch", "--json", paramMismatchFile, paramMismatchPatch]);
assert.equal(paramMismatchRepair.ok, true);
assert.equal(paramMismatchRepair.operations[0].op, "changeParamType");
assert.equal(paramMismatchRepair.operations[0].actual, "i32");
assert.equal(paramMismatchRepair.operations[0].value, "String");
assert.match(await readFile(paramMismatchFile, "utf8"), /fn echo\(value: String\) -> String/);
assert.equal((await zeroJson(["check", "--json", paramMismatchFile])).ok, true);

const paramIdentifierMismatchFile = join(outDir, "param-identifier-mismatch.0");
const paramIdentifierMismatchPatch = join(outDir, "param-identifier-mismatch.program-graph.patch");
await writeFile(paramIdentifierMismatchFile, `fn echo(value: String) -> String {
    return "ok"
}

pub fn main(input: i32) -> Void {
    let text: String = echo(input)
}
`);
const paramIdentifierMismatchPlan = await zeroJson(["fix", "--plan", "--json", paramIdentifierMismatchFile]);
assert.equal(paramIdentifierMismatchPlan.diagnostics[0].code, "TYP001");
const paramIdentifierMismatchCandidate = paramIdentifierMismatchPlan.fixes[0].graphPatchCandidates[0];
assert.equal(paramIdentifierMismatchCandidate.patch.operations[0].op, "changeParamType");
assert.equal(paramIdentifierMismatchCandidate.patch.operations[0].targetKind, "Param");
assert.equal(paramIdentifierMismatchCandidate.patch.operations[0].expect, "String");
assert.equal(paramIdentifierMismatchCandidate.patch.operations[0].value, "i32");
assert.match(paramIdentifierMismatchCandidate.patch.text[2], /^changeParamType node="#/);
await writeFile(paramIdentifierMismatchPatch, `${paramIdentifierMismatchCandidate.patch.text.join("\n")}\n`);
const paramIdentifierMismatchRepair = await zeroJson(["graph", "patch", "--json", paramIdentifierMismatchFile, paramIdentifierMismatchPatch]);
assert.equal(paramIdentifierMismatchRepair.ok, true);
assert.equal(paramIdentifierMismatchRepair.operations[0].op, "changeParamType");
assert.equal(paramIdentifierMismatchRepair.operations[0].actual, "String");
assert.equal(paramIdentifierMismatchRepair.operations[0].value, "i32");
assert.match(await readFile(paramIdentifierMismatchFile, "utf8"), /fn echo\(value: i32\) -> String/);
assert.equal((await zeroJson(["check", "--json", paramIdentifierMismatchFile])).ok, true);

const localIdentifierMismatchFile = join(outDir, "local-identifier-mismatch.0");
const localIdentifierMismatchPatch = join(outDir, "local-identifier-mismatch.program-graph.patch");
await writeFile(localIdentifierMismatchFile, `pub fn main(input: i32) -> Void {
    let text: String = input
}
`);
const localIdentifierMismatchPlan = await zeroJson(["fix", "--plan", "--json", localIdentifierMismatchFile]);
assert.equal(localIdentifierMismatchPlan.diagnostics[0].code, "TYP002");
const localIdentifierMismatchCandidate = localIdentifierMismatchPlan.fixes[0].graphPatchCandidates[0];
assert.equal(localIdentifierMismatchCandidate.patch.operations[0].op, "changeLocalType");
assert.equal(localIdentifierMismatchCandidate.patch.operations[0].targetKind, "Let");
assert.equal(localIdentifierMismatchCandidate.patch.operations[0].expect, "String");
assert.equal(localIdentifierMismatchCandidate.patch.operations[0].value, "i32");
assert.match(localIdentifierMismatchCandidate.patch.text[2], /^changeLocalType node="#/);
await writeFile(localIdentifierMismatchPatch, `${localIdentifierMismatchCandidate.patch.text.join("\n")}\n`);
const localIdentifierMismatchRepair = await zeroJson(["graph", "patch", "--json", localIdentifierMismatchFile, localIdentifierMismatchPatch]);
assert.equal(localIdentifierMismatchRepair.ok, true);
assert.equal(localIdentifierMismatchRepair.operations[0].op, "changeLocalType");
assert.equal(localIdentifierMismatchRepair.operations[0].actual, "String");
assert.equal(localIdentifierMismatchRepair.operations[0].value, "i32");
assert.match(await readFile(localIdentifierMismatchFile, "utf8"), /let text: i32 = input/);
assert.equal((await zeroJson(["check", "--json", localIdentifierMismatchFile])).ok, true);

const fieldDefaultMismatchFile = join(outDir, "field-default-mismatch.0");
const fieldDefaultMismatchPatch = join(outDir, "field-default-mismatch.program-graph.patch");
await writeFile(fieldDefaultMismatchFile, `type BadDefault {
    count: usize = "nope",
}

pub fn main() -> Void {
    let value: BadDefault = BadDefault {}
}
`);
const fieldDefaultMismatchPlan = await zeroJson(["fix", "--plan", "--json", fieldDefaultMismatchFile]);
assert.equal(fieldDefaultMismatchPlan.diagnostics[0].code, "TYP002");
const fieldDefaultMismatchCandidate = fieldDefaultMismatchPlan.fixes[0].graphPatchCandidates[0];
assert.equal(fieldDefaultMismatchCandidate.patch.operations[0].op, "changeFieldType");
assert.equal(fieldDefaultMismatchCandidate.patch.operations[0].targetKind, "Field");
assert.equal(fieldDefaultMismatchCandidate.patch.operations[0].expect, "usize");
assert.equal(fieldDefaultMismatchCandidate.patch.operations[0].value, "String");
assert.match(fieldDefaultMismatchCandidate.patch.text[2], /^changeFieldType node="#/);
await writeFile(fieldDefaultMismatchPatch, `${fieldDefaultMismatchCandidate.patch.text.join("\n")}\n`);
const fieldDefaultMismatchRepair = await zeroJson(["graph", "patch", "--json", fieldDefaultMismatchFile, fieldDefaultMismatchPatch]);
assert.equal(fieldDefaultMismatchRepair.ok, true);
assert.equal(fieldDefaultMismatchRepair.operations[0].op, "changeFieldType");
assert.equal(fieldDefaultMismatchRepair.operations[0].actual, "usize");
assert.equal(fieldDefaultMismatchRepair.operations[0].value, "String");
assert.match(await readFile(fieldDefaultMismatchFile, "utf8"), /count: String = "nope"/);
assert.equal((await zeroJson(["check", "--json", fieldDefaultMismatchFile])).ok, true);

const firstPatch = await zeroJson(["fix", "--patch", "--json", workFile]);
assert.equal(firstPatch.mode, "patch");
assert.equal(firstPatch.applied, false);
assert.equal(firstPatch.agentTransaction.ok, true);
assert.equal(firstPatch.agentTransaction.checkedEditSurface, "reviewable-source-patch");
assert.equal(firstPatch.agentTransaction.writePolicy, "preview-only");
assert.deepEqual(firstPatch.agentTransaction.command.argv, ["zero", "fix", "--patch", "--json", workFile]);
assert.equal(firstPatch.agentTransaction.rollback.changedPath, workFile);
assert.deepEqual(firstPatch.agentTransaction.rollback.restoreFields, ["patches[].path", "patches[].line", "patches[].old"]);
const firstPatchRollbackAction = assertRepairRollbackAction(firstPatch.agentTransaction);
assertCommand(firstPatch.agentTransaction.rollback.verificationCommands[0], "rollback-source-check", ["zero", "check", "--json", workFile]);
assertCommand(firstPatch.agentTransaction.rollback.verificationCommands[1], "rollback-graph-check", ["zero", "graph", "check", "--json", workFile]);
assert.deepEqual((await runRequiredCommands(firstPatch.agentTransaction.rollback.verificationCommands)).map((item) => item.purpose), ["rollback-source-check", "rollback-graph-check"]);
assert.equal(firstPatch.agentTransaction.patchContract.preconditions.oldLineMustMatch, true);
assert.equal(firstPatch.agentTransaction.patchContract.preconditions.diagnosticSpanMustStillMatch, true);
assert.equal(firstPatch.agentTransaction.phaseAudit.patch.status, "ready");
assert.equal(firstPatch.agentTransaction.phaseAudit.validate.status, "passed");
assert.equal(firstPatch.agentTransaction.phaseAudit.rewrite.status, "not-scheduled");
assert.equal(ledgerPhase(firstPatch.agentTransaction, "patch").status, "ready");
assert.equal(ledgerPhase(firstPatch.agentTransaction, "validate").status, "passed");
assert.equal(ledgerPhase(firstPatch.agentTransaction, "rewrite").status, "not-scheduled");
assert.equal(ledgerPhase(firstPatch.agentTransaction, "rollback").status, "available");
assert(ledgerPhase(firstPatch.agentTransaction, "rollback").evidenceFields.includes("agentTransaction.rollback.actions[]"));
assert.deepEqual(ledgerPhase(firstPatch.agentTransaction, "rollback").requiredPurposes.sort(), ["rollback-graph-check", "rollback-source-check"]);
assert.equal(firstPatch.fixes[0].id, "make-binding-mutable");
assert.equal(firstPatch.fixes[0].repairContract.transactionKind, "single-line-source-replacement");
assert.equal(firstPatch.patches[0].line, 4);
assert.match(firstPatch.patches[0].old, /let dst: \[4\]u8/);
assert.match(firstPatch.patches[0].new, /var dst: \[4\]u8/);

const firstApply = await zeroJson(["fix", "--apply", "--json", workFile]);
assert.equal(firstApply.applied, true);
assert.equal(firstApply.agentTransaction.mode, "apply");
assert.equal(firstApply.agentTransaction.appliesEdits, true);
assert.equal(firstApply.agentTransaction.writePolicy, "writes-source");
assert.deepEqual(firstApply.agentTransaction.command.argv, ["zero", "fix", "--apply", "--json", workFile]);
assert.equal(firstApply.agentTransaction.failure, null);
const firstApplyRollbackAction = assertRepairRollbackAction(firstApply.agentTransaction);
assertCommand(firstApply.agentTransaction.rollback.verificationCommands[0], "rollback-source-check", ["zero", "check", "--json", workFile]);
assertCommand(firstApply.agentTransaction.rollback.verificationCommands[1], "rollback-graph-check", ["zero", "graph", "check", "--json", workFile]);
assert.deepEqual((await runRequiredCommands(firstApply.agentTransaction.rollback.verificationCommands)).map((item) => item.purpose), ["rollback-source-check", "rollback-graph-check"]);
assert.equal(firstApply.agentTransaction.phaseAudit.patch.status, "ready");
assert.equal(firstApply.agentTransaction.phaseAudit.validate.status, "passed");
assert.equal(firstApply.agentTransaction.phaseAudit.rewrite.status, "applied");
assert.equal(firstApply.agentTransaction.phaseAudit.verify.status, "scheduled");
assert.equal(ledgerPhase(firstApply.agentTransaction, "rewrite").status, "applied");
assert.deepEqual(ledgerPhase(firstApply.agentTransaction, "verify").requiredPurposes.sort(), purposeSet(firstApply.agentTransaction.verificationCommands));
assert.deepEqual(ledgerPhase(firstApply.agentTransaction, "rollback").requiredPurposes.sort(), purposeSet(firstApply.agentTransaction.rollback.verificationCommands));
assertCommand(firstApply.agentTransaction.verificationCommands[0], "source-check", ["zero", "check", "--json", workFile]);
assertCommand(firstApply.agentTransaction.verificationCommands[1], "graph-check", ["zero", "graph", "check", "--json", workFile]);
assert.deepEqual((await runRequiredCommands(firstApply.agentTransaction.verificationCommands)).map((item) => item.purpose), ["source-check", "graph-check"]);

const secondCheck = await zeroJson(["check", "--json", workFile], { allowFailure: true });
assert.equal(secondCheck.ok, false);
assert.equal(secondCheck.diagnostics[0].code, "TYP002");
assert.equal(secondCheck.diagnostics[0].repair.id, "match-binding-annotation");

const secondExplain = await zeroJson(["explain", "--json", "TYP002"]);
assert.equal(secondExplain.code, "TYP002");
assert.equal(secondExplain.repair.id, "match-binding-annotation");
assert.match(secondExplain.repair.summary, /initializer's actual type/);
assert.match(secondExplain.examples.good, /let _copied: usize/);

const secondPatch = await zeroJson(["fix", "--patch", "--json", workFile]);
assert.equal(secondPatch.applied, false);
assert.equal(secondPatch.agentTransaction.ok, true);
assert.equal(secondPatch.agentTransaction.repairId, "match-binding-annotation");
assert.equal(secondPatch.fixes[0].appliesEdits, true);
assert.match(secondPatch.patches[0].old, /let _copied: i32 = std\.mem\.copy/);
assert.match(secondPatch.patches[0].new, /let _copied: usize = std\.mem\.copy/);

const secondApply = await zeroJson(["fix", "--apply", "--json", workFile]);
assert.equal(secondApply.applied, true);
assert.equal(secondApply.fixes[0].id, "match-binding-annotation");
assertCommand(secondApply.agentTransaction.verificationCommands[0], "source-check", ["zero", "check", "--json", workFile]);
assertCommand(secondApply.agentTransaction.verificationCommands[1], "graph-check", ["zero", "graph", "check", "--json", workFile]);
assert.deepEqual((await runRequiredCommands(secondApply.agentTransaction.verificationCommands)).map((item) => item.purpose), ["source-check", "graph-check"]);

const unsupportedFile = join(outDir, "unknown-name.0");
await writeFile(unsupportedFile, "pub fn main() -> Void {\n    missing()\n}\n");
const unsupported = await zeroJson(["fix", "--patch", "--json", unsupportedFile]);
assert.equal(unsupported.agentTransaction.ok, false);
assert.equal(unsupported.agentTransaction.failure.class, "unsupported-repair");
assert.equal(unsupported.agentTransaction.failure.phase, "patch");
assert.deepEqual(unsupported.agentTransaction.command.argv, ["zero", "fix", "--patch", "--json", unsupportedFile]);
assert(unsupported.agentTransaction.failureClasses.some((item) => item.class === "unsupported-repair" && item.phase === "patch"));
assert.equal(unsupported.agentTransaction.failure.retryable, true);
assert.match(unsupported.agentTransaction.failure.retryStrategy, /checked graph patch/);
assertCommand(unsupported.agentTransaction.failure.retryCommands[0], "repair-plan", ["zero", "fix", "--plan", "--json", unsupportedFile]);
assertCommand(unsupported.agentTransaction.failure.retryCommands[1], "graph-inspect", ["zero", "graph", "inspect", "--json", unsupportedFile]);
assert.deepEqual((await runRequiredCommands(unsupported.agentTransaction.failure.retryCommands)).map((item) => item.purpose), ["repair-plan", "graph-inspect"]);
const unsupportedTarget = await zeroJson(["fix", "--patch", "--json", "--target", "linux-musl-x64", "--profile", "dev", unsupportedFile]);
assert.deepEqual(unsupportedTarget.agentTransaction.command.argv, ["zero", "fix", "--patch", "--json", "--target", "linux-musl-x64", "--profile", "dev", unsupportedFile]);
assert.deepEqual(unsupportedTarget.agentTransaction.failure.retryCommands[0].argv, ["zero", "fix", "--plan", "--json", "--target", "linux-musl-x64", "--profile", "dev", unsupportedFile]);
assert.deepEqual(unsupportedTarget.agentTransaction.failure.retryCommands[1].argv, ["zero", "graph", "inspect", "--json", "--target", "linux-musl-x64", "--profile", "dev", unsupportedFile]);
assert.equal(unsupported.agentTransaction.phaseAudit.inspect.ok, true);
assert.equal(unsupported.agentTransaction.phaseAudit.plan.ok, true);
assert.equal(unsupported.agentTransaction.phaseAudit.patch.status, "unsupported");
assert.equal(unsupported.agentTransaction.phaseAudit.validate.status, "unsupported");
assert.equal(unsupported.agentTransaction.phaseAudit.verify.status, "not-scheduled");
assert.equal(unsupported.agentTransaction.proofLedger.failureClass, "unsupported-repair");
assert.equal(ledgerPhase(unsupported.agentTransaction, "patch").status, "unsupported");
assert.equal(ledgerPhase(unsupported.agentTransaction, "validate").status, "unsupported");
assert.equal(ledgerPhase(unsupported.agentTransaction, "verify").status, "not-scheduled");
assert.deepEqual(unsupported.agentTransaction.verificationCommands, []);
assert.equal(unsupported.fixes[0].repairContract.autoPatchSupported, false);

const fixed = await zeroJson(["check", "--json", workFile]);
assert.equal(fixed.ok, true);
assert.equal(fixed.diagnostics.length, 0);

const proofAudit = {
  required: executedProofs.length,
  passed: executedProofs.filter((item) => item.ok).length,
  failed: executedProofs.filter((item) => !item.ok).length,
  purposes: [...new Set(executedProofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(executedProofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
assert(proofAudit.required >= 10);
for (const purpose of ["source-check", "graph-check", "graph-dump", "repair-plan", "graph-inspect", "rollback-source-check", "rollback-graph-check"]) {
  assert(proofAudit.purposes.includes(purpose), `missing proof purpose ${purpose}`);
}
for (const purpose of ["source-check", "graph-check"]) {
  assert(proofAudit.passedPurposes.includes(purpose), `missing passed proof purpose ${purpose}`);
}
const candidateOps = [...new Set([
  firstPlan.fixes[0].graphPatchCandidates[0].patch.operations[0].op,
  graphPatchCandidate.patch.operations[0].op,
  graphPatchCandidate2.patch.operations[0].op,
  missingFunctionCandidate.patch.operations[0].op,
  replaceCalleeCandidate.patch.operations[0].op,
  duplicateFunctionCandidate.patch.operations[0].op,
  missingFunctionWithArgCandidate.patch.operations[0].op,
  missingFunctionWithTwoArgsCandidate.patch.operations[0].op,
  missingFunctionWithLocalArgCandidate.patch.operations[0].op,
  missingFunctionWithParamArgCandidate.patch.operations[0].op,
  returnMismatchCandidate.patch.operations[0].op,
  returnIdentifierMismatchCandidate.patch.operations[0].op,
  returnStdHelperMismatchCandidate.patch.operations[0].op,
  paramMismatchCandidate.patch.operations[0].op,
  paramIdentifierMismatchCandidate.patch.operations[0].op,
  localIdentifierMismatchCandidate.patch.operations[0].op,
  fieldDefaultMismatchCandidate.patch.operations[0].op,
])].sort();
const repairIds = [...new Set([
  firstPlan.fixes[0].graphPatchCandidates[0].repairId,
  graphPatchCandidate.repairId,
  graphPatchCandidate2.repairId,
  missingFunctionCandidate.repairId,
  replaceCalleeCandidate.repairId,
  duplicateFunctionCandidate.repairId,
  returnMismatchCandidate.repairId,
  paramMismatchCandidate.repairId,
  localIdentifierMismatchCandidate.repairId,
  fieldDefaultMismatchCandidate.repairId,
])].sort();
const graphPatchCandidates = [
  firstPlan.fixes[0].graphPatchCandidates[0],
  graphPatchCandidate,
  graphPatchCandidate2,
  missingFunctionCandidate,
  replaceCalleeCandidate,
  duplicateFunctionCandidate,
  missingFunctionWithArgCandidate,
  missingFunctionWithTwoArgsCandidate,
  missingFunctionWithLocalArgCandidate,
  missingFunctionWithParamArgCandidate,
  returnMismatchCandidate,
  returnIdentifierMismatchCandidate,
  returnStdHelperMismatchCandidate,
  paramMismatchCandidate,
  paramIdentifierMismatchCandidate,
  localIdentifierMismatchCandidate,
  fieldDefaultMismatchCandidate,
];
function candidateContractComplete(candidate) {
  const contract = candidate.candidateContract;
  return candidate.kind === "checked-graph-patch-candidate" &&
    contract?.kind === "checked-graph-patch-candidate-contract" &&
    contract.patchKind === "program-graph-patch-v1" &&
    contract.commandField === "command.argv" &&
    contract.patchTextField === "patch.text" &&
    contract.requiresPatchFile === true &&
    contract.stateFields?.includes("patch.graphHash") &&
    contract.auditFields?.includes("preconditions") &&
    contract.auditFields?.includes("verificationCommands") &&
    contract.resultContract === "agentQuery.checkedEditSurface.transactionContract";
}
function candidatePatchTextComplete(candidate) {
  return candidate.patch?.text?.[0] === "zero-program-graph-patch v1" &&
    typeof candidate.patch?.graphHash === "string" &&
    candidate.patch.graphHash.startsWith("graph:") &&
    candidate.patch?.text?.[1] === `expect graphHash "${candidate.patch.graphHash}"` &&
    candidate.patch?.operations?.length >= 1;
}
const candidateContractAudit = {
  candidates: graphPatchCandidates.length,
  completeContracts: graphPatchCandidates.filter(candidateContractComplete).length,
  completePatchText: graphPatchCandidates.filter(candidatePatchTextComplete).length,
  evidenceBacked: graphPatchCandidates.filter((candidate) =>
    candidate.evidence?.diagnosticSpan?.path &&
    candidate.evidence?.targetNode?.id &&
    candidate.evidence?.typedValues).length,
  targetProfilePreserved: firstTargetPlan.fixes[0].graphPatchCandidates[0].command.argv.includes("--target") &&
    firstTargetPlan.fixes[0].graphPatchCandidates[0].command.argv.includes("linux-musl-x64") &&
    firstTargetPlan.fixes[0].graphPatchCandidates[0].command.argv.includes("--profile") &&
    firstTargetPlan.fixes[0].graphPatchCandidates[0].command.argv.includes("dev") &&
    firstTargetPlan.fixes[0].graphPatchCandidates[0].verificationCommands.every((command) =>
      command.argv.includes("--target") && command.argv.includes("linux-musl-x64") &&
      command.argv.includes("--profile") && command.argv.includes("dev")),
  verificationPurposes: [...new Set(graphPatchCandidates.flatMap((candidate) =>
    candidate.verificationCommands?.map((command) => command.purpose) ?? []))].sort(),
};
assert(candidateOps.includes("removeFunction"));
assert(repairIds.includes("remove-duplicate-declaration"));
assert(candidateContractAudit.candidates >= 17);
assert.equal(candidateContractAudit.completeContracts, candidateContractAudit.candidates);
assert.equal(candidateContractAudit.completePatchText, candidateContractAudit.candidates);
assert(candidateContractAudit.evidenceBacked >= 10);
assert.equal(candidateContractAudit.targetProfilePreserved, true);
assert.deepEqual(candidateContractAudit.verificationPurposes, ["graph-check", "source-check"]);
const repairFailures = [
  missingSource,
  editNotFound,
  unsupported,
  unsupportedTarget,
].filter((item) => item?.agentTransaction?.failure);
const repairFailureAudit = {
  declaredClasses: [...new Set(firstPlan.agentTransaction.failureClasses.map((item) => item.class))].sort(),
  observedClasses: [...new Set(repairFailures.map((item) => item.agentTransaction.failure.class))].sort(),
  observedPhases: [...new Set(repairFailures.map((item) => item.agentTransaction.failure.phase))].sort(),
  retryPurposes: [...new Set(repairFailures.flatMap((item) =>
    item.agentTransaction.failure.retryCommands?.map((command) => command.purpose) ?? []))].sort(),
  targetProfileRetryPreserved: unsupportedTarget.agentTransaction.failure.retryCommands.every((command) =>
    command.argv.includes("--target") && command.argv.includes("linux-musl-x64") &&
    command.argv.includes("--profile") && command.argv.includes("dev")),
  retryCommandContracts: ["source-unavailable", "edit-not-found", "write-failed"].every((failureClass) =>
    firstPlan.agentTransaction.retryCommandContracts?.[failureClass]?.argv?.includes("--target") &&
    firstPlan.agentTransaction.retryCommandContracts?.[failureClass]?.argv?.includes("<same-target>") &&
    firstPlan.agentTransaction.retryCommandContracts?.[failureClass]?.argv?.includes("--profile") &&
    firstPlan.agentTransaction.retryCommandContracts?.[failureClass]?.argv?.includes("<same-profile>")) &&
    firstPlan.agentTransaction.retryCommandContracts?.["unsupported-repair"]?.commands?.every((command) =>
      command.argv.includes("--target") &&
      command.argv.includes("<same-target>") &&
      command.argv.includes("--profile") &&
      command.argv.includes("<same-profile>")),
  rollbackRestoreFields: firstPatch.agentTransaction.rollback.restoreFields,
  rollbackActions: [firstPlanRollbackAction, firstPatchRollbackAction, firstApplyRollbackAction].map((action) =>
    `${action.kind}:${action.pathField}:${action.oldTextField}`),
  noVerifyOnFailure: repairFailures.every((item) =>
    Array.isArray(item.agentTransaction.verificationCommands) &&
    item.agentTransaction.verificationCommands.length === 0),
};
const repairProofLedgerAudit = {
  phaseOrderStructured: JSON.stringify(firstPlan.agentTransaction.proofLedger.phaseOrder) === JSON.stringify(["inspect", "plan", "patch", "validate", "rewrite", "verify", "rollback"]) &&
    firstPlan.agentTransaction.proofLedger.phases.length === 7,
  resultFieldsDeclared: firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger") &&
    firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].phase") &&
    firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].status") &&
    firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].evidenceFields") &&
    firstPlan.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].requiredPurposes"),
  successLedgerEvidenceFields: ledgerPhase(firstPlan.agentTransaction, "inspect").evidenceFields.includes("agentTransaction.command.argv") &&
    ledgerPhase(firstPlan.agentTransaction, "plan").evidenceFields.includes("fixes[].repairContract") &&
    ledgerPhase(firstPatch.agentTransaction, "patch").evidenceFields.includes("patches[]") &&
    ledgerPhase(firstPatch.agentTransaction, "validate").evidenceFields.includes("agentTransaction.patchContract.preconditions") &&
    ledgerPhase(firstApply.agentTransaction, "rewrite").evidenceFields.includes("agentTransaction.writePolicy") &&
    ledgerPhase(firstApply.agentTransaction, "verify").evidenceFields.includes("agentTransaction.verificationCommands[].argv"),
  verificationPurposesMatched: JSON.stringify(ledgerPhase(firstApply.agentTransaction, "verify").requiredPurposes.slice().sort()) ===
    JSON.stringify(purposeSet(firstApply.agentTransaction.verificationCommands)),
  rollbackPurposesMatched: JSON.stringify(ledgerPhase(firstApply.agentTransaction, "rollback").requiredPurposes.slice().sort()) ===
    JSON.stringify(purposeSet(firstApply.agentTransaction.rollback.verificationCommands)),
  rollbackActionsStructured: firstPatch.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[]") &&
    firstPatch.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[].kind") &&
    firstPatch.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[].pathField") &&
    firstPatch.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[].oldTextField") &&
    firstPatch.agentTransaction.rollback.actions[0]?.kind === "restore-line" &&
    firstPatch.agentTransaction.rollback.actions[0]?.pathField === "patches[].path" &&
    firstPatch.agentTransaction.rollback.actions[0]?.lineField === "patches[].line" &&
    firstPatch.agentTransaction.rollback.actions[0]?.oldTextField === "patches[].old" &&
    firstPatch.agentTransaction.rollback.actions[0]?.requiresVerification === true &&
    ledgerPhase(firstPatch.agentTransaction, "rollback").evidenceFields.includes("agentTransaction.rollback.actions[]"),
  failureLedgerClassified: missingSource.agentTransaction.proofLedger.failureClass === "source-unavailable" &&
    ledgerPhase(missingSource.agentTransaction, "inspect").status === "failed" &&
    unsupported.agentTransaction.proofLedger.failureClass === "unsupported-repair" &&
    ledgerPhase(unsupported.agentTransaction, "patch").status === "unsupported" &&
    ledgerPhase(unsupported.agentTransaction, "verify").status === "not-scheduled",
};
for (const failureClass of ["source-unavailable", "unsupported-repair", "edit-not-found", "write-failed"]) assert(repairFailureAudit.declaredClasses.includes(failureClass), `missing declared repair failure class ${failureClass}`);
for (const failureClass of ["source-unavailable", "unsupported-repair", "edit-not-found"]) assert(repairFailureAudit.observedClasses.includes(failureClass), `missing observed repair failure class ${failureClass}`);
for (const purpose of ["source-check", "repair-plan", "graph-inspect"]) assert(repairFailureAudit.retryPurposes.includes(purpose), `missing repair retry purpose ${purpose}`);
assert.equal(repairFailureAudit.targetProfileRetryPreserved, true);
assert.deepEqual(repairFailureAudit.rollbackRestoreFields, ["patches[].path", "patches[].line", "patches[].old"]);
assert(repairFailureAudit.rollbackActions.includes("restore-line:patches[].path:patches[].old"));
assert.equal(repairFailureAudit.noVerifyOnFailure, true);
assert.equal(repairProofLedgerAudit.phaseOrderStructured, true);
assert.equal(repairProofLedgerAudit.resultFieldsDeclared, true);
assert.equal(repairProofLedgerAudit.successLedgerEvidenceFields, true);
assert.equal(repairProofLedgerAudit.verificationPurposesMatched, true);
assert.equal(repairProofLedgerAudit.rollbackPurposesMatched, true);
assert.equal(repairProofLedgerAudit.rollbackActionsStructured, true);
assert.equal(repairProofLedgerAudit.failureLedgerClassified, true);
console.log(JSON.stringify({ ok: true, kind: "agent-repair-transaction-smoke", proofAudit, proofLedgerAudit: repairProofLedgerAudit, failureAudit: repairFailureAudit, repairPlanAudit: { candidateOps, repairIds, candidateContractAudit } }, null, 2));
console.log("agent repair transaction smoke ok");
