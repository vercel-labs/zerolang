import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const outDir = join(".zero", "agent-graph-patch-transaction-smoke", String(process.pid));

type GraphNode = { id: string; kind: string; name?: string; type?: string; value?: string };
type GraphJson = { graphHash: string; nodes: GraphNode[] };
const executedProofs = [];
const requiredSemanticOperations = [
  "addFunction",
  "addImport",
  "addParam",
  "changeFieldType",
  "changeLocalType",
  "changeParamType",
  "changeReturnType",
  "removeFunction",
  "removeImport",
  "removeParam",
  "renameField",
  "renameImportAlias",
  "renameLocal",
  "renameParam",
  "renameSymbol",
  "replaceCallee",
  "replaceImport",
].sort();

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
const sourceVerificationResultFields = ["ok", "diagnostics", "agentCommand.verificationCommands"];
const graphVerificationResultFields = ["ok", "diagnostics", "graphHash", "agentCommand.verificationCommands"];
const artifactVerificationResultFields = ["ok", "graphHash", "validation.ok", "agentCommand.verificationCommands"];
function assertCommandResultFields(command, fields: string[]) {
  assert.deepEqual(command.resultFields, fields);
}

function assertOperationSurface(transaction) {
  assert(Array.isArray(transaction.operationKinds));
  assert(Array.isArray(transaction.operationContracts));
  const contractOps = transaction.operationContracts.map((item) => item.op).sort();
  for (const op of requiredSemanticOperations) {
    assert(transaction.operationKinds.includes(op), `operationKinds missing ${op}`);
    assert(contractOps.includes(op), `operationContracts missing ${op}`);
  }
}

function ledgerPhase(transaction, phase: string) {
  const found = transaction.proofLedger?.phases?.find((item) => item.phase === phase);
  assert(found, `missing proofLedger phase ${phase}`);
  return found;
}

function purposeSet(commands) {
  return commands.map((command) => command.purpose).sort();
}

function assertGraphPatchRollbackAction(transaction, savedKind: "source" | "artifact") {
  assert(transaction.resultFields.includes("agentTransaction.rollback.actions[]"));
  assert(transaction.resultFields.includes("agentTransaction.rollback.actions[].kind"));
  assert(transaction.resultFields.includes("agentTransaction.rollback.actions[].pathField"));
  assert(transaction.resultFields.includes("agentTransaction.rollback.actions[].savedKind"));
  const action = transaction.rollback.actions[0];
  assert.equal(action.pathField, "agentTransaction.rollback.savedPath");
  assert.equal(action.savedKind, savedKind);
  assert.equal(action.requiresVerification, true);
  if (savedKind === "source") {
    assert.equal(action.kind, "restore-path-from-vcs");
    assert.equal(action.condition, "before-retry-or-after-failed-save-verification");
  } else {
    assert.equal(action.kind, "replace-or-delete-artifact");
    assert.equal(action.condition, "created-or-replaced-by-command");
  }
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

const sourcePath = join(outDir, "hello.0");
const graphPath = join(outDir, "hello.program-graph");
const patchPath = join(outDir, "hello.program-graph.patch");
const stalePatchPath = join(outDir, "hello.stale.program-graph.patch");
const mismatchPatchPath = join(outDir, "hello.mismatch.program-graph.patch");
const invalidValuePatchPath = join(outDir, "hello.invalid-value.program-graph.patch");
const invalidSyntaxPatchPath = join(outDir, "hello.invalid-syntax.program-graph.patch");
const unreadablePatchPath = join(outDir, "hello.unreadable.program-graph.patch");
const patchedPath = join(outDir, "hello.patched.program-graph");

await writeFile(sourcePath, `pub fn main(world: World) -> Void raises {
    check world.out.write("hello transaction\\n")
}
`);

const graph = await zeroJson(["graph", "dump", "--json", "--out", graphPath, sourcePath]) as GraphJson;
const literal = graph.nodes.find((node) => node.kind === "Literal" && node.type === "String" && node.value === "hello transaction\n");
const mainFunction = graph.nodes.find((node) => node.kind === "Function" && node.name === "main");
assert(literal);
assert(mainFunction);

await writeFile(patchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graph.graphHash}"`,
  `set node="${literal.id}" field="value" expect="hello transaction\\n" value="hello patched transaction\\n"`,
  "",
].join("\n"));

