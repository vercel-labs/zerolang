import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent targets command smoke requires ZERO_BIN or ${defaultZeroBin}`);

async function zeroJson(args: string[]) {
  const { stdout } = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 });
  return JSON.parse(stdout);
}

async function zeroJsonAllowFailure(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 }).catch((error) => error);
  return JSON.parse(result.stdout);
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

const targets = await zeroJson(["targets", "--json"]);

assert.equal(targets.schemaVersion, 1);
assert.equal(targets.agentCommand.schemaVersion, 1);
assert.equal(targets.agentCommand.kind, "agent-targets-command-contract");
assert.deepEqual(targets.agentCommand.command.argv, ["zero", "targets", "--json"]);
assert(targets.agentCommand.auditFields.includes("targets[].directBackend"));
assert(targets.agentCommand.auditFields.includes("targets[].capabilityFacts"));
assert(targets.agentCommand.selectionFields.includes("targets[].name"));
assert(targets.agentCommand.selectionFields.includes("targets[].directBackend.status"));
assert.equal(targets.agentCommand.recommendedNextCommands[0].purpose, "doctor");
assert.equal(targets.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(targets.agentCommand.recommendedNextCommands[0].inputField, "targets[].name or command.argv target selection");
assert.deepEqual(targets.agentCommand.recommendedNextCommands[0].argv, ["zero", "doctor", "--json"]);
assert(targets.agentCommand.recommendedNextCommands[0].resultFields.includes("targetToolchains[].target"));
assert(targets.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.deepEqual(targets.agentCommand.verificationCommands.map((command) => command.purpose), ["target-selection"]);
assert.deepEqual(targets.agentCommand.verificationCommands[0].argv, ["zero", "targets", "--json"]);
assert(targets.targets.length > 0);
assert(targets.targets.some((target) => target.name === targets.host || target.aliases?.includes(targets.host)));

const targetReadinessAudit = {
  commandReplayable: JSON.stringify(targets.agentCommand.command.argv) === JSON.stringify(targets.agentCommand.verificationCommands[0].argv),
  jsonReplayable: targets.agentCommand.command.argv.includes("--json") &&
    targets.agentCommand.verificationCommands[0].argv.includes("--json"),
  selectionStructured: targets.agentCommand.selectionFields.includes("targets[].name") &&
    targets.agentCommand.selectionFields.includes("targets[].directBackend.status") &&
    targets.targets.every((target) => typeof target.name === "string" &&
      Array.isArray(target.aliases) &&
      typeof target.hosted === "boolean" &&
      target.directBackend &&
      typeof target.directBackend.status === "string"),
  capabilityFactsStructured: targets.agentCommand.auditFields.includes("targets[].capabilityFacts") &&
    targets.targets.every((target) => Array.isArray(target.capabilityFacts) &&
      target.capabilityFacts.length >= 1 &&
      target.capabilityFacts.every((fact) => typeof fact.name === "string" &&
        typeof fact.available === "boolean" &&
        typeof fact.source === "string")),
  toolchainFactsStructured: targets.agentCommand.auditFields.includes("targets[].toolchain") &&
    targets.agentCommand.auditFields.includes("targets[].libcFacts") &&
    targets.targets.every((target) => target.toolchain &&
      typeof target.toolchain.compilerTarget === "string" &&
      typeof target.toolchain.objectFormat === "string" &&
      typeof target.toolchain.requiresSysroot === "boolean" &&
      target.libcFacts &&
      typeof target.libcFacts.sysrootStatus === "string"),
  doctorFollowupStructured: targets.agentCommand.recommendedNextCommands.some((command) => command.purpose === "doctor" &&
    command.required === false &&
    command.inputField === "targets[].name or command.argv target selection" &&
    JSON.stringify(command.argv) === JSON.stringify(["zero", "doctor", "--json"]) &&
    command.resultFields.includes("targetToolchains[].target") &&
    command.resultFields.includes("targetToolchains[].status") &&
    command.resultFields.includes("agentCommand.verificationCommands")),
  hostTargetPresent: targets.targets.some((target) => target.name === targets.host || target.aliases?.includes(targets.host)),
};

assert.equal(targetReadinessAudit.commandReplayable, true);
assert.equal(targetReadinessAudit.jsonReplayable, true);
assert.equal(targetReadinessAudit.selectionStructured, true);
assert.equal(targetReadinessAudit.capabilityFactsStructured, true);
assert.equal(targetReadinessAudit.toolchainFactsStructured, true);
assert.equal(targetReadinessAudit.doctorFollowupStructured, true);
assert.equal(targetReadinessAudit.hostTargetPresent, true);

const proofs = await runRequiredCommands(targets.agentCommand.verificationCommands);
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 1);
assert.deepEqual(proofAudit.purposes, ["target-selection"]);
assert.deepEqual(proofAudit.passedPurposes, ["target-selection"]);

const invalidTarget = await zeroJsonAllowFailure(["build", "--json", "--target", "not-a-real-zero-target", "examples/hello.0"]);
const invalidTargetRecoveryStructured = invalidTarget.ok === false &&
  invalidTarget.agentCommand?.kind === "agent-command-failure-contract" &&
  invalidTarget.agentCommand?.failure?.class === "unknown-target" &&
  invalidTarget.agentCommand?.failure?.code === "TAR001" &&
  invalidTarget.agentCommand?.failure?.retryCommands?.[0]?.purpose === "target-selection" &&
  invalidTarget.agentCommand?.failure?.retryCommands?.[0]?.required === true &&
  JSON.stringify(invalidTarget.agentCommand?.failure?.retryCommands?.[0]?.argv) === JSON.stringify(["zero", "targets", "--json"]) &&
  invalidTarget.agentCommand?.failure?.retryCommands?.[0]?.resultFields?.includes("targets[].name") &&
  invalidTarget.agentCommand?.failure?.retryCommands?.[1]?.purpose === "correct-command-usage" &&
  invalidTarget.agentCommand?.failure?.retryCommands?.[1]?.required === false &&
  invalidTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.includes("<supported-target>") &&
  !invalidTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.includes("not-a-real-zero-target") &&
  JSON.stringify(invalidTarget.agentCommand?.recommendedNextCommands?.[0]?.argv) === JSON.stringify(["zero", "targets", "--json"]);

assert.equal(invalidTargetRecoveryStructured, true);

const invalidFixTarget = await zeroJsonAllowFailure(["fix", "--apply", "--json", "--target", "not-a-real-zero-target", "examples/hello.0"]);
const invalidGraphPatchTarget = await zeroJsonAllowFailure(["graph", "patch", "--json", "--target", "not-a-real-zero-target", "--expect-graph-hash", "graph:1234567890abcdef", "examples/hello.0", ".zero/missing.patch"]);
const invalidTargetCommandArgsPreserved = invalidFixTarget.agentCommand?.failure?.class === "unknown-target" &&
  invalidFixTarget.agentCommand?.command?.argv?.includes("--apply") &&
  invalidFixTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.includes("--apply") &&
  invalidFixTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.includes("<supported-target>") &&
  !invalidFixTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.includes("not-a-real-zero-target") &&
  invalidGraphPatchTarget.agentCommand?.failure?.class === "unknown-target" &&
  invalidGraphPatchTarget.agentCommand?.command?.argv?.includes("--expect-graph-hash") &&
  invalidGraphPatchTarget.agentCommand?.command?.argv?.includes("graph:1234567890abcdef") &&
  invalidGraphPatchTarget.agentCommand?.command?.argv?.at(-1) === ".zero/missing.patch" &&
  invalidGraphPatchTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.includes("--expect-graph-hash") &&
  invalidGraphPatchTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.includes("graph:1234567890abcdef") &&
  invalidGraphPatchTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.includes("<supported-target>") &&
  invalidGraphPatchTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.at(-1) === ".zero/missing.patch" &&
  !invalidGraphPatchTarget.agentCommand?.failure?.retryCommands?.[1]?.argv?.includes("not-a-real-zero-target");

assert.equal(invalidTargetCommandArgsPreserved, true);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-targets-command-smoke",
  host: targets.host,
  targets: targets.targets.length,
  targetReadinessAudit,
  invalidTargetRecoveryStructured,
  invalidTargetCommandArgsPreserved,
  proofAudit,
}, null, 2));
