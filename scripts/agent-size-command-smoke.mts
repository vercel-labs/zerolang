import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? ".zero/bin/zero";
const outDir = join(".zero", "agent-size-command-smoke", String(process.pid));
const artifactPath = join(outDir, "hello-size");
const repeatPath = join(outDir, "hello-size-repeat");
const graphPath = join(outDir, "hello.program-graph");
const graphArtifactPath = join(outDir, "hello-graph-size");
const graphRepeatPath = join(outDir, "hello-graph-size-repeat");
const fixture = "examples/hello.0";
const proofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean; artifactHash?: unknown; artifactBytes?: unknown }> = [];
const graphProofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean; artifactHash?: unknown; artifactBytes?: unknown }> = [];

mkdirSync(outDir, { recursive: true });
rmSync(artifactPath, { recursive: true, force: true });
rmSync(`${artifactPath}.exe`, { recursive: true, force: true });
rmSync(repeatPath, { recursive: true, force: true });
rmSync(`${repeatPath}.exe`, { recursive: true, force: true });
for (const path of [graphPath, graphArtifactPath, graphRepeatPath]) rmSync(path, { recursive: true, force: true });

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
    targetProofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: body.ok ?? true, artifactHash: body.artifactHash, artifactBytes: body.artifactBytes });
  }
}

function safetyStructured(report) {
  return report.agentCommand.auditFields.includes("safetyFacts") &&
    report.agentCommand.auditFields.includes("productionReadiness") &&
    report.safetyFacts?.schemaVersion === 1 &&
    report.safetyFacts?.bounds?.policy === "checked" &&
    report.safetyFacts?.initialization?.locals === "initializer-required" &&
    report.safetyFacts?.aliasing?.references === "provenance-checked" &&
    report.safetyFacts?.lifetimes?.borrowedStdlibResults === "provenance-checked" &&
    report.safetyFacts?.mir?.invalidMemoryContractsBlockEmission === true &&
    Array.isArray(report.safetyFacts?.uncheckedSurfaces) &&
    report.productionReadiness?.schemaVersion === 1 &&
    typeof report.productionReadiness?.status === "string" &&
    Array.isArray(report.productionReadiness?.blockingRisks) &&
    Array.isArray(report.productionReadiness?.nextRequiredControls);
}

function cacheStructured(report, expected: { sourceKind?: string; graphHash?: string } = {}) {
  const packageCacheAdvertised = report.agentCommand.auditFields.includes("packageCache");
  const compilerCacheAdvertised = report.agentCommand.auditFields.includes("compilerCaches");
  const incrementalAdvertised = report.agentCommand.auditFields.includes("incrementalInvalidation");
  const compilerVersion = report.packageCache?.cacheKeyInputs?.compilerVersion;
  const packageCacheOk = typeof compilerVersion === "string" &&
    typeof report.packageCache.cacheKeyInputs.target === "string" &&
    typeof report.packageCache.cacheKeyInputs.dependencyGraphHash === "string" &&
    typeof report.packageCache.cacheKeyInputs.manifestHash === "string" &&
    typeof report.packageCache.cacheKeyInputs.lockfileHash === "string" &&
    Array.isArray(report.packageCache.invalidationReasons) &&
    report.packageCache.invalidationReasons.includes("dependency graph changed");
  const compilerCachesOk = Array.isArray(report.compilerCaches) &&
    report.compilerCaches.length >= 1 &&
    report.compilerCaches.every((cache) => typeof cache.name === "string" &&
      /^[0-9a-f]{16}$/.test(cache.key) &&
      cache.compilerVersion === compilerVersion &&
      typeof cache.invalidatesOn === "string" &&
      typeof cache.dependencyGraphHash === "string" &&
      typeof cache.lockfileHash === "string" &&
      (!expected.sourceKind || cache.sourceKind === expected.sourceKind) &&
      (!expected.graphHash || cache.graphHash === expected.graphHash));
  const incrementalOk = report.incrementalInvalidation?.partialDiagnosticsStable === true &&
    report.incrementalInvalidation?.targetDependency === report.target &&
    report.incrementalInvalidation?.profileDependency === report.profile &&
    typeof report.incrementalInvalidation?.cacheHits === "number" &&
    typeof report.incrementalInvalidation?.cacheMisses === "number" &&
    report.incrementalInvalidation?.interfaceFingerprints?.algorithm === "fnv1a64-zero-interface-v1" &&
    (!expected.sourceKind || report.incrementalInvalidation.sourceKind === expected.sourceKind) &&
    (!expected.graphHash || report.incrementalInvalidation.graphInput?.graphHash === expected.graphHash);
  return {
    packageCacheAdvertised,
    compilerCacheAdvertised,
    incrementalAdvertised,
    packageCacheOk,
    compilerCachesOk,
    incrementalOk,
  };
}