const patched = await zeroJson(["graph", "patch", "--json", "--out", patchedPath, graphPath, patchPath]);
assert.equal(patched.ok, true);
assert.equal(patched.agentTransaction.kind, "compiler-mediated-graph-patch-transaction");
assert.equal(patched.agentTransaction.applied, true);
assert.equal(patched.agentTransaction.originalGraphHash, graph.graphHash);
assert.equal(patched.agentTransaction.patchedGraphHash, patched.patchedGraphHash);
assert.equal(patched.agentTransaction.operationCount, 1);
assert(patched.agentTransaction.resultFields.includes("agentTransaction.command.argv"));
assert.deepEqual(patched.agentTransaction.command.argv, ["zero", "graph", "patch", "--json", "--out", patchedPath, graphPath, patchPath]);
assert(patched.agentTransaction.resultFields.includes("agentTransaction.originalGraphHash"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.patchedGraphHash"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.operationCount"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.patchContract"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.proofLedger"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].phase"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].status"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].evidenceFields"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.proofLedger.phases[].requiredPurposes"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.rollback.savedPath"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.rollback.applied"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[]"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[].kind"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[].pathField"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[].savedKind"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.rollback.verificationCommands[].resultFields"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.verificationCommands[].resultFields"));
assert(patched.agentTransaction.resultFields.includes("saved.graphHash"));
assert(patched.agentTransaction.resultFields.includes("saveProof.semanticStable"));
assert(patched.agentTransaction.resultFields.includes("saveProof.comparedGraphHash"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.failure.code"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.failure.class"));
assert(patched.agentTransaction.resultFields.includes("agentTransaction.failure.retryCommands"));
assert(patched.agentTransaction.resultFields.includes("evidenceBindings[]"));
assert(patched.agentTransaction.resultFields.includes("evidenceBindings[].sourceFields"));
assert(patched.agentTransaction.resultFields.includes("operations[].retryCommands"));
assert(patched.agentTransaction.resultFields.includes("operations[].retryCommands[].purpose"));
assert(patched.agentTransaction.failureClasses.some((item) => item.code === "GPH001" && item.class === "invalid-patch" && item.phase === "parse" && item.retryable === true && item.retryCommand === "zero graph inspect --json <input>"));
assert(patched.agentTransaction.failureClasses.some((item) => item.code === "GPH005" && item.class === "precondition-failed" && item.phase === "patch" && item.retryable === true && item.retryCommand === "operations[].retryCommands[] graph-impact"));
assert(patched.agentTransaction.failureClasses.some((item) => item.code === "GPH006" && item.class === "invalid-result-graph" && item.phase === "validate" && item.retryable === false && item.retryCommand === null));
assert.deepEqual(patched.agentTransaction.retryCommandContracts.GPH001.argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(patched.agentTransaction.retryCommandContracts.GPH002.argv, ["zero", "graph", "dump", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(patched.agentTransaction.retryCommandContracts.GPH003.argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(patched.agentTransaction.retryCommandContracts.GPH004.argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.equal(patched.agentTransaction.retryCommandContracts.GPH005.argvField, "operations[].retryCommands[].argv");
assert.equal(patched.agentTransaction.retryCommandContracts.GPH006, null);
assert.equal(patched.agentTransaction.phaseAudit.inspect.ok, true);
assert.equal(patched.agentTransaction.phaseAudit.patch.ok, true);
assert.equal(patched.agentTransaction.phaseAudit.validate.ok, true);
assert.equal(patched.agentTransaction.phaseAudit.save.ok, true);
assert.equal(patched.agentTransaction.phaseAudit.verify.status, "scheduled");
assert.equal(patched.agentTransaction.proofLedger.schemaVersion, 1);
assert.equal(patched.agentTransaction.proofLedger.kind, "graph-patch-proof-ledger");
assert.deepEqual(patched.agentTransaction.proofLedger.phaseOrder, ["inspect", "patch", "validate", "save", "verify", "rollback"]);
assert.equal(ledgerPhase(patched.agentTransaction, "inspect").status, "passed");
assert.deepEqual(ledgerPhase(patched.agentTransaction, "inspect").evidenceFields, ["agentTransaction.originalGraphHash", "agentTransaction.command.argv"]);
assert.equal(ledgerPhase(patched.agentTransaction, "patch").status, "passed");
assert.equal(ledgerPhase(patched.agentTransaction, "validate").status, "passed");
assert.equal(ledgerPhase(patched.agentTransaction, "save").status, "passed");
assert.equal(ledgerPhase(patched.agentTransaction, "verify").status, "scheduled");
assert.deepEqual(ledgerPhase(patched.agentTransaction, "verify").requiredPurposes.sort(), ["artifact-validate", "graph-check"]);
assert.equal(ledgerPhase(patched.agentTransaction, "rollback").status, "available");
assert(ledgerPhase(patched.agentTransaction, "rollback").evidenceFields.includes("agentTransaction.rollback.actions[]"));
assert.deepEqual(ledgerPhase(patched.agentTransaction, "rollback").requiredPurposes.sort(), ["rollback-artifact-validate", "rollback-graph-check"]);
assert.deepEqual(ledgerPhase(patched.agentTransaction, "verify").requiredPurposes.sort(), purposeSet(patched.agentTransaction.verificationCommands));
assert.deepEqual(ledgerPhase(patched.agentTransaction, "rollback").requiredPurposes.sort(), purposeSet(patched.agentTransaction.rollback.verificationCommands));
assert.equal(patched.agentTransaction.proofLedger.verifiedBy, "agentTransaction.verificationCommands");
assertOperationSurface(patched.agentTransaction);
assert.equal(patched.agentTransaction.patchContract.kind, "program-graph-patch-v1");
assert.equal(patched.agentTransaction.patchContract.items, "operations");
assert.deepEqual(patched.agentTransaction.patchContract.required, ["op"]);
assert.equal(patched.agentTransaction.patchContract.preconditions.nodeIdsRequired, true);
assert(patched.agentTransaction.patchContract.resultFields.includes("operations[].actual"));
assert.equal(patched.agentTransaction.rollback.originalGraphHash, graph.graphHash);
assert.equal(patched.agentTransaction.rollback.savedKind, "artifact");
assert.equal(patched.agentTransaction.rollback.savedPath, patchedPath);
assert.equal(patched.agentTransaction.rollback.applied, true);
const artifactRollbackAction = assertGraphPatchRollbackAction(patched.agentTransaction, "artifact");
assert.equal(patched.saved.graphHash, patched.patchedGraphHash);
assert.equal(patched.saveProof.kind, "graph-patch-save-proof");
assert.equal(patched.saveProof.semanticStable, true);
assert.equal(patched.saveProof.comparedGraphHash, patched.patchedGraphHash);
assert.equal(patched.saveProof.savedGraphHash, patched.patchedGraphHash);
assertCommand(patched.agentTransaction.rollback.verificationCommands[0], "rollback-artifact-validate", ["zero", "graph", "validate", "--json", patchedPath]);
assertCommandResultFields(patched.agentTransaction.rollback.verificationCommands[0], artifactVerificationResultFields);
assertCommand(patched.agentTransaction.rollback.verificationCommands[1], "rollback-graph-check", ["zero", "graph", "check", "--json", patchedPath]);
assertCommandResultFields(patched.agentTransaction.rollback.verificationCommands[1], graphVerificationResultFields);
assert.deepEqual((await runRequiredCommands(patched.agentTransaction.rollback.verificationCommands)).map((item) => item.purpose), ["rollback-artifact-validate", "rollback-graph-check"]);
assertCommand(patched.agentTransaction.verificationCommands[0], "artifact-validate", ["zero", "graph", "validate", "--json", patchedPath]);
assertCommandResultFields(patched.agentTransaction.verificationCommands[0], artifactVerificationResultFields);
assertCommand(patched.agentTransaction.verificationCommands[1], "graph-check", ["zero", "graph", "check", "--json", patchedPath]);
assertCommandResultFields(patched.agentTransaction.verificationCommands[1], graphVerificationResultFields);
assert.deepEqual((await runRequiredCommands(patched.agentTransaction.verificationCommands)).map((item) => item.purpose), ["artifact-validate", "graph-check"]);
assert.equal(patched.operations[0].actual, "hello transaction\n");
assert.equal(patched.operations[0].value, "hello patched transaction\n");
assert.equal(patched.evidenceBindings[0].operationIndex, 0);
assert.equal(patched.evidenceBindings[0].op, "set");
assert.equal(patched.evidenceBindings[0].graphHash, graph.graphHash);
assert.equal(patched.evidenceBindings[0].node, literal.id);
assert.equal(patched.evidenceBindings[0].field, "value");
assert.equal(patched.evidenceBindings[0].expect, "hello transaction\n");
assert.equal(patched.evidenceBindings[0].actual, "hello transaction\n");
assert.equal(patched.evidenceBindings[0].value, "hello patched transaction\n");
assert(patched.evidenceBindings[0].sourceFields.includes("originalGraphHash"));
assert(patched.evidenceBindings[0].sourceFields.includes("operations[].actual"));
const patchedTargetPath = join(outDir, "hello.patched-target.program-graph");
const patchedTarget = await zeroJson(["graph", "patch", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", patchedTargetPath, graphPath, patchPath]);
assert.deepEqual(patchedTarget.agentTransaction.command.argv, ["zero", "graph", "patch", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", patchedTargetPath, graphPath, patchPath]);
assertCommand(patchedTarget.agentTransaction.verificationCommands[0], "artifact-validate", ["zero", "graph", "validate", "--json", "--target", "linux-musl-x64", "--profile", "dev", patchedTargetPath]);
assertCommandResultFields(patchedTarget.agentTransaction.verificationCommands[0], artifactVerificationResultFields);
assertCommand(patchedTarget.agentTransaction.verificationCommands[1], "graph-check", ["zero", "graph", "check", "--json", "--target", "linux-musl-x64", "--profile", "dev", patchedTargetPath]);
assertCommandResultFields(patchedTarget.agentTransaction.verificationCommands[1], graphVerificationResultFields);
assert.deepEqual((await runRequiredCommands(patchedTarget.agentTransaction.verificationCommands)).map((item) => item.purpose), ["artifact-validate", "graph-check"]);

const missingPatchInput = await zeroJson(["graph", "patch", "--json", graphPath], { allowFailure: true });
assert.equal(missingPatchInput.ok, false);
assert.equal(missingPatchInput.agentTransaction.kind, "compiler-mediated-graph-patch-transaction");
assert.equal(missingPatchInput.agentTransaction.failure.code, "BLD002");
assert.equal(missingPatchInput.agentTransaction.failure.retryable, false);
assert.deepEqual(missingPatchInput.agentTransaction.verificationCommands, []);
const unreadablePatchInput = await zeroJson(["graph", "patch", "--json", graphPath, unreadablePatchPath], { allowFailure: true });
assert.equal(unreadablePatchInput.ok, false);
assert.equal(unreadablePatchInput.diagnostic.code, "GPH001");
assert.equal(unreadablePatchInput.agentTransaction.failure.code, "GPH001");
assert.equal(unreadablePatchInput.agentTransaction.failure.class, "invalid-patch");
assert.equal(unreadablePatchInput.agentTransaction.failure.phase, "parse");
assert.equal(unreadablePatchInput.agentTransaction.failure.retryable, true);
assert.equal(unreadablePatchInput.agentTransaction.failure.retryCommands[0].purpose, "graph-inspect");
assert.equal(unreadablePatchInput.agentTransaction.failure.retryCommands[0].required, true);
assert(unreadablePatchInput.agentTransaction.failure.retryCommands[0].argv.includes("--target"));
assert.equal(unreadablePatchInput.agentTransaction.failure.retryCommands[0].argv.at(-1), graphPath);
assert.deepEqual(unreadablePatchInput.agentTransaction.verificationCommands, []);

await execFileAsync(zero, ["graph", "validate", patchedPath]);
await execFileAsync(zero, ["graph", "check", patchedPath]);
assert.match(await readFile(patchedPath, "utf8"), /hello patched transaction/);

await writeFile(stalePatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "graph:0000000000000000"`,
  `set node="${literal.id}" field="value" expect="hello transaction\\n" value="stale should not save\\n"`,
  "",
].join("\n"));
const stale = await zeroJson(["graph", "patch", "--json", "--out", join(outDir, "stale.program-graph"), graphPath, stalePatchPath], { allowFailure: true });
assert.equal(stale.ok, false);
assert.equal(stale.agentTransaction.failure.code, "GPH002");
assert.equal(stale.agentTransaction.failure.class, "stale-graph");
assert.equal(stale.agentTransaction.failure.phase, "inspect");
assert.deepEqual(stale.agentTransaction.command.argv, ["zero", "graph", "patch", "--json", "--out", join(outDir, "stale.program-graph"), graphPath, stalePatchPath]);
assert.equal(stale.agentTransaction.failure.retryable, true);
assert.match(stale.agentTransaction.failure.retryStrategy, /rerun graph dump/);
assert.equal(stale.agentTransaction.failure.retryCommands[0].purpose, "graph-dump");
assert.equal(stale.agentTransaction.failure.retryCommands[0].required, true);
assert.deepEqual(stale.agentTransaction.failure.retryCommands[0].argv, ["zero", "graph", "dump", "--json", graphPath]);
assert.deepEqual((await runRequiredCommands(stale.agentTransaction.failure.retryCommands)).map((item) => item.purpose), ["graph-dump"]);
assert.equal(stale.agentTransaction.phaseAudit.inspect.ok, false);
assert.equal(stale.agentTransaction.phaseAudit.patch.ok, false);
assert.equal(stale.agentTransaction.phaseAudit.verify.status, "not-scheduled");
assert.equal(stale.agentTransaction.proofLedger.failureCode, "GPH002");
assert.equal(ledgerPhase(stale.agentTransaction, "inspect").status, "failed");
assert.equal(ledgerPhase(stale.agentTransaction, "verify").status, "not-scheduled");
assert.equal(ledgerPhase(stale.agentTransaction, "rollback").status, "not-available");
assert.deepEqual(ledgerPhase(stale.agentTransaction, "verify").requiredPurposes, []);
assert.equal(stale.agentTransaction.applied, false);
assert.equal(stale.agentTransaction.rollback.applied, false);
assert.equal(stale.agentTransaction.rollback.savedPath, null);
assert.deepEqual(stale.agentTransaction.verificationCommands, []);
const staleTarget = await zeroJson(["graph", "patch", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", join(outDir, "stale-target.program-graph"), graphPath, stalePatchPath], { allowFailure: true });
assert.deepEqual(staleTarget.agentTransaction.command.argv, ["zero", "graph", "patch", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", join(outDir, "stale-target.program-graph"), graphPath, stalePatchPath]);
assert.equal(staleTarget.agentTransaction.failure.retryCommands[0].purpose, "graph-dump");
assert.equal(staleTarget.agentTransaction.failure.retryCommands[0].required, true);
assert.deepEqual(staleTarget.agentTransaction.failure.retryCommands[0].argv, ["zero", "graph", "dump", "--json", "--target", "linux-musl-x64", "--profile", "dev", graphPath]);
assert.deepEqual((await runRequiredCommands(staleTarget.agentTransaction.failure.retryCommands)).map((item) => item.purpose), ["graph-dump"]);

await writeFile(mismatchPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graph.graphHash}"`,
  `set node="${literal.id}" field="value" expect="not current\\n" value="mismatch should not save\\n"`,
  "",
].join("\n"));
const mismatch = await zeroJson(["graph", "patch", "--json", "--out", join(outDir, "mismatch.program-graph"), graphPath, mismatchPatchPath], { allowFailure: true });
assert.equal(mismatch.ok, false);
assert.equal(mismatch.agentTransaction.failure.code, "GPH005");
assert.equal(mismatch.agentTransaction.failure.class, "precondition-failed");
assert.equal(mismatch.agentTransaction.failure.phase, "patch");
assert.match(mismatch.agentTransaction.failure.retryStrategy, /operations\[\]\.retryCommands graph-impact/);
assert.equal(mismatch.agentTransaction.failure.retryCommands[0].purpose, "graph-inspect");
assert.equal(mismatch.agentTransaction.failure.retryCommands[0].required, true);
assert.deepEqual(mismatch.agentTransaction.failure.retryCommands[0].argv, ["zero", "graph", "inspect", "--json", graphPath]);
assertCommand(mismatch.operations[0].retryCommands[0], "graph-impact", ["zero", "graph", "impact", "--json", "--node", literal.id, graphPath]);
assert.deepEqual((await runRequiredCommands(mismatch.agentTransaction.failure.retryCommands)).map((item) => item.purpose), ["graph-inspect"]);
assert.equal(mismatch.agentTransaction.phaseAudit.inspect.ok, true);
assert.equal(mismatch.agentTransaction.phaseAudit.patch.ok, false);
assert.equal(mismatch.agentTransaction.phaseAudit.validate.ok, false);
assert.equal(mismatch.agentTransaction.applied, false);
assert.equal(mismatch.operations[0].actual, "hello transaction\n");
assert.equal(mismatch.saved, null);

await writeFile(invalidValuePatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graph.graphHash}"`,
  `set node="${mainFunction.id}" field="public" value="maybe"`,
  "",
].join("\n"));
const invalidValue = await zeroJson(["graph", "patch", "--json", "--out", join(outDir, "invalid-value.program-graph"), graphPath, invalidValuePatchPath], { allowFailure: true });
assert.equal(invalidValue.ok, false);
assert.equal(invalidValue.agentTransaction.failure.code, "GPH003");
assert.equal(invalidValue.agentTransaction.failure.class, "invalid-patch-value");
assert.equal(invalidValue.agentTransaction.failure.phase, "patch");
assert.match(invalidValue.agentTransaction.failure.retryStrategy, /valid typed values/);
assertCommand(invalidValue.agentTransaction.failure.retryCommands[0], "graph-inspect", ["zero", "graph", "inspect", "--json", graphPath]);
assert.deepEqual((await runRequiredCommands(invalidValue.agentTransaction.failure.retryCommands)).map((item) => item.purpose), ["graph-inspect"]);
assert.equal(invalidValue.agentTransaction.phaseAudit.inspect.ok, true);
assert.equal(invalidValue.agentTransaction.phaseAudit.patch.ok, false);
assert.equal(invalidValue.agentTransaction.applied, false);
assert.equal(invalidValue.saved, null);

await writeFile(invalidSyntaxPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graph.graphHash}"`,
  `set node="${literal.id}" field="value" expect="hello transaction\\n" value="\\q"`,
  "",
].join("\n"));
const invalidSyntax = await zeroJson(["graph", "patch", "--json", "--out", join(outDir, "invalid-syntax.program-graph"), graphPath, invalidSyntaxPatchPath], { allowFailure: true });
assert.equal(invalidSyntax.ok, false);
assert.equal(invalidSyntax.agentTransaction.failure.code, "GPH001");
assert.equal(invalidSyntax.agentTransaction.failure.class, "invalid-patch");
assert.equal(invalidSyntax.agentTransaction.failure.phase, "parse");
assert.match(invalidSyntax.agentTransaction.failure.retryStrategy, /syntactically valid patch input/);
assertCommand(invalidSyntax.agentTransaction.failure.retryCommands[0], "graph-inspect", ["zero", "graph", "inspect", "--json", graphPath]);
assert.deepEqual((await runRequiredCommands(invalidSyntax.agentTransaction.failure.retryCommands)).map((item) => item.purpose), ["graph-inspect"]);
assert.equal(invalidSyntax.agentTransaction.phaseAudit.inspect.ok, true);
assert.equal(invalidSyntax.agentTransaction.phaseAudit.patch.ok, false);
assert.equal(invalidSyntax.agentTransaction.applied, false);
assert.equal(invalidSyntax.saved, null);

const replaceCalleeSource = join(outDir, "replace-callee.0");
await writeFile(replaceCalleeSource, `fn inc(value: i32) -> i32 {
    return value + 1
}

fn same(value: i32) -> i32 {
    return value
}

pub fn main() -> Void {
    let result: i32 = inc(41)
    expect result == 42
}
`);
const replaceGraph = await zeroJson(["graph", "dump", "--json", replaceCalleeSource]) as GraphJson;
const call = replaceGraph.nodes.find((node) => node.kind === "Call" && node.name === "inc");
assert(call);
const replaceCalleeMismatch = await zeroJson([
  "graph",
  "patch",
  "--json",
  replaceCalleeSource,
  "--expect-graph-hash",
  replaceGraph.graphHash,
  "--op",
  `replaceCallee node="${call.id}" expect="oldInc" value="same"`,
], { allowFailure: true });
assert.equal(replaceCalleeMismatch.ok, false);
assert.equal(replaceCalleeMismatch.operations[0].op, "replaceCallee");
assert.equal(replaceCalleeMismatch.operations[0].code, "GPH005");
assert.equal(replaceCalleeMismatch.operations[0].actual, "inc");
assert.equal(replaceCalleeMismatch.agentTransaction.failure.class, "precondition-failed");
assert.deepEqual(replaceCalleeMismatch.agentTransaction.command.argv, ["zero", "graph", "patch", "--json", replaceCalleeSource, "--expect-graph-hash", replaceGraph.graphHash, "--op", `replaceCallee node="${call.id}" expect="oldInc" value="same"`]);
assert.deepEqual(replaceCalleeMismatch.agentTransaction.verificationCommands, []);
assert.match(await readFile(replaceCalleeSource, "utf8"), /inc\(41\)/);
const replaceCallee = await zeroJson([
  "graph",
  "patch",
  "--json",
  replaceCalleeSource,
  "--expect-graph-hash",
  replaceGraph.graphHash,
  "--op",
  `replaceCallee node="${call.id}" expect="inc" value="same"`,
]);
assert.equal(replaceCallee.ok, true);
assert.equal(replaceCallee.operations[0].op, "replaceCallee");
assert.equal(replaceCallee.operations[0].actual, "inc");
assert.equal(replaceCallee.operations[0].value, "same");
assert.equal(replaceCallee.agentTransaction.operationCount, 1);
assert.deepEqual(replaceCallee.agentTransaction.command.argv, ["zero", "graph", "patch", "--json", replaceCalleeSource, "--expect-graph-hash", replaceGraph.graphHash, "--op", `replaceCallee node="${call.id}" expect="inc" value="same"`]);
assert.match(await readFile(replaceCalleeSource, "utf8"), /same\(41\)/);
await execFileAsync(zero, ["check", replaceCalleeSource]);

const renameSymbolSource = join(outDir, "rename-symbol.0");
const renameSymbolPatch = join(outDir, "rename-symbol.program-graph.patch");
await writeFile(renameSymbolSource, `fn helper(value: i32) -> i32 {
    return value + 1
}

fn existing(value: i32) -> i32 {
    return value
}

pub fn main() -> Void {
    let result: i32 = helper(41)
    expect result == 42
}
`);
const renameSymbolGraph = await zeroJson(["graph", "dump", "--json", renameSymbolSource]) as GraphJson;
const helperFunction = renameSymbolGraph.nodes.find((node) => node.kind === "Function" && node.name === "helper");
assert(helperFunction);
await writeFile(renameSymbolPatch, [
  "zero-program-graph-patch v1",
  `expect graphHash "${renameSymbolGraph.graphHash}"`,
  `renameSymbol node="${helperFunction.id}" expect="helper" value="compute"`,
  "",
].join("\n"));
const renameSymbol = await zeroJson(["graph", "patch", "--json", renameSymbolSource, renameSymbolPatch]);
assert.equal(renameSymbol.ok, true);
assert.equal(renameSymbol.operations[0].op, "renameSymbol");
assert.equal(renameSymbol.operations[0].actual, "helper");
assert.equal(renameSymbol.operations[0].value, "compute");
assert.equal(renameSymbol.agentTransaction.operationKinds.includes("renameSymbol"), true);
assert.equal(renameSymbol.agentTransaction.rollback.savedKind, "source");
const sourceRollbackAction = assertGraphPatchRollbackAction(renameSymbol.agentTransaction, "source");
assert.equal(renameSymbol.saved.graphHash, renameSymbol.patchedGraphHash);
assert.equal(renameSymbol.saveProof.semanticStable, true);
assert.equal(renameSymbol.saveProof.savedGraphHash, renameSymbol.patchedGraphHash);
assertCommand(renameSymbol.agentTransaction.rollback.verificationCommands[0], "rollback-source-check", ["zero", "check", "--json", renameSymbolSource]);
assertCommandResultFields(renameSymbol.agentTransaction.rollback.verificationCommands[0], sourceVerificationResultFields);
assertCommand(renameSymbol.agentTransaction.rollback.verificationCommands[1], "rollback-graph-check", ["zero", "graph", "check", "--json", renameSymbolSource]);
assertCommandResultFields(renameSymbol.agentTransaction.rollback.verificationCommands[1], graphVerificationResultFields);
assert.deepEqual((await runRequiredCommands(renameSymbol.agentTransaction.rollback.verificationCommands)).map((item) => item.purpose), ["rollback-source-check", "rollback-graph-check"]);
assert.deepEqual(renameSymbol.agentTransaction.command.argv, ["zero", "graph", "patch", "--json", renameSymbolSource, renameSymbolPatch]);
assertCommand(renameSymbol.agentTransaction.verificationCommands[0], "source-check", ["zero", "check", "--json", renameSymbolSource]);
assertCommandResultFields(renameSymbol.agentTransaction.verificationCommands[0], sourceVerificationResultFields);
assertCommand(renameSymbol.agentTransaction.verificationCommands[1], "graph-check", ["zero", "graph", "check", "--json", renameSymbolSource]);
assertCommandResultFields(renameSymbol.agentTransaction.verificationCommands[1], graphVerificationResultFields);
assert.deepEqual((await runRequiredCommands(renameSymbol.agentTransaction.verificationCommands)).map((item) => item.purpose), ["source-check", "graph-check"]);
const renamedSource = await readFile(renameSymbolSource, "utf8");
assert.match(renamedSource, /fn compute\(value: i32\)/);
assert.match(renamedSource, /compute\(41\)/);
await execFileAsync(zero, ["check", renameSymbolSource]);
const renamedGraph = await zeroJson(["graph", "dump", "--json", renameSymbolSource]) as GraphJson;
const computeFunction = renamedGraph.nodes.find((node) => node.kind === "Function" && node.name === "compute");
assert(computeFunction);
const duplicateRename = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameSymbolSource,
  "--expect-graph-hash",
  renamedGraph.graphHash,
  "--op",
  `renameSymbol node="${computeFunction.id}" expect="compute" value="existing"`,
], { allowFailure: true });
assert.equal(duplicateRename.ok, false);
assert.equal(duplicateRename.operations[0].op, "renameSymbol");
assert.equal(duplicateRename.operations[0].code, "GPH005");
assert.equal(duplicateRename.agentTransaction.failure.class, "precondition-failed");

const addImportSource = join(outDir, "add-import.0");
const addImportPatch = join(outDir, "add-import.program-graph.patch");
await writeFile(addImportSource, `pub fn main() -> Void {
}
`);
const addImportGraph = await zeroJson(["graph", "dump", "--json", addImportSource]) as GraphJson;
await writeFile(addImportPatch, [
  "zero-program-graph-patch v1",
  `expect graphHash "${addImportGraph.graphHash}"`,
  `addImport name="std.mem"`,
  "",
].join("\n"));
const addImport = await zeroJson(["graph", "patch", "--json", addImportSource, addImportPatch]);
assert.equal(addImport.ok, true);
assert.equal(addImport.operations[0].op, "addImport");
assert.equal(addImport.operations[0].name, "std.mem");
assert.equal(addImport.operations[0].value, "std.mem");
assert.match(addImport.operations[0].node, /^#import_/);
assert.equal(addImport.agentTransaction.operationKinds.includes("addImport"), true);
assert.equal(addImport.agentTransaction.applied, true);
assert.equal(addImport.agentTransaction.rollback.savedKind, "source");
assert.equal(addImport.agentTransaction.rollback.savedPath, addImportSource);
assert.deepEqual(addImport.agentTransaction.verificationCommands[0].argv, ["zero", "check", "--json", addImportSource]);
assert.deepEqual(addImport.agentTransaction.verificationCommands[1].argv, ["zero", "graph", "check", "--json", addImportSource]);
assert.match(await readFile(addImportSource, "utf8"), /^use std\.mem\n\npub fn main/);
await execFileAsync(zero, ["check", addImportSource]);
const duplicateImport = await zeroJson(["graph", "patch", "--json", addImportSource, "--op", `addImport name="std.mem"`], { allowFailure: true });
assert.equal(duplicateImport.ok, false);
assert.equal(duplicateImport.operations[0].op, "addImport");
assert.equal(duplicateImport.operations[0].code, "GPH005");
assert.equal(duplicateImport.agentTransaction.failure.class, "precondition-failed");
const removeImport = await zeroJson(["graph", "patch", "--json", addImportSource, "--op", `removeImport name="std.mem" expect="std.mem"`]);
assert.equal(removeImport.ok, true);
assert.equal(removeImport.operations[0].op, "removeImport");
assert.equal(removeImport.operations[0].actual, "std.mem");
assert.match(removeImport.operations[0].node, /^#/);
assert.equal(removeImport.agentTransaction.operationKinds.includes("removeImport"), true);
assert.equal(removeImport.agentTransaction.rollback.savedKind, "source");
assert.deepEqual(removeImport.agentTransaction.verificationCommands[0].argv, ["zero", "check", "--json", addImportSource]);
assert.doesNotMatch(await readFile(addImportSource, "utf8"), /^use std\.mem/m);
await execFileAsync(zero, ["check", addImportSource]);
const missingImport = await zeroJson(["graph", "patch", "--json", addImportSource, "--op", `removeImport name="std.mem"`], { allowFailure: true });
assert.equal(missingImport.ok, false);
assert.equal(missingImport.operations[0].op, "removeImport");
assert.equal(missingImport.operations[0].code, "GPH004");
assert.equal(missingImport.agentTransaction.failure.class, "missing-graph-target");

const explicitImportSource = join(outDir, "add-import-explicit-node.0");
await writeFile(explicitImportSource, `pub fn main() -> Void {
}
`);
const explicitImport = await zeroJson([
  "graph",
  "patch",
  "--json",
  explicitImportSource,
  "--op",
  `addImport node="#explicit_import_std_ascii" name="std.ascii"`,
]);
assert.equal(explicitImport.ok, true);
assert.equal(explicitImport.operations[0].op, "addImport");
assert.equal(explicitImport.operations[0].node, "#explicit_import_std_ascii");
assert.match(await readFile(explicitImportSource, "utf8"), /^use std\.ascii\n\npub fn main/);
await execFileAsync(zero, ["check", explicitImportSource]);

const addFunctionSource = join(outDir, "add-function.0");
await writeFile(addFunctionSource, `pub fn main() -> Void {
}
`);
const addFunctionGraph = await zeroJson(["graph", "dump", "--json", addFunctionSource]) as GraphJson;
const addFunction = await zeroJson([
  "graph",
  "patch",
  "--json",
  addFunctionSource,
  "--expect-graph-hash",
  addFunctionGraph.graphHash,
  "--op",
  `addFunction name="answer" type="i32" value="42"`,
]);
assert.equal(addFunction.ok, true);
assert.equal(addFunction.operations[0].op, "addFunction");
assert.equal(addFunction.operations[0].actual, addFunctionGraph.nodes.find((node) => node.kind === "Module")?.id);
assert.match(addFunction.operations[0].node, /^#fn_/);
assert.equal(addFunction.agentTransaction.operationKinds.includes("addFunction"), true);
const addFunctionText = await readFile(addFunctionSource, "utf8");
assert.match(addFunctionText, /fn answer\(\) -> i32 {\n    return 42\n}/);
await execFileAsync(zero, ["check", addFunctionSource]);
const explicitFunctionSource = join(outDir, "add-function-explicit-node.0");
await writeFile(explicitFunctionSource, `pub fn main() -> Void {
}
`);
const explicitFunction = await zeroJson([
  "graph",
  "patch",
  "--json",
  explicitFunctionSource,
  "--op",
  `addFunction node="#explicit_answer_fn" name="explicitAnswer" type="i32" value="7"`,
]);
assert.equal(explicitFunction.ok, true);
assert.equal(explicitFunction.operations[0].op, "addFunction");
assert.equal(explicitFunction.operations[0].node, "#explicit_answer_fn");
assert.match(await readFile(explicitFunctionSource, "utf8"), /fn explicitAnswer\(\) -> i32 {\n    return 7\n}/);
await execFileAsync(zero, ["check", explicitFunctionSource]);
const addFunctionWithParams = await zeroJson([
  "graph",
  "patch",
  "--json",
  addFunctionSource,
  "--op",
  `addFunction name="answerWithInput" type="i32" params="arg0:i32,arg1:Bool" value="42"`,
]);
assert.equal(addFunctionWithParams.ok, true);
assert.equal(addFunctionWithParams.operations[0].op, "addFunction");
assert.equal(addFunctionWithParams.operations[0].params, "arg0:i32,arg1:Bool");
assert.equal(addFunctionWithParams.agentTransaction.operationKinds.includes("addFunction"), true);
const addFunctionWithParamsText = await readFile(addFunctionSource, "utf8");
assert.match(addFunctionWithParamsText, /fn answerWithInput\(arg0: i32, arg1: Bool\) -> i32 {\n    return 42\n}/);
await execFileAsync(zero, ["check", addFunctionSource]);
const duplicateFunction = await zeroJson(["graph", "patch", "--json", addFunctionSource, "--op", `addFunction name="answer" type="i32" value="0"`], { allowFailure: true });
assert.equal(duplicateFunction.ok, false);
assert.equal(duplicateFunction.operations[0].op, "addFunction");
assert.equal(duplicateFunction.operations[0].code, "GPH005");
assert.equal(duplicateFunction.agentTransaction.failure.class, "precondition-failed");

const removeFunctionSource = join(outDir, "remove-function.0");
await writeFile(removeFunctionSource, `fn unused() -> i32 {
    return 1
}

fn used() -> i32 {
    return 2
}

pub fn main() -> Void {
    let result: i32 = used()
}
`);
const removeFunctionGraph = await zeroJson(["graph", "dump", "--json", removeFunctionSource]) as GraphJson;
const unusedFunction = removeFunctionGraph.nodes.find((node) => node.kind === "Function" && node.name === "unused");
const usedFunction = removeFunctionGraph.nodes.find((node) => node.kind === "Function" && node.name === "used");
assert(unusedFunction);
assert(usedFunction);
const removeUsedFunction = await zeroJson([
  "graph",
  "patch",
  "--json",
  removeFunctionSource,
  "--op",
  `removeFunction node="${usedFunction.id}" expect="used"`,
], { allowFailure: true });
assert.equal(removeUsedFunction.ok, false);
assert.equal(removeUsedFunction.operations[0].op, "removeFunction");
assert.equal(removeUsedFunction.operations[0].code, "GPH005");
assert.match(removeUsedFunction.operations[0].message, /still called/);
assertCommand(removeUsedFunction.operations[0].retryCommands[0], "graph-impact", ["zero", "graph", "impact", "--json", "--node", usedFunction.id, removeFunctionSource]);
assert.deepEqual((await runRequiredCommands(removeUsedFunction.operations[0].retryCommands)).map((item) => item.purpose), ["graph-impact"]);
const badRemoveFunction = await zeroJson([
  "graph",
  "patch",
  "--json",
  removeFunctionSource,
  "--op",
  `removeFunction node="${unusedFunction.id}" expect="stale"`,
], { allowFailure: true });
assert.equal(badRemoveFunction.ok, false);
assert.equal(badRemoveFunction.operations[0].op, "removeFunction");
assert.equal(badRemoveFunction.operations[0].code, "GPH005");
assert.equal(badRemoveFunction.operations[0].actual, "unused");
const removeFunction = await zeroJson([
  "graph",
  "patch",
  "--json",
  removeFunctionSource,
  "--op",
  `removeFunction node="${unusedFunction.id}" expect="unused"`,
]);
assert.equal(removeFunction.ok, true);
assert.equal(removeFunction.operations[0].op, "removeFunction");
assert.equal(removeFunction.operations[0].actual, "unused");
assert.equal(removeFunction.agentTransaction.operationKinds.includes("removeFunction"), true);
const removeFunctionText = await readFile(removeFunctionSource, "utf8");
assert.doesNotMatch(removeFunctionText, /fn unused/);
assert.match(removeFunctionText, /fn used\(\) -> i32/);
await execFileAsync(zero, ["check", removeFunctionSource]);

const addParamSource = join(outDir, "add-param.0");
await writeFile(addParamSource, `fn add(left: i32, right: i32) -> i32 {
    return left + right
}

pub fn main() -> Void {
    let total: i32 = add(40, 2)
}
`);
const addParamGraph = await zeroJson(["graph", "dump", "--json", addParamSource]) as GraphJson;
const addFunctionNode = addParamGraph.nodes.find((node) => node.kind === "Function" && node.name === "add");
assert(addFunctionNode);
const addParamMissingValue = await zeroJson([
  "graph",
  "patch",
  "--json",
  addParamSource,
  "--expect-graph-hash",
  addParamGraph.graphHash,
  "--op",
  `addParam node="${addFunctionNode.id}" expect="add" name="bias" type="i32"`,
], { allowFailure: true });
assert.equal(addParamMissingValue.ok, false);
assert.equal(addParamMissingValue.operations[0].op, "addParam");
assert.equal(addParamMissingValue.operations[0].code, "GPH001");
assert.equal(addParamMissingValue.agentTransaction.applied, false);
const addParam = await zeroJson([
  "graph",
  "patch",
  "--json",
  addParamSource,
  "--expect-graph-hash",
  addParamGraph.graphHash,
  "--op",
  `addParam node="${addFunctionNode.id}" expect="add" name="bias" type="i32" value="0"`,
]);
assert.equal(addParam.ok, true);
assert.equal(addParam.operations[0].op, "addParam");
assert.equal(addParam.operations[0].actual, "add");
assert.match(addParam.operations[0].to, /^#param_/);
assert.equal(addParam.agentTransaction.operationKinds.includes("addParam"), true);
const addParamText = await readFile(addParamSource, "utf8");
assert.match(addParamText, /fn add\(left: i32, right: i32, bias: i32\) -> i32/);
assert.match(addParamText, /add\(40, 2, 0\)/);
await execFileAsync(zero, ["check", addParamSource]);
const duplicateAddedParam = await zeroJson(["graph", "patch", "--json", addParamSource, "--op", `addParam node="${addFunctionNode.id}" expect="add" name="bias" type="i32" value="0"`], { allowFailure: true });
assert.equal(duplicateAddedParam.ok, false);
assert.equal(duplicateAddedParam.operations[0].op, "addParam");
assert.equal(duplicateAddedParam.operations[0].code, "GPH005");

const removeParamSource = join(outDir, "remove-param.0");
await writeFile(removeParamSource, `fn add(left: i32, right: i32, unused: i32) -> i32 {
    return left + right
}

pub fn main() -> Void {
    let total: i32 = add(40, 2, 0)
}
`);
const removeParamGraph = await zeroJson(["graph", "dump", "--json", removeParamSource]) as GraphJson;
const unusedParam = removeParamGraph.nodes.find((node) => node.kind === "Param" && node.name === "unused");
assert(unusedParam);
const usedParam = removeParamGraph.nodes.find((node) => node.kind === "Param" && node.name === "left");
assert(usedParam);
const removeUsedParam = await zeroJson([
  "graph",
  "patch",
  "--json",
  removeParamSource,
  "--expect-graph-hash",
  removeParamGraph.graphHash,
  "--op",
  `removeParam node="${usedParam.id}" expect="left"`,
], { allowFailure: true });
assert.equal(removeUsedParam.ok, false);
assert.equal(removeUsedParam.operations[0].op, "removeParam");
assert.equal(removeUsedParam.operations[0].code, "GPH005");
assert.match(removeUsedParam.operations[0].message, /still used/);
assert.equal(removeUsedParam.agentTransaction.applied, false);
const removeParam = await zeroJson([
  "graph",
  "patch",
  "--json",
  removeParamSource,
  "--expect-graph-hash",
  removeParamGraph.graphHash,
  "--op",
  `removeParam node="${unusedParam.id}" expect="unused"`,
]);
assert.equal(removeParam.ok, true);
assert.equal(removeParam.operations[0].op, "removeParam");
assert.equal(removeParam.operations[0].actual, "unused");
assert.equal(removeParam.agentTransaction.operationKinds.includes("removeParam"), true);
const removeParamText = await readFile(removeParamSource, "utf8");
assert.match(removeParamText, /fn add\(left: i32, right: i32\) -> i32/);
assert.match(removeParamText, /add\(40, 2\)/);
await execFileAsync(zero, ["check", removeParamSource]);

const replaceImportSource = join(outDir, "replace-import.0");
await writeFile(replaceImportSource, `use std.mem
use std.io

pub fn main() -> Void {
}
`);
const duplicateReplaceImport = await zeroJson([
  "graph",
  "patch",
  "--json",
  replaceImportSource,
  "--op",
  `replaceImport name="std.mem" expect="std.mem" value="std.io"`,
], { allowFailure: true });
assert.equal(duplicateReplaceImport.ok, false);
assert.equal(duplicateReplaceImport.operations[0].op, "replaceImport");
assert.equal(duplicateReplaceImport.operations[0].code, "GPH005");
assert.equal(duplicateReplaceImport.agentTransaction.failure.class, "precondition-failed");
const replaceImport = await zeroJson([
  "graph",
  "patch",
  "--json",
  replaceImportSource,
  "--op",
  `replaceImport name="std.mem" expect="std.mem" value="std.ascii"`,
]);
assert.equal(replaceImport.ok, true);
assert.equal(replaceImport.operations[0].op, "replaceImport");
assert.equal(replaceImport.operations[0].actual, "std.mem");
assert.equal(replaceImport.operations[0].value, "std.ascii");
assert.equal(replaceImport.agentTransaction.operationKinds.includes("replaceImport"), true);
const replaceImportText = await readFile(replaceImportSource, "utf8");
assert.match(replaceImportText, /^use std\.ascii\n\nuse std\.io\n\npub fn main/);
await execFileAsync(zero, ["check", replaceImportSource]);

const renameImportAliasSource = join(outDir, "rename-import-alias.0");
await writeFile(renameImportAliasSource, `use std.mem as memory
use std.mem as bytes

pub fn main() -> Void {
}
`);
const aliasGraph = await zeroJson(["graph", "dump", "--json", renameImportAliasSource]) as GraphJson;
const memoryImport = aliasGraph.nodes.find((node) => node.kind === "Import" && node.name === "std.mem" && node.value === "memory");
assert(memoryImport);
const duplicateAlias = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameImportAliasSource,
  "--op",
  `renameImportAlias node="${memoryImport.id}" expect="memory" value="bytes"`,
], { allowFailure: true });
assert.equal(duplicateAlias.ok, false);
assert.equal(duplicateAlias.operations[0].op, "renameImportAlias");
assert.equal(duplicateAlias.operations[0].code, "GPH005");
assert.equal(duplicateAlias.agentTransaction.failure.class, "precondition-failed");
const renameImportAlias = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameImportAliasSource,
  "--op",
  `renameImportAlias node="${memoryImport.id}" expect="memory" value="buffer"`,
]);
assert.equal(renameImportAlias.ok, true);
assert.equal(renameImportAlias.operations[0].op, "renameImportAlias");
assert.equal(renameImportAlias.operations[0].actual, "memory");
assert.equal(renameImportAlias.operations[0].value, "buffer");
assert.equal(renameImportAlias.agentTransaction.operationKinds.includes("renameImportAlias"), true);
assert.match(await readFile(renameImportAliasSource, "utf8"), /^use std\.mem as buffer\n\nuse std\.mem as bytes\n\npub fn main/);
await execFileAsync(zero, ["check", renameImportAliasSource]);

const changeReturnTypeSource = join(outDir, "change-return-type.0");
await writeFile(changeReturnTypeSource, `fn value() -> i32 {
    return 41
}

pub fn main() -> Void {
}
`);
const returnTypeGraph = await zeroJson(["graph", "dump", "--json", changeReturnTypeSource]) as GraphJson;
const valueFunction = returnTypeGraph.nodes.find((node) => node.kind === "Function" && node.name === "value");
assert(valueFunction);
const badReturnType = await zeroJson([
  "graph",
  "patch",
  "--json",
  changeReturnTypeSource,
  "--op",
  `changeReturnType node="${valueFunction.id}" expect="usize" value="i64"`,
], { allowFailure: true });
assert.equal(badReturnType.ok, false);
assert.equal(badReturnType.operations[0].op, "changeReturnType");
assert.equal(badReturnType.operations[0].code, "GPH005");
assert.equal(badReturnType.operations[0].actual, "i32");
assert.equal(badReturnType.agentTransaction.failure.class, "precondition-failed");
const changeReturnType = await zeroJson([
  "graph",
  "patch",
  "--json",
  changeReturnTypeSource,
  "--op",
  `changeReturnType node="${valueFunction.id}" expect="i32" value="i64"`,
]);
assert.equal(changeReturnType.ok, true);
assert.equal(changeReturnType.operations[0].op, "changeReturnType");
assert.equal(changeReturnType.operations[0].actual, "i32");
assert.equal(changeReturnType.operations[0].value, "i64");
assert.equal(changeReturnType.agentTransaction.operationKinds.includes("changeReturnType"), true);
assert.match(await readFile(changeReturnTypeSource, "utf8"), /^fn value\(\) -> i64/);
await execFileAsync(zero, ["check", changeReturnTypeSource]);

const renameParamSource = join(outDir, "rename-param.0");
await writeFile(renameParamSource, `fn echo(value: i32, other: i32) -> i32 {
    return value
}

pub fn main() -> Void {
}
`);
const renameParamGraph = await zeroJson(["graph", "dump", "--json", renameParamSource]) as GraphJson;
const renameValueParam = renameParamGraph.nodes.find((node) => node.kind === "Param" && node.name === "value" && node.type === "i32");
assert(renameValueParam);
const duplicateParam = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameParamSource,
  "--op",
  `renameParam node="${renameValueParam.id}" expect="value" value="other"`,
], { allowFailure: true });
assert.equal(duplicateParam.ok, false);
assert.equal(duplicateParam.operations[0].op, "renameParam");
assert.equal(duplicateParam.operations[0].code, "GPH005");
assert.equal(duplicateParam.agentTransaction.failure.class, "precondition-failed");
const badRenameParam = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameParamSource,
  "--op",
  `renameParam node="${renameValueParam.id}" expect="stale" value="input"`,
], { allowFailure: true });
assert.equal(badRenameParam.ok, false);
assert.equal(badRenameParam.operations[0].op, "renameParam");
assert.equal(badRenameParam.operations[0].code, "GPH005");
assert.equal(badRenameParam.operations[0].actual, "value");
const renameParam = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameParamSource,
  "--op",
  `renameParam node="${renameValueParam.id}" expect="value" value="input"`,
]);
assert.equal(renameParam.ok, true);
assert.equal(renameParam.operations[0].op, "renameParam");
assert.equal(renameParam.operations[0].actual, "value");
assert.equal(renameParam.operations[0].value, "input");
assert.equal(renameParam.agentTransaction.operationKinds.includes("renameParam"), true);
assert.match(await readFile(renameParamSource, "utf8"), /^fn echo\(input: i32, other: i32\) -> i32 {\n    return input/m);
await execFileAsync(zero, ["check", renameParamSource]);

const renameLocalSource = join(outDir, "rename-local.0");
await writeFile(renameLocalSource, `fn total() -> i32 {
    let count: i32 = 40
    let next: i32 = count + 2
    return next
}

pub fn main() -> Void {
}
`);
const renameLocalGraph = await zeroJson(["graph", "dump", "--json", renameLocalSource]) as GraphJson;
const countLocal = renameLocalGraph.nodes.find((node) => node.kind === "Let" && node.name === "count" && node.type === "i32");
assert(countLocal);
const nextLocal = renameLocalGraph.nodes.find((node) => node.kind === "Let" && node.name === "next" && node.type === "i32");
assert(nextLocal);
const duplicateLocal = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameLocalSource,
  "--op",
  `renameLocal node="${countLocal.id}" expect="count" value="next"`,
], { allowFailure: true });
assert.equal(duplicateLocal.ok, false);
assert.equal(duplicateLocal.operations[0].op, "renameLocal");
assert.equal(duplicateLocal.operations[0].code, "GPH005");
assert.equal(duplicateLocal.agentTransaction.failure.class, "precondition-failed");
const badRenameLocal = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameLocalSource,
  "--op",
  `renameLocal node="${countLocal.id}" expect="stale" value="totalCount"`,
], { allowFailure: true });
assert.equal(badRenameLocal.ok, false);
assert.equal(badRenameLocal.operations[0].op, "renameLocal");
assert.equal(badRenameLocal.operations[0].code, "GPH005");
assert.equal(badRenameLocal.operations[0].actual, "count");
const renameLocal = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameLocalSource,
  "--op",
  `renameLocal node="${countLocal.id}" expect="count" value="totalCount"`,
]);
assert.equal(renameLocal.ok, true);
assert.equal(renameLocal.operations[0].op, "renameLocal");
assert.equal(renameLocal.operations[0].actual, "count");
assert.equal(renameLocal.operations[0].value, "totalCount");
assert.equal(renameLocal.agentTransaction.operationKinds.includes("renameLocal"), true);
assert.match(await readFile(renameLocalSource, "utf8"), /let totalCount: i32 = 40\n    let next: i32 = totalCount \+ 2/);
await execFileAsync(zero, ["check", renameLocalSource]);

