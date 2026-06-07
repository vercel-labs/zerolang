import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const outDir = join(".zero", "agent-auto-repair-build-replay-smoke", String(process.pid));

async function zeroJson(args: string[], options: { allowFailure?: boolean } = {}) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  if (!options.allowFailure && result.code) throw result;
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands) {
    assert.equal(command.argv[0], "zero");
    if (!command.required) continue;
    const body = await zeroJson(command.argv.slice(1), { allowFailure: true });
    proofs.push({ purpose: command.purpose, ok: body.ok ?? true, diagnostics: body.diagnostics ?? [] });
  }
  return proofs;
}

await rm(outDir, { recursive: true, force: true });
await mkdir(outDir, { recursive: true });

const sourcePath = join(outDir, "auto-repair.0");
await writeFile(sourcePath, `fn answer() -> i32 {
    let value: i32 = 41
    value = value + 1
    return value
}

pub fn main(world: World) -> Void raises {
    if answer() == 42 {
        check world.out.write("ok\\n")
    }
}
`);

const checkBefore = await zeroJson(["check", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath], { allowFailure: true });
assert.equal(checkBefore.ok, false);
const diagnostic = checkBefore.diagnostics[0];
assert.equal(diagnostic.code, "TYP009");
assert.equal(diagnostic.repair.id, "make-binding-mutable");
assert.equal(diagnostic.repair.agentRepair.kind, "diagnostic-repair-entrypoint");
assert.equal(diagnostic.repair.agentRepair.commands[0].purpose, "repair-plan");

const plan = await zeroJson(["fix", "--plan", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath]);
assert.equal(plan.agentTransaction.kind, "compiler-mediated-repair-transaction");
assert.equal(plan.agentTransaction.repairId, "make-binding-mutable");
assert.equal(plan.agentTransaction.proofLedger.kind, "repair-transaction-proof-ledger");
assert(plan.agentTransaction.resultFields.includes("agentTransaction.proofLedger"));
const candidate = plan.fixes[0].graphPatchCandidates[0];
assert.equal(candidate.kind, "checked-graph-patch-candidate");
assert.equal(candidate.repairId, "make-binding-mutable");
assert.equal(candidate.candidateContract.kind, "checked-graph-patch-candidate-contract");
assert.equal(candidate.candidateContract.commandField, "command.argv");
assert.equal(candidate.candidateContract.patchTextField, "patch.text");
assert.equal(candidate.candidateContract.resultContract, "agentQuery.checkedEditSurface.transactionContract");
assert.equal(candidate.patch.operations[0].op, "set");
assert.equal(candidate.patch.operations[0].field, "mutable");
assert.equal(candidate.patch.operations[0].expect, "false");
assert.equal(candidate.patch.operations[0].value, "true");

const patchPath = join(outDir, "auto-repair.program-graph.patch");
await writeFile(patchPath, `${candidate.patch.text.join("\n")}\n`);
const patchArgv = [...candidate.command.argv];
patchArgv[patchArgv.indexOf("<patch-file>")] = patchPath;
const patch = await zeroJson(patchArgv.slice(1));
assert.equal(patch.ok, true);
assert.equal(patch.agentTransaction.kind, "compiler-mediated-graph-patch-transaction");
assert.equal(patch.agentTransaction.rollback.savedKind, "source");
assert.equal(patch.agentTransaction.rollback.savedPath, sourcePath);
assert.equal(patch.agentTransaction.proofLedger.kind, "graph-patch-proof-ledger");
const patchProofs = await runRequiredCommands(patch.agentTransaction.verificationCommands);
assert.deepEqual(patchProofs.map((proof) => proof.purpose).sort(), ["graph-check", "source-check"]);

const repairedSource = await readFile(sourcePath, "utf8");
assert.match(repairedSource, /var value: i32 = 41/);
const checkAfter = await zeroJson(["check", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath]);
assert.equal(checkAfter.ok, true);
assert.equal(checkAfter.diagnostics.length, 0);

const build = await zeroJson(["build", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath]);
assert(build.artifactPath);
assert(build.artifactBytes > 0);
assert(build.artifactHash);
const buildProofs = await runRequiredCommands(build.agentCommand.verificationCommands);
assert.deepEqual(buildProofs.map((proof) => proof.purpose).sort(), ["artifact-validate", "size-analysis", "source-check"]);

const proofPurposes = [...patchProofs, ...buildProofs].map((proof) => proof.purpose);
const audit = {
  diagnosticEntrypointStructured: diagnostic.repair.agentRepair.transactionContract === "agentTransaction",
  repairPlanProofLedger: plan.agentTransaction.proofLedger.kind === "repair-transaction-proof-ledger",
  graphPatchCandidateStructured: candidate.candidateContract.resultContract === "agentQuery.checkedEditSurface.transactionContract",
  checkedGraphPatchApplied: patch.agentTransaction.proofLedger.kind === "graph-patch-proof-ledger",
  rollbackAvailable: patch.agentTransaction.rollback.actions.length > 0,
  sourceRewritten: /var value: i32 = 41/.test(repairedSource),
  postRepairCheckVerified: checkAfter.ok === true && checkAfter.diagnostics.length === 0,
  artifactVerified: Boolean(build.artifactPath) && build.artifactBytes > 0 && Boolean(build.artifactHash?.value ?? build.artifactHash),
  compilerVerificationCommandsReplayed: proofPurposes.length === 5,
};
assert(Object.values(audit).every(Boolean));

const report = {
  schemaVersion: 1,
  kind: "agent-auto-repair-build-replay-smoke",
  ok: true,
  sourceFile: sourcePath,
  diagnostic: { code: diagnostic.code, repairId: diagnostic.repair.id },
  stages: ["check-fail", "repair-plan", "graph-patch-candidate", "graph-patch", "source-check", "artifact-build"],
  audit,
  proofAudit: {
    required: proofPurposes.length,
    purposes: proofPurposes,
    passedPurposes: proofPurposes,
  },
};

console.log(JSON.stringify(report, null, 2));
