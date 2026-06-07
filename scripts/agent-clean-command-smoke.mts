import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent clean command smoke requires ZERO_BIN or ${defaultZeroBin}`);

async function zeroJson(args: string[], options: { allowFailure?: boolean; env?: NodeJS.ProcessEnv } = {}) {
  const result = await execFileAsync(zero, args, { env: { ...process.env, ...options.env }, maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  if (!options.allowFailure && result.code) throw result;
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const result = await execFileAsync(zero, command.argv.slice(1), { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: !result.code });
  }
  return proofs;
}

const probe = process.platform === "win32" ? ".zero\\out\\agent-clean-smoke\\tmp.txt" : ".zero/out/agent-clean-smoke/tmp.txt";
mkdirSync(process.platform === "win32" ? ".zero\\out\\agent-clean-smoke" : ".zero/out/agent-clean-smoke", { recursive: true });
writeFileSync(probe, "tmp");
assert(existsSync(probe));

const clean = await zeroJson(["clean", "--json"]);
assert.equal(clean.ok, true);
assert.equal(clean.agentCommand.kind, "agent-clean-command-contract");
assert.deepEqual(clean.agentCommand.command.argv, ["zero", "clean", "--json"]);
assert.equal(clean.agentCommand.writePolicy.name, "destructive-clean");
assert.equal(clean.agentCommand.writePolicy.deletesArtifacts, true);
assert.equal(clean.agentCommand.writePolicy.rollbackAvailable, false);
assert.equal(clean.agentCommand.writePolicy.externalRestoreRequired, true);
assert.equal(clean.agentCommand.writePolicy.allowedRootsField, "removedRoots[].path");
assert.equal(clean.agentCommand.writePolicy.verificationField, "agentCommand.verificationCommands");
assert(clean.agentCommand.auditFields.includes("agentCommand.writePolicy"));
assert(clean.agentCommand.auditFields.includes("removedRoots[].path"));
assert.equal(clean.removedRoots[0].path, ".zero/out");
assert.equal(clean.removedRoots[0].existedBefore, true);
assert.equal(clean.removedRoots[0].existsAfter, false);
assert(clean.removed.some((item: string) => item.endsWith("agent-clean-smoke/tmp.txt") || item.endsWith("agent-clean-smoke\\tmp.txt")));
assert(!existsSync(probe));

const cleanFailure = await zeroJson(["clean", "--json"], {
  allowFailure: true,
  env: { ZERO_CLEAN_TEST_FAIL_PATH: ".zero/out" },
});
assert.equal(cleanFailure.ok, false);
assert.equal(cleanFailure.failedPath, ".zero/out");
assert.equal(cleanFailure.failure.class, "artifact-clean-failed");
assert.equal(cleanFailure.failure.externalRestoreRequired, true);
assert.equal(cleanFailure.failure.retryCommands[0].purpose, "clean-audit");
assert.equal(cleanFailure.failure.retryCommands[0].required, true);
assert.deepEqual(cleanFailure.failure.retryCommands[0].argv, ["zero", "clean", "--json"]);
assert.deepEqual(cleanFailure.recommendedNextCommands[0].argv, cleanFailure.failure.retryCommands[0].argv);
assert.equal(cleanFailure.agentCommand.writePolicy.name, "destructive-clean");

const proofs = await runRequiredCommands(clean.agentCommand.verificationCommands);
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 1);
assert.deepEqual(proofAudit.purposes, ["clean-audit"]);
assert.deepEqual(proofAudit.passedPurposes, ["clean-audit"]);

const commandAudit = {
  replayable: JSON.stringify(clean.agentCommand.command.argv) === JSON.stringify(["zero", "clean", "--json"]),
  destructiveCleanPolicyAuditable: clean.agentCommand.writePolicy.name === "destructive-clean" &&
    clean.agentCommand.writePolicy.rollbackAvailable === false &&
    clean.agentCommand.writePolicy.allowedRootsField === "removedRoots[].path" &&
    clean.removedRoots.every((root) => root.path.startsWith(".zero/")),
  failureRetryStructured: cleanFailure.failure.retryCommands[0].purpose === "clean-audit" &&
    JSON.stringify(cleanFailure.recommendedNextCommands[0].argv) === JSON.stringify(cleanFailure.failure.retryCommands[0].argv) &&
    cleanFailure.failure.externalRestoreRequired === true,
};

console.log(JSON.stringify({
  ok: true,
  kind: "agent-clean-command-smoke",
  removedCount: clean.removedCount,
  removedRoots: clean.removedRoots.map((root) => root.path),
  commandAudit,
  proofAudit,
}, null, 2));
