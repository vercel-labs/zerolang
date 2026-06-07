#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const outDir = join(".zero", "agent-repair-demo", String(process.pid));
const zeroBin = process.env.ZERO_BIN ?? "bin/zero";
mkdirSync(outDir, { recursive: true });

const brokenSource = readFileSync("examples/agent-repair-demo/broken.0", "utf8");
const workFile = join(outDir, "main.0");
writeFileSync(workFile, brokenSource);

function zeroJson(args, allowFailure = false) {
  try {
    return JSON.parse(execFileSync(zeroBin, args, { encoding: "utf8" }));
  } catch (error) {
    if (!allowFailure) throw error;
    return JSON.parse(error.stdout.toString());
  }
}

function zero(args) {
  return execFileSync(zeroBin, args, { encoding: "utf8" }).replace(/\r\n/g, "\n");
}

function runRequiredVerificationCommands(commands) {
  const results = [];
  for (const command of commands) {
    assert.equal(typeof command.purpose, "string");
    assert.equal(typeof command.required, "boolean");
    assert(Array.isArray(command.argv));
    if (!command.required) continue;
    assert.equal(command.argv[0], "zero");
    const body = zeroJson(command.argv.slice(1), true);
    results.push({ purpose: command.purpose, ok: body.ok ?? true });
  }
  return results;
}

const check = zeroJson(["check", "--json", workFile], true);
assert.equal(check.ok, false);
assert.equal(check.diagnostics[0].code, "TYP009");
assert.equal(check.diagnostics[0].repair.id, "make-binding-mutable");

const explain = zeroJson(["explain", "--json", "TYP009"]);
assert.equal(explain.code, "TYP009");
assert.equal(explain.repair.id, "make-binding-mutable");

const plan = zeroJson(["fix", "--plan", "--json", workFile]);
assert.equal(plan.mode, "plan");
assert.equal(plan.appliesEdits, false);
assert.equal(plan.fixes[0].id, "make-binding-mutable");
assert.equal(plan.agentTransaction.kind, "compiler-mediated-repair-transaction");
assert.equal(plan.agentTransaction.checkedEditSurface, "plan-only");

const firstRepair = zeroJson(["fix", "--apply", "--json", workFile]);
assert.equal(firstRepair.agentTransaction.mode, "apply");
assert.equal(firstRepair.fixes[0].id, "make-binding-mutable");
assert.equal(firstRepair.applied, true);
assert.deepEqual(runRequiredVerificationCommands(firstRepair.agentTransaction.verificationCommands).map((item) => item.purpose), ["source-check", "graph-check"]);

const secondCheck = zeroJson(["check", "--json", workFile], true);
assert.equal(secondCheck.ok, false);
assert.equal(secondCheck.diagnostics[0].code, "TYP002");
assert.equal(secondCheck.diagnostics[0].repair.id, "match-binding-annotation");

const secondRepair = zeroJson(["fix", "--apply", "--json", workFile]);
assert.equal(secondRepair.fixes[0].id, "match-binding-annotation");
assert.equal(secondRepair.patches[0].new.includes("let _copied: usize"), true);
assert.equal(secondRepair.applied, true);
assert.deepEqual(runRequiredVerificationCommands(secondRepair.agentTransaction.verificationCommands).map((item) => item.purpose), ["source-check", "graph-check"]);

const fixed = zeroJson(["check", "--json", workFile]);
assert.equal(fixed.ok, true);
assert.equal(fixed.diagnostics.length, 0);

const graphPath = join(outDir, "main.program-graph");
const patchedGraphPath = join(outDir, "main.patched.program-graph");
const graph = zeroJson(["graph", "dump", "--json", "--out", graphPath, workFile]);
const copied = graph.nodes.find((node) => node.kind === "Let" && node.name === "_copied");
assert(copied);
assert.equal(copied.type, "usize");
const graphPatch = [
  "zero-program-graph-patch v1",
  `expect graphHash "${graph.graphHash}"`,
  `rename node="${copied.id}" expect="_copied" value="copied_len"`,
  "",
].join("\n");
const graphPatchResult = zeroJson(["graph", "patch", "--json", "--out", patchedGraphPath, graphPath, "--patch-text", graphPatch]);
assert.equal(graphPatchResult.ok, true);
assert.equal(graphPatchResult.agentTransaction.kind, "compiler-mediated-graph-patch-transaction");
assert.equal(graphPatchResult.agentTransaction.originalGraphHash, graph.graphHash);
assert.equal(graphPatchResult.agentTransaction.rollback.savedPath, patchedGraphPath);
assert.equal(graphPatchResult.operations[0].op, "rename");
assert.equal(graphPatchResult.operations[0].actual, "_copied");
assert.deepEqual(runRequiredVerificationCommands(graphPatchResult.agentTransaction.verificationCommands).map((item) => item.purpose), ["artifact-validate", "graph-check"]);
assert.match(zero(["graph", "view", patchedGraphPath]), /let copied_len: usize = std\.mem\.copy/);

const projectDir = join(outDir, "project");
rmSync(projectDir, { recursive: true, force: true });
execFileSync(zeroBin, ["new", "package", projectDir], { stdio: ["ignore", "ignore", "pipe"] });

function assertNoC(body) {
  assert.equal(body.generatedCBytes ?? 0, 0);
  assert.equal(body.cBridgeFallback ?? body.selfHostRouting?.cBridge?.required ?? false, false);
  assert.notEqual(body.legacy, true);
}

const projectCheck = zeroJson(["check", "--json", projectDir]);
assert.equal(projectCheck.ok, true);
assertNoC(projectCheck);

const projectTest = zeroJson(["test", "--json", projectDir]);
assert.equal(projectTest.ok, true);
assert.equal(projectTest.testBackend, "direct-frontend");
assertNoC(projectTest);

const projectMain = join(projectDir, "src", "main.0");
const projectMainSource = readFileSync(projectMain, "utf8");
writeFileSync(projectMain, projectMainSource.split('\n\ntest "package import works"')[0] + "\n");

const projectGraph = zeroJson(["graph", "--json", projectDir]);
assert.equal(projectGraph.selfHostRouting.cBridge.required, false);

const projectDoc = zeroJson(["doc", "--json", projectDir]);
assertNoC(projectDoc);

const projectSize = zeroJson(["size", "--json", projectDir]);
assertNoC(projectSize);

const projectMem = zeroJson(["mem", "--json", projectDir]);
assertNoC(projectMem);

const releaseOut = join(outDir, "project-linux-release");
const projectRelease = zeroJson(["build", "--json", "--release", "tiny", "--target", "linux-musl-x64", projectDir, "--out", releaseOut]);
assert.equal(projectRelease.emit, "exe");
assert.equal(projectRelease.compiler, "zero-elf64");
assertNoC(projectRelease);

console.log("agent repair demo ok");
