import { execFile } from "node:child_process";
import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { lstat, mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";
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
    "native/zero-c/src/program_graph_adjacency.c",
    "native/zero-c/src/program_graph_clone.c",
    "native/zero-c/src/program_graph_identity.c",
    "native/zero-c/src/program_graph_import.c",
    "native/zero-c/src/program_graph_lower.c",
    "native/zero-c/src/program_graph_node_id.c",
    "native/zero-c/src/program_graph_order.c",
    "native/zero-c/src/program_graph_format.c",
    "native/zero-c/src/program_graph_handle.c",
    "native/zero-c/src/program_graph_compare.c",
    "native/zero-c/src/program_graph_projection_fast.c",
    "native/zero-c/src/program_graph_reconcile.c",
    "native/zero-c/src/program_graph_reconcile_apply.c",
    "native/zero-c/src/program_graph_resolve.c",
    "native/zero-c/src/program_graph_semantics.c",
    "native/zero-c/src/program_graph_source_map.c",
    "native/zero-c/src/program_graph_store.c",
    "native/zero-c/src/program_graph_store_binary.c",
    "native/zero-c/src/program_graph_store_prune.c",
    "native/zero-c/src/program_graph_store_tables.c",
    "native/zero-c/src/program_graph_std_deps.c",
    "native/zero-c/src/program_graph_std_merge.c",
    "native/zero-c/src/program_graph_std_prune.c",
    "native/zero-c/src/program_graph_string_map.c",
    "native/zero-c/src/program_graph_validate.c",
    "native/zero-c/src/program_graph_view.c",
    "native/zero-c/src/c_import.c",
    "native/zero-c/src/canonical_text.c",
    "native/zero-c/src/canonical_text_format.c",
    "native/zero-c/src/canonical_text_program.c",
    "native/zero-c/src/canonical_text_write.c",
    "native/zero-c/src/fs_read.c",
    "native/zero-c/src/std_sig.c",
    "native/zero-c/src/std_source.c",
    "native/zero-c/src/ast.c",
    "native/zero-c/tests/program_graph_smoke_stubs.c",
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

const zero = process.env.ZERO_BIN || (existsSync(".zero/bin/zero") ? ".zero/bin/zero" : "bin/zero");
const checkedInBinaryRoot = "examples/binary-graph-store";
const checkedInBinaryCrmRoot = "examples/crm-api";
const binaryRoot = `/tmp/zero-program-graph-binary-store-${process.pid}`;

async function zeroRun(args: string[]) {
  return execFileAsync(zero, args, { encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
}

async function zeroRunMaybe(args: string[]) {
  try {
    const result = await zeroRun(args);
    return { code: 0, stdout: result.stdout, stderr: result.stderr };
  } catch (error: any) {
    return {
      code: error.code ?? error.status ?? 1,
      stdout: error.stdout ?? "",
      stderr: error.stderr ?? "",
    };
  }
}

async function findCheckedInGraphStores(root: string, stores: string[] = []) {
  if (!existsSync(root)) return stores;
  for (const entry of await readdir(root, { withFileTypes: true })) {
    const path = `${root}/${entry.name}`;
    if (entry.isDirectory()) {
      if (entry.name === ".zero" || entry.name === "node_modules") continue;
      await findCheckedInGraphStores(path, stores);
    } else if (entry.isFile() && entry.name === "zero.graph") {
      stores.push(path);
    } else if (entry.isSymbolicLink()) {
      const stat = await lstat(path);
      if (stat.isFile() && entry.name === "zero.graph") stores.push(path);
    }
  }
  return stores;
}

async function findCheckedInFiles(root: string, suffix: string, files: string[] = []) {
  if (!existsSync(root)) return files;
  for (const entry of await readdir(root, { withFileTypes: true })) {
    const path = `${root}/${entry.name}`;
    if (entry.isDirectory()) {
      if (entry.name === ".zero" || entry.name === "node_modules") continue;
      await findCheckedInFiles(path, suffix, files);
    } else if (entry.isFile() && entry.name.endsWith(suffix)) {
      files.push(path);
    } else if (entry.isSymbolicLink()) {
      const stat = await lstat(path);
      if (stat.isFile() && entry.name.endsWith(suffix)) files.push(path);
    }
  }
  return files;
}

async function findAtomicWriteTempFiles(root: string, files: string[] = []) {
  if (!existsSync(root)) return files;
  for (const entry of await readdir(root, { withFileTypes: true })) {
    const path = `${root}/${entry.name}`;
    if (entry.name.includes(".zero-tmp-")) {
      files.push(path);
      continue;
    }
    if (entry.isDirectory()) {
      await findAtomicWriteTempFiles(path, files);
    }
  }
  return files;
}

async function assertNoAtomicWriteTempFiles(root: string) {
  assert.deepEqual(await findAtomicWriteTempFiles(root), [], `${root}: atomic graph writes must clean up temporary files`);
}

function projectionHasGraphAuthority(path: string) {
  if (existsSync(path.replace(/\.0$/, ".graph"))) return true;
  for (let current = dirname(path); current !== "." && current !== "/"; current = dirname(current)) {
    if (existsSync(join(current, "zero.graph"))) return true;
  }
  return false;
}

function assertBinaryGraphStore(path: string, bytes: Buffer) {
  assert.equal(bytes.subarray(0, 8).toString("binary"), "ZRGBIN1\0", `${path} should be a binary repository graph store`);
}

const BINARY_NODE_COUNT_OFFSET = 40;
const BINARY_EDGE_COUNT_OFFSET = 48;
const BINARY_STRING_BYTES_OFFSET = 64;
const BINARY_MODULE_IDENTITY_REF_OFFSET = 72;

function corruptBinaryStoreModuleIdentity(bytes: Buffer) {
  const copy = Buffer.from(bytes);
  const stringBytes = Number(copy.readBigUInt64LE(BINARY_STRING_BYTES_OFFSET));
  const moduleIdentityOffset = Number(copy.readBigUInt64LE(BINARY_MODULE_IDENTITY_REF_OFFSET));
  const moduleIdentityLen = Number(copy.readBigUInt64LE(BINARY_MODULE_IDENTITY_REF_OFFSET + 8));
  assert(stringBytes <= copy.length, "binary store fixture has invalid string table size");
  assert(moduleIdentityLen > 2, "binary store fixture module identity is too short to corrupt");
  const stringsOffset = copy.length - stringBytes;
  assert(moduleIdentityOffset + 1 < stringBytes, "binary store fixture module identity offset is out of range");
  copy[stringsOffset + moduleIdentityOffset + 1] = 0;
  return copy;
}

function corruptBinaryStoreNodeCount(bytes: Buffer) {
  const copy = Buffer.from(bytes);
  copy.writeBigUInt64LE(1_000_001n, BINARY_NODE_COUNT_OFFSET);
  return copy;
}

function corruptBinaryStoreEdgeCount(bytes: Buffer) {
  const copy = Buffer.from(bytes);
  copy.writeBigUInt64LE(4_000_001n, BINARY_EDGE_COUNT_OFFSET);
  return copy;
}

async function assertInvalidBinaryStoreRejected(root: string, bytes: Buffer, label: string) {
  await rm(root, { recursive: true, force: true });
  await mkdir(root, { recursive: true });
  await writeFile(
    `${root}/zero.toml`,
    '[package]\nname = "corrupt-binary-store"\nversion = "0.1.0"\n\n[targets.cli]\nkind = "exe"\nmain = "main.0"\n',
  );
  await writeFile(`${root}/main.0`, "pub fn main() -> i32 { return 0 }\n");
  await writeFile(`${root}/zero.graph`, bytes);
  const status = await zeroRunMaybe(["status", "--json", root]);
  assert.notEqual(status.code, 0, `${label}: corrupt binary store status should fail`);
  const body = JSON.parse(status.stdout);
  assert.equal(body.repositoryGraph.storePresent, true);
  assert.equal(body.repositoryGraph.storeValid, false);
  assert.equal(body.repositoryGraph.projectionState, "store-invalid");
  assert.equal(body.diagnostics[0].code, "RGP003");
  assert.match(body.diagnostics[0].message, /repository graph store is invalid|invalid repository graph store|invalid binary repository graph store/);
}

const checkedInStores = [
  ...(await findCheckedInGraphStores("examples")),
  ...(await findCheckedInGraphStores("conformance")),
  ...(await findCheckedInGraphStores("benchmarks")),
].sort();
assert(checkedInStores.length > 0, "expected checked-in repository graph stores");
for (const store of checkedInStores) {
  assertBinaryGraphStore(store, await readFile(store));
}

const checkedInGraphArtifacts = [
  ...(await findCheckedInFiles("examples", ".graph")),
  ...(await findCheckedInFiles("conformance", ".graph")),
  ...(await findCheckedInFiles("benchmarks", ".graph")),
  ...(await findCheckedInFiles("std", ".graph")),
].sort();
assert(checkedInGraphArtifacts.length > 0, "expected checked-in graph artifacts");
for (const graph of checkedInGraphArtifacts) {
  assertBinaryGraphStore(graph, await readFile(graph));
}

const checkedInProjections = [
  ...(await findCheckedInFiles("examples", ".0")),
  ...(await findCheckedInFiles("conformance", ".0")),
  ...(await findCheckedInFiles("benchmarks", ".0")),
  ...(await findCheckedInFiles("std", ".0")),
].sort();
assert(checkedInProjections.length > 0, "expected checked-in source projections");
for (const projection of checkedInProjections) {
  assert.equal(projectionHasGraphAuthority(projection), true, `${projection} must have a graph authority`);
}

for (const store of checkedInStores) {
  const root = dirname(store);
  const status = JSON.parse((await zeroRun(["status", "--json", root])).stdout);
  assert.equal(status.store.encoding, "binary", `${root}: checked-in repository graph store must be binary`);
  assert.equal(status.repositoryGraph.projectionValidity, "clean", `${root}: checked-in source projections must be clean`);
  assert.equal((await zeroRun(["verify-projection", root])).stdout, "repository graph verify-projection ok\n", `${root}: verify-projection`);
}

const checkedInBinaryStore = await readFile(`${checkedInBinaryRoot}/zero.graph`);
assertBinaryGraphStore(`${checkedInBinaryRoot}/zero.graph`, checkedInBinaryStore);
const checkedInBinaryStatus = JSON.parse((await zeroRun(["status", "--json", checkedInBinaryRoot])).stdout);
assert.equal(checkedInBinaryStatus.store.encoding, "binary");
assert.equal(checkedInBinaryStatus.repositoryGraph.projectionValidity, "clean");
assert.equal((await zeroRun(["check", checkedInBinaryRoot])).stdout, "ok\n");
assert.equal((await zeroRun(["test", checkedInBinaryRoot])).stdout, "1 test(s) ok\n");
assert.equal((await zeroRun(["run", checkedInBinaryRoot])).stdout, "binary graph store example\n");
assert.equal((await zeroRun(["verify-projection", checkedInBinaryRoot])).stdout, "repository graph verify-projection ok\n");

const checkedInBinaryCrmStore = await readFile(`${checkedInBinaryCrmRoot}/zero.graph`);
assertBinaryGraphStore(`${checkedInBinaryCrmRoot}/zero.graph`, checkedInBinaryCrmStore);
const checkedInBinaryCrmStatus = JSON.parse((await zeroRun(["status", "--json", checkedInBinaryCrmRoot])).stdout);
assert.equal(checkedInBinaryCrmStatus.store.encoding, "binary");
assert.equal(checkedInBinaryCrmStatus.repositoryGraph.projectionValidity, "clean");
assert.equal((await zeroRun(["check", checkedInBinaryCrmRoot])).stdout, "ok\n");
assert.equal((await zeroRun(["verify-projection", checkedInBinaryCrmRoot])).stdout, "repository graph verify-projection ok\n");

await rm(binaryRoot, { recursive: true, force: true });
await mkdir(binaryRoot, { recursive: true });
await zeroRun(["init", binaryRoot]);
await assertNoAtomicWriteTempFiles(binaryRoot);
let binaryStore = await readFile(`${binaryRoot}/zero.graph`);
assert.equal(binaryStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");

await zeroRun(["patch", binaryRoot, "--op", "addMain", "--op", 'addCheckWrite fn="main" text="hello binary\\n"']);
await assertNoAtomicWriteTempFiles(binaryRoot);
binaryStore = await readFile(`${binaryRoot}/zero.graph`);
assert.equal(binaryStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");
await assertInvalidBinaryStoreRejected(
  `/tmp/zero-program-graph-binary-corrupt-nul-${process.pid}`,
  corruptBinaryStoreModuleIdentity(binaryStore),
  "embedded nul",
);
await assertInvalidBinaryStoreRejected(
  `/tmp/zero-program-graph-binary-corrupt-count-${process.pid}`,
  corruptBinaryStoreNodeCount(binaryStore),
  "node count",
);
await assertInvalidBinaryStoreRejected(
  `/tmp/zero-program-graph-binary-corrupt-edge-count-${process.pid}`,
  corruptBinaryStoreEdgeCount(binaryStore),
  "edge count",
);

const binaryStatus = JSON.parse((await zeroRun(["status", "--json", binaryRoot])).stdout);
assert.equal(binaryStatus.store.encoding, "binary");
assert.equal(binaryStatus.storage.encoding, "single-file-binary");
assert.equal(binaryStatus.storage.defaultEncoding, "binary");
assert.equal(binaryStatus.storage.binaryAvailable, true);

assert.equal((await zeroRun(["check", binaryRoot])).stdout, "ok\n");
assert.equal((await zeroRun(["run", binaryRoot])).stdout, "hello binary\n");
assert.match((await zeroRun(["export", binaryRoot])).stdout, /repository graph export ok/);
await assertNoAtomicWriteTempFiles(binaryRoot);
assert.equal((await zeroRun(["verify-projection", binaryRoot])).stdout, "repository graph verify-projection ok\n");

const textRoot = `/tmp/zero-program-graph-binary-convert-${process.pid}`;
await rm(textRoot, { recursive: true, force: true });
await mkdir(textRoot, { recursive: true });
await zeroRun(["init", "--format", "text", textRoot]);
await zeroRun(["patch", textRoot, "--op", "addMain", "--op", 'addCheckWrite fn="main" text="convert me\\n"']);
await assertNoAtomicWriteTempFiles(textRoot);
assert.match((await readFile(`${textRoot}/zero.graph`, "utf8")).slice(0, 64), /^zero-repository-graph v1/);
await zeroRun(["export", textRoot]);
await zeroRun(["import", "--format", "binary", textRoot]);
await assertNoAtomicWriteTempFiles(textRoot);
const convertedStore = await readFile(`${textRoot}/zero.graph`);
assert.equal(convertedStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");
assert.equal((await zeroRun(["run", textRoot])).stdout, "convert me\n");
assert.equal((await zeroRun(["verify-projection", textRoot])).stdout, "repository graph verify-projection ok\n");

const projectionDefaultRoot = `/tmp/zero-program-graph-projection-default-${process.pid}`;
await rm(projectionDefaultRoot, { recursive: true, force: true });
await mkdir(projectionDefaultRoot, { recursive: true });
await writeFile(
  `${projectionDefaultRoot}/main.0`,
  'pub fn main(world: World) -> Void raises {\n    check world.out.write("projection default\\n")\n}\n',
);
await writeFile(
  `${projectionDefaultRoot}/zero.toml`,
  '[package]\nname = "projection-default"\nversion = "0.1.0"\n\n[targets.cli]\nkind = "exe"\nmain = "main.0"\n',
);
await zeroRun(["import", projectionDefaultRoot]);
await assertNoAtomicWriteTempFiles(projectionDefaultRoot);
const projectionDefaultStore = await readFile(`${projectionDefaultRoot}/zero.graph`);
assert.equal(projectionDefaultStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");
assert.equal((await zeroRun(["run", projectionDefaultRoot])).stdout, "projection default\n");

const invalidFormat = await zeroRun(["import", "--format", "pickle", textRoot]).catch((error) => error);
assert.notEqual(invalidFormat.code ?? invalidFormat.status, 0);
assert.match(invalidFormat.stderr, /repository graph store format is not supported/);
