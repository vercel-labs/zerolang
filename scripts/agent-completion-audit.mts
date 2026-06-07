import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const stripTypeArgs = ["--experimental-strip-types", "--disable-warning=ExperimentalWarning"];
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent completion audit requires ZERO_BIN or ${defaultZeroBin}`);

async function jsonFromCommand(command: string, args: string[], env = process.env) {
  const { stdout } = await execFileAsync(command, args, { env: { ...env, ZERO_BIN: zero }, maxBuffer: 32 * 1024 * 1024 });
  return JSON.parse(stdout);
}

function evidencePassed(field: string, value: unknown) {
  if (typeof value === "boolean") {
    if (field === "fullSourceReadRequiredForGraphLookup") return value === false;
    return value === true;
  }
  if (typeof value === "number") {
    if (field === "tokenSavingSampleRatio") return value <= 0.02;
    if (field === "tokenSavingFullInspectRatio") return value <= 0.001;
    if (field === "sourceUnderstandingProofCommands") return value >= 3;
    if (field === "authoringReplayProofCommands") return value >= 7;
    if (field === "autoRepairBuildReplayProofCommands") return value >= 5;
    if (field === "rollbackReplayProofCommands") return value >= 2;
    if (field === "repairPlanCandidateEvidenceBacked") return value >= 10;
    if (field === "semanticEditOperations") return value >= 17;
    if (field === "semanticEditCategories") return value >= 5;
    if (field === "objectiveAuditEvidenceItems") return value >= 12;
    return value > 0;
  }
  return value !== null && value !== undefined;
}

const manifest = await jsonFromCommand(zero, ["agent", "protocol", "--json"]);
const gate = await jsonFromCommand(process.execPath, [...stripTypeArgs, "scripts/agent-contracts-gate.mts", "--summary-only"]);

assert.equal(manifest.kind, "agent-protocol-manifest");
assert.equal(manifest.objectiveContract?.kind, "agent-native-build-system-objective-contract");
assert.equal(gate.kind, "agent-contracts-gate");
assert.equal(gate.ok, true);
assert.deepEqual(gate.gateFailures, []);

const summary = gate.summary ?? {};
const requirements = manifest.objectiveContract.requirements.map((requirement) => {
  const evidence = requirement.evidenceFields.map((field: string) => ({
    field,
    value: summary[field] ?? null,
    passed: evidencePassed(field, summary[field]),
  }));
  return {
    id: requirement.id,
    passed: evidence.every((item) => item.passed),
    evidence,
  };
});

const expectedRequirementIds = [
  "humanReadableSource",
  "semanticGraphUnderstanding",
  "compilerMediatedModification",
  "compilerMediatedRepair",
  "refactoringSurface",
  "verifiableBuildAndTest",
  "auditableProofReceipts",
  "rollbackProven",
  "tokenEfficientGraphProtocol",
  "hallucinationResistance",
  "autoRepairToBuild",
  "protocolNotJustLanguage",
];
assert.deepEqual(requirements.map((item) => item.id), expectedRequirementIds);

const summaryAudit = {
  gateOk: gate.ok === true,
  gateFailuresEmpty: Array.isArray(gate.gateFailures) && gate.gateFailures.length === 0,
  objectiveContractPresent: manifest.objectiveContract?.summaryField === "objectiveAuditOperational" &&
    manifest.objectiveContract?.evidenceCountField === "objectiveAuditEvidenceItems",
  objectiveSummaryPassed: summary.objectiveAuditOperational === true && summary.objectiveAuditEvidenceItems >= expectedRequirementIds.length,
  advantageScorePassed: summary.agentNativeAdvantageScore === 100 &&
    summary.agentNativeReliableEdits === true &&
    summary.agentNativeTokenEfficient === true &&
    summary.agentNativeHallucinationResistant === true &&
    summary.agentNativeAutoRepairable === true &&
    summary.agentNativeVerifiableBuildSystem === true,
  requirementsPassed: requirements.every((item) => item.passed),
};
const failedRequirements = requirements.filter((item) => !item.passed);
if (failedRequirements.length > 0) {
  console.error(JSON.stringify({ failedRequirements }, null, 2));
}
for (const [name, passed] of Object.entries(summaryAudit)) {
  assert.equal(passed, true, `completion audit failed: ${name}`);
}

const report = {
  schemaVersion: 1,
  kind: "agent-completion-audit",
  ok: true,
  zeroBin: zero,
  protocolVersion: manifest.protocolVersion,
  localGate: manifest.objectiveContract.localGate,
  summaryAudit,
  requirements,
  evidenceItems: requirements.reduce((count, requirement) => count + requirement.evidence.filter((item) => item.passed).length, 0),
};

console.log(JSON.stringify(report, null, 2));
