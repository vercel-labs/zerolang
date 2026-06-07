import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, rmSync } from "node:fs";
import { dirname, join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent new command smoke requires ZERO_BIN or ${defaultZeroBin}`);

async function zeroJson(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const body = await zeroJson(command.argv.slice(1));
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: body.ok ?? true });
  }
  return proofs;
}

const root = join(".zero", "agent-new-command-smoke", String(process.pid), "app");
rmSync(root, { recursive: true, force: true });
mkdirSync(dirname(root), { recursive: true });

const created = await zeroJson(["new", "--json", "cli", root]);
assert.equal(created.schemaVersion, 1);
assert.equal(created.ok, true);
assert.equal(created.template, "cli");
assert.equal(created.project.path, root);
assert.equal(created.project.entry, "src/main.0");
assert(created.project.files.includes("zero.json"));
assert(created.project.files.includes("src/main.0"));
for (const file of created.project.files) assert(existsSync(join(root, file)), `created project should contain ${file}`);

assert.equal(created.agentCommand.kind, "agent-new-command-contract");
assert.deepEqual(created.agentCommand.command.argv, ["zero", "new", "--json", "cli", root]);
assert.equal(created.agentCommand.writePolicy.name, "writes-source-tree");
assert.equal(created.agentCommand.writePolicy.writesSource, true);
assert.equal(created.agentCommand.writePolicy.writesArtifacts, false);
assert.equal(created.agentCommand.writePolicy.createsRootField, "project.path");
assert.equal(created.agentCommand.writePolicy.rollbackField, "project.path");
assert.equal(created.agentCommand.writePolicy.rollbackPolicy, "agent-enforced-delete-created-root");
assert(created.agentCommand.writePolicy.rollbackActions.some((action) =>
  action.kind === "delete-path" &&
  action.pathField === "project.path" &&
  action.condition === "created-by-command"));
assert.equal(created.agentCommand.writePolicy.verificationField, "agentCommand.verificationCommands");
assert(created.agentCommand.auditFields.includes("agentCommand.writePolicy"));
assert(created.agentCommand.auditFields.includes("project.files"));

const existsFailure = await zeroJson(["new", "--json", "cli", root]);
assert.equal(existsFailure.ok, false);
assert.equal(existsFailure.failure.class, "path-exists");
assert.equal(existsFailure.agentCommand.writePolicy.name, "writes-source-tree");
assert.equal(existsFailure.failure.retryCommands[0].purpose, "choose-new-project-path");
assert.equal(existsFailure.failure.retryCommands[0].required, true);
assert.deepEqual(existsFailure.failure.retryCommands[0].argv, ["zero", "new", "--json", "cli", "<new-project-path>"]);
assert.deepEqual(existsFailure.recommendedNextCommands[0].argv, existsFailure.failure.retryCommands[0].argv);

const unknownFailure = await zeroJson(["new", "--json", "service", join(".zero", "agent-new-command-smoke", String(process.pid), "service")]);
assert.equal(unknownFailure.ok, false);
assert.equal(unknownFailure.failure.class, "unknown-template");
assert.deepEqual(unknownFailure.failure.expected, ["cli", "lib", "package"]);
assert.equal(unknownFailure.failure.retryCommands.length, 3);
assert(unknownFailure.failure.retryCommands.some((command) => JSON.stringify(command.argv) === JSON.stringify(["zero", "new", "--json", "cli", unknownFailure.project.path])));
assert(unknownFailure.failure.retryCommands.every((command) => command.purpose === "choose-template" && command.required === true));
assert.equal(unknownFailure.recommendedNextCommands[0].purpose, "choose-template");

const proofs = await runRequiredCommands(created.agentCommand.verificationCommands);
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.deepEqual(proofAudit.purposes, ["source-check", "test-run"]);
assert.deepEqual(proofAudit.passedPurposes, ["source-check", "test-run"]);

const commandAudit = {
  replayable: JSON.stringify(created.agentCommand.command.argv) === JSON.stringify(["zero", "new", "--json", "cli", root]),
  projectWritePolicyAuditable: created.agentCommand.writePolicy.name === "writes-source-tree" &&
    created.agentCommand.writePolicy.createsRootField === "project.path" &&
    created.agentCommand.writePolicy.rollbackField === "project.path" &&
    created.agentCommand.writePolicy.rollbackActions.some((action) => action.pathField === "project.path"),
  projectFilesAuditable: created.project.files.every((file: string) => existsSync(join(root, file))),
  pathExistsFailureAuditable: existsFailure.failure.class === "path-exists" &&
    existsFailure.project.path === root &&
    existsFailure.agentCommand.writePolicy.name === "writes-source-tree",
  failureRetryStructured: existsFailure.failure.retryCommands[0].purpose === "choose-new-project-path" &&
    JSON.stringify(existsFailure.recommendedNextCommands[0].argv) === JSON.stringify(existsFailure.failure.retryCommands[0].argv) &&
    unknownFailure.failure.retryCommands.length === 3 &&
    unknownFailure.failure.retryCommands.every((command) => command.purpose === "choose-template" && command.required === true),
};

console.log(JSON.stringify({
  ok: true,
  kind: "agent-new-command-smoke",
  project: created.project,
  commandAudit,
  proofAudit,
}, null, 2));
