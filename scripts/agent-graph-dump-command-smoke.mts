import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent graph dump command smoke requires ZERO_BIN or ${defaultZeroBin}`);

const input = "examples/hello.0";
const tempDir = mkdtempSync(join(tmpdir(), "zero-graph-dump-"));

async function zeroJson(args: string[]) {
  const { stdout } = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 });
  return JSON.parse(stdout);
}

async function zeroJsonFailure(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  assert(result.stdout, `zero ${args.join(" ")} should print JSON`);
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

const dump = await zeroJson(["graph", "dump", "--json", input]);
const { stdout: graphText } = await execFileAsync(zero, ["graph", "dump", input], { maxBuffer: 16 * 1024 * 1024 });
assert.equal(dump.agentCommand.kind, "agent-graph-dump-command-contract");
assert.deepEqual(dump.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-dump"]);
assert(dump.agentCommand.auditFields.includes("nodes"));
assert(dump.agentCommand.auditFields.includes("graphHash"));
assert(dump.agentCommand.auditFields.includes("agentCommand.writePolicy"));
assert.equal(dump.agentCommand.writePolicy.name, "preview-only");
assert.equal(dump.agentCommand.writePolicy.writesArtifacts, false);
assert(dump.agentCommand.stateFields.includes("graphHash"));
assert.equal(dump.agentCommand.recommendedNextCommands[0].purpose, "graph-view");
assert.equal(dump.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(dump.agentCommand.recommendedNextCommands[0].when, "after graph dump when a token-bounded artifact view is needed");
assert.equal(dump.agentCommand.recommendedNextCommands[0].inputField, "saved.path or command.argv input");
assert.deepEqual(dump.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "view", "--json", "<saved.path-or-input>"]);
assert(dump.agentCommand.recommendedNextCommands[0].resultFields.includes("graphHash"));
assert(dump.agentCommand.recommendedNextCommands[0].resultFields.includes("moduleIdentity"));
assert(dump.agentCommand.recommendedNextCommands[0].resultFields.includes("view"));
assert(dump.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert(dump.graphHash.startsWith("graph:"));
assert.equal(dump.validation.ok, true);
assert(dump.nodes.some((node) => node.kind === "Function" && node.name === "main"));

const outPath = join(tempDir, "hello.program-graph");
const outDump = await zeroJson(["graph", "dump", "--json", "--out", outPath, input]);
assert.equal(outDump.agentCommand.kind, "agent-graph-dump-command-contract");
assert.equal(outDump.saved.path, outPath);
assert.equal(outDump.saved.kind, "program-graph");
assert.equal(outDump.saved.byteStable, true);
assert.equal(outDump.agentCommand.writePolicy.name, "writes-artifact");
assert.equal(outDump.agentCommand.writePolicy.writesSource, false);
assert.equal(outDump.agentCommand.writePolicy.writesArtifacts, true);
assert.equal(outDump.agentCommand.writePolicy.artifactPathField, "saved.path");
assert.equal(outDump.agentCommand.writePolicy.rollbackField, "saved.path");
assert.equal(outDump.agentCommand.writePolicy.verificationField, "agentCommand.verificationCommands");
assert.equal(outDump.agentCommand.writePolicy.rollbackPolicy, "agent-enforced-delete-or-replace");
assert(outDump.agentCommand.writePolicy.rollbackActions.some((action) => action.kind === "delete-path" && action.pathField === "saved.path"));
assert.equal(outDump.agentCommand.artifact.pathField, "saved.path");
assert.equal(outDump.agentCommand.artifact.hashField, "graphHash");
assert.equal(readFileSync(outPath, "utf8").replace(/\r\n/g, "\n"), graphText.replace(/\r\n/g, "\n"));

const proofs = await runRequiredCommands(dump.agentCommand.verificationCommands);
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 1);
assert.deepEqual(proofAudit.purposes, ["graph-dump"]);
assert.deepEqual(proofAudit.passedPurposes, ["graph-dump"]);

const outProofs = await runRequiredCommands(outDump.agentCommand.verificationCommands);
const outProofAudit = {
  required: outProofs.length,
  passed: outProofs.filter((item) => item.ok).length,
  failed: outProofs.filter((item) => !item.ok).length,
  purposes: [...new Set(outProofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(outProofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
assert.equal(outProofAudit.required, 1);
assert.deepEqual(outProofAudit.purposes, ["graph-dump"]);
assert.deepEqual(outProofAudit.passedPurposes, ["graph-dump"]);

const unsupportedOutFailures = await Promise.all([
  zeroJsonFailure(["graph", "inspect", "--json", "--target", "linux-musl-x64", "--profile", "audit", "--out", join(tempDir, "inspect.out"), input]),
  zeroJsonFailure(["graph", "check", "--json", "--target", "linux-musl-x64", "--profile", "audit", "--out", join(tempDir, "check.out"), input]),
  zeroJsonFailure(["graph", "compare", "--json", "--target", "linux-musl-x64", "--profile", "audit", "--out", join(tempDir, "compare.out"), "--against", input, input]),
]);
for (const report of unsupportedOutFailures) {
  assert.equal(report.agentCommand.kind, "agent-command-failure-contract");
  assert.equal(report.agentCommand.failure.retryCommands[0].purpose, "correct-command-usage");
  assert.equal(report.agentCommand.failure.retryCommands[0].required, true);
  assert.deepEqual(report.agentCommand.recommendedNextCommands[0].argv, report.agentCommand.failure.retryCommands[0].argv);
  assert(!report.agentCommand.failure.retryCommands[0].argv.includes("--out"));
  assert(report.agentCommand.failure.retryCommands[0].argv.includes("--target"));
  assert(report.agentCommand.failure.retryCommands[0].argv.includes("linux-musl-x64"));
  assert(report.agentCommand.failure.retryCommands[0].argv.includes("--profile"));
  assert(report.agentCommand.failure.retryCommands[0].argv.includes("audit"));
}
assert(!unsupportedOutFailures[0].agentCommand.failure.retryCommands[0].argv.includes("--node"));

const commandAudit = {
  replayable: JSON.stringify(dump.agentCommand.command.argv) === JSON.stringify(["zero", "graph", "dump", "--json", "--target", dump.agentCommand.command.argv[5], "--profile", dump.agentCommand.command.argv[7], input]),
  previewWritePolicyAuditable: dump.agentCommand.writePolicy.name === "preview-only" && dump.agentCommand.writePolicy.writesArtifacts === false,
  artifactWritePolicyAuditable: outDump.agentCommand.writePolicy.name === "writes-artifact" &&
    outDump.agentCommand.writePolicy.artifactPathField === "saved.path" &&
    outDump.agentCommand.writePolicy.rollbackField === "saved.path" &&
    outDump.agentCommand.writePolicy.verificationField === "agentCommand.verificationCommands" &&
    outDump.saved.path === outPath,
  viewFollowupStructured: dump.agentCommand.recommendedNextCommands[0].purpose === "graph-view" &&
    dump.agentCommand.recommendedNextCommands[0].inputField === "saved.path or command.argv input" &&
    dump.agentCommand.recommendedNextCommands[0].argv.includes("<saved.path-or-input>") &&
    dump.agentCommand.recommendedNextCommands[0].resultFields.includes("view") &&
    dump.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands") &&
    outDump.agentCommand.recommendedNextCommands[0].purpose === "graph-view",
  unsupportedOutRetryStructured: unsupportedOutFailures.every((report) =>
    report.agentCommand?.kind === "agent-command-failure-contract" &&
    report.agentCommand?.failure?.retryCommands?.[0]?.purpose === "correct-command-usage" &&
    report.agentCommand?.failure?.retryCommands?.[0]?.required === true &&
    !report.agentCommand?.failure?.retryCommands?.[0]?.argv?.includes("--out") &&
    report.agentCommand?.failure?.retryCommands?.[0]?.argv?.includes("--target") &&
    report.agentCommand?.failure?.retryCommands?.[0]?.argv?.includes("--profile") &&
    JSON.stringify(report.agentCommand?.recommendedNextCommands?.[0]?.argv) === JSON.stringify(report.agentCommand?.failure?.retryCommands?.[0]?.argv)) &&
    !unsupportedOutFailures[0].agentCommand.failure.retryCommands[0].argv.includes("--node"),
};

rmSync(tempDir, { recursive: true, force: true });

console.log(JSON.stringify({
  ok: true,
  kind: "agent-graph-dump-command-smoke",
  graphHash: dump.graphHash,
  nodes: dump.nodes.length,
  edges: dump.edges.length,
  validationOk: dump.validation.ok,
  commandAudit,
  proofAudit,
  outProofAudit,
}, null, 2));
