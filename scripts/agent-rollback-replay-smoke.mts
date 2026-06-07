import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const outDir = join(".zero", "agent-rollback-replay-smoke", String(process.pid));

async function zeroJson(args: string[], options: { allowFailure?: boolean } = {}) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  if (!options.allowFailure && result.code) throw result;
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[]; resultFields?: string[] }>) {
  const proofs = [];
  for (const command of commands) {
    assert.equal(command.argv[0], "zero");
    if (!command.required) continue;
    const body = await zeroJson(command.argv.slice(1), { allowFailure: true });
    proofs.push({
      purpose: command.purpose,
      ok: body.ok ?? true,
      graphHash: body.graphHash ?? body.programGraph?.graphHash ?? null,
      diagnostics: body.diagnostics ?? [],
      declaredFields: command.resultFields ?? [],
      observedTopLevelFields: Object.keys(body).sort(),
    });
  }
  return proofs;
}

await rm(outDir, { recursive: true, force: true });
await mkdir(outDir, { recursive: true });

const sourcePath = join(outDir, "rollback.0");
const originalSource = `fn helper(value: i32) -> i32 {
    return value + 1
}

pub fn main(world: World) -> Void raises {
    if helper(41) == 42 {
        check world.out.write("ok\\n")
    }
}
`;

await writeFile(sourcePath, originalSource);

const find = await zeroJson(["graph", "find", "--json", "--target", "win32-x64.exe", "--profile", "release", "--symbol", "helper", sourcePath]);
assert.equal(find.ok, true);
const helperFunction = find.matches.find((node) => node.kind === "Function" && node.name === "helper");
assert(helperFunction, "graph find should expose helper function before patching");
const originalGraphHash = find.graphHash;
const originalGraphCheck = await zeroJson(["graph", "check", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath]);
assert.equal(originalGraphCheck.ok, true);
const originalCheckGraphHash = originalGraphCheck.graphHash;

const patch = await zeroJson([
  "graph",
  "patch",
  "--json",
  "--target",
  "win32-x64.exe",
  "--profile",
  "release",
  sourcePath,
  "--expect-graph-hash",
  originalGraphHash,
  "--op",
  `renameSymbol node="${helperFunction.id}" expect="helper" value="compute"`,
]);
assert.equal(patch.ok, true);
assert.equal(patch.agentTransaction.rollback.savedKind, "source");
assert.equal(patch.agentTransaction.proofLedger.kind, "graph-patch-proof-ledger");
assert(patch.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[]"));
assert(patch.agentTransaction.resultFields.includes("agentTransaction.rollback.verificationCommands[].resultFields"));

const changedSource = await readFile(sourcePath, "utf8");
assert.match(changedSource, /fn compute\(value: i32\)/);
assert.match(changedSource, /compute\(41\)/);
assert.notEqual(patch.patchedGraphHash, originalGraphHash);

const rollbackAction = patch.agentTransaction.rollback.actions[0];
assert.equal(rollbackAction.kind, "restore-path-from-vcs");
assert.equal(rollbackAction.savedKind, "source");
assert.equal(rollbackAction.pathField, "agentTransaction.rollback.savedPath");
assert.equal(rollbackAction.requiresVerification, true);
assert.equal(patch.agentTransaction.rollback.savedPath, sourcePath);
assert.deepEqual(
  patch.agentTransaction.rollback.verificationCommands.map((command) => command.purpose).sort(),
  ["rollback-graph-check", "rollback-source-check"],
);

await writeFile(sourcePath, originalSource);
const restoredSource = await readFile(sourcePath, "utf8");
assert.equal(restoredSource, originalSource);

const rollbackProofs = await runRequiredCommands(patch.agentTransaction.rollback.verificationCommands);
const rollbackGraphProof = rollbackProofs.find((proof) => proof.purpose === "rollback-graph-check");
const rollbackSourceProof = rollbackProofs.find((proof) => proof.purpose === "rollback-source-check");
assert(rollbackGraphProof);
assert(rollbackSourceProof);

const inspectAfterRollback = await zeroJson(["graph", "inspect", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath]);
assert.equal(inspectAfterRollback.programGraph.graphHash, originalGraphHash);
assert(inspectAfterRollback.programGraph.nodes.some((node) => node.kind === "Function" && node.name === "helper"));
assert(!inspectAfterRollback.programGraph.nodes.some((node) => node.kind === "Function" && node.name === "compute"));

const proofPurposes = rollbackProofs.map((proof) => proof.purpose);
const audit = {
  rollbackActionStructured: rollbackAction.kind === "restore-path-from-vcs" &&
    rollbackAction.pathField === "agentTransaction.rollback.savedPath" &&
    rollbackAction.requiresVerification === true,
  originalStateCaptured: patch.agentTransaction.rollback.originalGraphHash === originalGraphHash &&
    patch.agentTransaction.rollback.savedPath === sourcePath,
  sourceMutationObserved: /fn compute\(value: i32\)/.test(changedSource) && /compute\(41\)/.test(changedSource),
  restoreApplied: restoredSource === originalSource,
  rollbackVerificationCommandsReplayed: proofPurposes.length === 2 &&
    proofPurposes.includes("rollback-source-check") &&
    proofPurposes.includes("rollback-graph-check"),
  sourceCheckRestored: rollbackSourceProof.ok === true && rollbackSourceProof.diagnostics.length === 0,
  graphCheckHashRestored: rollbackGraphProof.graphHash === originalCheckGraphHash,
  semanticGraphHashRestored: inspectAfterRollback.programGraph.graphHash === originalGraphHash,
  semanticIdentityRestored: inspectAfterRollback.programGraph.nodes.some((node) => node.kind === "Function" && node.name === "helper") &&
    !inspectAfterRollback.programGraph.nodes.some((node) => node.kind === "Function" && node.name === "compute"),
};
for (const [name, passed] of Object.entries(audit)) {
  assert.equal(passed, true, `rollback audit failed: ${name}`);
}

const report = {
  schemaVersion: 1,
  kind: "agent-rollback-replay-smoke",
  ok: true,
  sourceFile: sourcePath,
  stages: ["graph-find", "graph-patch", "source-restore", "rollback-source-check", "rollback-graph-check", "graph-inspect"],
  graph: {
    originalGraphHash,
    originalCheckGraphHash,
    patchedGraphHash: patch.patchedGraphHash,
    restoredGraphHash: inspectAfterRollback.programGraph.graphHash,
    restoredCheckGraphHash: rollbackGraphProof.graphHash,
    targetNode: helperFunction.id,
    nodeHash: helperFunction.nodeHash,
  },
  audit,
  proofAudit: {
    required: proofPurposes.length,
    purposes: proofPurposes,
    passedPurposes: proofPurposes,
  },
};

console.log(JSON.stringify(report, null, 2));
