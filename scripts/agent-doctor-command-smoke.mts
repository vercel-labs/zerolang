import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent doctor command smoke requires ZERO_BIN or ${defaultZeroBin}`);

async function zeroJson(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  assert(result.stdout, `zero ${args.join(" ")} should print JSON`);
  return { body: JSON.parse(result.stdout), ok: !result.code };
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const result = await execFileAsync(zero, command.argv.slice(1), { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: Boolean(result.stdout) });
  }
  return proofs;
}

const doctor = await zeroJson(["doctor", "--json"]);
assert.equal(doctor.body.schemaVersion, 1);
assert(["ok", "warning", "error"].includes(doctor.body.status));
assert.equal(doctor.body.agentCommand.kind, "agent-doctor-command-contract");
assert.deepEqual(doctor.body.agentCommand.command.argv, ["zero", "doctor", "--json"]);
assert.deepEqual(doctor.body.agentCommand.verificationCommands.map((command) => command.purpose), ["doctor"]);
assert.equal(doctor.body.agentCommand.recommendedNextCommands[0].purpose, "target-selection");
assert.equal(doctor.body.agentCommand.recommendedNextCommands[0].required, false);
assert.deepEqual(doctor.body.agentCommand.recommendedNextCommands[0].argv, ["zero", "targets", "--json"]);
assert(doctor.body.agentCommand.recommendedNextCommands[0].resultFields.includes("targets[].directBackend.status"));
assert(doctor.body.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert(doctor.body.agentCommand.auditFields.includes("targetToolchains"));
assert(doctor.body.agentCommand.readinessFields.includes("targetToolchains[].target"));
assert(doctor.body.checks.some((check) => check.name === "host"));
assert(doctor.body.checks.some((check) => check.name === "native-c-compiler"));
assert(Array.isArray(doctor.body.targetToolchains));
assert(doctor.body.targetToolchains.length >= 1);
assert(doctor.body.targetToolchains.some((toolchain) => toolchain.target === doctor.body.host));

const doctorReadinessAudit = {
  commandReplayable: JSON.stringify(doctor.body.agentCommand.command.argv) === JSON.stringify(doctor.body.agentCommand.verificationCommands[0].argv),
  checksStructured: doctor.body.agentCommand.auditFields.includes("checks") &&
    doctor.body.agentCommand.readinessFields.includes("checks[].name") &&
    doctor.body.agentCommand.readinessFields.includes("checks[].status") &&
    doctor.body.checks.every((check) => typeof check.name === "string" &&
      ["ok", "warning", "error"].includes(check.status) &&
      typeof check.message === "string"),
  targetToolchainsStructured: doctor.body.agentCommand.auditFields.includes("targetToolchains") &&
    doctor.body.agentCommand.readinessFields.includes("targetToolchains[].target") &&
    doctor.body.agentCommand.readinessFields.includes("targetToolchains[].status") &&
    doctor.body.targetToolchains.every((toolchain) => typeof toolchain.target === "string" &&
      ["ok", "warning", "error"].includes(toolchain.status) &&
      typeof toolchain.message === "string" &&
      typeof toolchain.driverKind === "string" &&
      typeof toolchain.selectionSource === "string" &&
      typeof toolchain.targetTriple === "string" &&
      typeof toolchain.requiresSysroot === "boolean" &&
      typeof toolchain.sysrootStatus === "string"),
  hostTargetReadinessStructured: doctor.body.targetToolchains.some((toolchain) => toolchain.target === doctor.body.host &&
    ["ok", "warning", "error"].includes(toolchain.status) &&
    typeof toolchain.message === "string"),
  targetSelectionFollowupStructured: doctor.body.agentCommand.recommendedNextCommands.some((command) =>
    command.purpose === "target-selection" &&
    command.required === false &&
    JSON.stringify(command.argv) === JSON.stringify(["zero", "targets", "--json"]) &&
    command.inputField === "targetToolchains[].target" &&
    command.resultFields.includes("targets[].directBackend.status") &&
    command.resultFields.includes("agentCommand.verificationCommands")),
};

assert.equal(doctorReadinessAudit.commandReplayable, true);
assert.equal(doctorReadinessAudit.checksStructured, true);
assert.equal(doctorReadinessAudit.targetToolchainsStructured, true);
assert.equal(doctorReadinessAudit.hostTargetReadinessStructured, true);
assert.equal(doctorReadinessAudit.targetSelectionFollowupStructured, true);

const proofs = await runRequiredCommands(doctor.body.agentCommand.verificationCommands);
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 1);
assert.deepEqual(proofAudit.purposes, ["doctor"]);
assert.deepEqual(proofAudit.passedPurposes, ["doctor"]);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-doctor-command-smoke",
  status: doctor.body.status,
  commandSucceeded: doctor.ok,
  host: doctor.body.host,
  checks: doctor.body.checks.length,
  targetToolchains: doctor.body.targetToolchains.length,
  doctorReadinessAudit,
  proofAudit,
}, null, 2));
