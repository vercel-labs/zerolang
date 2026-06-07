import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent explain command smoke requires ZERO_BIN or ${defaultZeroBin}`);

async function zeroJson(args: string[]) {
  const { stdout } = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 });
  return JSON.parse(stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const result = await execFileAsync(zero, command.argv.slice(1), { maxBuffer: 8 * 1024 * 1024 }).catch((error) => error);
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: !result.code });
  }
  return proofs;
}

const codes = ["TYP002", "TYP009", "TAR002"];
const explanations = [];
const proofs = [];
const recommendedPurposes = new Set<string>();

for (const code of codes) {
  const explanation = await zeroJson(["explain", "--json", code]);
  assert.equal(explanation.schemaVersion, 1);
  assert.equal(explanation.code, code);
  assert.equal(explanation.agentCommand.kind, "agent-explain-command-contract");
  assert.deepEqual(explanation.agentCommand.command.argv, ["zero", "explain", "--json", code]);
  assert(explanation.agentCommand.auditFields.includes("repair"));
  assert(explanation.agentCommand.repairFields.includes("repair.id"));
  assert.equal(explanation.agentCommand.recommendedNextCommands[0].purpose, "source-check");
  assert.equal(explanation.agentCommand.recommendedNextCommands[0].required, true);
  assert.equal(explanation.agentCommand.recommendedNextCommands[1].purpose, "repair-plan");
  assert.equal(explanation.agentCommand.recommendedNextCommands[1].required, false);
  assert.equal(explanation.agentCommand.recommendedNextCommands[1].resultContract, "agentTransaction");
  assert(explanation.agentCommand.recommendedNextCommands[1].resultFields.includes("agentTransaction.proofLedger"));
  assert(explanation.agentCommand.recommendedNextCommands[1].resultFields.includes("agentTransaction.rollback.actions[]"));
  assert(explanation.agentCommand.recommendedNextCommands[1].resultFields.includes("agentTransaction.rollback.verificationCommands"));
  assert(explanation.agentCommand.recommendedNextCommands[1].resultFields.includes("fixes[].graphPatchCandidates[]"));
  assert.equal(explanation.agentCommand.recommendedNextCommands[2].purpose, "graph-inspect");
  assert.equal(explanation.agentCommand.recommendedNextCommands[2].required, false);
  for (const command of explanation.agentCommand.recommendedNextCommands) {
    assert.equal(command.argv[0], "zero");
    recommendedPurposes.add(command.purpose);
  }
  assert.deepEqual(explanation.agentCommand.verificationCommands.map((command) => command.purpose), ["explain"]);
  assert.deepEqual(explanation.agentCommand.command.argv, explanation.agentCommand.verificationCommands[0].argv);
  assert(explanation.repair.id);
  assert(explanation.why);
  explanations.push({ code, repairId: explanation.repair.id, category: explanation.category });
  proofs.push(...await runRequiredCommands(explanation.agentCommand.verificationCommands));
}

const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, codes.length);
assert.deepEqual(proofAudit.purposes, ["explain"]);
assert.deepEqual(proofAudit.passedPurposes, ["explain"]);

const guidanceAudit = {
  recommendedPurposes: [...recommendedPurposes].sort(),
  requiredSourceCheck: explanations.length === codes.length &&
    [...recommendedPurposes].includes("source-check"),
  optionalRepairPlan: [...recommendedPurposes].includes("repair-plan"),
  optionalGraphInspect: [...recommendedPurposes].includes("graph-inspect"),
  repairPlanResultContract: explanations.length === codes.length,
  repairPlanRollbackFields: true,
  commandReplayable: proofs.every((proof) => JSON.stringify(proof.argv) === JSON.stringify(["zero", "explain", "--json", proof.argv.at(-1)])),
  repairIds: [...new Set(explanations.map((item) => item.repairId))].sort(),
};
assert.deepEqual(guidanceAudit.recommendedPurposes, ["graph-inspect", "repair-plan", "source-check"]);
assert.equal(guidanceAudit.requiredSourceCheck, true);
assert.equal(guidanceAudit.optionalRepairPlan, true);
assert.equal(guidanceAudit.optionalGraphInspect, true);
assert.equal(guidanceAudit.repairPlanResultContract, true);
assert.equal(guidanceAudit.repairPlanRollbackFields, true);
assert.equal(guidanceAudit.commandReplayable, true);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-explain-command-smoke",
  codes: explanations,
  guidanceAudit,
  proofAudit,
}, null, 2));
