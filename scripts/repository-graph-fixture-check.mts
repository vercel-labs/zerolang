#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { performance } from "node:perf_hooks";
import { resolve } from "node:path";

const root = "conformance/program-graph";
const storePath = `${root}/zero.graph`;
const target = "linux-musl-x64";
const zeroBin = resolve("bin/zero");
const execMaxBuffer = 16 * 1024 * 1024;

const budgets = {
  statusMs: 2000,
  verifySyncMs: 3000,
  syncFromSourceMs: 5000,
  syncFromGraphMs: 5000,
};

function run(args: string[]) {
  const start = performance.now();
  const stdout = execFileSync(zeroBin, args, {
    encoding: "utf8",
    maxBuffer: execMaxBuffer,
    stdio: ["ignore", "pipe", "pipe"],
  });
  return { stdout, elapsedMs: performance.now() - start };
}

function runJson(args: string[]) {
  const result = run(args);
  return { ...result, body: JSON.parse(result.stdout) };
}

function assertBudget(name: string, elapsedMs: number, maxMs: number) {
  assert(
    elapsedMs <= maxMs,
    `${name} exceeded repository graph fixture budget: ${elapsedMs.toFixed(1)}ms > ${maxMs}ms`,
  );
}

const sourceBefore = readFileSync(`${root}/hello.0`, "utf8");
const storeBefore = readFileSync(storePath, "utf8");

const status = runJson(["graph", "status", "--json", "--target", target, root]);
assert.equal(status.body.ok, true);
assert.equal(status.body.repositoryGraph.storePresent, true);
assert.equal(status.body.repositoryGraph.storeValid, true);
assert.equal(status.body.repositoryGraph.syncState, "clean");
assertBudget("status", status.elapsedMs, budgets.statusMs);

const verify = runJson(["graph", "verify-sync", "--json", "--target", target, root]);
assert.equal(verify.body.ok, true);
assert.equal(verify.body.writes, false);
assert.equal(verify.body.repositoryGraph.syncState, "clean");
assertBudget("verify-sync", verify.elapsedMs, budgets.verifySyncMs);

const fromSource = runJson(["graph", "sync", "--from-source", "--json", "--target", target, root]);
assert.equal(fromSource.body.ok, true);
assert.equal(fromSource.body.repositoryGraph.syncState, "clean");
assert.deepEqual(fromSource.body.changedPaths, [storePath]);
assert.equal(readFileSync(`${root}/hello.0`, "utf8"), sourceBefore);
assert.equal(readFileSync(storePath, "utf8"), storeBefore);
assertBudget("sync --from-source", fromSource.elapsedMs, budgets.syncFromSourceMs);

const fromGraph = runJson(["graph", "sync", "--from-graph", "--json", "--target", target, root]);
assert.equal(fromGraph.body.ok, true);
assert.equal(fromGraph.body.repositoryGraph.syncState, "clean");
assert.deepEqual(fromGraph.body.changedPaths, []);
assert.equal(readFileSync(`${root}/hello.0`, "utf8"), sourceBefore);
assert.equal(readFileSync(storePath, "utf8"), storeBefore);
assertBudget("sync --from-graph", fromGraph.elapsedMs, budgets.syncFromGraphMs);

console.log(
  `repository graph fixture ok (status ${status.elapsedMs.toFixed(1)}ms, verify ${verify.elapsedMs.toFixed(1)}ms, sync-source ${fromSource.elapsedMs.toFixed(1)}ms, sync-graph ${fromGraph.elapsedMs.toFixed(1)}ms)`,
);