function artifactWritePolicyStructured(report) {
  return report.agentCommand.auditFields.includes("agentCommand.writePolicy") &&
    report.agentCommand.writePolicy?.name === "writes-artifact" &&
    report.agentCommand.writePolicy?.writesSource === false &&
    report.agentCommand.writePolicy?.writesArtifacts === true &&
    report.agentCommand.writePolicy?.artifactPathField === report.agentCommand.artifact.pathField &&
    report.agentCommand.writePolicy?.requiresVerification === true &&
    report.agentCommand.writePolicy?.verificationField === "agentCommand.verificationCommands" &&
    report.agentCommand.writePolicy?.rollbackField === "artifactPath" &&
    report.agentCommand.writePolicy?.rollbackPolicy === "agent-enforced-delete-or-replace" &&
    report.agentCommand.writePolicy?.rollbackActions?.some((action) =>
      action.kind === "delete-path" &&
      action.pathField === "artifactPath" &&
      action.condition === "created-by-command" &&
      action.requiresVerification === true);
}

const size = await zeroJson(["size", "--json", "--out", artifactPath, fixture]);
const repeat = await zeroJson(["size", "--json", "--out", repeatPath, fixture]);

assert.equal(size.schemaVersion, 1);
assert.equal(size.agentCommand.kind, "agent-size-command-contract");
assert.equal(size.agentCommand.sourceKind, "source");
assert.equal(size.agentCommand.artifact.pathField, "artifactPath");
assert.equal(size.agentCommand.artifact.bytesField, "artifactBytes");
assert.equal(size.agentCommand.artifact.hashField, "artifactHash");
assert.deepEqual(size.agentCommand.artifact.verificationFields, ["artifactHash", "artifactBytes"]);
assert(size.agentCommand.auditFields.includes("artifactHash"));
assert.equal(artifactWritePolicyStructured(size), true);
assert.equal(size.generatedCBytes, 0);
assert.equal(size.cBridgeFallback, false);
assert.equal(size.artifactBytes > 0, true);
assert.equal(existsSync(size.artifactPath) || existsSync(size.artifactPath.replace(/\\/g, "/")), true);
assert.equal(size.artifactHash.algorithm, "fnv1a64");
assert.match(size.artifactHash.value, /^[0-9a-f]{16}$/);
assert.deepEqual(repeat.artifactHash, size.artifactHash);
assert.equal(repeat.artifactBytes, size.artifactBytes);
assert.deepEqual(size.agentCommand.verificationCommands.map((command) => command.purpose), ["source-check", "size-analysis"]);
assert.equal(size.agentCommand.recommendedNextCommands[0].purpose, "artifact-validate");
assert.equal(size.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(size.agentCommand.recommendedNextCommands[0].when, "after size budget review when a runnable artifact is required");
assert.equal(size.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(size.agentCommand.recommendedNextCommands[0].argv, ["zero", "build", "--json", "--emit", "exe", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert(size.agentCommand.recommendedNextCommands[0].resultFields.includes("artifactHash"));
assert(size.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.writePolicy"));
assert(size.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
await runRequiredCommands(size.agentCommand.verificationCommands);
const sizeProof = proofs.find((item) => item.purpose === "size-analysis");
assert(sizeProof);
assert.deepEqual(sizeProof.argv, size.agentCommand.command.argv);
assert.deepEqual(sizeProof.artifactHash, size.artifactHash);
assert.equal(sizeProof.artifactBytes, size.artifactBytes);

const proofAudit = {
  required: proofs.filter((item) => item.required).length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 2);
assert.deepEqual(proofAudit.passedPurposes, ["size-analysis", "source-check"]);

const graphDump = await zeroJson(["graph", "dump", "--json", "--out", graphPath, fixture]);
assert.equal(existsSync(graphPath), true);
const graphSize = await zeroJson(["graph", "size", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", graphArtifactPath, graphPath]);
const graphRepeat = await zeroJson(["graph", "size", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", graphRepeatPath, graphPath]);
assert.equal(graphSize.schemaVersion, 1);
assert.equal(graphSize.agentCommand.kind, "agent-graph-size-command-contract");
assert.equal(graphSize.agentCommand.sourceKind, "program-graph");
assert.equal(graphSize.graph.artifact, graphPath);
assert.equal(graphSize.graph.graphHash, graphDump.graphHash);
assert.equal(graphSize.agentCommand.artifact.pathField, "artifactPath");
assert.equal(graphSize.agentCommand.artifact.bytesField, "artifactBytes");
assert.equal(graphSize.agentCommand.artifact.hashField, "artifactHash");
assert.deepEqual(graphSize.agentCommand.artifact.verificationFields, ["artifactHash", "artifactBytes"]);
assert(graphSize.agentCommand.auditFields.includes("artifactHash"));
assert.equal(artifactWritePolicyStructured(graphSize), true);
assert.equal(graphSize.artifactBytes > 0, true);
assert.equal(existsSync(graphSize.artifactPath) || existsSync(graphSize.artifactPath.replace(/\\/g, "/")), true);
assert.equal(graphSize.artifactHash.algorithm, "fnv1a64");
assert.match(graphSize.artifactHash.value, /^[0-9a-f]{16}$/);
assert.deepEqual(graphRepeat.artifactHash, graphSize.artifactHash);
assert.equal(graphRepeat.artifactBytes, graphSize.artifactBytes);
assert.deepEqual(graphSize.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-check", "graph-size"]);
assert.equal(graphSize.agentCommand.recommendedNextCommands[0].purpose, "artifact-validate");
assert.equal(graphSize.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(graphSize.agentCommand.recommendedNextCommands[0].when, "after graph size budget review when a runnable artifact is required");
assert.equal(graphSize.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(graphSize.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "build", "--json", "--emit", "exe", "--target", "<same-target>", "--profile", "<same-profile>", "<program-graph>"]);
assert(graphSize.agentCommand.recommendedNextCommands[0].resultFields.includes("artifactHash"));
assert(graphSize.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.writePolicy"));
assert(graphSize.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
await runRequiredCommands(graphSize.agentCommand.verificationCommands, graphProofs);
const graphSizeProof = graphProofs.find((item) => item.purpose === "graph-size");
assert(graphSizeProof);
assert.deepEqual(graphSizeProof.argv, graphSize.agentCommand.command.argv);
assert.deepEqual(graphSizeProof.artifactHash, graphSize.artifactHash);
assert.equal(graphSizeProof.artifactBytes, graphSize.artifactBytes);
const commandAudit = {
  replayable: JSON.stringify(sizeProof.argv) === JSON.stringify(size.agentCommand.command.argv) &&
    JSON.stringify(graphSizeProof.argv) === JSON.stringify(graphSize.agentCommand.command.argv),
  artifactEvidenceMatched: JSON.stringify(sizeProof.artifactHash) === JSON.stringify(size.artifactHash) &&
    sizeProof.artifactBytes === size.artifactBytes &&
    JSON.stringify(graphSizeProof.artifactHash) === JSON.stringify(graphSize.artifactHash) &&
    graphSizeProof.artifactBytes === graphSize.artifactBytes,
  artifactWritePolicyAuditable: artifactWritePolicyStructured(size) && artifactWritePolicyStructured(graphSize),
  artifactRollbackActionAuditable: size.agentCommand.writePolicy.rollbackActions[0].pathField === size.agentCommand.writePolicy.rollbackField &&
    graphSize.agentCommand.writePolicy.rollbackActions[0].pathField === graphSize.agentCommand.writePolicy.rollbackField,
  sizeBuildFollowupStructured: size.agentCommand.recommendedNextCommands[0].purpose === "artifact-validate" &&
    size.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    size.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    size.agentCommand.recommendedNextCommands[0].resultFields.includes("artifactHash") &&
    graphSize.agentCommand.recommendedNextCommands[0].purpose === "artifact-validate" &&
    graphSize.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    graphSize.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    graphSize.agentCommand.recommendedNextCommands[0].resultFields.includes("artifactHash") &&
    graphSize.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"),
  targetProfilePreserved: graphSize.agentCommand.command.argv.includes("--target") &&
    graphSize.agentCommand.command.argv.includes("linux-musl-x64") &&
    graphSize.agentCommand.command.argv.includes("--profile") &&
    graphSize.agentCommand.command.argv.includes("dev") &&
    JSON.stringify(graphSizeProof.argv) === JSON.stringify(graphSize.agentCommand.command.argv),
};
assert.equal(commandAudit.replayable, true);
assert.equal(commandAudit.artifactEvidenceMatched, true);
assert.equal(commandAudit.artifactWritePolicyAuditable, true);
assert.equal(commandAudit.artifactRollbackActionAuditable, true);
assert.equal(commandAudit.sizeBuildFollowupStructured, true);
assert.equal(commandAudit.targetProfilePreserved, true);
const safetyAudit = {
  sourceSafetyStructured: safetyStructured(size),
  graphSafetyStructured: safetyStructured(graphSize),
  productionGateStructured: size.productionReadiness?.gate === "sensitive-production-v1" &&
    graphSize.productionReadiness?.gate === "sensitive-production-v1",
  blockingRisksAuditable: size.productionReadiness?.blockingRisks?.some((risk) => risk.id === "runtime-integer-overflow-unchecked") &&
    size.productionReadiness?.blockingRisks?.some((risk) => risk.id === "host-effects-not-sandboxed") &&
    graphSize.productionReadiness?.blockingRisks?.some((risk) => risk.id === "runtime-integer-overflow-unchecked"),
  uncheckedSurfacesAuditable: size.safetyFacts?.uncheckedSurfaces?.some((surface) => surface.surface === "C imports") &&
    size.safetyFacts?.uncheckedSurfaces?.some((surface) => surface.surface === "host filesystem/process/network effects") &&
    graphSize.safetyFacts?.uncheckedSurfaces?.some((surface) => surface.surface === "C imports"),
};
assert.equal(safetyAudit.sourceSafetyStructured, true);
assert.equal(safetyAudit.graphSafetyStructured, true);
assert.equal(safetyAudit.productionGateStructured, true);
assert.equal(safetyAudit.blockingRisksAuditable, true);
assert.equal(safetyAudit.uncheckedSurfacesAuditable, true);
const cacheAudit = {
  source: cacheStructured(size),
  graph: cacheStructured(graphSize, { sourceKind: "program-graph", graphHash: graphDump.graphHash }),
};
for (const audit of [cacheAudit.source, cacheAudit.graph]) {
  assert.equal(audit.packageCacheAdvertised, true);
  assert.equal(audit.compilerCacheAdvertised, true);
  assert.equal(audit.incrementalAdvertised, true);
  assert.equal(audit.packageCacheOk, true);
  assert.equal(audit.compilerCachesOk, true);
  assert.equal(audit.incrementalOk, true);
}
const graphProofAudit = {
  required: graphProofs.filter((item) => item.required).length,
  passed: graphProofs.filter((item) => item.ok).length,
  failed: graphProofs.filter((item) => !item.ok).length,
  purposes: [...new Set(graphProofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(graphProofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
assert.equal(graphProofAudit.required, 2);
assert.deepEqual(graphProofAudit.passedPurposes, ["graph-check", "graph-size"]);
console.log(JSON.stringify({ ok: true, kind: "agent-size-command-smoke", artifactPath: size.artifactPath, artifactBytes: size.artifactBytes, artifactHash: size.artifactHash, repeatArtifactHash: repeat.artifactHash, commandAudit, safetyAudit, cacheAudit, proofAudit, graph: { artifactPath: graphSize.artifactPath, artifactBytes: graphSize.artifactBytes, artifactHash: graphSize.artifactHash, repeatArtifactHash: graphRepeat.artifactHash, proofAudit: graphProofAudit } }, null, 2));
console.log("agent size command smoke ok");
