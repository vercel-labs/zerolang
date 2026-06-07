import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync, statSync } from "node:fs";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? ".zero/bin/zero";
const outDir = join(".zero", "agent-ship-command-smoke", String(process.pid));
const artifactPath = join(outDir, "hello-release");
const graphPath = join(outDir, "hello.program-graph");
const graphArtifactPath = join(outDir, "hello-graph-release");
const fixture = "examples/hello.0";
const proofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean; checksum?: unknown; artifactBytes?: number; artifactKinds?: string[] }> = [];
const graphProofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean; checksum?: unknown; artifactBytes?: number; artifactKinds?: string[] }> = [];

mkdirSync(outDir, { recursive: true });
rmSync(graphPath, { recursive: true, force: true });
for (const base of [artifactPath, graphArtifactPath]) {
  for (const suffix of ["", ".stripped", ".checksum", ".zeroar", ".debug.json", ".size.json", ".sbom.json"]) {
    rmSync(`${base}${suffix}`, { recursive: true, force: true });
  }
}

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
    targetProofs.push({
      purpose: command.purpose,
      required: command.required,
      argv: command.argv,
      ok: body.ok ?? true,
      checksum: body.checksum,
      artifactBytes: body.artifactBytes,
      artifactKinds: Array.isArray(body.artifacts) ? body.artifacts.map((artifact) => artifact.kind).sort() : undefined,
    });
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

function assertArtifactExists(path: string) {
  assert.equal(existsSync(path) || existsSync(path.replace(/\\/g, "/")), true, `${path} should exist`);
}