const changeLocalTypeSource = join(outDir, "change-local-type.0");
await writeFile(changeLocalTypeSource, `fn local() -> i32 {
    let value: i32 = 40
    return value
}

pub fn main() -> Void {
}
`);
const localTypeGraph = await zeroJson(["graph", "dump", "--json", changeLocalTypeSource]) as GraphJson;
const localValue = localTypeGraph.nodes.find((node) => node.kind === "Let" && node.name === "value" && node.type === "i32");
assert(localValue);
const localFunction = localTypeGraph.nodes.find((node) => node.kind === "Function" && node.name === "local" && node.type === "i32");
assert(localFunction);
const badLocalType = await zeroJson([
  "graph",
  "patch",
  "--json",
  changeLocalTypeSource,
  "--op",
  `changeLocalType node="${localValue.id}" expect="usize" value="i64"`,
], { allowFailure: true });
assert.equal(badLocalType.ok, false);
assert.equal(badLocalType.operations[0].op, "changeLocalType");
assert.equal(badLocalType.operations[0].code, "GPH005");
assert.equal(badLocalType.operations[0].actual, "i32");
assert.equal(badLocalType.agentTransaction.failure.class, "precondition-failed");
const changeLocalType = await zeroJson([
  "graph",
  "patch",
  "--json",
  changeLocalTypeSource,
  "--op",
  `changeLocalType node="${localValue.id}" expect="i32" value="i64"`,
  "--op",
  `changeReturnType node="${localFunction.id}" expect="i32" value="i64"`,
]);
assert.equal(changeLocalType.ok, true);
assert.equal(changeLocalType.operationCount, 2);
assert.equal(changeLocalType.operations[0].op, "changeLocalType");
assert.equal(changeLocalType.operations[0].actual, "i32");
assert.equal(changeLocalType.operations[0].value, "i64");
assert.equal(changeLocalType.operations[1].op, "changeReturnType");
assert.equal(changeLocalType.agentTransaction.operationKinds.includes("changeLocalType"), true);
assert.match(await readFile(changeLocalTypeSource, "utf8"), /^fn local\(\) -> i64 {\n    let value: i64 = 40\n    return value/m);
await execFileAsync(zero, ["check", changeLocalTypeSource]);

const renameFieldSource = join(outDir, "rename-field.0");
await writeFile(renameFieldSource, `type Point {
    x: i32,
    y: i32,
}

fn sum(point: Point) -> i32 {
    return point.x + point.y
}

pub fn main() -> Void {
    let point: Point = Point { x: 40, y: 2 }
    let total: i32 = sum(point)
}
`);
const renameFieldGraph = await zeroJson(["graph", "dump", "--json", renameFieldSource]) as GraphJson;
const xField = renameFieldGraph.nodes.find((node) => node.kind === "Field" && node.name === "x" && node.type === "i32");
assert(xField);
const duplicateField = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameFieldSource,
  "--op",
  `renameField node="${xField.id}" expect="x" value="y"`,
], { allowFailure: true });
assert.equal(duplicateField.ok, false);
assert.equal(duplicateField.operations[0].op, "renameField");
assert.equal(duplicateField.operations[0].code, "GPH005");
assert.equal(duplicateField.agentTransaction.failure.class, "precondition-failed");
const badRenameField = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameFieldSource,
  "--op",
  `renameField node="${xField.id}" expect="stale" value="left"`,
], { allowFailure: true });
assert.equal(badRenameField.ok, false);
assert.equal(badRenameField.operations[0].op, "renameField");
assert.equal(badRenameField.operations[0].code, "GPH005");
assert.equal(badRenameField.operations[0].actual, "x");
const renameField = await zeroJson([
  "graph",
  "patch",
  "--json",
  renameFieldSource,
  "--op",
  `renameField node="${xField.id}" expect="x" value="left"`,
]);
assert.equal(renameField.ok, true);
assert.equal(renameField.operations[0].op, "renameField");
assert.equal(renameField.operations[0].actual, "x");
assert.equal(renameField.operations[0].value, "left");
assert.equal(renameField.agentTransaction.operationKinds.includes("renameField"), true);
const renameFieldText = await readFile(renameFieldSource, "utf8");
assert.match(renameFieldText, /left: i32/);
assert.match(renameFieldText, /return point\.left \+ point\.y/);
assert.match(renameFieldText, /Point \{ left: 40, y: 2 \}/);
await execFileAsync(zero, ["check", renameFieldSource]);

