#!/usr/bin/env node
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const outDir = ".zero/playground-wasm-smoke";

await mkdir(outDir, { recursive: true });

async function buildAndRun(input, outName, expected) {
  const outBase = join(outDir, outName);
  const wasmPath = `${outBase}.wasm`;
  await rm(wasmPath, { force: true });
  const report = await execFileAsync("bin/zero", [
    "build",
    "--json",
    "--emit",
    "wasm",
    "--target",
    "wasm32-web",
    input,
    "--out",
    outBase,
  ]);
  const body = JSON.parse(report.stdout);
  assert.equal(body.target, "wasm32-web");
  assert.equal(body.compiler, "zero-wasm");
  assert.equal(body.generatedCBytes, 0);
  assert.equal(body.objectBackend.targetFacts.selectedEmitter, "zero-wasm");

  const bytes = await readFile(wasmPath);
  assert.equal(bytes[0], 0x00);
  assert.equal(bytes[1], 0x61);
  assert.equal(bytes[2], 0x73);
  assert.equal(bytes[3], 0x6d);

  const { instance } = await WebAssembly.instantiate(bytes, {});
  assert.equal(instance.exports.main(), expected);
}

async function buildAndTrap(input, outName) {
  const outBase = join(outDir, outName);
  const wasmPath = `${outBase}.wasm`;
  await rm(wasmPath, { force: true });
  const report = await execFileAsync("bin/zero", [
    "build",
    "--json",
    "--emit",
    "wasm",
    "--target",
    "wasm32-web",
    input,
    "--out",
    outBase,
  ]);
  const body = JSON.parse(report.stdout);
  assert.equal(body.generatedCBytes, 0);
  assert.equal(body.objectBackend.directFacts.runtime.linearMemory, true);
  assert.equal(body.objectBackend.directFacts.runtime.boundsTraps, "pay-as-used");
  const bytes = await readFile(wasmPath);
  const { instance } = await WebAssembly.instantiate(bytes, {});
  assert.throws(() => instance.exports.main(), WebAssembly.RuntimeError);
}

async function buildAndPeekMemory(input, outName) {
  const outBase = join(outDir, outName);
  const wasmPath = `${outBase}.wasm`;
  await rm(wasmPath, { force: true });
  const report = await execFileAsync("bin/zero", [
    "build",
    "--json",
    "--emit",
    "wasm",
    "--target",
    "wasm32-web",
    input,
    "--out",
    outBase,
  ]);
  const body = JSON.parse(report.stdout);
  assert.equal(body.generatedCBytes, 0);
  assert.equal(body.objectBackend.directFacts.runtime.linearMemory, true);
  const bytes = await readFile(wasmPath);
  const { instance } = await WebAssembly.instantiate(bytes, {});
  assert(instance.exports.memory instanceof WebAssembly.Memory);
  new Uint8Array(instance.exports.memory.buffer)[1024] = 90;
  assert.equal(instance.exports.read_at(1024), 90);
}

await buildAndRun("examples/direct-array-sum.0", "direct-array-sum", 10);
await buildAndRun("examples/direct-u8-helper-call.0", "direct-u8-helper-call", 1);
await buildAndRun("examples/direct-package-call-order", "direct-package-call-order", 27);
await buildAndTrap("examples/direct-array-bounds-trap.0", "direct-array-bounds-trap");
await buildAndRun("examples/direct-string-len.0", "direct-string-len", 5);
await buildAndRun("examples/direct-string-literal.0", "direct-string-literal", 116);
await buildAndRun("examples/direct-span-read.0", "direct-span-read", 107);
await buildAndRun("examples/direct-string-eql.0", "direct-string-eql", 1);
await buildAndRun("examples/direct-byte-view-locals.0", "direct-byte-view-locals", 107);
await buildAndRun("examples/direct-mutspan-len.0", "direct-mutspan-len", 5);
await buildAndPeekMemory("examples/direct-memory-peek.0", "direct-memory-peek");
await buildAndRun("examples/direct-byte-copy-fill.0", "direct-byte-copy-fill", 111);
await buildAndRun("examples/direct-alloc-bump.0", "direct-alloc-bump", 114);
await buildAndRun("examples/direct-alloc-overflow.0", "direct-alloc-overflow", 1);
await buildAndRun("examples/direct-token-shape.0", "direct-token-shape", 65);
await buildAndRun("examples/direct-enum-match.0", "direct-enum-match", 2);
await buildAndRun("examples/direct-raises-basic.0", "direct-raises-basic", 7);
await buildAndRun("examples/direct-rescue-basic.0", "direct-rescue-basic", 9);
await buildAndRun("examples/direct-byte-buf.0", "direct-byte-buf", 66);
await buildAndRun("examples/direct-generic-identity.0", "direct-generic-identity", 42);
await buildAndRun("examples/direct-generic-fixedbuf.0", "direct-generic-fixedbuf", 55);
await buildAndRun("examples/direct-generic-vec.0", "direct-generic-vec", 61);
await buildAndRun("compiler-zero", "compiler-zero-stage0", 48);

