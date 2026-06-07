import { execFile } from "node:child_process";
import assert from "node:assert/strict";
import { mkdir, readFile, rm } from "node:fs/promises";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const cc = process.env.CC ?? "cc";
const out = `/tmp/zero-program-graph-smoke-${process.pid}`;

try {
  await execFileAsync(cc, [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-I",
    "native/zero-c/include",
    "-I",
    "native/zero-c/src",
    "native/zero-c/src/program_graph.c",
    "native/zero-c/src/program_graph_identity.c",
    "native/zero-c/src/program_graph_import.c",
    "native/zero-c/src/program_graph_lower.c",
    "native/zero-c/src/program_graph_node_id.c",
    "native/zero-c/src/program_graph_order.c",
    "native/zero-c/src/program_graph_format.c",
    "native/zero-c/src/program_graph_resolve.c",
    "native/zero-c/src/program_graph_semantics.c",
    "native/zero-c/src/program_graph_source_map.c",
    "native/zero-c/src/program_graph_validate.c",
    "native/zero-c/src/c_import.c",
    "native/zero-c/src/canonical_text.c",
    "native/zero-c/src/std_sig.c",
    "native/zero-c/src/std_source.c",
    "native/zero-c/src/ast.c",
    "native/zero-c/tests/program_graph_smoke.c",
    "-o",
    out,
  ]);
  const result = await execFileAsync(out);
  if (!result.stdout.includes("program graph smoke ok")) {
    throw new Error(`unexpected ProgramGraph smoke output: ${result.stdout}`);
  }
} finally {
  await rm(out, { force: true });
}

const zero = "bin/zero";
const binaryRoot = `/tmp/zero-program-graph-binary-store-${process.pid}`;

async function zeroRun(args: string[]) {
  return execFileAsync(zero, args, { encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
}

await rm(binaryRoot, { recursive: true, force: true });
await mkdir(binaryRoot, { recursive: true });
await zeroRun(["init", "--format", "binary", binaryRoot]);
let binaryStore = await readFile(`${binaryRoot}/zero.graph`);
assert.equal(binaryStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");

await zeroRun(["patch", binaryRoot, "--op", "addMain", "--op", 'addCheckWrite fn="main" text="hello binary\\n"']);
binaryStore = await readFile(`${binaryRoot}/zero.graph`);
assert.equal(binaryStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");

const binaryStatus = JSON.parse((await zeroRun(["status", "--json", binaryRoot])).stdout);
assert.equal(binaryStatus.store.encoding, "binary");
assert.equal(binaryStatus.storage.encoding, "single-file-binary");
assert.equal(binaryStatus.storage.defaultEncoding, "text");
assert.equal(binaryStatus.storage.binaryAvailable, true);

assert.equal((await zeroRun(["check", binaryRoot])).stdout, "ok\n");
assert.equal((await zeroRun(["run", binaryRoot])).stdout, "hello binary\n");
assert.match((await zeroRun(["sync", "--from-graph", binaryRoot])).stdout, /repository graph sync ok/);
assert.equal((await zeroRun(["verify-sync", binaryRoot])).stdout, "repository graph verify-sync ok\n");

const textRoot = `/tmp/zero-program-graph-binary-convert-${process.pid}`;
await rm(textRoot, { recursive: true, force: true });
await mkdir(textRoot, { recursive: true });
await zeroRun(["init", textRoot]);
await zeroRun(["patch", textRoot, "--op", "addMain", "--op", 'addCheckWrite fn="main" text="convert me\\n"']);
assert.match((await readFile(`${textRoot}/zero.graph`, "utf8")).slice(0, 64), /^zero-repository-graph v1/);
await zeroRun(["sync", "--from-graph", textRoot]);
await zeroRun(["sync", "--from-source", "--format", "binary", textRoot]);
const convertedStore = await readFile(`${textRoot}/zero.graph`);
assert.equal(convertedStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");
assert.equal((await zeroRun(["run", textRoot])).stdout, "convert me\n");
assert.equal((await zeroRun(["verify-sync", textRoot])).stdout, "repository graph verify-sync ok\n");

const invalidFormat = await zeroRun(["sync", "--from-source", "--format", "pickle", textRoot]).catch((error) => error);
assert.notEqual(invalidFormat.code ?? invalidFormat.status, 0);
assert.match(invalidFormat.stderr, /repository graph store format is not supported/);