const changeFieldTypeSource = join(outDir, "change-field-type.0");
await writeFile(changeFieldTypeSource, `type Point {
    x: i32,
    y: i32,
}

fn getX(point: Point) -> i32 {
    return point.x
}

pub fn main() -> Void {
    let point: Point = Point { x: 40, y: 2 }
}
`);
const changeFieldTypeGraph = await zeroJson(["graph", "dump", "--json", changeFieldTypeSource]) as GraphJson;
const xTypeField = changeFieldTypeGraph.nodes.find((node) => node.kind === "Field" && node.name === "x" && node.type === "i32");
assert(xTypeField);
const getXFunction = changeFieldTypeGraph.nodes.find((node) => node.kind === "Function" && node.name === "getX" && node.type === "i32");
assert(getXFunction);
const badFieldType = await zeroJson([
  "graph",
  "patch",
  "--json",
  changeFieldTypeSource,
  "--op",
  `changeFieldType node="${xTypeField.id}" expect="usize" value="i64"`,
], { allowFailure: true });
assert.equal(badFieldType.ok, false);
assert.equal(badFieldType.operations[0].op, "changeFieldType");
assert.equal(badFieldType.operations[0].code, "GPH005");
assert.equal(badFieldType.operations[0].actual, "i32");
assert.equal(badFieldType.agentTransaction.failure.class, "precondition-failed");
const changeFieldType = await zeroJson([
  "graph",
  "patch",
  "--json",
  changeFieldTypeSource,
  "--op",
  `changeFieldType node="${xTypeField.id}" expect="i32" value="i64"`,
  "--op",
  `changeReturnType node="${getXFunction.id}" expect="i32" value="i64"`,
]);
assert.equal(changeFieldType.ok, true);
assert.equal(changeFieldType.operationCount, 2);
assert.equal(changeFieldType.operations[0].op, "changeFieldType");
assert.equal(changeFieldType.operations[0].actual, "i32");
assert.equal(changeFieldType.operations[0].value, "i64");
assert.equal(changeFieldType.operations[1].op, "changeReturnType");
assert.equal(changeFieldType.agentTransaction.operationKinds.includes("changeFieldType"), true);
const changeFieldTypeText = await readFile(changeFieldTypeSource, "utf8");
assert.match(changeFieldTypeText, /x: i64/);
assert.match(changeFieldTypeText, /fn getX\(point: Point\) -> i64/);
assert.match(changeFieldTypeText, /Point \{ x: 40, y: 2 \}/);
await execFileAsync(zero, ["check", changeFieldTypeSource]);

