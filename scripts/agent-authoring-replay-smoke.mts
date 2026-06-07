import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const outDir = join(".zero", "agent-authoring-replay-smoke", String(process.pid));

async function zeroJson(args: string[], options: { allowFailure?: boolean } = {}) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  if (!options.allowFailure && result.code) throw result;
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands) {
    assert.equal(command.argv[0], "zero");
    if (!command.required) continue;
    const body = await zeroJson(command.argv.slice(1), { allowFailure: true });
    proofs.push({
      purpose: command.purpose,
      ok: body.ok ?? true,
      declaredFields: command.resultFields ?? [],
      observedTopLevelFields: Object.keys(body).sort(),
    });
  }
  return proofs;
}

await rm(outDir, { recursive: true, force: true });
await mkdir(outDir, { recursive: true });

const sourcePath = join(outDir, "authoring.0");
await writeFile(sourcePath, `fn helper(value: i32) -> i32 {
    return value + 1
}

test "helper increments" {
    expect helper(41) == 42
}

pub fn main(world: World) -> Void raises {
    if helper(41) == 42 {
        check world.out.write("ok\\n")
    }
}
`);

const find = await zeroJson(["graph", "find", "--json", "--target", "win32-x64.exe", "--profile", "release", "--symbol", "helper", sourcePath]);
assert.equal(find.ok, true);
assert.equal(find.agentCommand.kind, "agent-graph-find-command-contract");
assert.equal(find.agentCommand.readPolicy.fullSourceRequired, false);
const helperFunction = find.matches.find((node) => node.kind === "Function" && node.name === "helper");
assert(helperFunction, "graph find should expose the helper function node");

const impact = await zeroJson(["graph", "impact", "--json", "--target", "win32-x64.exe", "--profile", "release", "--node", helperFunction.id, sourcePath]);
assert.equal(impact.ok, true);
assert.equal(impact.agentCommand.kind, "agent-graph-impact-command-contract");
assert(impact.editGuards.includes("renameSymbol-updates-references"));
assert.equal(impact.agentCommand.readPolicy.fullSourceRequired, false);

const slice = await zeroJson(["graph", "slice", "--json", "--target", "win32-x64.exe", "--profile", "release", "--node", helperFunction.id, sourcePath]);
assert.equal(slice.ok, true);
assert.equal(slice.agentCommand.kind, "agent-graph-slice-command-contract");
assert(slice.nodes.some((node) => node.id === helperFunction.id && node.nodeHash));
assert.equal(slice.tokenStrategy.readFullSourceRequired, false);

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
  find.graphHash,
  "--op",
  `renameSymbol node="${helperFunction.id}" expect="helper" value="compute"`,
]);
assert.equal(patch.ok, true);
assert.equal(patch.operations[0].op, "renameSymbol");
assert.equal(patch.operations[0].actual, "helper");
assert.equal(patch.operations[0].value, "compute");
assert.equal(patch.agentTransaction.rollback.savedKind, "source");
assert.equal(patch.agentTransaction.proofLedger.kind, "graph-patch-proof-ledger");
assert(patch.agentTransaction.resultFields.includes("agentTransaction.proofLedger"));
assert(patch.agentTransaction.resultFields.includes("agentTransaction.rollback.actions[]"));
assert(patch.agentTransaction.resultFields.includes("agentTransaction.verificationCommands[].resultFields"));
const patchProofs = await runRequiredCommands(patch.agentTransaction.verificationCommands);
assert.deepEqual(patchProofs.map((proof) => proof.purpose).sort(), ["graph-check", "source-check"]);

const renamedSource = await readFile(sourcePath, "utf8");
assert.match(renamedSource, /fn compute\(value: i32\)/);
assert.match(renamedSource, /compute\(41\)/);

const check = await zeroJson(["check", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath]);
assert.equal(check.ok, true);
assert.equal(check.diagnostics.length, 0);

const test = await zeroJson(["test", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath]);
assert.equal(test.ok, true);
assert.equal(test.passedTests, 1);
assert.equal(test.failedTests, 0);
assert.equal(test.agentCommand.kind, "agent-test-command-contract");
const testProofs = await runRequiredCommands(test.agentCommand.verificationCommands);
assert.deepEqual(testProofs.map((proof) => proof.purpose).sort(), ["source-check", "test-run"]);

const build = await zeroJson(["build", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath]);
assert(build.artifactPath);
assert(build.artifactBytes > 0);
assert(build.artifactHash);
assert.equal(build.agentCommand.kind, "agent-build-command-contract");
const buildProofs = await runRequiredCommands(build.agentCommand.verificationCommands);
assert.deepEqual(buildProofs.map((proof) => proof.purpose).sort(), ["artifact-validate", "size-analysis", "source-check"]);

const proofPurposes = [...patchProofs, ...testProofs, ...buildProofs].map((proof) => proof.purpose);
const audit = {
  graphFindTokenBounded: find.agentCommand.readPolicy.fullSourceRequired === false,
  graphImpactTokenBounded: impact.agentCommand.readPolicy.fullSourceRequired === false,
  graphSliceTokenBounded: slice.tokenStrategy.readFullSourceRequired === false,
  checkedGraphEdit: patch.agentTransaction.proofLedger.kind === "graph-patch-proof-ledger",
  rollbackAvailable: patch.agentTransaction.rollback.actions.length > 0,
  compilerVerificationCommandsReplayed: proofPurposes.length === 7,
  sourceRewritten: /fn compute\(value: i32\)/.test(renamedSource) && /compute\(41\)/.test(renamedSource),
  behaviorVerified: test.passedTests === 1 && test.failedTests === 0,
  artifactVerified: Boolean(build.artifactPath) && build.artifactBytes > 0 && Boolean(build.artifactHash?.value ?? build.artifactHash),
};
assert(Object.values(audit).every(Boolean));

const report = {
  schemaVersion: 1,
  kind: "agent-authoring-replay-smoke",
  ok: true,
  sourceFile: sourcePath,
  stages: ["graph-find", "graph-impact", "graph-slice", "graph-patch", "source-check", "test-run", "artifact-build"],
  graph: {
    originalGraphHash: find.graphHash,
    patchedGraphHash: patch.patchedGraphHash,
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
