import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent graph compare command smoke requires ZERO_BIN or ${defaultZeroBin}`);

const input = "examples/hello.0";
const tmpDir = join(".zero", "agent-graph-compare-command-smoke", String(process.pid));
const artifact = join(tmpDir, "hello.program-graph");
rmSync(tmpDir, { recursive: true, force: true });
mkdirSync(tmpDir, { recursive: true });

async function zeroJson(args: string[]) {
  const { stdout } = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 });
  return JSON.parse(stdout);
}

async function zeroJsonFailure(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  assert.notEqual(result.code ?? 0, 0);
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

await execFileAsync(zero, ["graph", "dump", "--out", artifact, input], { maxBuffer: 16 * 1024 * 1024 });
const compare = await zeroJson(["graph", "compare", "--json", "--against", artifact, input]);
assert.equal(compare.agentCommand.kind, "agent-graph-compare-command-contract");
assert.deepEqual(compare.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-compare"]);
assert(compare.agentCommand.auditFields.includes("comparison"));
assert(compare.agentCommand.stateFields.includes("semanticStable"));
assert.equal(compare.ok, true);
assert.equal(compare.semanticStable, true);
assert.equal(compare.comparison.ok, true);
assert.equal(compare.agentCommand.recommendedNextCommands[0].purpose, "graph-view");
assert.equal(compare.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(compare.agentCommand.recommendedNextCommands[0].when, "semanticStable");
assert.equal(compare.agentCommand.recommendedNextCommands[0].inputField, "right.input or left.input");
assert.deepEqual(compare.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "view", "--json", "<program-graph-or-source>"]);
assert(compare.agentCommand.recommendedNextCommands[0].resultFields.includes("graphHash"));
assert(compare.agentCommand.recommendedNextCommands[0].resultFields.includes("view"));
assert(compare.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.equal(compare.agentCommand.recommendedNextCommands[1].purpose, "graph-view");
assert.equal(compare.agentCommand.recommendedNextCommands[1].required, false);
assert.equal(compare.agentCommand.recommendedNextCommands[1].when, "semanticStable == false");
assert.equal(compare.agentCommand.recommendedNextCommands[1].inputField, "left.input or right.input");
assert.deepEqual(compare.agentCommand.recommendedNextCommands[1].argv, ["zero", "graph", "view", "--json", "<left-or-right-input>"]);
assert(compare.agentCommand.recommendedNextCommands[1].resultFields.includes("comparison.code"));
assert(compare.agentCommand.recommendedNextCommands[1].resultFields.includes("comparison.field"));
assert(compare.left.graphHash.startsWith("graph:"));
assert.equal(compare.left.graphHash, compare.right.graphHash);
assert.equal(compare.left.sourceKind, "checked-source");
assert.equal(compare.right.sourceKind, "program-graph");

const proofs = await runRequiredCommands(compare.agentCommand.verificationCommands);
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

const commandAudit = {
  graphViewFollowupStructured: compare.agentCommand.recommendedNextCommands[0].purpose === "graph-view" &&
    compare.agentCommand.recommendedNextCommands[0].when === "semanticStable" &&
    compare.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"),
  unstableGraphViewFollowupStructured: compare.agentCommand.recommendedNextCommands[1].purpose === "graph-view" &&
    compare.agentCommand.recommendedNextCommands[1].when === "semanticStable == false" &&
    compare.agentCommand.recommendedNextCommands[1].inputField === "left.input or right.input" &&
    compare.agentCommand.recommendedNextCommands[1].resultFields.includes("comparison.code") &&
    compare.agentCommand.recommendedNextCommands[1].resultFields.includes("comparison.field"),
};
assert.equal(commandAudit.graphViewFollowupStructured, true);
assert.equal(commandAudit.unstableGraphViewFollowupStructured, true);

const unstableCompare = await zeroJsonFailure(["graph", "compare", "--json", "--against", "examples/add.0", input]);
assert.equal(unstableCompare.ok, false);
assert.equal(unstableCompare.semanticStable, false);
assert.equal(unstableCompare.agentCommand.kind, "agent-graph-compare-command-contract");
assert.equal(unstableCompare.agentCommand.recommendedNextCommands[1].purpose, "graph-view");
assert.equal(unstableCompare.agentCommand.recommendedNextCommands[1].when, "semanticStable == false");

const compareMissingAgainst = await zeroJsonFailure(["graph", "compare", "--json", input]);
const findMissingSymbol = await zeroJsonFailure(["graph", "find", "--json", input]);
const sliceMissingNode = await zeroJsonFailure(["graph", "slice", "--json", input]);
const impactMissingNode = await zeroJsonFailure(["graph", "impact", "--json", input]);
const missingGraphArtifact = await zeroJsonFailure(["graph", "test", "--json", "--target", "linux-musl-x64", join(tmpDir, "missing.program-graph")]);
const failureReports = [compareMissingAgainst, findMissingSymbol, sliceMissingNode, impactMissingNode];
for (const report of failureReports) {
  assert.equal(report.ok, false);
  assert.equal(report.agentCommand.kind, "agent-command-failure-contract");
  assert.equal(report.agentCommand.failure.class, "invalid-command-usage");
  assert.equal(report.agentCommand.failure.retryCommands[0].purpose, "correct-command-usage");
  assert.equal(report.agentCommand.failure.retryCommands[0].required, true);
  assert(report.agentCommand.failure.retryCommands[0].argv.includes("--json"));
  assert(report.agentCommand.auditFields.includes("agentCommand.failure.retryCommands"));
  assert.equal(report.agentCommand.recommendedNextCommands[0].purpose, "correct-command-usage");
  assert.deepEqual(report.agentCommand.recommendedNextCommands[0].argv, report.agentCommand.failure.retryCommands[0].argv);
}
assert(compareMissingAgainst.agentCommand.failure.retryCommands[0].argv.includes("--against"));
assert(findMissingSymbol.agentCommand.failure.retryCommands[0].argv.includes("--symbol"));
assert(sliceMissingNode.agentCommand.failure.retryCommands[0].argv.includes("--node"));
assert(impactMissingNode.agentCommand.failure.retryCommands[0].argv.includes("--node"));
assert.equal(missingGraphArtifact.agentCommand.failure.class, "missing-graph-artifact");
assert.equal(missingGraphArtifact.agentCommand.failure.retryCommands[0].purpose, "graph-artifact-create");
assert.equal(missingGraphArtifact.agentCommand.failure.retryCommands[0].required, true);
assert.deepEqual(missingGraphArtifact.agentCommand.recommendedNextCommands[0].argv, missingGraphArtifact.agentCommand.failure.retryCommands[0].argv);
assert(missingGraphArtifact.agentCommand.failure.retryCommands[0].argv.includes("--out"));
assert(missingGraphArtifact.agentCommand.failure.retryCommands[0].argv.includes("<source-or-package>"));
assert(missingGraphArtifact.agentCommand.failure.retryCommands[0].resultFields.includes("saved.path"));

const commandFailureAudit = {
  commandFailureRetryStructured: failureReports.every((report) =>
    report.agentCommand?.kind === "agent-command-failure-contract" &&
    report.agentCommand?.failure?.class === "invalid-command-usage" &&
    report.agentCommand?.failure?.retryCommands?.[0]?.purpose === "correct-command-usage" &&
    report.agentCommand?.recommendedNextCommands?.[0]?.purpose === "correct-command-usage" &&
    JSON.stringify(report.agentCommand.recommendedNextCommands[0].argv) === JSON.stringify(report.agentCommand.failure.retryCommands[0].argv)),
  graphUsageRetryArgsBound: compareMissingAgainst.agentCommand.failure.retryCommands[0].argv.includes("--against") &&
    findMissingSymbol.agentCommand.failure.retryCommands[0].argv.includes("--symbol") &&
    sliceMissingNode.agentCommand.failure.retryCommands[0].argv.includes("--node") &&
    impactMissingNode.agentCommand.failure.retryCommands[0].argv.includes("--node"),
  missingGraphArtifactRecoveryStructured: missingGraphArtifact.agentCommand?.failure?.class === "missing-graph-artifact" &&
    missingGraphArtifact.agentCommand?.failure?.retryCommands?.[0]?.purpose === "graph-artifact-create" &&
    missingGraphArtifact.agentCommand?.failure?.retryCommands?.[0]?.argv?.includes("--out") &&
    missingGraphArtifact.agentCommand?.failure?.retryCommands?.[0]?.argv?.includes("<source-or-package>") &&
    missingGraphArtifact.agentCommand?.failure?.retryCommands?.[0]?.resultFields?.includes("saved.path") &&
    JSON.stringify(missingGraphArtifact.agentCommand.recommendedNextCommands[0].argv) === JSON.stringify(missingGraphArtifact.agentCommand.failure.retryCommands[0].argv),
};
assert.equal(commandFailureAudit.commandFailureRetryStructured, true);
assert.equal(commandFailureAudit.graphUsageRetryArgsBound, true);
assert.equal(commandFailureAudit.missingGraphArtifactRecoveryStructured, true);

assert.equal(proofAudit.required, 1);
assert.deepEqual(proofAudit.purposes, ["graph-compare"]);
assert.deepEqual(proofAudit.passedPurposes, ["graph-compare"]);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-graph-compare-command-smoke",
  semanticStable: compare.semanticStable,
  graphHash: compare.left.graphHash,
  leftSourceKind: compare.left.sourceKind,
  rightSourceKind: compare.right.sourceKind,
  commandAudit,
  commandFailureAudit,
  proofAudit,
}, null, 2));