const changeParamTypeSource = join(outDir, "change-param-type.0");
await writeFile(changeParamTypeSource, `fn echo(value: i32) -> i32 {
    return value
}

pub fn main() -> Void {
}
`);
const paramTypeGraph = await zeroJson(["graph", "dump", "--json", changeParamTypeSource]) as GraphJson;
const echoFunction = paramTypeGraph.nodes.find((node) => node.kind === "Function" && node.name === "echo" && node.type === "i32");
assert(echoFunction);
const valueParam = paramTypeGraph.nodes.find((node) => node.kind === "Param" && node.name === "value" && node.type === "i32");
assert(valueParam);
const badParamType = await zeroJson([
  "graph",
  "patch",
  "--json",
  changeParamTypeSource,
  "--op",
  `changeParamType node="${valueParam.id}" expect="usize" value="i64"`,
], { allowFailure: true });
assert.equal(badParamType.ok, false);
assert.equal(badParamType.operations[0].op, "changeParamType");
assert.equal(badParamType.operations[0].code, "GPH005");
assert.equal(badParamType.operations[0].actual, "i32");
assert.equal(badParamType.agentTransaction.failure.class, "precondition-failed");
const changeParamType = await zeroJson([
  "graph",
  "patch",
  "--json",
  changeParamTypeSource,
  "--op",
  `changeParamType node="${valueParam.id}" expect="i32" value="i64"`,
  "--op",
  `changeReturnType node="${echoFunction.id}" expect="i32" value="i64"`,
]);
assert.equal(changeParamType.ok, true);
assert.equal(changeParamType.operationCount, 2);
assert.equal(changeParamType.operations[0].op, "changeParamType");
assert.equal(changeParamType.operations[0].actual, "i32");
assert.equal(changeParamType.operations[0].value, "i64");
assert.equal(changeParamType.operations[1].op, "changeReturnType");
assert.equal(changeParamType.agentTransaction.operationKinds.includes("changeParamType"), true);
assert.match(await readFile(changeParamTypeSource, "utf8"), /^fn echo\(value: i64\) -> i64/);
await execFileAsync(zero, ["check", changeParamTypeSource]);

