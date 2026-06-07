import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent version command smoke requires ZERO_BIN or ${defaultZeroBin}`);

async function zeroJson(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const body = await zeroJson(command.argv.slice(1));
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: body.schemaVersion === 1 && typeof body.version === "string" });
  }
  return proofs;
}

const version = await zeroJson(["--version", "--json"]);
assert.equal(version.schemaVersion, 1);
assert.equal(typeof version.version, "string");
assert.equal(typeof version.host, "string");
assert.equal(version.backend, "zero-c");
assert(Array.isArray(version.targets));
assert(version.targets.includes(version.host));
assert.equal(typeof version.targetCompiler.available, "boolean");
assert.equal(typeof version.crossCompilation.ready, "boolean");

assert.equal(version.agentCommand.schemaVersion, 1);
assert.equal(version.agentCommand.kind, "agent-version-command-contract");
assert.deepEqual(version.agentCommand.command.argv, ["zero", "--version", "--json"]);
assert.equal(version.agentCommand.readPolicy.name, "compiler-provenance");
assert.equal(version.agentCommand.readPolicy.writesSource, false);
assert.equal(version.agentCommand.readPolicy.writesArtifacts, false);
assert.equal(version.agentCommand.readPolicy.versionMatchedToBinary, true);
assert(version.agentCommand.auditFields.includes("agentCommand.readPolicy"));
assert(version.agentCommand.auditFields.includes("targetCompiler"));
assert(version.agentCommand.auditFields.includes("crossCompilation"));
assert.equal(version.agentCommand.recommendedNextCommands[0].purpose, "agent-protocol");
assert.equal(version.agentCommand.recommendedNextCommands[0].required, false);
assert.deepEqual(version.agentCommand.recommendedNextCommands[0].argv, ["zero", "agent", "protocol", "--json"]);
assert(version.agentCommand.recommendedNextCommands[0].resultFields.includes("localGates[].command.argv"));
assert.equal(version.agentCommand.verificationCommands[0].purpose, "version-read");
assert.deepEqual(version.agentCommand.verificationCommands[0].argv, ["zero", "--version", "--json"]);

const proofs = await runRequiredCommands(version.agentCommand.verificationCommands);
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
assert.deepEqual(proofAudit.purposes, ["version-read"]);
assert.deepEqual(proofAudit.passedPurposes, ["version-read"]);

const commandAudit = {
  replayable: JSON.stringify(version.agentCommand.command.argv) === JSON.stringify(["zero", "--version", "--json"]),
  readPolicyAuditable: version.agentCommand.readPolicy.name === "compiler-provenance" &&
    version.agentCommand.readPolicy.versionMatchedToBinary === true &&
    version.agentCommand.readPolicy.writesSource === false &&
    version.agentCommand.readPolicy.writesArtifacts === false,
  protocolFollowupStructured: version.agentCommand.recommendedNextCommands.some((item) =>
    item.purpose === "agent-protocol" &&
    item.required === false &&
    JSON.stringify(item.argv) === JSON.stringify(["zero", "agent", "protocol", "--json"])),
};

console.log(JSON.stringify({
  ok: true,
  kind: "agent-version-command-smoke",
  commandAudit,
  proofAudit,
  version: {
    version: version.version,
    host: version.host,
    backend: version.backend,
    targets: version.targets.length,
    targetCompilerAvailable: version.targetCompiler.available,
    crossCompilationReady: version.crossCompilation.ready,
  },
}, null, 2));