const publicCompilerBase = "docs-site/public/playground/zeroc-zero";
const publicCompilerWasm = `${publicCompilerBase}.wasm`;
await mkdir("docs-site/public/playground", { recursive: true });
await rm(publicCompilerWasm, { force: true });
const publicCompilerReport = await execFileAsync("bin/zero", [
  "build",
  "--json",
  "--emit",
  "wasm",
  "--target",
  "wasm32-web",
  "compiler-zero",
  "--out",
  publicCompilerBase,
]);
const publicCompilerBody = JSON.parse(publicCompilerReport.stdout);
assert.equal(publicCompilerBody.generatedCBytes, 0);
assert.equal(publicCompilerBody.selfHostRouting.wasmEmitter.selected, true);
const publicCompilerBytes = await readFile(publicCompilerWasm);
const publicCompiler = await WebAssembly.instantiate(publicCompilerBytes, {});
assert.equal(publicCompiler.instance.exports.main(), 48);
assert(publicCompiler.instance.exports.memory instanceof WebAssembly.Memory);
const publicCompilerSourceBytes = new TextEncoder().encode("pub fun main() -> Void {}\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerSourceBytes, 1024);
assert.equal(publicCompiler.instance.exports.compiler_accept_source_buffer(1024, publicCompilerSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 63);
assert.equal(publicCompiler.instance.exports.compiler_token_summary(1024, publicCompilerSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 1025);
assert.equal(publicCompiler.instance.exports.compiler_source_hash(1024, publicCompilerSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength) >>> 0, 3863097883);
assert.equal(publicCompiler.instance.exports.compiler_source_location_at(1024, publicCompilerSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 18), 1019);
assert.equal(publicCompiler.instance.exports.compiler_source_location_at(1024, publicCompilerSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 26), 2001);
assert.equal(publicCompiler.instance.exports.compiler_source_order_key(1, 2, 3863097883, 3863097884), 64);
assert.equal(publicCompiler.instance.exports.compiler_source_order_key(2, 1, 3863097883, 3863097884), 65);
assert.equal(publicCompiler.instance.exports.compiler_source_order_key(1, 1, 3863097883, 3863097884), 66);
const publicCompilerManifestBytes = await readFile("compiler-zero/zero.json");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerManifestBytes, 4096);
assert.equal(publicCompiler.instance.exports.compiler_manifest_summary(4096, publicCompilerManifestBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 52);
assert.equal(publicCompiler.instance.exports.compiler_hosted_package_source_plan(4096, publicCompilerManifestBytes.length, 1024, publicCompilerSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 1), 68);
assert.equal(publicCompiler.instance.exports.compiler_hosted_package_source_plan(4096, publicCompilerManifestBytes.length, 1024, publicCompilerSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 0), 0);
const publicCompilerRuntimeAbiBytes = new TextEncoder().encode('export c fun add(a: i32, b: i32) -> i32 { return a + b }\npub fun main(world: World, args: Args, env: Env, alloc: Alloc) -> Void raises { Bad } { let bytes: [4]u8 = [1, 2, 3, 4] check world.out.write("ok") check world.err.write("bad") raise Bad }\n');
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerRuntimeAbiBytes, 34816);
assert.equal(publicCompiler.instance.exports.compiler_runtime_abi_summary(34816, publicCompilerRuntimeAbiBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 1), 1023);
assert.equal(publicCompiler.instance.exports.compiler_runtime_memory_layout(64, 4, 1), 6400041);
assert.equal(publicCompiler.instance.exports.compiler_runtime_shim_cost(1, 1, 1, 1, 1), 11610);
assert.equal(publicCompiler.instance.exports.compiler_runtime_helper_summary(34816, publicCompilerRuntimeAbiBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 1), 249);
assert.equal(publicCompiler.instance.exports.compiler_runtime_helper_summary(34816, publicCompilerRuntimeAbiBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 2), 250);
assert.equal(publicCompiler.instance.exports.compiler_runtime_helper_summary(34816, publicCompilerRuntimeAbiBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 3), 252);
assert.equal(publicCompiler.instance.exports.compiler_browser_compile_request(1024, publicCompilerSourceBytes.length, 4096, publicCompilerManifestBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 1, 2, 3), 1020363);
assert.equal(publicCompiler.instance.exports.compiler_browser_compile_response(511, 255, 65535, 141), 800141);
assert.equal(publicCompiler.instance.exports.compiler_browser_artifact_plan(34816, publicCompilerRuntimeAbiBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 1, 3), 1311);
const publicCompilerPlaygroundHelloBytes = new TextEncoder().encode('pub fun main(world: World) -> Void raises { check world.out.write("hello from zero\\n") }\n');
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerPlaygroundHelloBytes, 12288);
assert.equal(publicCompiler.instance.exports.compiler_self_host_write_playground_wasm(12288, publicCompilerPlaygroundHelloBytes.length, 14336, 512, publicCompiler.instance.exports.memory.buffer.byteLength), 157);
assert.equal(new Uint8Array(publicCompiler.instance.exports.memory.buffer)[14336], 0);
assert.equal(new Uint8Array(publicCompiler.instance.exports.memory.buffer)[14337], 97);
assert.equal(new Uint8Array(publicCompiler.instance.exports.memory.buffer)[14338], 115);
assert.equal(new Uint8Array(publicCompiler.instance.exports.memory.buffer)[14339], 109);
const publicCompilerPlaygroundCustomBytes = new TextEncoder().encode('pub fun main(world: World) -> Void raises { check world.out.write("hello from zeroooo\\n") }\n');
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerPlaygroundCustomBytes, 12288);
assert.equal(publicCompiler.instance.exports.compiler_self_host_write_playground_wasm(12288, publicCompilerPlaygroundCustomBytes.length, 14336, 512, publicCompiler.instance.exports.memory.buffer.byteLength), 160);
const publicCompilerModuleSourceBytes = new TextEncoder().encode("use lib\npub fun main() -> Void {}\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerModuleSourceBytes, 8192);
assert.equal(publicCompiler.instance.exports.compiler_module_import_summary(8192, publicCompilerModuleSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 2), 73);
assert.equal(publicCompiler.instance.exports.compiler_module_import_summary(8192, publicCompilerModuleSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 1), 0);
assert.equal(publicCompiler.instance.exports.compiler_module_import_diag_code(8192, publicCompilerModuleSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 1), 3003);
assert.equal(publicCompiler.instance.exports.compiler_module_graph_reject_code(1, 1, 0, 0, 0), 3003);
assert.equal(publicCompiler.instance.exports.compiler_module_graph_reject_code(2, 1, 1, 0, 0), 3008);
assert.equal(publicCompiler.instance.exports.compiler_module_graph_reject_code(2, 1, 0, 1, 0), 3009);
assert.equal(publicCompiler.instance.exports.compiler_module_graph_reject_code(2, 1, 0, 0, 1), 3010);
const publicCompilerScopeBytes = new TextEncoder().encode("pub shape Point { x: i32 }\npub fun main() -> Void {\n let x = 1\n let y = x\n}\nfun helper() -> Void {}\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerScopeBytes, 32768);
assert.equal(publicCompiler.instance.exports.compiler_scope_summary(32768, publicCompilerScopeBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 40202);
assert.equal(publicCompiler.instance.exports.compiler_name_resolution_packet(32768, publicCompilerScopeBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 0, 0), 0);
assert.equal(publicCompiler.instance.exports.compiler_name_resolution_packet(32768, publicCompilerScopeBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 1, 0), 3003);
assert.equal(publicCompiler.instance.exports.compiler_name_resolution_packet(32768, publicCompilerScopeBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 0, 1), 3009);
const publicCompilerMemberNameBytes = new TextEncoder().encode("type BytePair = Pair<u8, u8>\nshape Pair<T, static N: usize> { left: T }\npub fun main() -> Void { pair.left }\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerMemberNameBytes, 40960);
assert.equal(publicCompiler.instance.exports.compiler_member_name_summary(40960, publicCompilerMemberNameBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 1010201);
const publicCompilerTypeSurfaceBytes = new TextEncoder().encode("shape Box { items: [4]u8, name: String, next: ref<Box> } enum Color { red } choice Event { quit } pub fun main(flag: Bool) -> Void { let n: usize = 1 }\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerTypeSurfaceBytes, 53248);
assert.equal(publicCompiler.instance.exports.compiler_type_surface_summary(53248, publicCompilerTypeSurfaceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 63);
const publicCompilerExpressionSurfaceBytes = new TextEncoder().encode("let mut pair: Pair = Pair { left: 1, right: 2 } pair.left = pair.items[0]");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerExpressionSurfaceBytes, 57344);
assert.equal(publicCompiler.instance.exports.compiler_expression_surface_summary(57344, publicCompilerExpressionSurfaceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 63);
const publicCompilerControlFlowBytes = new TextEncoder().encode("if ok { check call() } while ok { return } rescue err { return }");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerControlFlowBytes, 61440);
assert.equal(publicCompiler.instance.exports.compiler_control_flow_summary(61440, publicCompilerControlFlowBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 15);
const publicCompilerGenericStaticBytes = new TextEncoder().encode("interface Readable<T> { fun read(self: ref<T>) -> i32 } shape FixedVec<T, static N: usize> { items: [N]T fun push(self: mutref<Self>, value: T) -> Void {} } fun first<T, static N: usize>(vec: ref<FixedVec<T,N>>) -> T { return vec.items[0] } pub fun main() -> Void { vec.push(1) }\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerGenericStaticBytes, 45056);
assert.equal(publicCompiler.instance.exports.compiler_generic_static_summary(45056, publicCompilerGenericStaticBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 255);
const publicCompilerFallibilityBytes = new TextEncoder().encode("fun fail() -> Void raises { BadInput } { raise BadInput } pub fun main() -> Void raises { check fail() rescue err { return } }\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerFallibilityBytes, 47104);
assert.equal(publicCompiler.instance.exports.compiler_fallibility_summary(47104, publicCompilerFallibilityBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 255);
assert.equal(publicCompiler.instance.exports.compiler_fallibility_diag_code(1, 0, 0, 0), 1003);
assert.equal(publicCompiler.instance.exports.compiler_fallibility_diag_code(0, 1, 0, 0), 1002);
assert.equal(publicCompiler.instance.exports.compiler_fallibility_diag_code(0, 0, 0, 1), 1001);
const publicCompilerCapabilityBytes = new TextEncoder().encode('pub fun main(world: World, net: Net) -> Void raises { let fs = world.fs() let env = std.env.get("X") let proc = std.proc.spawn("noop") check world.out.write("target web") }\n');
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerCapabilityBytes, 59392);
assert.equal(publicCompiler.instance.exports.compiler_capability_summary(59392, publicCompilerCapabilityBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 255);
assert.equal(publicCompiler.instance.exports.compiler_capability_diag_code(1, 0), 6002);
assert.equal(publicCompiler.instance.exports.compiler_capability_diag_code(1, 1), 0);
const publicCompilerOwnershipBytes = new TextEncoder().encode("shape Receiver { value: i32 fun add(self: mutref<Self>) -> Void { self.value = self.value + 1 } } pub fun main() -> Void { let mut bytes: [2]u8 = [0, 0] let span: MutSpan<u8> = bytes bytes[0] = 1 let owned: owned<Receiver> = Receiver { value: 1 } consume(owned) Receiver.add(&mut owned) }\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerOwnershipBytes, 6144);
assert.equal(publicCompiler.instance.exports.compiler_ownership_summary(6144, publicCompilerOwnershipBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 255);
assert.equal(publicCompiler.instance.exports.compiler_ownership_diag_code(1, 0, 0, 0), 1009);
assert.equal(publicCompiler.instance.exports.compiler_ownership_diag_code(0, 1, 0, 0), 3013);
assert.equal(publicCompiler.instance.exports.compiler_ownership_diag_code(0, 0, 1, 0), 3048);
assert.equal(publicCompiler.instance.exports.compiler_ownership_diag_code(0, 0, 0, 1), 3109);
const publicCompilerMirBytes = new TextEncoder().encode('pub const LIMIT: usize = 4\nshape Pair { left: i32, right: i32 }\nenum Color { red }\nchoice Result { ok: i32, bad }\npub fun main(world: World) -> i32 raises { Bad } { let local: [4]u8 = [1, 2, 3, 4] let msg: String = "ok" let span: Span<u8> = local if local[0] == 1 { return helper(span.len) } raise Bad }\n');
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerMirBytes, 32768);
assert.equal(publicCompiler.instance.exports.compiler_mir_lowering_summary(32768, publicCompilerMirBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 65535);
const publicCompilerMirHashA = publicCompiler.instance.exports.compiler_mir_hash(32768, publicCompilerMirBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 1);
const publicCompilerMirHashB = publicCompiler.instance.exports.compiler_mir_hash(32768, publicCompilerMirBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 2);
assert(publicCompilerMirHashA > 0);
assert(publicCompilerMirHashB > 0);
assert.equal(publicCompiler.instance.exports.compiler_mir_module_order_hash(1, publicCompilerMirHashA, 2, publicCompilerMirHashB), publicCompiler.instance.exports.compiler_mir_module_order_hash(2, publicCompilerMirHashB, 1, publicCompilerMirHashA));
assert.equal(publicCompiler.instance.exports.compiler_mir_json_contract(65535, 1, 2), 167535);
const publicCompilerParseItemBytes = new TextEncoder().encode('import math\nuse lib\nconst answer: i32 = 42\nshape Point { x: i32 }\nenum Color { red }\nchoice Event { quit }\nextern c "x.h" as x\npub fun main() -> Void {}\ntest "ok" {}\n');
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerParseItemBytes, 16384);
assert.equal(publicCompiler.instance.exports.compiler_parse_item_summary(16384, publicCompilerParseItemBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 511);
const publicCompilerParseStmtBytes = new TextEncoder().encode('pub fun main() -> Void raises {\n let x = 1\n if x { check world.out.write("x") }\n while x { return }\n match x { ._ { raise Bad } }\n let y = call() rescue err { return }\n}\n');
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerParseStmtBytes, 20480);
assert.equal(publicCompiler.instance.exports.compiler_parse_stmt_expr_summary(20480, publicCompilerParseStmtBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 255);
const publicCompilerParseRootBytes = await readFile("conformance/parse/compiler-smoke.0");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerParseRootBytes, 24576);
assert.equal(publicCompiler.instance.exports.compiler_parse_root_summary(24576, publicCompilerParseRootBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 1010101);
const publicCompilerParsePublicBytes = new TextEncoder().encode('pub const answer: i32 = 42\npub shape Point { x: i32 }\npub enum Color { red }\npub choice Event { quit }\npub fun main() -> Void {}\nfun helper() -> Void {}\n');
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerParsePublicBytes, 28672);
assert.equal(publicCompiler.instance.exports.compiler_parse_public_api_summary(28672, publicCompilerParsePublicBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 11111);
const publicCompilerParseAstBytes = new TextEncoder().encode("pub const answer: i32 = 42\nshape Point { x: i32 }\npub fun main() -> Void { let x = 1 return }\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerParseAstBytes, 49152);
assert.equal(publicCompiler.instance.exports.compiler_parse_ast_summary(49152, publicCompilerParseAstBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength), 1010081);
const publicCompilerDiagSourceBytes = new TextEncoder().encode("pub fun main() -> Void {}\nmissing\n");
new Uint8Array(publicCompiler.instance.exports.memory.buffer).set(publicCompilerDiagSourceBytes, 36864);
assert.equal(publicCompiler.instance.exports.compiler_diagnostic_packet_build(36864, publicCompilerDiagSourceBytes.length, publicCompiler.instance.exports.memory.buffer.byteLength, 26, 3003, 1, 2), 2014203);
assert.equal(publicCompiler.instance.exports.compiler_diagnostic_detail_summary(1, 2, 3, 4, 5, 6), 123456);
assert.equal(publicCompiler.instance.exports.compiler_related_span_summary(2, 3, 7, 11, 4), 2037114);

const originalFetch = globalThis.fetch;
const playgroundArtifactFetches = [];
globalThis.fetch = async (url, options) => {
  const href = String(url);
  if (href.includes("zeroc-zero.wasm")) {
    playgroundArtifactFetches.push({ href, cache: options?.cache ?? null });
  }
  const path = href.startsWith("file:") ? new URL(href).pathname : "docs-site/public/playground/zeroc-zero.wasm";
  const bytes = await readFile(path);
  return new Response(bytes, { status: 200, headers: { "Content-Type": "application/wasm" } });
};
try {
  const { hydrateWasmResult, runZero } = await import("../docs-site/lib/zero-wasm.js");
  const playgroundRunnerSource = await readFile("docs-site/lib/zero-wasm.js", "utf8");
  assert(!playgroundRunnerSource.includes("selfHostStdoutLiteral"));
  assert(!playgroundRunnerSource.includes("evaluateStdoutBlock"));
  const helloSource = await readFile("examples/hello.0", "utf8");
  const helloResult = await runZero("auto", helloSource, "wasm32-web");
  assert.equal(helloResult.ok, true);
  assert.equal(helloResult.stdout, "hello from zero\n");
  assert.equal(helloResult.wasmRun.stdout, "hello from zero\n");
  assert.equal(helloResult.execution, null);
  assert.equal(helloResult.compilerLoader.primary, "zeroc-zero.wasm");
  assert.equal(helloResult.compilerLoader.compatibilityUsed, false);
  assert.equal(helloResult.compilerLoader.writtenWasmBytes, 157);
  assert.equal(helloResult.emittedArtifact.source, "zeroc-zero.wasm");
  assert.equal(helloResult.emittedArtifact.executable, true);
  assert.equal(helloResult.emittedArtifact.bytes, 157);
  const customHelloSource = `pub fun main(world: World) -> Void raises {
    check world.out.write("hello from zeroooo\\n")
}
`;
  const customHelloResult = await runZero("auto", customHelloSource, "wasm32-web");
  assert.equal(customHelloResult.ok, true);
  assert.equal(customHelloResult.stdout, "hello from zeroooo\n");
  assert.equal(customHelloResult.wasmRun.stdout, "hello from zeroooo\n");
  assert.equal(customHelloResult.execution, null);
  assert.equal(customHelloResult.compilerLoader.writtenWasmBytes, 160);
  assert.equal(customHelloResult.emittedArtifact.executable, true);
  assert.equal(customHelloResult.emittedArtifact.bytes, 160);
  const mathSource = `fun answer() -> i32 {
    return 40 + 2
}

pub fun main(world: World) -> Void raises {
    let value = answer()
    if value == 42 {
        check world.out.write("math works\\n")
    } else {
        check world.out.write("math broke\\n")
    }
}
`;
  const mathResult = await runZero("auto", mathSource, "wasm32-web");
  assert.equal(mathResult.ok, true);
  assert.equal(mathResult.stdout, "math works\n");
  assert.equal(mathResult.wasmRun.stdout, "math works\n");
  assert.equal(mathResult.execution, null);
  assert.equal(mathResult.emittedArtifact.executable, true);
  assert.equal(mathResult.emittedArtifact.bytes, 152);
  const mathFalseSource = `fun answer() -> i32 {
    return 40 + 2
}

pub fun main(world: World) -> Void raises {
    let value = answer()
    if value == 43 {
        check world.out.write("math works\\n")
    } else {
        check world.out.write("math broke\\n")
    }
}
`;
  const mathFalseResult = await runZero("auto", mathFalseSource, "wasm32-web");
  assert.equal(mathFalseResult.ok, true);
  assert.equal(mathFalseResult.stdout, "math broke\n");
  assert.equal(mathFalseResult.wasmRun.stdout, "math broke\n");
  assert.equal(mathFalseResult.execution, null);
  assert.equal(mathFalseResult.emittedArtifact.executable, true);
  assert.equal(mathFalseResult.emittedArtifact.bytes, 152);
  const noOutputResult = await runZero("auto", "pub fun main() -> Void {}\n", "wasm32-web");
  assert.equal(noOutputResult.ok, true);
  assert.equal(noOutputResult.stdout, undefined);
  assert(!Object.hasOwn(noOutputResult, "stdout"));
  assert.equal(noOutputResult.execution.status, "unsupported");
  const nestedBranchResult = await runZero("auto", `pub fun main(world: World) -> Void raises {
    if true {
      if false {
        check world.out.write("inner broke\\n")
      } else {
        check world.out.write("inner works\\n")
      }
    }
}
`, "wasm32-web");
  assert.equal(nestedBranchResult.ok, true);
  assert.equal(nestedBranchResult.stdout, "inner works\n");
  assert.equal(nestedBranchResult.wasmRun.stdout, "inner works\n");
  assert.equal(nestedBranchResult.execution, null);
  assert.equal(nestedBranchResult.emittedArtifact.executable, true);
  assert.equal(nestedBranchResult.emittedArtifact.bytes, 153);

  const helloCaptureBase = join(outDir, "hello-capture");
  const helloCaptureWasm = `${helloCaptureBase}.wasm`;
  await rm(helloCaptureWasm, { force: true });
  const helloCaptureReport = await execFileAsync("bin/zero", [
    "build",
    "--json",
    "--emit",
    "wasm",
    "--target",
    "wasm32-web",
    "examples/hello.0",
    "--out",
    helloCaptureBase,
  ]);
  const helloCaptureBody = JSON.parse(helloCaptureReport.stdout);
  assert.equal(helloCaptureBody.generatedCBytes, 0);
  const helloCaptureBytes = await readFile(helloCaptureWasm);
  const hydratedHello = await hydrateWasmResult({
    ok: true,
    target: "wasm32-web",
    message: "fixture",
    wasmBase64: Buffer.from(helloCaptureBytes).toString("base64"),
  });
  assert.equal(hydratedHello.stdout, "hello from zero\n");
  assert.equal(hydratedHello.wasmRun.stdout, "hello from zero\n");
  assert(playgroundArtifactFetches.length > 0);
  const playgroundCompilerKeys = playgroundArtifactFetches.map((item) => new URL(item.href).searchParams.get("zeroCompiler"));
  assert(playgroundCompilerKeys.every(Boolean));
  assert(new Set(playgroundCompilerKeys).size >= 5);
  assert(playgroundArtifactFetches.every((item) => item.cache === "no-store"));
} finally {
  globalThis.fetch = originalFetch;
}

globalThis.fetch = async (url) => {
  const href = String(url);
  if (href.includes("zeroc-zero.wasm")) {
    return new Response(null, { status: 404 });
  }
  return new Response(null, { status: 404 });
};
try {
  const { runZero } = await import(`../docs-site/lib/zero-wasm.js?missing-compiler=${Date.now()}`);
  const missingCompiler = await runZero("emit-wasm", "export c fun main() -> i32 { return 42 }\n", "wasm32-web");
  assert.equal(missingCompiler.ok, false);
  assert.equal(missingCompiler.compilerLoader.compatibility, "removed");
  assert.equal(missingCompiler.compilerLoader.compatibilityUsed, false);
  assert.equal(missingCompiler.diagnostics[0].code, "AGT002");
} finally {
  globalThis.fetch = originalFetch;
}

console.log("playground wasm smoke ok");
