import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent skills command smoke requires ZERO_BIN or ${defaultZeroBin}`);

async function zeroJson(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const body = await zeroJson(command.argv.slice(1));
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: body.success === true });
  }
  return proofs;
}

function assertSkillsContract(report, argv: string[]) {
  assert.equal(report.agentCommand.schemaVersion, 1);
  assert.equal(report.agentCommand.kind, "agent-skills-command-contract");
  assert.deepEqual(report.agentCommand.command.argv, argv);
  assert.equal(report.agentCommand.readPolicy.name, "bundled-compiler-guidance");
  assert.equal(report.agentCommand.readPolicy.source, "embedded-skills");
  assert.equal(report.agentCommand.readPolicy.writesSource, false);
  assert.equal(report.agentCommand.readPolicy.writesArtifacts, false);
  assert.equal(report.agentCommand.readPolicy.versionMatchedToCompiler, true);
  assert(report.agentCommand.auditFields.includes("agentCommand.readPolicy"));
  assert(report.agentCommand.auditFields.includes("data[].content"));
  assert.deepEqual(report.agentCommand.verificationCommands[0].argv, argv);
  assert.equal(report.agentCommand.verificationCommands[0].purpose, "skills-read");
}

const list = await zeroJson(["skills", "list", "--json"]);
assert.equal(list.success, true);
assert(list.data.some((skill) => skill.name === "agent"));
assertSkillsContract(list, ["zero", "skills", "list", "--json"]);

const language = await zeroJson(["skills", "get", "language", "--json"]);
assert.equal(language.success, true);
assert.equal(language.data[0].name, "language");
assert.match(language.data[0].content, /# zerolang Language/);
assertSkillsContract(language, ["zero", "skills", "get", "--json", "language"]);

const missing = await zeroJson(["skills", "get", "missing", "--json"]);
assert.equal(missing.success, false);
assert.match(missing.error, /Skill not found/);
assertSkillsContract(missing, ["zero", "skills", "get", "--json", "missing"]);

const proofs = [
  ...(await runRequiredCommands(list.agentCommand.verificationCommands)),
  ...(await runRequiredCommands(language.agentCommand.verificationCommands)),
];
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.deepEqual(proofAudit.purposes, ["skills-read"]);
assert.deepEqual(proofAudit.passedPurposes, ["skills-read"]);

const commandAudit = {
  listReplayable: JSON.stringify(list.agentCommand.command.argv) === JSON.stringify(["zero", "skills", "list", "--json"]),
  getReplayable: JSON.stringify(language.agentCommand.command.argv) === JSON.stringify(["zero", "skills", "get", "--json", "language"]),
  readPolicyAuditable: [list, language, missing].every((item) =>
    item.agentCommand.readPolicy.name === "bundled-compiler-guidance" &&
    item.agentCommand.readPolicy.versionMatchedToCompiler === true &&
    item.agentCommand.readPolicy.writesSource === false &&
    item.agentCommand.readPolicy.writesArtifacts === false),
  failureContractAuditable: missing.success === false &&
    missing.agentCommand.verificationCommands[0].purpose === "skills-read",
};

console.log(JSON.stringify({
  ok: true,
  kind: "agent-skills-command-smoke",
  skills: list.data.length,
  commandAudit,
  proofAudit,
}, null, 2));
