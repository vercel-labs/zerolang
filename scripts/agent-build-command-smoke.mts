import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? ".zero/bin/zero";
const outDir = join(".zero", "agent-build-command-smoke", String(process.pid));
const sourcePath = join(outDir, "src", "hello.0");
const graphPath = join(outDir, "hello.program-graph");
const graphBuildPath = join(outDir, "hello-graph-build");
const proofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean; artifactHash?: unknown; artifactBytes?: unknown }> = [];
const graphProofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean; artifactHash?: unknown; artifactBytes?: unknown }> = [];

mkdirSync(dirname(sourcePath), { recursive: true });
writeFileSync(sourcePath, readFileSync("examples/hello.0", "utf8"));

async function zeroJson(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
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

const defaultOutRoot = join(".zero", "out", ".zero", "agent-build-command-smoke", String(process.pid));
rmSync(defaultOutRoot, { recursive: true, force: true });

const build = await zeroJson(["build", "--json", sourcePath]);
const repeatPath = join(outDir, "repeat.exe");
const repeatBuild = await zeroJson(["build", "--json", "--out", repeatPath, sourcePath]);
assert.equal(build.schemaVersion, 1);
assert.equal(build.agentCommand.kind, "agent-build-command-contract");
assert.equal(build.agentCommand.sourceKind, "source");
assert.equal(build.agentCommand.artifact.pathField, "artifactPath");
assert.equal(build.agentCommand.artifact.bytesField, "artifactBytes");
assert.equal(build.agentCommand.artifact.kindField, "emit");
assert.equal(build.agentCommand.artifact.hashField, "artifactHash");
assert.deepEqual(build.agentCommand.artifact.verificationFields, ["artifactHash", "artifactBytes"]);
assert.equal(artifactWritePolicyStructured(build), true);
assert.equal(build.emit, "exe");
assert.equal(build.generatedCBytes, 0);
assert.equal(build.artifactBytes > 0, true);
assert.equal(existsSync(build.artifactPath) || existsSync(build.artifactPath.replace(/\\/g, "/")), true);
assert.equal(build.artifactHash.algorithm, "fnv1a64");
assert.match(build.artifactHash.value, /^[0-9a-f]{16}$/);
assert.deepEqual(repeatBuild.artifactHash, build.artifactHash);
assert.equal(repeatBuild.artifactBytes, build.artifactBytes);
assert(build.agentCommand.auditFields.includes("artifactPath"));
assert(build.agentCommand.auditFields.includes("artifactHash"));
assert(build.agentCommand.auditFields.includes("releaseTargetContract"));
assert.equal(build.agentCommand.recommendedNextCommands[0].purpose, "test-run");
assert.equal(build.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(build.agentCommand.recommendedNextCommands[0].when, "after artifact validation when behavioral confidence is required");
assert.equal(build.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(build.agentCommand.recommendedNextCommands[0].argv, ["zero", "test", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert(build.agentCommand.recommendedNextCommands[0].resultFields.includes("passedTests"));
assert(build.agentCommand.recommendedNextCommands[0].resultFields.includes("failedTests"));
assert(build.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.deepEqual(build.agentCommand.verificationCommands.map((command) => command.purpose), ["source-check", "size-analysis", "artifact-validate"]);
assert(build.agentCommand.verificationCommands.find((command) => command.purpose === "size-analysis")?.resultFields.includes("sizeBreakdown"));
assert(build.agentCommand.verificationCommands.find((command) => command.purpose === "artifact-validate")?.resultFields.includes("artifactHash"));
await runRequiredCommands(build.agentCommand.verificationCommands);
const artifactProof = proofs.find((item) => item.purpose === "artifact-validate");
assert(artifactProof);
assert.deepEqual(artifactProof.argv, build.agentCommand.command.argv);
assert.deepEqual(artifactProof.artifactHash, build.artifactHash);
assert.equal(artifactProof.artifactBytes, build.artifactBytes);

const proofAudit = {
  required: proofs.filter((item) => item.required).length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 3);
assert.deepEqual(proofAudit.passedPurposes, ["artifact-validate", "size-analysis", "source-check"]);

const graphDump = await zeroJson(["graph", "dump", "--json", "--out", graphPath, sourcePath]);
assert.equal(existsSync(graphPath), true);
const graphBuild = await zeroJson(["graph", "build", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", graphBuildPath, graphPath]);
const graphRepeatPath = join(outDir, "hello-graph-build-repeat");
const graphRepeatBuild = await zeroJson(["graph", "build", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", graphRepeatPath, graphPath]);
assert.equal(graphBuild.schemaVersion, 1);
assert.equal(graphBuild.agentCommand.kind, "agent-graph-build-command-contract");
assert.equal(graphBuild.agentCommand.sourceKind, "program-graph");
assert.equal(graphBuild.graph.artifact, graphPath);
assert.equal(graphBuild.graph.graphHash, graphDump.graphHash);
assert.equal(graphBuild.agentCommand.artifact.pathField, "artifactPath");
assert.equal(graphBuild.agentCommand.artifact.bytesField, "artifactBytes");
assert.equal(graphBuild.agentCommand.artifact.kindField, "emit");
assert.equal(graphBuild.agentCommand.artifact.hashField, "artifactHash");
assert.deepEqual(graphBuild.agentCommand.artifact.verificationFields, ["artifactHash", "artifactBytes"]);
assert.equal(artifactWritePolicyStructured(graphBuild), true);
assert.equal(graphBuild.artifactHash.algorithm, "fnv1a64");
assert.match(graphBuild.artifactHash.value, /^[0-9a-f]{16}$/);
assert.deepEqual(graphRepeatBuild.artifactHash, graphBuild.artifactHash);
assert.equal(graphRepeatBuild.artifactBytes, graphBuild.artifactBytes);
assert.deepEqual(graphBuild.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-check", "graph-size", "artifact-validate"]);
assert(graphBuild.agentCommand.verificationCommands.find((command) => command.purpose === "graph-size")?.resultFields.includes("sizeBreakdown"));
assert(graphBuild.agentCommand.verificationCommands.find((command) => command.purpose === "artifact-validate")?.resultFields.includes("artifactHash"));
assert.equal(graphBuild.agentCommand.recommendedNextCommands[0].purpose, "graph-size");
assert.equal(graphBuild.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(graphBuild.agentCommand.recommendedNextCommands[0].when, "after artifact build or budget review");
assert.equal(graphBuild.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(graphBuild.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "size", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<program-graph>"]);
assert(graphBuild.agentCommand.recommendedNextCommands[0].resultFields.includes("sizeBreakdown"));
assert(graphBuild.agentCommand.recommendedNextCommands[0].resultFields.includes("profileBudget"));
assert(graphBuild.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.equal(graphBuild.agentCommand.recommendedNextCommands[1].purpose, "graph-test");
assert.equal(graphBuild.agentCommand.recommendedNextCommands[1].required, false);
assert.equal(graphBuild.agentCommand.recommendedNextCommands[1].when, "after artifact validation when graph-backed behavioral confidence is required");
assert.equal(graphBuild.agentCommand.recommendedNextCommands[1].inputField, "command.argv input");
assert.deepEqual(graphBuild.agentCommand.recommendedNextCommands[1].argv, ["zero", "graph", "test", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<program-graph>"]);
assert(graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("ok"));
assert(graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("passedTests"));
assert(graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("failedTests"));
assert(graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("unexpectedPasses"));
assert(graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("results[]"));
assert(graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("results[].status"));
assert(graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"));
await runRequiredCommands(graphBuild.agentCommand.verificationCommands, graphProofs);
const graphArtifactProof = graphProofs.find((item) => item.purpose === "artifact-validate");
assert(graphArtifactProof);
assert.deepEqual(graphArtifactProof.argv, graphBuild.agentCommand.command.argv);
assert.deepEqual(graphArtifactProof.artifactHash, graphBuild.artifactHash);
assert.equal(graphArtifactProof.artifactBytes, graphBuild.artifactBytes);
const sourceRunJson = await zeroJson(["run", "--json", "--target", build.target, "--profile", build.profile, sourcePath]);
const graphRunJson = await zeroJson(["graph", "run", "--json", "--target", graphBuild.target, "--profile", graphBuild.profile, graphPath]);
assert.equal(sourceRunJson.ok, false);
assert.equal(graphRunJson.ok, false);
assert.equal(sourceRunJson.agentCommand.kind, "agent-run-json-unsupported-contract");
assert.equal(graphRunJson.agentCommand.kind, "agent-run-json-unsupported-contract");
assert.equal(sourceRunJson.agentCommand.stdoutPolicy, "reserved-for-program-output");
assert.equal(graphRunJson.agentCommand.stdoutPolicy, "reserved-for-program-output");
assert.equal(sourceRunJson.agentCommand.recommendedNextCommands[0].purpose, "artifact-validate");
assert.equal(graphRunJson.agentCommand.recommendedNextCommands[0].purpose, "artifact-validate");
assert.equal(sourceRunJson.agentCommand.recommendedNextCommands[0].required, true);
assert.equal(graphRunJson.agentCommand.recommendedNextCommands[0].required, true);
assert.deepEqual(sourceRunJson.agentCommand.recommendedNextCommands[0].argv, ["zero", "build", "--json", "--emit", "exe", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(graphRunJson.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "build", "--json", "--emit", "exe", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.equal(sourceRunJson.agentCommand.recommendedNextCommands[1].purpose, "test-run");
assert.equal(graphRunJson.agentCommand.recommendedNextCommands[1].purpose, "test-run");
assert.deepEqual(sourceRunJson.agentCommand.recommendedNextCommands[1].argv, ["zero", "test", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(graphRunJson.agentCommand.recommendedNextCommands[1].argv, ["zero", "graph", "test", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(sourceRunJson.agentCommand.verificationCommands[0].argv, ["zero", "build", "--json", "--emit", "exe", "--target", build.target, "--profile", build.profile, sourcePath]);
assert.deepEqual(graphRunJson.agentCommand.verificationCommands[0].argv, ["zero", "graph", "build", "--json", "--emit", "exe", "--target", graphBuild.target, "--profile", graphBuild.profile, graphPath]);
const buildabilityFailure = await zeroJson(["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", "examples/direct-call-add.0", "--out", join(outDir, "direct-call-add-blocked")]);
assert.equal(buildabilityFailure.ok, false);
assert.equal(buildabilityFailure.agentCommand.kind, "agent-build-failure-command-contract");
assert.equal(buildabilityFailure.agentCommand.failure.class, "target-buildability");
assert.equal(buildabilityFailure.diagnostics[0].code, "BLD004");
assert.equal(buildabilityFailure.agentCommand.recommendedNextCommands[0].purpose, "source-check");
assert.equal(buildabilityFailure.agentCommand.recommendedNextCommands[0].required, true);
assert.equal(buildabilityFailure.agentCommand.recommendedNextCommands[1].purpose, "target-selection");
assert.equal(buildabilityFailure.agentCommand.recommendedNextCommands[2].purpose, "doctor");
assert.equal(buildabilityFailure.agentCommand.recommendedNextCommands[3].purpose, "graph-inspect");
assert(buildabilityFailure.agentCommand.recommendedNextCommands[0].argv.includes("--emit"));
assert(buildabilityFailure.agentCommand.recommendedNextCommands[0].argv.includes("linux-musl-x64"));
assert(buildabilityFailure.agentCommand.recommendedNextCommands[0].resultFields.includes("targetReadiness"));
assert(buildabilityFailure.agentCommand.recommendedNextCommands[1].resultFields.includes("targets[].directBackend"));
assert(buildabilityFailure.agentCommand.recommendedNextCommands[3].resultFields.includes("programGraph.nodes[].nodeHash"));
const blockedGraphPath = join(outDir, "direct-call-add.program-graph");
await zeroJson(["graph", "dump", "--json", "--out", blockedGraphPath, "examples/direct-call-add.0"]);
const graphBuildabilityFailure = await zeroJson(["graph", "build", "--json", "--emit", "exe", "--target", "linux-musl-x64", blockedGraphPath, "--out", join(outDir, "direct-call-add-graph-blocked")]);
assert.equal(graphBuildabilityFailure.ok, false);
assert.equal(graphBuildabilityFailure.agentCommand.kind, "agent-graph-build-failure-command-contract");
assert.deepEqual(graphBuildabilityFailure.agentCommand.command.argv, ["zero", "graph", "build", "--json", "--emit", "exe", "--target", "linux-musl-x64", "--profile", "release", blockedGraphPath]);
assert.equal(graphBuildabilityFailure.agentCommand.recommendedNextCommands[0].purpose, "graph-check");
assert.equal(graphBuildabilityFailure.agentCommand.recommendedNextCommands[0].required, true);
assert.deepEqual(graphBuildabilityFailure.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "check", "--json", "--target", "linux-musl-x64", "--profile", "release", blockedGraphPath]);
assert(graphBuildabilityFailure.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.graphHash"));
assert.equal(graphBuildabilityFailure.agentCommand.recommendedNextCommands[1].purpose, "target-selection");
assert.equal(graphBuildabilityFailure.agentCommand.recommendedNextCommands[2].purpose, "doctor");
assert.equal(graphBuildabilityFailure.agentCommand.recommendedNextCommands[3].purpose, "graph-inspect");
assert.deepEqual(graphBuildabilityFailure.agentCommand.verificationCommands[0].argv, graphBuildabilityFailure.agentCommand.recommendedNextCommands[0].argv);
const graphArtifactCommandMismatches = await Promise.all([
  zeroJson(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "--profile", "dev", "--out", join(outDir, "mismatch-build"), graphPath]),
  zeroJson(["size", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", join(outDir, "mismatch-size"), graphPath]),
  zeroJson(["ship", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", join(outDir, "mismatch-ship"), graphPath]),
  zeroJson(["test", "--json", "--target", "linux-musl-x64", "--profile", "dev", graphPath]),
]);
for (const [index, mismatch] of graphArtifactCommandMismatches.entries()) {
  const command = ["build", "size", "ship", "test"][index];
  assert.equal(mismatch.ok, false);
  assert.equal(mismatch.agentCommand.kind, "agent-command-failure-contract");
  assert.equal(mismatch.agentCommand.failure.class, "graph-artifact-command-mismatch");
  assert.equal(mismatch.agentCommand.failure.retryCommands[0].purpose, "use-graph-artifact-command");
  assert.equal(mismatch.agentCommand.failure.retryCommands[0].required, true);
  assert.deepEqual(mismatch.agentCommand.recommendedNextCommands[0].argv, mismatch.agentCommand.failure.retryCommands[0].argv);
  assert.deepEqual(mismatch.agentCommand.failure.retryCommands[0].argv.slice(0, 4), ["zero", "graph", command, "--json"]);
  assert(mismatch.agentCommand.failure.retryCommands[0].argv.includes("--target"));
  assert(mismatch.agentCommand.failure.retryCommands[0].argv.includes("linux-musl-x64"));
  assert(mismatch.agentCommand.failure.retryCommands[0].argv.includes("--profile"));
  assert(mismatch.agentCommand.failure.retryCommands[0].argv.includes("dev"));
  assert.equal(mismatch.agentCommand.failure.retryCommands[0].argv.at(-1), graphPath);
  assert(mismatch.agentCommand.failure.retryCommands[0].resultFields.includes("agentCommand.sourceKind"));
}
const commandAudit = {
  replayable: JSON.stringify(artifactProof.argv) === JSON.stringify(build.agentCommand.command.argv) &&
    JSON.stringify(graphArtifactProof.argv) === JSON.stringify(graphBuild.agentCommand.command.argv),
  artifactEvidenceMatched: JSON.stringify(artifactProof.artifactHash) === JSON.stringify(build.artifactHash) &&
    artifactProof.artifactBytes === build.artifactBytes &&
    JSON.stringify(graphArtifactProof.artifactHash) === JSON.stringify(graphBuild.artifactHash) &&
    graphArtifactProof.artifactBytes === graphBuild.artifactBytes,
  artifactWritePolicyAuditable: artifactWritePolicyStructured(build) && artifactWritePolicyStructured(graphBuild),
  artifactRollbackActionAuditable: build.agentCommand.writePolicy.rollbackActions[0].pathField === build.agentCommand.writePolicy.rollbackField &&
    graphBuild.agentCommand.writePolicy.rollbackActions[0].pathField === graphBuild.agentCommand.writePolicy.rollbackField,
  buildTestFollowupStructured: build.agentCommand.recommendedNextCommands[0].purpose === "test-run" &&
    build.agentCommand.recommendedNextCommands[0].required === false &&
    build.agentCommand.recommendedNextCommands[0].inputField === "command.argv input" &&
    build.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    build.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    build.agentCommand.recommendedNextCommands[0].resultFields.includes("passedTests") &&
    build.agentCommand.recommendedNextCommands[0].resultFields.includes("failedTests") &&
    build.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"),
  graphBuildSizeFollowupStructured: graphBuild.agentCommand.recommendedNextCommands[0].purpose === "graph-size" &&
    graphBuild.agentCommand.recommendedNextCommands[0].inputField === "command.argv input" &&
    graphBuild.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    graphBuild.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    graphBuild.agentCommand.recommendedNextCommands[0].resultFields.includes("profileBudget") &&
    graphBuild.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"),
  graphBuildTestFollowupStructured: graphBuild.agentCommand.recommendedNextCommands[1].purpose === "graph-test" &&
    graphBuild.agentCommand.recommendedNextCommands[1].required === false &&
    graphBuild.agentCommand.recommendedNextCommands[1].inputField === "command.argv input" &&
    graphBuild.agentCommand.recommendedNextCommands[1].argv.includes("<same-target>") &&
    graphBuild.agentCommand.recommendedNextCommands[1].argv.includes("<same-profile>") &&
    graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("passedTests") &&
    graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("failedTests") &&
    graphBuild.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"),
  runJsonUnsupportedFollowupStructured: [sourceRunJson, graphRunJson].every((report) =>
    report.agentCommand?.kind === "agent-run-json-unsupported-contract" &&
    report.agentCommand?.stdoutPolicy === "reserved-for-program-output" &&
    report.agentCommand?.recommendedNextCommands?.[0]?.purpose === "artifact-validate" &&
    report.agentCommand?.recommendedNextCommands?.[0]?.required === true &&
    report.agentCommand?.recommendedNextCommands?.[0]?.argv?.includes("<same-target>") &&
    report.agentCommand?.recommendedNextCommands?.[0]?.argv?.includes("<same-profile>") &&
    report.agentCommand?.recommendedNextCommands?.[0]?.resultFields?.includes("artifactHash") &&
    report.agentCommand?.recommendedNextCommands?.[0]?.resultFields?.includes("agentCommand.verificationCommands") &&
    report.agentCommand?.recommendedNextCommands?.[1]?.purpose === "test-run" &&
    report.agentCommand?.recommendedNextCommands?.[1]?.argv?.includes("<same-target>") &&
    report.agentCommand?.recommendedNextCommands?.[1]?.argv?.includes("<same-profile>") &&
    report.agentCommand?.verificationCommands?.[0]?.purpose === "artifact-validate"),
  buildFailureRecoveryStructured: buildabilityFailure.agentCommand?.kind === "agent-build-failure-command-contract" &&
    buildabilityFailure.agentCommand?.failure?.class === "target-buildability" &&
    buildabilityFailure.agentCommand?.recommendedNextCommands?.some((command) => command.purpose === "source-check" && command.required === true && command.resultFields.includes("targetReadiness")) &&
    buildabilityFailure.agentCommand?.recommendedNextCommands?.some((command) => command.purpose === "target-selection" && command.resultFields.includes("targets[].directBackend")) &&
    buildabilityFailure.agentCommand?.recommendedNextCommands?.some((command) => command.purpose === "doctor" && command.resultFields.includes("targetToolchains[]")) &&
    buildabilityFailure.agentCommand?.recommendedNextCommands?.some((command) => command.purpose === "graph-inspect" && command.resultFields.includes("programGraph.nodes[].nodeHash")) &&
    buildabilityFailure.diagnostics?.[0]?.backendBlocker?.stage === "buildability",
  graphBuildFailureRecoveryStructured: graphBuildabilityFailure.agentCommand?.kind === "agent-graph-build-failure-command-contract" &&
    graphBuildabilityFailure.agentCommand?.command?.argv?.[1] === "graph" &&
    graphBuildabilityFailure.agentCommand?.recommendedNextCommands?.some((command) => command.purpose === "graph-check" && command.required === true && command.resultFields.includes("programGraph.graphHash")) &&
    graphBuildabilityFailure.agentCommand?.recommendedNextCommands?.some((command) => command.purpose === "target-selection" && command.resultFields.includes("targets[].directBackend")) &&
    graphBuildabilityFailure.agentCommand?.recommendedNextCommands?.some((command) => command.purpose === "doctor" && command.resultFields.includes("targetToolchains[]")) &&
    graphBuildabilityFailure.agentCommand?.recommendedNextCommands?.some((command) => command.purpose === "graph-inspect" && command.resultFields.includes("programGraph.nodes[].nodeHash")) &&
    graphBuildabilityFailure.agentCommand?.verificationCommands?.[0]?.purpose === "graph-check",
  graphArtifactCommandRerouteStructured: graphArtifactCommandMismatches.every((report) =>
    report.agentCommand?.failure?.class === "graph-artifact-command-mismatch" &&
    report.agentCommand?.failure?.retryCommands?.[0]?.purpose === "use-graph-artifact-command" &&
    report.agentCommand?.failure?.retryCommands?.[0]?.required === true &&
    report.agentCommand?.failure?.retryCommands?.[0]?.argv?.[1] === "graph" &&
    report.agentCommand?.failure?.retryCommands?.[0]?.argv?.includes("--target") &&
    report.agentCommand?.failure?.retryCommands?.[0]?.argv?.includes("--profile") &&
    report.agentCommand?.failure?.retryCommands?.[0]?.argv?.at(-1) === graphPath &&
    JSON.stringify(report.agentCommand?.recommendedNextCommands?.[0]?.argv) === JSON.stringify(report.agentCommand?.failure?.retryCommands?.[0]?.argv) &&
    report.agentCommand?.failure?.retryCommands?.[0]?.resultFields?.includes("agentCommand.sourceKind")),
  targetProfilePreserved: graphBuild.agentCommand.command.argv.includes("--target") &&
    graphBuild.agentCommand.command.argv.includes("linux-musl-x64") &&
    graphBuild.agentCommand.command.argv.includes("--profile") &&
    graphBuild.agentCommand.command.argv.includes("dev") &&
    JSON.stringify(graphArtifactProof.argv) === JSON.stringify(graphBuild.agentCommand.command.argv),
};
assert.equal(commandAudit.replayable, true);
assert.equal(commandAudit.artifactEvidenceMatched, true);
assert.equal(commandAudit.artifactWritePolicyAuditable, true);
assert.equal(commandAudit.artifactRollbackActionAuditable, true);
assert.equal(commandAudit.buildTestFollowupStructured, true);
assert.equal(commandAudit.graphBuildSizeFollowupStructured, true);
assert.equal(commandAudit.graphBuildTestFollowupStructured, true);
assert.equal(commandAudit.runJsonUnsupportedFollowupStructured, true);
assert.equal(commandAudit.buildFailureRecoveryStructured, true);
assert.equal(commandAudit.graphBuildFailureRecoveryStructured, true);
assert.equal(commandAudit.graphArtifactCommandRerouteStructured, true);
assert.equal(commandAudit.targetProfilePreserved, true);
const safetyAudit = {
  sourceSafetyStructured: safetyStructured(build),
  graphSafetyStructured: safetyStructured(graphBuild),
  productionGateStructured: build.productionReadiness?.gate === "sensitive-production-v1" &&
    graphBuild.productionReadiness?.gate === "sensitive-production-v1",
  blockingRisksAuditable: build.productionReadiness?.blockingRisks?.some((risk) => risk.id === "runtime-integer-overflow-unchecked") &&
    build.productionReadiness?.blockingRisks?.some((risk) => risk.id === "host-effects-not-sandboxed") &&
    graphBuild.productionReadiness?.blockingRisks?.some((risk) => risk.id === "runtime-integer-overflow-unchecked"),
  uncheckedSurfacesAuditable: build.safetyFacts?.uncheckedSurfaces?.some((surface) => surface.surface === "C imports") &&
    build.safetyFacts?.uncheckedSurfaces?.some((surface) => surface.surface === "host filesystem/process/network effects") &&
    graphBuild.safetyFacts?.uncheckedSurfaces?.some((surface) => surface.surface === "C imports"),
};
assert.equal(safetyAudit.sourceSafetyStructured, true);
assert.equal(safetyAudit.graphSafetyStructured, true);
assert.equal(safetyAudit.productionGateStructured, true);
assert.equal(safetyAudit.blockingRisksAuditable, true);
assert.equal(safetyAudit.uncheckedSurfacesAuditable, true);
const cacheAudit = {
  source: cacheStructured(build),
  graph: cacheStructured(graphBuild, { sourceKind: "program-graph", graphHash: graphDump.graphHash }),
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
assert.equal(graphProofAudit.required, 3);
assert.deepEqual(graphProofAudit.passedPurposes, ["artifact-validate", "graph-check", "graph-size"]);
console.log(JSON.stringify({ ok: true, kind: "agent-build-command-smoke", artifactPath: build.artifactPath, artifactBytes: build.artifactBytes, artifactHash: build.artifactHash, repeatArtifactHash: repeatBuild.artifactHash, commandAudit, safetyAudit, cacheAudit, proofAudit, graph: { artifactPath: graphBuild.artifactPath, artifactBytes: graphBuild.artifactBytes, artifactHash: graphBuild.artifactHash, repeatArtifactHash: graphRepeatBuild.artifactHash, proofAudit: graphProofAudit } }, null, 2));
console.log("agent build command smoke ok");