const ship = await zeroJson(["ship", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", artifactPath, fixture]);
const repeat = await zeroJson(["ship", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", artifactPath, fixture]);

assert.equal(ship.schemaVersion, 1);
assert.equal(ship.ok, true);
assert.equal(ship.command, "ship");
assert.equal(ship.agentCommand.kind, "agent-ship-command-contract");
assert.equal(ship.agentCommand.sourceKind, "source");
assert.equal(ship.agentCommand.artifact.pathField, "artifactPath");
assert.equal(ship.agentCommand.artifact.bytesField, "artifactBytes");
assert.equal(ship.agentCommand.artifact.checksumField, "checksum");
assert.deepEqual(ship.agentCommand.artifact.verificationFields, ["checksum", "artifactBytes", "artifacts"]);
assert.equal(artifactWritePolicyStructured(ship), true);
assert(ship.agentCommand.auditFields.includes("releasePreview"));
assert(ship.agentCommand.auditFields.includes("artifacts"));
assert(ship.agentCommand.auditFields.includes("releaseTargetContract"));
assert.equal(ship.releasePreview.deterministic, true);
assert.equal(ship.releaseTargetContract.determinism.repeatBuildHash, "checked-by-command-contracts");
assert.equal(ship.generatedCBytes, 0);
assert.equal(ship.cBridgeFallback, false);
assert.equal(ship.artifactBytes > 0, true);
assert.equal(ship.checksum.algorithm, "fnv1a64");
assert.match(ship.checksum.value, /^[0-9a-f]{16}$/);
assert.equal(repeat.checksum.value, ship.checksum.value);
assert.equal(repeat.artifactBytes, ship.artifactBytes);
assertArtifactExists(ship.artifactPath);
assertArtifactExists(ship.checksum.path);

const artifactKinds = new Set(ship.artifacts.map((artifact) => artifact.kind));
for (const kind of ["binary", "stripped-binary", "checksum", "archive", "debug-symbol-metadata", "size-report", "sbom-placeholder"]) {
  assert(artifactKinds.has(kind), `ship report should include ${kind}`);
}
for (const artifact of ship.artifacts) {
  assertArtifactExists(artifact.path);
  assert.equal(statSync(artifact.path).size, artifact.bytes);
}

const checksumText = readFileSync(ship.checksum.path, "utf8");
assert(checksumText.includes(ship.checksum.value));
assert(checksumText.includes(ship.artifactPath));
assert.equal(JSON.parse(readFileSync(ship.releasePreview.sizeReport, "utf8")).artifactBytes, ship.artifactBytes);
assert.equal(JSON.parse(readFileSync(ship.releasePreview.debugSymbols, "utf8")).kind, "zero-debug-symbol-metadata");
assert.equal(JSON.parse(readFileSync(ship.releasePreview.sbom, "utf8")).kind, "zero-sbom-placeholder");
assert.match(readFileSync(ship.releasePreview.archive, "utf8"), /zero archive manifest v1/);
assert.deepEqual(ship.agentCommand.verificationCommands.map((command) => command.purpose), ["source-check", "size-analysis", "artifact-validate"]);
assert(ship.agentCommand.verificationCommands.find((command) => command.purpose === "size-analysis")?.resultFields.includes("sizeBreakdown"));
assert(ship.agentCommand.verificationCommands.find((command) => command.purpose === "artifact-validate")?.resultFields.includes("checksum"));
assert.equal(ship.agentCommand.recommendedNextCommands[0].purpose, "size-analysis");
assert.equal(ship.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(ship.agentCommand.recommendedNextCommands[0].when, "after release preview or artifact audit");
assert.equal(ship.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(ship.agentCommand.recommendedNextCommands[0].argv, ["zero", "size", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert(ship.agentCommand.recommendedNextCommands[0].resultFields.includes("sizeBreakdown"));
assert(ship.agentCommand.recommendedNextCommands[0].resultFields.includes("profileBudget"));
assert(ship.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.equal(ship.agentCommand.recommendedNextCommands[1].purpose, "test-run");
assert.equal(ship.agentCommand.recommendedNextCommands[1].required, false);
assert.equal(ship.agentCommand.recommendedNextCommands[1].when, "after release preview when behavioral confidence is required");
assert.equal(ship.agentCommand.recommendedNextCommands[1].inputField, "command.argv input");
assert.deepEqual(ship.agentCommand.recommendedNextCommands[1].argv, ["zero", "test", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert(ship.agentCommand.recommendedNextCommands[1].resultFields.includes("ok"));
assert(ship.agentCommand.recommendedNextCommands[1].resultFields.includes("passedTests"));
assert(ship.agentCommand.recommendedNextCommands[1].resultFields.includes("failedTests"));
assert(ship.agentCommand.recommendedNextCommands[1].resultFields.includes("unexpectedPasses"));
assert(ship.agentCommand.recommendedNextCommands[1].resultFields.includes("results[]"));
assert(ship.agentCommand.recommendedNextCommands[1].resultFields.includes("results[].status"));
assert(ship.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"));
await runRequiredCommands(ship.agentCommand.verificationCommands);
const artifactProof = proofs.find((item) => item.purpose === "artifact-validate");
assert(artifactProof, "ship proof should include artifact-validate");
assert.deepEqual(artifactProof.argv, ship.agentCommand.command.argv);
assert.deepEqual(artifactProof.checksum, ship.checksum);
assert.equal(artifactProof.artifactBytes, ship.artifactBytes);
assert.deepEqual(artifactProof.artifactKinds, [...artifactKinds].sort());

const proofAudit = {
  required: proofs.filter((item) => item.required).length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 3);
assert.deepEqual(proofAudit.passedPurposes, ["artifact-validate", "size-analysis", "source-check"]);

const graphDump = await zeroJson(["graph", "dump", "--json", "--out", graphPath, fixture]);
assert.equal(existsSync(graphPath), true);
const graphShip = await zeroJson(["graph", "ship", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", graphArtifactPath, graphPath]);
const graphRepeat = await zeroJson(["graph", "ship", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", graphArtifactPath, graphPath]);
assert.equal(graphShip.schemaVersion, 1);
assert.equal(graphShip.ok, true);
assert.equal(graphShip.command, "ship");
assert.equal(graphShip.agentCommand.kind, "agent-ship-command-contract");
assert.equal(graphShip.agentCommand.sourceKind, "program-graph");
assert.equal(graphShip.graph.artifact, graphPath);
assert.equal(graphShip.graph.graphHash, graphDump.graphHash);
assert.equal(graphShip.agentCommand.artifact.pathField, "artifactPath");
assert.equal(graphShip.agentCommand.artifact.bytesField, "artifactBytes");
assert.equal(graphShip.agentCommand.artifact.checksumField, "checksum");
assert.deepEqual(graphShip.agentCommand.artifact.verificationFields, ["checksum", "artifactBytes", "artifacts"]);
assert.equal(artifactWritePolicyStructured(graphShip), true);
assert.equal(graphShip.checksum.algorithm, "fnv1a64");
assert.match(graphShip.checksum.value, /^[0-9a-f]{16}$/);
assert.equal(graphRepeat.checksum.value, graphShip.checksum.value);
assert.equal(graphRepeat.artifactBytes, graphShip.artifactBytes);
assert.deepEqual(graphShip.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-check", "graph-size", "artifact-validate"]);
assert(graphShip.agentCommand.verificationCommands.find((command) => command.purpose === "graph-size")?.resultFields.includes("sizeBreakdown"));
assert(graphShip.agentCommand.verificationCommands.find((command) => command.purpose === "artifact-validate")?.resultFields.includes("checksum"));
assert.equal(graphShip.agentCommand.recommendedNextCommands[0].purpose, "graph-size");
assert.equal(graphShip.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(graphShip.agentCommand.recommendedNextCommands[0].when, "after release preview or artifact audit");
assert.equal(graphShip.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(graphShip.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "size", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<program-graph>"]);
assert(graphShip.agentCommand.recommendedNextCommands[0].resultFields.includes("sizeBreakdown"));
assert(graphShip.agentCommand.recommendedNextCommands[0].resultFields.includes("profileBudget"));
assert(graphShip.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.equal(graphShip.agentCommand.recommendedNextCommands[1].purpose, "graph-test");
assert.equal(graphShip.agentCommand.recommendedNextCommands[1].required, false);
assert.equal(graphShip.agentCommand.recommendedNextCommands[1].when, "after release preview when graph-backed behavioral confidence is required");
assert.equal(graphShip.agentCommand.recommendedNextCommands[1].inputField, "command.argv input");
assert.deepEqual(graphShip.agentCommand.recommendedNextCommands[1].argv, ["zero", "graph", "test", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<program-graph>"]);
assert(graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("ok"));
assert(graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("passedTests"));
assert(graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("failedTests"));
assert(graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("unexpectedPasses"));
assert(graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("results[]"));
assert(graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("results[].status"));
assert(graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"));
const graphArtifactKinds = new Set(graphShip.artifacts.map((artifact) => artifact.kind));
for (const kind of ["binary", "stripped-binary", "checksum", "archive", "debug-symbol-metadata", "size-report", "sbom-placeholder"]) {
  assert(graphArtifactKinds.has(kind), `graph ship report should include ${kind}`);
}
await runRequiredCommands(graphShip.agentCommand.verificationCommands, graphProofs);
const graphArtifactProof = graphProofs.find((item) => item.purpose === "artifact-validate");
assert(graphArtifactProof, "graph ship proof should include artifact-validate");
assert.deepEqual(graphArtifactProof.argv, graphShip.agentCommand.command.argv);
assert.deepEqual(graphArtifactProof.checksum, graphShip.checksum);
assert.equal(graphArtifactProof.artifactBytes, graphShip.artifactBytes);
assert.deepEqual(graphArtifactProof.artifactKinds, [...graphArtifactKinds].sort());
const commandAudit = {
  replayable: JSON.stringify(artifactProof.argv) === JSON.stringify(ship.agentCommand.command.argv) &&
    JSON.stringify(graphArtifactProof.argv) === JSON.stringify(graphShip.agentCommand.command.argv),
  artifactEvidenceMatched: JSON.stringify(artifactProof.checksum) === JSON.stringify(ship.checksum) &&
    artifactProof.artifactBytes === ship.artifactBytes &&
    JSON.stringify(artifactProof.artifactKinds) === JSON.stringify([...artifactKinds].sort()) &&
    JSON.stringify(graphArtifactProof.checksum) === JSON.stringify(graphShip.checksum) &&
    graphArtifactProof.artifactBytes === graphShip.artifactBytes &&
    JSON.stringify(graphArtifactProof.artifactKinds) === JSON.stringify([...graphArtifactKinds].sort()),
  artifactWritePolicyAuditable: artifactWritePolicyStructured(ship) && artifactWritePolicyStructured(graphShip),
  artifactRollbackActionAuditable: ship.agentCommand.writePolicy.rollbackActions[0].pathField === ship.agentCommand.writePolicy.rollbackField &&
    graphShip.agentCommand.writePolicy.rollbackActions[0].pathField === graphShip.agentCommand.writePolicy.rollbackField,
  graphShipSizeFollowupStructured: graphShip.agentCommand.recommendedNextCommands[0].purpose === "graph-size" &&
    graphShip.agentCommand.recommendedNextCommands[0].inputField === "command.argv input" &&
    graphShip.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    graphShip.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    graphShip.agentCommand.recommendedNextCommands[0].resultFields.includes("profileBudget") &&
    graphShip.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"),
  graphShipTestFollowupStructured: graphShip.agentCommand.recommendedNextCommands[1].purpose === "graph-test" &&
    graphShip.agentCommand.recommendedNextCommands[1].required === false &&
    graphShip.agentCommand.recommendedNextCommands[1].inputField === "command.argv input" &&
    graphShip.agentCommand.recommendedNextCommands[1].argv.includes("<same-target>") &&
    graphShip.agentCommand.recommendedNextCommands[1].argv.includes("<same-profile>") &&
    graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("passedTests") &&
    graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("failedTests") &&
    graphShip.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"),
  sourceShipSizeFollowupStructured: ship.agentCommand.recommendedNextCommands[0].purpose === "size-analysis" &&
    ship.agentCommand.recommendedNextCommands[0].inputField === "command.argv input" &&
    ship.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    ship.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    ship.agentCommand.recommendedNextCommands[0].resultFields.includes("profileBudget") &&
    ship.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"),
  sourceShipTestFollowupStructured: ship.agentCommand.recommendedNextCommands[1].purpose === "test-run" &&
    ship.agentCommand.recommendedNextCommands[1].required === false &&
    ship.agentCommand.recommendedNextCommands[1].inputField === "command.argv input" &&
    ship.agentCommand.recommendedNextCommands[1].argv.includes("<same-target>") &&
    ship.agentCommand.recommendedNextCommands[1].argv.includes("<same-profile>") &&
    ship.agentCommand.recommendedNextCommands[1].resultFields.includes("passedTests") &&
    ship.agentCommand.recommendedNextCommands[1].resultFields.includes("failedTests") &&
    ship.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"),
  targetProfilePreserved: ship.agentCommand.command.argv.includes("--target") &&
    ship.agentCommand.command.argv.includes("linux-musl-x64") &&
    ship.agentCommand.command.argv.includes("--profile") &&
    ship.agentCommand.command.argv.includes("dev") &&
    graphShip.agentCommand.command.argv.includes("--target") &&
    graphShip.agentCommand.command.argv.includes("linux-musl-x64") &&
    graphShip.agentCommand.command.argv.includes("--profile") &&
    graphShip.agentCommand.command.argv.includes("dev") &&
    JSON.stringify(artifactProof.argv) === JSON.stringify(ship.agentCommand.command.argv) &&
    JSON.stringify(graphArtifactProof.argv) === JSON.stringify(graphShip.agentCommand.command.argv),
};
assert.equal(commandAudit.replayable, true);
assert.equal(commandAudit.artifactEvidenceMatched, true);
assert.equal(commandAudit.artifactWritePolicyAuditable, true);
assert.equal(commandAudit.artifactRollbackActionAuditable, true);
assert.equal(commandAudit.graphShipSizeFollowupStructured, true);
assert.equal(commandAudit.graphShipTestFollowupStructured, true);
assert.equal(commandAudit.sourceShipSizeFollowupStructured, true);
assert.equal(commandAudit.sourceShipTestFollowupStructured, true);
assert.equal(commandAudit.targetProfilePreserved, true);
const safetyAudit = {
  sourceSafetyStructured: safetyStructured(ship),
  graphSafetyStructured: safetyStructured(graphShip),
  productionGateStructured: ship.productionReadiness?.gate === "sensitive-production-v1" &&
    graphShip.productionReadiness?.gate === "sensitive-production-v1",
  blockingRisksAuditable: ship.productionReadiness?.blockingRisks?.some((risk) => risk.id === "runtime-integer-overflow-unchecked") &&
    ship.productionReadiness?.blockingRisks?.some((risk) => risk.id === "release-sbom-placeholder") &&
    graphShip.productionReadiness?.blockingRisks?.some((risk) => risk.id === "runtime-integer-overflow-unchecked"),
  uncheckedSurfacesAuditable: ship.safetyFacts?.uncheckedSurfaces?.some((surface) => surface.surface === "C imports") &&
    ship.safetyFacts?.uncheckedSurfaces?.some((surface) => surface.surface === "host filesystem/process/network effects") &&
    graphShip.safetyFacts?.uncheckedSurfaces?.some((surface) => surface.surface === "C imports"),
};
assert.equal(safetyAudit.sourceSafetyStructured, true);
assert.equal(safetyAudit.graphSafetyStructured, true);
assert.equal(safetyAudit.productionGateStructured, true);
assert.equal(safetyAudit.blockingRisksAuditable, true);
assert.equal(safetyAudit.uncheckedSurfacesAuditable, true);
const cacheAudit = {
  source: cacheStructured(ship),
  graph: cacheStructured(graphShip, { sourceKind: "program-graph", graphHash: graphDump.graphHash }),
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
console.log(JSON.stringify({
  ok: true,
  kind: "agent-ship-command-smoke",
  artifactPath: ship.artifactPath,
  artifactBytes: ship.artifactBytes,
  checksum: ship.checksum,
  repeatChecksum: repeat.checksum,
  artifactKinds: [...artifactKinds].sort(),
  commandAudit,
  safetyAudit,
  cacheAudit,
  proofAudit,
  graph: {
    artifactPath: graphShip.artifactPath,
    artifactBytes: graphShip.artifactBytes,
    checksum: graphShip.checksum,
    repeatChecksum: graphRepeat.checksum,
    artifactKinds: [...graphArtifactKinds].sort(),
    proofAudit: graphProofAudit,
  },
}, null, 2));
console.log("agent ship command smoke ok");