const proofAudit = {
  required: executedProofs.length,
  passed: executedProofs.filter((item) => item.ok).length,
  failed: executedProofs.filter((item) => !item.ok).length,
  purposes: [...new Set(executedProofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(executedProofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
const successfulSemanticEdits = [
  replaceCallee,
  renameSymbol,
  addImport,
  removeImport,
  addFunction,
  addFunctionWithParams,
  removeFunction,
  addParam,
  removeParam,
  replaceImport,
  renameImportAlias,
  changeReturnType,
  renameParam,
  renameLocal,
  changeLocalType,
  renameField,
  changeFieldType,
  changeParamType,
];
const rejectedSemanticEdits = [
  replaceCalleeMismatch,
  duplicateRename,
  duplicateImport,
  missingImport,
  duplicateFunction,
  removeUsedFunction,
  badRemoveFunction,
  addParamMissingValue,
  duplicateAddedParam,
  removeUsedParam,
  duplicateReplaceImport,
  duplicateAlias,
  badReturnType,
  duplicateParam,
  badRenameParam,
  duplicateLocal,
  badRenameLocal,
  badLocalType,
  duplicateField,
  badRenameField,
  badFieldType,
  badParamType,
];
const coveredOperations = [...new Set(successfulSemanticEdits.flatMap((item) => item.operations?.filter((op) => op.ok !== false).map((op) => op.op) ?? []))].sort();
const conflictRejections = rejectedSemanticEdits.filter((item) => item.ok === false && item.agentTransaction?.applied === false).length;
const semanticEditAudit = {
  coveredOperations,
  categories: ["call-graph", "imports", "signature", "symbol-rename", "type-propagation"].sort(),
  checkedPreconditions: ["graph-hash", "expect-value", "node-kind", "name-conflict", "stale-context"].sort(),
  operationSurfaceBacked: requiredSemanticOperations.every((op) => patched.agentTransaction.operationKinds.includes(op) && patched.agentTransaction.operationContracts.some((item) => item.op === op)),
  sourceBackedTransactions: successfulSemanticEdits.filter((item) => item.agentTransaction?.rollback?.savedKind === "source").length,
  conflictRejections,
  checkedSourceRewrite: true,
};
const failedTransactions = [
  missingPatchInput,
  unreadablePatchInput,
  stale,
  mismatch,
  invalidValue,
  invalidSyntax,
  missingImport,
  staleTarget,
  replaceCalleeMismatch,
].filter((item) => item?.agentTransaction?.failure);
const failureAudit = {
  declaredCodes: [...new Set(patched.agentTransaction.failureClasses.map((item) => item.code))].sort(),
  declaredClasses: [...new Set(patched.agentTransaction.failureClasses.map((item) => item.class))].sort(),
  observedCodes: [...new Set(failedTransactions.map((item) => item.agentTransaction.failure.code))].sort(),
  observedClasses: [...new Set(failedTransactions.map((item) => item.agentTransaction.failure.class))].sort(),
  observedPhases: [...new Set(failedTransactions.map((item) => item.agentTransaction.failure.phase))].sort(),
  retryPurposes: [...new Set(failedTransactions.flatMap((item) =>
    item.agentTransaction.failure.retryCommands?.map((command) => command.purpose) ?? []))].sort(),
  operationRetryPurposes: [...new Set(failedTransactions.flatMap((item) =>
    item.operations?.flatMap((op) => op.retryCommands?.map((command) => command.purpose) ?? []) ?? []))].sort(),
  nonRetryableCodes: patched.agentTransaction.failureClasses
    .filter((item) => item.retryable === false)
    .map((item) => item.code)
    .sort(),
  targetProfileRetryPreserved: staleTarget.agentTransaction.failure.retryCommands[0].argv.includes("--target") &&
    staleTarget.agentTransaction.failure.retryCommands[0].argv.includes("linux-musl-x64") &&
    staleTarget.agentTransaction.failure.retryCommands[0].argv.includes("--profile") &&
    staleTarget.agentTransaction.failure.retryCommands[0].argv.includes("dev"),
  noVerifyOnFailure: failedTransactions.every((item) =>
    Array.isArray(item.agentTransaction.verificationCommands) &&
    item.agentTransaction.verificationCommands.length === 0),
  failureRetryCommandContracts: ["GPH001", "GPH002", "GPH003", "GPH004"].every((code) =>
    patched.agentTransaction.retryCommandContracts?.[code]?.argv?.includes("--target") &&
    patched.agentTransaction.retryCommandContracts?.[code]?.argv?.includes("<same-target>") &&
    patched.agentTransaction.retryCommandContracts?.[code]?.argv?.includes("--profile") &&
    patched.agentTransaction.retryCommandContracts?.[code]?.argv?.includes("<same-profile>")) &&
    patched.agentTransaction.retryCommandContracts?.GPH005?.argvField === "operations[].retryCommands[].argv" &&
    patched.agentTransaction.retryCommandContracts?.GPH006 === null,
  invalidPatchInputRetryStructured: unreadablePatchInput.agentTransaction.failure.code === "GPH001" &&
    unreadablePatchInput.agentTransaction.failure.retryable === true &&
    unreadablePatchInput.agentTransaction.failure.retryCommands[0]?.purpose === "graph-inspect" &&
    unreadablePatchInput.agentTransaction.failure.retryCommands[0]?.argv?.includes("--target") &&
    unreadablePatchInput.agentTransaction.failure.retryCommands[0]?.argv?.at(-1) === graphPath &&
    unreadablePatchInput.agentTransaction.verificationCommands.length === 0,
  inlinePatchCommandArgsPreserved: replaceCallee.agentTransaction.command.argv.includes("--expect-graph-hash") &&
    replaceCallee.agentTransaction.command.argv.includes(replaceGraph.graphHash) &&
    replaceCallee.agentTransaction.command.argv.includes("--op") &&
    replaceCalleeMismatch.agentTransaction.command.argv.includes("--expect-graph-hash") &&
    replaceCalleeMismatch.agentTransaction.command.argv.includes("--op") &&
    !replaceCallee.agentTransaction.command.argv.includes("<inline>") &&
    !replaceCalleeMismatch.agentTransaction.command.argv.includes("<inline>"),
};
const proofLedgerAudit = {
  phaseOrderStructured: JSON.stringify(patched.agentTransaction.proofLedger.phaseOrder) === JSON.stringify(["inspect", "patch", "validate", "save", "verify", "rollback"]) &&
    patched.agentTransaction.proofLedger.phases.length === 6,
  successLedgerEvidenceFields: ledgerPhase(patched.agentTransaction, "inspect").evidenceFields.includes("agentTransaction.originalGraphHash") &&
    ledgerPhase(patched.agentTransaction, "patch").evidenceFields.includes("agentTransaction.patchContract") &&
    ledgerPhase(patched.agentTransaction, "validate").evidenceFields.includes("phaseAudit.validate.proof") &&
    ledgerPhase(patched.agentTransaction, "save").evidenceFields.includes("saveProof.comparedGraphHash") &&
    ledgerPhase(patched.agentTransaction, "verify").evidenceFields.includes("agentTransaction.verificationCommands[].argv") &&
    ledgerPhase(patched.agentTransaction, "rollback").evidenceFields.includes("agentTransaction.rollback.verificationCommands[].argv"),
  verificationPurposesMatched: JSON.stringify(ledgerPhase(patched.agentTransaction, "verify").requiredPurposes.slice().sort()) ===
    JSON.stringify(purposeSet(patched.agentTransaction.verificationCommands)),
  rollbackPurposesMatched: JSON.stringify(ledgerPhase(patched.agentTransaction, "rollback").requiredPurposes.slice().sort()) ===
    JSON.stringify(purposeSet(patched.agentTransaction.rollback.verificationCommands)),
  rollbackActionsStructured: artifactRollbackAction.kind === "replace-or-delete-artifact" &&
    artifactRollbackAction.pathField === "agentTransaction.rollback.savedPath" &&
    artifactRollbackAction.savedKind === "artifact" &&
    artifactRollbackAction.requiresVerification === true &&
    sourceRollbackAction.kind === "restore-path-from-vcs" &&
    sourceRollbackAction.pathField === "agentTransaction.rollback.savedPath" &&
    sourceRollbackAction.savedKind === "source" &&
    sourceRollbackAction.requiresVerification === true &&
    ledgerPhase(patched.agentTransaction, "rollback").evidenceFields.includes("agentTransaction.rollback.actions[]"),
  failureLedgerClassified: stale.agentTransaction.proofLedger.failureCode === "GPH002" &&
    ledgerPhase(stale.agentTransaction, "inspect").status === "failed" &&
    ledgerPhase(stale.agentTransaction, "verify").status === "not-scheduled" &&
    ledgerPhase(stale.agentTransaction, "rollback").status === "not-available",
};
const evidenceAudit = {
  resultFieldsDeclared: patched.agentTransaction.resultFields.includes("evidenceBindings[]") &&
    patched.agentTransaction.resultFields.includes("evidenceBindings[].sourceFields"),
  bindingsMatchOperations: patched.evidenceBindings.length === patched.operationCount &&
    patched.evidenceBindings.every((binding, index) =>
      binding.operationIndex === index &&
      binding.op === patched.operations[index].op &&
      binding.graphHash === patched.originalGraphHash),
  compilerFactSourcesDeclared: patched.evidenceBindings.every((binding) =>
    binding.sourceFields.includes("originalGraphHash") &&
    binding.sourceFields.includes("operations[].op") &&
    binding.sourceFields.includes("operations[].actual") &&
    binding.sourceFields.includes("operations[].value")),
  preconditionActualBound: patched.evidenceBindings[0].expect === "hello transaction\n" &&
    patched.evidenceBindings[0].actual === patched.operations[0].actual &&
    patched.evidenceBindings[0].value === patched.operations[0].value,
};
const verificationResultFieldAudit = {
  resultFieldsDeclared: patched.agentTransaction.resultFields.includes("agentTransaction.verificationCommands[].resultFields") &&
    patched.agentTransaction.resultFields.includes("agentTransaction.rollback.verificationCommands[].resultFields"),
  scheduledCommandsDeclared: patched.agentTransaction.verificationCommands.every((command) => Array.isArray(command.resultFields) && command.resultFields.includes("agentCommand.verificationCommands")) &&
    renameSymbol.agentTransaction.verificationCommands.every((command) => Array.isArray(command.resultFields) && command.resultFields.includes("agentCommand.verificationCommands")),
  rollbackCommandsDeclared: patched.agentTransaction.rollback.verificationCommands.every((command) => Array.isArray(command.resultFields) && command.resultFields.includes("agentCommand.verificationCommands")) &&
    renameSymbol.agentTransaction.rollback.verificationCommands.every((command) => Array.isArray(command.resultFields) && command.resultFields.includes("agentCommand.verificationCommands")),
  purposeSpecificFields: JSON.stringify(patched.agentTransaction.verificationCommands[0].resultFields) === JSON.stringify(artifactVerificationResultFields) &&
    JSON.stringify(patched.agentTransaction.verificationCommands[1].resultFields) === JSON.stringify(graphVerificationResultFields) &&
    JSON.stringify(renameSymbol.agentTransaction.verificationCommands[0].resultFields) === JSON.stringify(sourceVerificationResultFields),
};
for (const code of ["BLD002", "GPH001", "GPH002", "GPH003", "GPH004", "GPH005"]) assert(failureAudit.observedCodes.includes(code), `missing observed failure code ${code}`);
for (const code of ["GPH001", "GPH002", "GPH003", "GPH004", "GPH005", "GPH006"]) assert(failureAudit.declaredCodes.includes(code), `missing declared failure code ${code}`);
assert(failureAudit.nonRetryableCodes.includes("GPH006"));
for (const purpose of ["graph-dump", "graph-impact", "graph-inspect"]) assert([...failureAudit.retryPurposes, ...failureAudit.operationRetryPurposes].includes(purpose), `missing failure retry purpose ${purpose}`);
assert.equal(failureAudit.targetProfileRetryPreserved, true);
assert.equal(failureAudit.noVerifyOnFailure, true);
assert.equal(failureAudit.invalidPatchInputRetryStructured, true);
assert.equal(failureAudit.inlinePatchCommandArgsPreserved, true);
assert(proofAudit.required >= 10);
for (const purpose of ["artifact-validate", "graph-check", "source-check", "graph-dump", "graph-impact", "graph-inspect", "rollback-artifact-validate", "rollback-source-check", "rollback-graph-check"]) {
  assert(proofAudit.purposes.includes(purpose), `missing proof purpose ${purpose}`);
}
assert(semanticEditAudit.coveredOperations.includes("replaceCallee"));
assert(semanticEditAudit.coveredOperations.includes("renameSymbol"));
assert(semanticEditAudit.coveredOperations.includes("changeParamType"));
assert(semanticEditAudit.coveredOperations.includes("replaceImport"));
assert.deepEqual(semanticEditAudit.coveredOperations, requiredSemanticOperations);
assert.equal(semanticEditAudit.operationSurfaceBacked, true);
assert(semanticEditAudit.sourceBackedTransactions >= 10);
assert(semanticEditAudit.conflictRejections >= 8);
assert.equal(proofLedgerAudit.phaseOrderStructured, true);
assert.equal(proofLedgerAudit.successLedgerEvidenceFields, true);
assert.equal(proofLedgerAudit.verificationPurposesMatched, true);
assert.equal(proofLedgerAudit.rollbackPurposesMatched, true);
assert.equal(proofLedgerAudit.rollbackActionsStructured, true);
assert.equal(proofLedgerAudit.failureLedgerClassified, true);
assert.equal(evidenceAudit.resultFieldsDeclared, true);
assert.equal(evidenceAudit.bindingsMatchOperations, true);
assert.equal(evidenceAudit.compilerFactSourcesDeclared, true);
assert.equal(evidenceAudit.preconditionActualBound, true);
assert.equal(verificationResultFieldAudit.resultFieldsDeclared, true);
assert.equal(verificationResultFieldAudit.scheduledCommandsDeclared, true);
assert.equal(verificationResultFieldAudit.rollbackCommandsDeclared, true);
assert.equal(verificationResultFieldAudit.purposeSpecificFields, true);
console.log(JSON.stringify({ ok: true, kind: "agent-graph-patch-transaction-smoke", proofAudit, proofLedgerAudit, evidenceAudit, verificationResultFieldAudit, semanticEditAudit, failureAudit }, null, 2));
console.log("agent graph patch transaction smoke ok");
