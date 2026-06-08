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
    "native/zero-c/src/program_graph_identity.c",
    "native/zero-c/src/program_graph_import.c",
    "native/zero-c/src/program_graph_lower.c",
    "native/zero-c/src/program_graph_node_id.c",
    "native/zero-c/src/program_graph_order.c",
    "native/zero-c/src/program_graph_format.c",
    "native/zero-c/src/program_graph_compare.c",
    "native/zero-c/src/program_graph_reconcile_apply.c",
    "native/zero-c/src/program_graph_resolve.c",
    "native/zero-c/src/program_graph_semantics.c",
    "native/zero-c/src/program_graph_source_map.c",
    "native/zero-c/src/program_graph_store.c",
    "native/zero-c/src/program_graph_store_binary.c",
    "native/zero-c/src/program_graph_store_prune.c",
    "native/zero-c/src/program_graph_store_tables.c",
    "native/zero-c/src/program_graph_std_merge.c",
    "native/zero-c/src/program_graph_validate.c",
    "native/zero-c/src/program_graph_view.c",
    "native/zero-c/src/c_import.c",
    "native/zero-c/src/canonical_text.c",
    "native/zero-c/src/canonical_text_format.c",
    "native/zero-c/src/canonical_text_program.c",
    "native/zero-c/src/canonical_text_write.c",
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
].sort();
assert(checkedInGraphArtifacts.length > 0, "expected checked-in graph artifacts");
for (const graph of checkedInGraphArtifacts) {
  assertBinaryGraphStore(graph, await readFile(graph));
}

const checkedInProjections = [
  ...(await findCheckedInFiles("examples", ".0")),
  ...(await findCheckedInFiles("conformance", ".0")),
  ...(await findCheckedInFiles("benchmarks", ".0")),
].sort();
assert(checkedInProjections.length > 0, "expected checked-in source projections");
for (const projection of checkedInProjections) {
  assert.equal(projectionHasGraphAuthority(projection), true, `${projection} must have a graph authority`);
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
let binaryStore = await readFile(`${binaryRoot}/zero.graph`);
assert.equal(binaryStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");

await zeroRun(["patch", binaryRoot, "--op", "addMain", "--op", 'addCheckWrite fn="main" text="hello binary\\n"']);
binaryStore = await readFile(`${binaryRoot}/zero.graph`);
assert.equal(binaryStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");

const binaryStatus = JSON.parse((await zeroRun(["status", "--json", binaryRoot])).stdout);
assert.equal(binaryStatus.store.encoding, "binary");
assert.equal(binaryStatus.storage.encoding, "single-file-binary");
assert.equal(binaryStatus.storage.defaultEncoding, "binary");
assert.equal(binaryStatus.storage.binaryAvailable, true);

assert.equal((await zeroRun(["check", binaryRoot])).stdout, "ok\n");
assert.equal((await zeroRun(["run", binaryRoot])).stdout, "hello binary\n");
assert.match((await zeroRun(["export", binaryRoot])).stdout, /repository graph export ok/);
assert.equal((await zeroRun(["verify-projection", binaryRoot])).stdout, "repository graph verify-projection ok\n");

const textRoot = `/tmp/zero-program-graph-binary-convert-${process.pid}`;
await rm(textRoot, { recursive: true, force: true });
await mkdir(textRoot, { recursive: true });
await zeroRun(["init", "--format", "text", textRoot]);
await zeroRun(["patch", textRoot, "--op", "addMain", "--op", 'addCheckWrite fn="main" text="convert me\\n"']);
assert.match((await readFile(`${textRoot}/zero.graph`, "utf8")).slice(0, 64), /^zero-repository-graph v1/);
await zeroRun(["export", textRoot]);
await zeroRun(["import", "--format", "binary", textRoot]);
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
const projectionDefaultStore = await readFile(`${projectionDefaultRoot}/zero.graph`);
assert.equal(projectionDefaultStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");
assert.equal((await zeroRun(["run", projectionDefaultRoot])).stdout, "projection default\n");

const invalidFormat = await zeroRun(["import", "--format", "pickle", textRoot]).catch((error) => error);
assert.notEqual(invalidFormat.code ?? invalidFormat.status, 0);
assert.match(invalidFormat.stderr, /repository graph store format is not supported/);
