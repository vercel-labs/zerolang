import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? ".zero/bin/zero";
const passFixture = "conformance/native/pass/std-testing-helpers-test.0";
const failFixture = "conformance/native/fail/test-unexpected-pass.0";
const outDir = join(".zero", "agent-test-command-smoke", String(process.pid));
const graphPassPath = join(outDir, "pass.program-graph");
const graphFailPath = join(outDir, "fail.program-graph");
const proofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean }> = [];
const graphProofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean }> = [];

mkdirSync(outDir, { recursive: true });
for (const path of [graphPassPath, graphFailPath]) rmSync(path, { recursive: true, force: true });

async function zeroJson(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  assert(result.stdout, `zero ${args.join(" ")} should print JSON`);
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>, targetProofs = proofs) {
  for (const command of commands) {
    assert.equal(typeof command.purpose, "string");
    assert.equal(typeof command.required, "boolean");
    assert(Array.isArray(command.argv));
    if (!command.required) continue;
    assert.equal(command.argv[0], "zero");
    const body = await zeroJson(command.argv.slice(1));
    targetProofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: body.ok ?? true });
  }
}

function assertTestContract(report, input: string) {
  assert.equal(report.schemaVersion, 1);
  assert.equal(report.agentCommand.kind, "agent-test-command-contract");
  assert.equal(report.agentCommand.sourceKind, "source");
  assert(report.agentCommand.auditFields.includes("results"));
  assert(report.agentCommand.auditFields.includes("testDiscovery"));
  assert(report.agentCommand.stateFields.includes("results[].status"));
  assert(report.agentCommand.stateFields.includes("results[].expectedFailure"));
  assert(report.agentCommand.stateFields.includes("results[].location.sourceFile"));
  assert(report.agentCommand.failureFields.includes("results[].failure.message"));
  assert(report.agentCommand.failureFields.includes("results[].location.sourceFile"));
  assert(report.agentCommand.failureFields.includes("stderr"));
  assert.equal(report.agentCommand.recommendedNextCommands[0].purpose, "graph-inspect");
  assert.equal(report.agentCommand.recommendedNextCommands[0].required, false);
  assert.equal(report.agentCommand.recommendedNextCommands[0].when, "failedTests > 0 or unexpectedPasses > 0");
  assert.equal(report.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
  assert.deepEqual(report.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
  assert(report.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.graphHash"));
  assert(report.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.nodes[].id"));
  assert(report.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
  assert.deepEqual(report.agentCommand.verificationCommands.map((command) => command.purpose), ["source-check", "test-run"]);
  assert.equal(report.agentCommand.command.argv.at(-1), input);
}

function structuredFailureEvidence(report, expectedSource: string) {
  return report.agentCommand.auditFields.includes("testDiscovery") &&
    report.agentCommand.auditFields.includes("results") &&
    report.agentCommand.auditFields.includes("expectedFailures") &&
    report.agentCommand.auditFields.includes("unexpectedPasses") &&
    report.agentCommand.stateFields.includes("results[].status") &&
    report.agentCommand.stateFields.includes("results[].expectedFailure") &&
    report.agentCommand.stateFields.includes("results[].location.sourceFile") &&
    report.agentCommand.stateFields.includes("results[].location.line") &&
    report.agentCommand.stateFields.includes("results[].location.column") &&
    report.agentCommand.failureFields.includes("results[].failure.message") &&
    report.agentCommand.failureFields.includes("results[].failure.sourceFile") &&
    report.agentCommand.failureFields.includes("results[].failure.line") &&
    report.agentCommand.failureFields.includes("results[].failure.column") &&
    report.results[0].status === "unexpected-pass" &&
    report.results[0].expectedFailure === true &&
    report.results[0].failure?.message === "expected failure unexpectedly passed" &&
    report.results[0].failure?.sourceFile === expectedSource &&
    report.results[0].location?.sourceFile === expectedSource &&
    typeof report.results[0].failure?.line === "number" &&
    typeof report.results[0].failure?.column === "number" &&
    report.results[0].failure.line === report.results[0].location.line &&
    report.results[0].failure.column === report.results[0].location.column;
}

function structuredDiscovery(report, expectedMode: string) {
  return report.testDiscovery?.mode === expectedMode &&
    typeof report.testDiscovery?.selectedTests === "number" &&
    typeof report.testDiscovery?.discoveredTests === "number" &&
    report.selectedTests === report.testDiscovery.selectedTests &&
    report.discoveredTests === report.testDiscovery.discoveredTests;
}

const passing = await zeroJson(["test", "--json", passFixture]);
const failing = await zeroJson(["test", "--json", failFixture]);
const noTests = await zeroJson(["test", "--json", "examples/hello.0"]);
assertTestContract(passing, passFixture);
assertTestContract(failing, failFixture);
assertTestContract(noTests, "examples/hello.0");
assert.deepEqual(passing.agentCommand.command.argv, passing.agentCommand.verificationCommands.find((command) => command.purpose === "test-run")?.argv);
assert.deepEqual(failing.agentCommand.command.argv, failing.agentCommand.verificationCommands.find((command) => command.purpose === "test-run")?.argv);
assert.deepEqual(noTests.agentCommand.command.argv, noTests.agentCommand.verificationCommands.find((command) => command.purpose === "test-run")?.argv);

assert.equal(passing.ok, true);
assert.equal(passing.selectedTests, 1);
assert.equal(passing.results[0].status, "passed");
assert.equal(passing.results[0].failure, null);

assert.equal(failing.ok, false);
assert.equal(failing.failedTests, 1);
assert.equal(failing.unexpectedPasses, 1);
assert.equal(failing.results[0].status, "unexpected-pass");
assert.equal(failing.results[0].expectedFailure, true);
assert.equal(failing.results[0].failure.message, "expected failure unexpectedly passed");
assert.equal(failing.results[0].failure.sourceFile, failFixture);
assert.equal(failing.results[0].location.sourceFile, failFixture);
assert.equal(noTests.ok, true);
assert.equal(noTests.selectedTests, 0);
assert.equal(noTests.discoveredTests, 0);
assert.equal(noTests.results.length, 0);
assert.equal(noTests.agentCommand.recommendedNextCommands[1].purpose, "doc-audit");
assert.equal(noTests.agentCommand.recommendedNextCommands[1].required, false);
assert.equal(noTests.agentCommand.recommendedNextCommands[1].when, "selectedTests == 0");
assert.equal(noTests.agentCommand.recommendedNextCommands[1].inputField, "command.argv input");
assert.deepEqual(noTests.agentCommand.recommendedNextCommands[1].argv, ["zero", "doc", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert(noTests.agentCommand.recommendedNextCommands[1].resultFields.includes("publicationGate.missingExampleCount"));
assert(noTests.agentCommand.recommendedNextCommands[1].resultFields.includes("symbols[].name"));
assert(noTests.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.recommendedNextCommands"));

await runRequiredCommands(passing.agentCommand.verificationCommands);
await runRequiredCommands(failing.agentCommand.verificationCommands);
await runRequiredCommands(noTests.agentCommand.verificationCommands);

const proofAudit = {
  required: proofs.filter((item) => item.required).length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
  failedPurposes: [...new Set(proofs.filter((item) => !item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 6);
assert.deepEqual(proofAudit.purposes, ["source-check", "test-run"]);
assert(proofAudit.passedPurposes.includes("source-check"));
assert(proofAudit.passedPurposes.includes("test-run"));
assert.deepEqual(proofAudit.failedPurposes, ["test-run"]);

const graphPassDump = await zeroJson(["graph", "dump", "--json", "--out", graphPassPath, passFixture]);
const graphFailDump = await zeroJson(["graph", "dump", "--json", "--out", graphFailPath, failFixture]);
assert.equal(existsSync(graphPassPath), true);
assert.equal(existsSync(graphFailPath), true);
const graphPassing = await zeroJson(["graph", "test", "--json", graphPassPath]);
const graphFailing = await zeroJson(["graph", "test", "--json", graphFailPath]);

assert.equal(graphPassing.schemaVersion, 1);
assert.equal(graphPassing.ok, true);
assert.equal(graphPassing.agentCommand.kind, "agent-graph-test-command-contract");
assert.equal(graphPassing.agentCommand.sourceKind, "program-graph");
assert.equal(graphPassing.graph.artifact, graphPassPath);
assert.equal(graphPassing.graph.graphHash, graphPassDump.graphHash);
assert.equal(graphPassing.testDiscovery.mode, "program-graph");
assert.equal(graphPassing.selectedTests, 1);
assert.equal(graphPassing.results[0].status, "passed");
assert.equal(graphPassing.results[0].failure, null);
assert.deepEqual(graphPassing.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-check", "graph-test"]);
assert.equal(graphPassing.agentCommand.recommendedNextCommands[0].purpose, "graph-inspect");
assert.equal(graphPassing.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(graphPassing.agentCommand.recommendedNextCommands[0].when, "failedTests > 0 or unexpectedPasses > 0");
assert.equal(graphPassing.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(graphPassing.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<program-graph>"]);
assert(graphPassing.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.graphHash"));
assert(graphPassing.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.nodes[].nodeHash"));
assert(graphPassing.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));

assert.equal(graphFailing.schemaVersion, 1);
assert.equal(graphFailing.ok, false);
assert.equal(graphFailing.agentCommand.kind, "agent-graph-test-command-contract");
assert.equal(graphFailing.agentCommand.sourceKind, "program-graph");
assert.equal(graphFailing.graph.artifact, graphFailPath);
assert.equal(graphFailing.graph.graphHash, graphFailDump.graphHash);
assert.equal(graphFailing.failedTests, 1);
assert.equal(graphFailing.unexpectedPasses, 1);
assert.equal(graphFailing.results[0].status, "unexpected-pass");
assert.equal(graphFailing.results[0].expectedFailure, true);
assert.equal(graphFailing.results[0].failure.message, "expected failure unexpectedly passed");
assert.equal(graphFailing.results[0].failure.sourceFile, graphFailPath);
assert.deepEqual(graphFailing.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-check", "graph-test"]);
assert.deepEqual(graphPassing.agentCommand.command.argv, graphPassing.agentCommand.verificationCommands.find((command) => command.purpose === "graph-test")?.argv);
assert.deepEqual(graphFailing.agentCommand.command.argv, graphFailing.agentCommand.verificationCommands.find((command) => command.purpose === "graph-test")?.argv);

await runRequiredCommands(graphPassing.agentCommand.verificationCommands, graphProofs);
await runRequiredCommands(graphFailing.agentCommand.verificationCommands, graphProofs);
const graphProofAudit = {
  required: graphProofs.filter((item) => item.required).length,
  passed: graphProofs.filter((item) => item.ok).length,
  failed: graphProofs.filter((item) => !item.ok).length,
  purposes: [...new Set(graphProofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(graphProofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
  failedPurposes: [...new Set(graphProofs.filter((item) => !item.ok).map((item) => item.purpose))].sort(),
};
assert.equal(graphProofAudit.required, 4);
assert.deepEqual(graphProofAudit.purposes, ["graph-check", "graph-test"]);
assert(graphProofAudit.passedPurposes.includes("graph-check"));
assert(graphProofAudit.passedPurposes.includes("graph-test"));
assert.deepEqual(graphProofAudit.failedPurposes, ["graph-test"]);

const commandAudit = {
  replayable: [passing, failing, graphPassing, graphFailing].every((item) => {
    const testProof = item.agentCommand.verificationCommands.find((command) =>
      command.purpose === "test-run" || command.purpose === "graph-test");
    return JSON.stringify(item.agentCommand.command.argv) === JSON.stringify(testProof?.argv);
  }),
  failureEvidenceMatched: failing.results[0].failure.sourceFile === failFixture &&
    graphFailing.results[0].failure.message === "expected failure unexpectedly passed" &&
    proofAudit.failedPurposes.includes("test-run") &&
    graphProofAudit.failedPurposes.includes("graph-test"),
  failureInspectFollowupStructured: [passing, failing].every((item) =>
    item.agentCommand.recommendedNextCommands[0].purpose === "graph-inspect" &&
    item.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    item.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    item.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.graphHash")) &&
    [graphPassing, graphFailing].every((item) =>
      item.agentCommand.recommendedNextCommands[0].purpose === "graph-inspect" &&
      item.agentCommand.recommendedNextCommands[0].inputField === "command.argv input" &&
      item.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.nodes[].nodeHash") &&
      item.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands")),
  noTestsDocAuditFollowupStructured: noTests.selectedTests === 0 &&
    noTests.agentCommand.recommendedNextCommands[1].purpose === "doc-audit" &&
    noTests.agentCommand.recommendedNextCommands[1].when === "selectedTests == 0" &&
    noTests.agentCommand.recommendedNextCommands[1].argv.includes("<same-target>") &&
    noTests.agentCommand.recommendedNextCommands[1].argv.includes("<same-profile>") &&
    noTests.agentCommand.recommendedNextCommands[1].resultFields.includes("publicationGate.missingExampleCount") &&
    noTests.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.recommendedNextCommands"),
};
assert.equal(commandAudit.replayable, true);
assert.equal(commandAudit.failureEvidenceMatched, true);
assert.equal(commandAudit.failureInspectFollowupStructured, true);
assert.equal(commandAudit.noTestsDocAuditFollowupStructured, true);
const testEvidenceAudit = {
  sourceFailureFieldsStructured: structuredFailureEvidence(failing, failFixture),
  graphFailureFieldsStructured: structuredFailureEvidence(graphFailing, graphFailPath),
  sourceDiscoveryStructured: structuredDiscovery(passing, "single-file") &&
    structuredDiscovery(failing, "single-file"),
  graphDiscoveryStructured: structuredDiscovery(graphPassing, "program-graph") &&
    structuredDiscovery(graphFailing, "program-graph"),
  graphHashBound: graphPassing.graph.graphHash === graphPassDump.graphHash &&
    graphFailing.graph.graphHash === graphFailDump.graphHash &&
    graphPassing.agentCommand.stateFields.includes("graph.graphHash") &&
    graphFailing.agentCommand.stateFields.includes("graph.graphHash"),
  expectedFailureRegressionAudited: failing.unexpectedPasses === 1 &&
    graphFailing.unexpectedPasses === 1 &&
    proofAudit.failedPurposes.includes("test-run") &&
    graphProofAudit.failedPurposes.includes("graph-test"),
  noTestsAudited: noTests.selectedTests === 0 &&
    noTests.discoveredTests === 0 &&
    noTests.agentCommand.stateFields.includes("selectedTests") &&
    noTests.agentCommand.stateFields.includes("discoveredTests"),
};
assert.equal(testEvidenceAudit.sourceFailureFieldsStructured, true);
assert.equal(testEvidenceAudit.graphFailureFieldsStructured, true);
assert.equal(testEvidenceAudit.sourceDiscoveryStructured, true);
assert.equal(testEvidenceAudit.graphDiscoveryStructured, true);
assert.equal(testEvidenceAudit.graphHashBound, true);
assert.equal(testEvidenceAudit.expectedFailureRegressionAudited, true);
assert.equal(testEvidenceAudit.noTestsAudited, true);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-test-command-smoke",
  pass: { selectedTests: passing.selectedTests, status: passing.results[0].status },
  fail: { failedTests: failing.failedTests, unexpectedPasses: failing.unexpectedPasses, status: failing.results[0].status },
  noTests: { selectedTests: noTests.selectedTests, discoveredTests: noTests.discoveredTests },
  commandAudit,
  testEvidenceAudit,
  proofAudit,
  graph: {
    pass: { selectedTests: graphPassing.selectedTests, status: graphPassing.results[0].status },
    fail: { failedTests: graphFailing.failedTests, unexpectedPasses: graphFailing.unexpectedPasses, status: graphFailing.results[0].status },
    proofAudit: graphProofAudit,
  },
}, null, 2));
console.log("agent test command smoke ok");
