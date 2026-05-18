#!/usr/bin/env node
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const outDir = ".zero/agent-repair-demo";
mkdirSync(outDir, { recursive: true });

const keysDir = join(outDir, "keys");
const trustDir = join(outDir, "trust");
const trustedKeysPath = join(trustDir, "trusted-keys.json");
rmSync(keysDir, { recursive: true, force: true });
rmSync(trustDir, { recursive: true, force: true });
mkdirSync(keysDir, { recursive: true });
mkdirSync(trustDir, { recursive: true });

const zeroEnv = { ...process.env, ZERO_KEYS_DIR: keysDir, ZERO_TRUSTED_KEYS: trustedKeysPath };

const brokenSource = readFileSync("examples/agent-repair-demo/broken.0", "utf8");
const workFile = join(outDir, "main.0");
writeFileSync(workFile, brokenSource);

function zeroJson(args, allowFailure = false) {
  try {
    return JSON.parse(execFileSync("bin/zero", args, { encoding: "utf8", env: zeroEnv }));
  } catch (error) {
    if (!allowFailure) throw error;
    return JSON.parse(error.stdout.toString());
  }
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

const fixedSource = brokenSource.replace("    let dst: [4]u8", "    let mut dst: [4]u8");
writeFileSync(workFile, fixedSource);

const fixed = zeroJson(["check", "--json", workFile]);
assert.equal(fixed.ok, true);
assert.equal(fixed.diagnostics.length, 0);

const keyNew = JSON.parse(execFileSync("bin/zero", [
  "keys",
  "new",
  "--email",
  "agent-demo@example.com",
  "--name",
  "Agent Demo",
  "--label",
  "default",
  "--json",
], { encoding: "utf8", env: zeroEnv }));
const keyId = keyNew.key.id;
const publicKey = readFileSync(join(keysDir, keyId, "public.key"), "utf8").trim();
const trustedKeys = {
  schemaVersion: 1,
  format: "zero-trusted-keys-v1",
  keys: {
    publishers: [
      {
        keyId,
        publicKey,
        label: "default",
        name: "Agent Demo",
        email: "agent-demo@example.com",
      },
    ],
    threshold: 1,
    revoked: [],
  },
};
writeFileSync(trustedKeysPath, `${JSON.stringify(trustedKeys, null, 2)}\n`);

const projectDir = join(outDir, "project");
rmSync(projectDir, { recursive: true, force: true });
execFileSync("bin/zero", ["new", "package", "--insecure", projectDir], { stdio: ["ignore", "ignore", "pipe"], env: zeroEnv });

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
