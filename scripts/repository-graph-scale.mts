#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";

const root = join("/tmp", `zero-repository-graph-scale-${process.pid}`);
const source = join(root, "main.0");
const store = join(root, "zero.graph");
const zero = resolve("bin/zero");
const maxMs = Number(process.env.ZERO_REPOSITORY_GRAPH_SCALE_MAX_MS ?? "15000");

function json(args: string[], allowFailure = false) {
  try {
    const stdout = execFileSync(zero, args, { encoding: "utf8", maxBuffer: 16 * 1024 * 1024, stdio: ["ignore", "pipe", "pipe"] });
    return { code: 0, body: JSON.parse(stdout) };
  } catch (error) {
    if (!allowFailure) throw error;
    return { code: error.status ?? 1, body: JSON.parse(error.stdout?.toString() ?? "{}") };
  }
}

function elapsed<T>(fn: () => T) {
  const started = Date.now();
  const value = fn();
  return { value, ms: Date.now() - started };
}

rmSync(root, { recursive: true, force: true });
mkdirSync(root, { recursive: true });

const functions: string[] = [];
for (let i = 0; i < 96; i++) {
  functions.push(`fn value_${i}() -> i32 {\n    return ${i}\n}\n`);
}
writeFileSync(
  source,
  `${functions.join("\n")}\npub fn main(world: World) -> Void raises {\n    check world.out.write("repository graph scale ok\\n")\n}\n`,
);

const sync = elapsed(() => json(["graph", "sync", "--from-source", "--json", source]));
assert.equal(sync.value.code, 0);
assert.equal(sync.value.body.repositoryGraph.syncState, "clean");
assert(sync.ms < maxMs, `repository graph sync took ${sync.ms}ms`);

const status = elapsed(() => json(["graph", "status", "--json", source]));
assert.equal(status.value.body.repositoryGraph.storeValid, true);
assert.equal(status.value.body.storage.encoding, "single-file-text");
assert.equal(status.value.body.storage.interface, "ProgramGraphStore");
assert(status.value.body.scale.nodes >= 300, `expected a large graph, got ${status.value.body.scale.nodes} nodes`);
assert(status.value.body.scale.edges >= 200, `expected many graph edges, got ${status.value.body.scale.edges}`);
assert(status.ms < maxMs, `repository graph status took ${status.ms}ms`);

const verify = elapsed(() => json(["graph", "verify-sync", "--json", source]));
assert.equal(verify.value.code, 0);
assert.equal(verify.value.body.ok, true);
assert(verify.ms < maxMs, `repository graph verify-sync took ${verify.ms}ms`);

const originalStore = readFileSync(store, "utf8");
writeFileSync(store, originalStore.replace("zero-repository-graph v1", "zero-repository-graph v2"));
const futureSchema = json(["graph", "status", "--json", source], true);
assert.notEqual(futureSchema.code, 0);
assert.equal(futureSchema.body.diagnostics[0].code, "RGP003");
assert.match(futureSchema.body.diagnostics[0].actual, /invalid repository graph store|valid zero.graph/);
writeFileSync(store, originalStore);

console.log(`repository graph scale ok (${status.value.body.scale.nodes} nodes, ${status.value.body.scale.edges} edges)`);
