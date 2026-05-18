#!/usr/bin/env node
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm } from "node:fs/promises";
import { join } from "node:path";
import { performance } from "node:perf_hooks";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const outDir = ".zero/browser-compiler-hardening";

await mkdir(outDir, { recursive: true });

async function buildStage(name) {
  const outBase = join(outDir, name);
  const wasmPath = `${outBase}.wasm`;
  await rm(wasmPath, { force: true });
  const report = await execFileAsync("bin/zero", [
    "build",
    "--json",
    "--emit",
    "wasm",
    "--target",
    "wasm32-web",
    "compiler-zero",
    "--out",
    outBase,
  ]);
  const body = JSON.parse(report.stdout);
  const bytes = await readFile(wasmPath);
  return { body, bytes };
}

const stageA = await buildStage("compiler-zero-a");
const stageB = await buildStage("compiler-zero-b");
assert.equal(Buffer.compare(stageA.bytes, stageB.bytes), 0, "stage0 wasm should be deterministic");
assert(stageA.bytes.byteLength < 32 * 1024, "stage0 browser compiler seed should stay under 32KiB");
assert.equal(stageA.body.objectBackend.targetFacts.fallbackPolicy, "explicit-direct-never-c-bridge");

const started = performance.now();
const compiled = await WebAssembly.compile(stageA.bytes);
const compileMs = performance.now() - started;
assert(compileMs < 2000, `stage0 wasm compile took ${compileMs}ms`);
const instance = await WebAssembly.instantiate(compiled, {});
assert.equal(instance.exports.main(), 48);
assert(instance.exports.memory instanceof WebAssembly.Memory);
assert.equal(instance.exports.compiler_runtime_arena_plan(64, 16), 54);
assert.equal(instance.exports.compiler_source_input_mode(1, 1, 0), 61);
assert.equal(instance.exports.compiler_source_input_mode(2, 0, 1), 62);
const sourceBytes = new TextEncoder().encode("pub fun main() -> Void {}\n");
new Uint8Array(instance.exports.memory.buffer).set(sourceBytes, 1024);
assert.equal(instance.exports.compiler_accept_source_buffer(1024, sourceBytes.length, instance.exports.memory.buffer.byteLength), 63);
assert.equal(instance.exports.compiler_token_summary(1024, sourceBytes.length, instance.exports.memory.buffer.byteLength), 1025);
assert.equal(instance.exports.compiler_source_hash(1024, sourceBytes.length, instance.exports.memory.buffer.byteLength) >>> 0, 3863097883);
assert.equal(instance.exports.compiler_source_location_at(1024, sourceBytes.length, instance.exports.memory.buffer.byteLength, 18), 1019);
assert.equal(instance.exports.compiler_source_location_at(1024, sourceBytes.length, instance.exports.memory.buffer.byteLength, 26), 2001);
assert.equal(instance.exports.compiler_source_order_key(1, 2, 3863097883, 3863097884), 64);
assert.equal(instance.exports.compiler_source_order_key(2, 1, 3863097883, 3863097884), 65);
assert.equal(instance.exports.compiler_source_order_key(1, 1, 3863097883, 3863097884), 66);
const manifestBytes = await readFile("compiler-zero/zero.json");
new Uint8Array(instance.exports.memory.buffer).set(manifestBytes, 4096);
assert.equal(instance.exports.compiler_manifest_summary(4096, manifestBytes.length, instance.exports.memory.buffer.byteLength), 52);
assert.equal(instance.exports.compiler_hosted_package_source_plan(4096, manifestBytes.length, 1024, sourceBytes.length, instance.exports.memory.buffer.byteLength, 1), 68);
assert.equal(instance.exports.compiler_hosted_package_source_plan(4096, manifestBytes.length, 1024, sourceBytes.length, instance.exports.memory.buffer.byteLength, 0), 0);
const runtimeAbiBytes = new TextEncoder().encode('export c fun add(a: i32, b: i32) -> i32 { return a + b }\npub fun main(world: World, args: Args, env: Env, alloc: Alloc) -> Void raises { Bad } { let bytes: [4]u8 = [1, 2, 3, 4] check world.out.write("ok") check world.err.write("bad") raise Bad }\n');
new Uint8Array(instance.exports.memory.buffer).set(runtimeAbiBytes, 34816);
assert.equal(instance.exports.compiler_runtime_abi_summary(34816, runtimeAbiBytes.length, instance.exports.memory.buffer.byteLength, 1), 1023);
assert.equal(instance.exports.compiler_runtime_memory_layout(64, 4, 1), 6400041);
assert.equal(instance.exports.compiler_runtime_shim_cost(1, 1, 1, 1, 1), 11610);
assert.equal(instance.exports.compiler_runtime_helper_summary(34816, runtimeAbiBytes.length, instance.exports.memory.buffer.byteLength, 1), 249);
assert.equal(instance.exports.compiler_runtime_helper_summary(34816, runtimeAbiBytes.length, instance.exports.memory.buffer.byteLength, 2), 250);
assert.equal(instance.exports.compiler_runtime_helper_summary(34816, runtimeAbiBytes.length, instance.exports.memory.buffer.byteLength, 3), 252);
assert.equal(instance.exports.compiler_browser_compile_request(1024, sourceBytes.length, 4096, manifestBytes.length, instance.exports.memory.buffer.byteLength, 1, 2, 3), 1020363);
assert.equal(instance.exports.compiler_browser_compile_response(511, 255, 65535, 141), 800141);
assert.equal(instance.exports.compiler_browser_artifact_plan(34816, runtimeAbiBytes.length, instance.exports.memory.buffer.byteLength, 1, 3), 1311);
assert.equal(instance.exports.compiler_self_host_source_graph_plan(1024, sourceBytes.length, 4096, manifestBytes.length, instance.exports.memory.buffer.byteLength, 1), 52680001);
assert.equal(instance.exports.compiler_self_host_compile_request(1024, sourceBytes.length, 4096, manifestBytes.length, instance.exports.memory.buffer.byteLength, 1, 2, 3), 1821673);
assert.equal(instance.exports.compiler_self_host_module_graph_packet(1024, sourceBytes.length, 4096, manifestBytes.length, instance.exports.memory.buffer.byteLength, 1), 13171111);
assert.equal(instance.exports.compiler_self_host_source_diag_code(1024, sourceBytes.length, 4096, manifestBytes.length, instance.exports.memory.buffer.byteLength, 1), 0);
assert.equal(instance.exports.compiler_self_host_write_minimal_wasm(8192, 8, instance.exports.memory.buffer.byteLength), 8);
assert.equal(instance.exports.compiler_self_host_minimal_wasm_hash(8192, 8, instance.exports.memory.buffer.byteLength), 1836278016);
assert.equal(instance.exports.compiler_self_host_write_return42_wasm(14336, 64, instance.exports.memory.buffer.byteLength), 38);
const playgroundHelloBytes = new TextEncoder().encode('pub fun main(world: World) -> Void raises { check world.out.write("hello from zero\\n") }\n');
new Uint8Array(instance.exports.memory.buffer).set(playgroundHelloBytes, 20480);
assert.equal(instance.exports.compiler_self_host_write_playground_wasm(20480, playgroundHelloBytes.length, 24576, 512, instance.exports.memory.buffer.byteLength), 157);
assert.equal(instance.exports.compiler_self_host_command_response(1, 52680001, 1821673, 1836278016), 4010001);
assert.equal(instance.exports.compiler_self_host_command_response(2, 52680001, 1821673, 1836278016), 4020008);
assert.equal(instance.exports.compiler_self_host_command_response(3, 52680001, 1821673, 1836278016), 4030001);
assert.equal(instance.exports.compiler_self_host_command_response(4, 52680001, 1821673, 1836278016), 4040008);
assert.equal(instance.exports.compiler_self_host_fixed_point_packet(2, 4020008, 7009121, 1836278016), 2203300);
assert.equal(instance.exports.compiler_self_host_fixed_point_packet(3, 4020008, 7009121, 1836278016), 3303300);
assert.equal(instance.exports.compiler_self_host_native_artifact_plan(1, 1, 0, 0), 801101);
assert.equal(instance.exports.compiler_self_host_native_artifact_plan(2, 2, 0, 0), 802202);
assert.equal(instance.exports.compiler_self_host_native_artifact_plan(2, 3, 0, 0), 802303);
assert.equal(instance.exports.compiler_self_host_native_artifact_plan(2, 2, 0, 1), 0);
assert.equal(instance.exports.compiler_self_host_target_capability_diag(2, 1, 1, 3), 0);
assert.equal(instance.exports.compiler_self_host_target_capability_diag(5, 1, 0, 2), 6002);
assert.equal(instance.exports.compiler_self_host_required_capabilities(), 8191);
assert.equal(instance.exports.compiler_self_host_supported_capabilities(), 8191);
assert.equal(instance.exports.compiler_self_host_gap_mask(), 0);
assert.equal(instance.exports.compiler_self_host_gap_diag_code(512), 7003);
assert.equal(instance.exports.compiler_self_host_gap_diag_code(1024), 7004);
assert.equal(instance.exports.compiler_self_host_gap_diag_code(2048), 7005);
assert.equal(instance.exports.compiler_self_host_gap_diag_code(4096), 7006);
assert.equal(instance.exports.compiler_self_host_stage_plan(3, 8191, 8191, 0, 0), 3300);
const unsupportedSelfHostBytes = new TextEncoder().encode("pub extern c fun main() -> i32\n");
new Uint8Array(instance.exports.memory.buffer).set(unsupportedSelfHostBytes, 12288);
assert.equal(instance.exports.compiler_self_host_source_diag_code(12288, unsupportedSelfHostBytes.length, 4096, manifestBytes.length, instance.exports.memory.buffer.byteLength, 1), 7009);
assert.equal(instance.exports.compiler_self_host_diagnostic_packet(7009, 1, 4, 2), 7009121);
const moduleSourceBytes = new TextEncoder().encode("use lib\npub fun main() -> Void {}\n");
new Uint8Array(instance.exports.memory.buffer).set(moduleSourceBytes, 8192);
assert.equal(instance.exports.compiler_module_import_summary(8192, moduleSourceBytes.length, instance.exports.memory.buffer.byteLength, 2), 73);
assert.equal(instance.exports.compiler_module_import_summary(8192, moduleSourceBytes.length, instance.exports.memory.buffer.byteLength, 1), 0);
assert.equal(instance.exports.compiler_module_import_diag_code(8192, moduleSourceBytes.length, instance.exports.memory.buffer.byteLength, 1), 3003);
assert.equal(instance.exports.compiler_module_graph_reject_code(1, 1, 0, 0, 0), 3003);
assert.equal(instance.exports.compiler_module_graph_reject_code(2, 1, 1, 0, 0), 3008);
assert.equal(instance.exports.compiler_module_graph_reject_code(2, 1, 0, 1, 0), 3009);
assert.equal(instance.exports.compiler_module_graph_reject_code(2, 1, 0, 0, 1), 3010);
const scopeBytes = new TextEncoder().encode("pub shape Point { x: i32 }\npub fun main() -> Void {\n let x = 1\n let y = x\n}\nfun helper() -> Void {}\n");
new Uint8Array(instance.exports.memory.buffer).set(scopeBytes, 32768);
assert.equal(instance.exports.compiler_scope_summary(32768, scopeBytes.length, instance.exports.memory.buffer.byteLength), 40202);
assert.equal(instance.exports.compiler_name_resolution_packet(32768, scopeBytes.length, instance.exports.memory.buffer.byteLength, 0, 0), 0);
assert.equal(instance.exports.compiler_name_resolution_packet(32768, scopeBytes.length, instance.exports.memory.buffer.byteLength, 1, 0), 3003);
assert.equal(instance.exports.compiler_name_resolution_packet(32768, scopeBytes.length, instance.exports.memory.buffer.byteLength, 0, 1), 3009);
const memberNameBytes = new TextEncoder().encode("type BytePair = Pair<u8, u8>\nshape Pair<T, static N: usize> { left: T }\npub fun main() -> Void { pair.left }\n");
new Uint8Array(instance.exports.memory.buffer).set(memberNameBytes, 40960);
assert.equal(instance.exports.compiler_member_name_summary(40960, memberNameBytes.length, instance.exports.memory.buffer.byteLength), 1010201);
const typeSurfaceBytes = new TextEncoder().encode("shape Box { items: [4]u8, name: String, next: ref<Box> } enum Color { red } choice Event { quit } pub fun main(flag: Bool) -> Void { let n: usize = 1 }\n");
new Uint8Array(instance.exports.memory.buffer).set(typeSurfaceBytes, 53248);
assert.equal(instance.exports.compiler_type_surface_summary(53248, typeSurfaceBytes.length, instance.exports.memory.buffer.byteLength), 63);
const expressionSurfaceBytes = new TextEncoder().encode("let mut pair: Pair = Pair { left: 1, right: 2 } pair.left = pair.items[0]");
new Uint8Array(instance.exports.memory.buffer).set(expressionSurfaceBytes, 57344);
assert.equal(instance.exports.compiler_expression_surface_summary(57344, expressionSurfaceBytes.length, instance.exports.memory.buffer.byteLength), 63);
const controlFlowBytes = new TextEncoder().encode("if ok { check call() } while ok { return } rescue err { return }");
new Uint8Array(instance.exports.memory.buffer).set(controlFlowBytes, 61440);
assert.equal(instance.exports.compiler_control_flow_summary(61440, controlFlowBytes.length, instance.exports.memory.buffer.byteLength), 15);
const genericStaticBytes = new TextEncoder().encode("interface Readable<T> { fun read(self: ref<T>) -> i32 } shape FixedVec<T, static N: usize> { items: [N]T fun push(self: mutref<Self>, value: T) -> Void {} } fun first<T, static N: usize>(vec: ref<FixedVec<T,N>>) -> T { return vec.items[0] } pub fun main() -> Void { vec.push(1) }\n");
new Uint8Array(instance.exports.memory.buffer).set(genericStaticBytes, 45056);
assert.equal(instance.exports.compiler_generic_static_summary(45056, genericStaticBytes.length, instance.exports.memory.buffer.byteLength), 255);
const fallibilityBytes = new TextEncoder().encode("fun fail() -> Void raises { BadInput } { raise BadInput } pub fun main() -> Void raises { check fail() rescue err { return } }\n");
new Uint8Array(instance.exports.memory.buffer).set(fallibilityBytes, 47104);
assert.equal(instance.exports.compiler_fallibility_summary(47104, fallibilityBytes.length, instance.exports.memory.buffer.byteLength), 255);
assert.equal(instance.exports.compiler_fallibility_diag_code(1, 0, 0, 0), 1003);
assert.equal(instance.exports.compiler_fallibility_diag_code(0, 1, 0, 0), 1002);
assert.equal(instance.exports.compiler_fallibility_diag_code(0, 0, 0, 1), 1001);
const capabilityBytes = new TextEncoder().encode('pub fun main(world: World, net: Net) -> Void raises { let fs = world.fs() let env = std.env.get("X") let proc = std.proc.spawn("noop") check world.out.write("target web") }\n');
new Uint8Array(instance.exports.memory.buffer).set(capabilityBytes, 59392);
assert.equal(instance.exports.compiler_capability_summary(59392, capabilityBytes.length, instance.exports.memory.buffer.byteLength), 255);
assert.equal(instance.exports.compiler_capability_diag_code(1, 0), 6002);
assert.equal(instance.exports.compiler_capability_diag_code(1, 1), 0);
const ownershipBytes = new TextEncoder().encode("shape Receiver { value: i32 fun add(self: mutref<Self>) -> Void { self.value = self.value + 1 } } pub fun main() -> Void { let mut bytes: [2]u8 = [0, 0] let span: MutSpan<u8> = bytes bytes[0] = 1 let owned: owned<Receiver> = Receiver { value: 1 } consume(owned) Receiver.add(&mut owned) }\n");
new Uint8Array(instance.exports.memory.buffer).set(ownershipBytes, 6144);
assert.equal(instance.exports.compiler_ownership_summary(6144, ownershipBytes.length, instance.exports.memory.buffer.byteLength), 255);
assert.equal(instance.exports.compiler_ownership_diag_code(1, 0, 0, 0), 1009);
assert.equal(instance.exports.compiler_ownership_diag_code(0, 1, 0, 0), 3013);
assert.equal(instance.exports.compiler_ownership_diag_code(0, 0, 1, 0), 3048);
assert.equal(instance.exports.compiler_ownership_diag_code(0, 0, 0, 1), 3109);
const mirBytes = new TextEncoder().encode('pub const LIMIT: usize = 4\nshape Pair { left: i32, right: i32 }\nenum Color { red }\nchoice Result { ok: i32, bad }\npub fun main(world: World) -> i32 raises { Bad } { let local: [4]u8 = [1, 2, 3, 4] let msg: String = "ok" let span: Span<u8> = local if local[0] == 1 { return helper(span.len) } raise Bad }\n');
new Uint8Array(instance.exports.memory.buffer).set(mirBytes, 32768);
assert.equal(instance.exports.compiler_mir_lowering_summary(32768, mirBytes.length, instance.exports.memory.buffer.byteLength), 65535);
const mirHashA = instance.exports.compiler_mir_hash(32768, mirBytes.length, instance.exports.memory.buffer.byteLength, 1);
const mirHashB = instance.exports.compiler_mir_hash(32768, mirBytes.length, instance.exports.memory.buffer.byteLength, 2);
assert(mirHashA > 0);
assert(mirHashB > 0);
assert.equal(instance.exports.compiler_mir_module_order_hash(1, mirHashA, 2, mirHashB), instance.exports.compiler_mir_module_order_hash(2, mirHashB, 1, mirHashA));
assert.equal(instance.exports.compiler_mir_json_contract(65535, 1, 2), 167535);
const parseItemBytes = new TextEncoder().encode('import math\nuse lib\nconst answer: i32 = 42\nshape Point { x: i32 }\nenum Color { red }\nchoice Event { quit }\nextern c "x.h" as x\npub fun main() -> Void {}\ntest "ok" {}\n');
new Uint8Array(instance.exports.memory.buffer).set(parseItemBytes, 16384);
assert.equal(instance.exports.compiler_parse_item_summary(16384, parseItemBytes.length, instance.exports.memory.buffer.byteLength), 511);
const parseStmtBytes = new TextEncoder().encode('pub fun main() -> Void raises {\n let x = 1\n if x { check world.out.write("x") }\n while x { return }\n match x { ._ { raise Bad } }\n let y = call() rescue err { return }\n}\n');
new Uint8Array(instance.exports.memory.buffer).set(parseStmtBytes, 20480);
assert.equal(instance.exports.compiler_parse_stmt_expr_summary(20480, parseStmtBytes.length, instance.exports.memory.buffer.byteLength), 255);
const parseRootBytes = await readFile("conformance/parse/compiler-smoke.0");
new Uint8Array(instance.exports.memory.buffer).set(parseRootBytes, 24576);
assert.equal(instance.exports.compiler_parse_root_summary(24576, parseRootBytes.length, instance.exports.memory.buffer.byteLength), 1010101);
const parsePublicBytes = new TextEncoder().encode('pub const answer: i32 = 42\npub shape Point { x: i32 }\npub enum Color { red }\npub choice Event { quit }\npub fun main() -> Void {}\nfun helper() -> Void {}\n');
new Uint8Array(instance.exports.memory.buffer).set(parsePublicBytes, 28672);
assert.equal(instance.exports.compiler_parse_public_api_summary(28672, parsePublicBytes.length, instance.exports.memory.buffer.byteLength), 11111);
const parseAstBytes = new TextEncoder().encode("pub const answer: i32 = 42\nshape Point { x: i32 }\npub fun main() -> Void { let x = 1 return }\n");
new Uint8Array(instance.exports.memory.buffer).set(parseAstBytes, 49152);
assert.equal(instance.exports.compiler_parse_ast_summary(49152, parseAstBytes.length, instance.exports.memory.buffer.byteLength), 1010081);
assert.equal(instance.exports.compiler_byte_buffer_plan(8, 2), 71);
const diagSourceBytes = new TextEncoder().encode("pub fun main() -> Void {}\nmissing\n");
new Uint8Array(instance.exports.memory.buffer).set(diagSourceBytes, 36864);
assert.equal(instance.exports.compiler_diagnostic_packet_build(36864, diagSourceBytes.length, instance.exports.memory.buffer.byteLength, 26, 3003, 1, 2), 2014203);
assert.equal(instance.exports.compiler_diagnostic_detail_summary(1, 2, 3, 4, 5, 6), 123456);
assert.equal(instance.exports.compiler_related_span_summary(2, 3, 7, 11, 4), 2037114);
for (const privateSymbol of [
  "compiler_receive_manifest_counts",
  "compiler_parse_manifest_header",
  "compiler_accept_preloaded_manifest",
  "compiler_resolve_package_modules",
  "compiler_source_cache_key",
  "compiler_name_resolution_diag_code",
  "compiler_graph_json_schema_version",
  "compiler_graph_fact_counts",
  "compiler_public_interface_fingerprint",
  "compiler_public_status_fact",
  "compiler_check_primitive_binary",
  "compiler_check_data_type",
  "compiler_check_function_call",
  "compiler_check_return_type",
  "compiler_check_mutability",
  "compiler_check_move_state",
  "compiler_infer_private_error_set",
  "compiler_check_public_error_set",
  "compiler_check_target_capability",
  "compiler_target_capability_fact",
  "compiler_target_object_format",
  "compiler_target_backend_selection",
  "compiler_deny_target_runtime_feature",
  "compiler_checker_diag_packet_code",
  "compiler_mir_type_kind_count",
  "compiler_mir_value_kind_count",
  "compiler_mir_instr_kind_count",
  "compiler_lower_checked_ast_node",
  "compiler_mir_json_schema_version",
  "compiler_mir_fixture_hash",
  "compiler_mir_order_independent_hash",
  "compiler_wasm_section_kind_count",
  "compiler_wasm_core_section_score",
  "compiler_wasm_emit_feature",
  "compiler_wasm_deterministic_hash",
]) {
  assert.equal(instance.exports[privateSymbol], undefined, `${privateSymbol} should stay private until it is a real browser compiler API`);
}

const helloBuild = await execFileAsync("bin/zero", [
  "build",
  "--json",
  "--emit",
  "wasm",
  "--target",
  "wasm32-web",
  "examples/hello.0",
  "--out",
  join(outDir, "hello-web"),
]);
const helloBody = JSON.parse(helloBuild.stdout);
assert.equal(helloBody.generatedCBytes, 0);
assert.equal(helloBody.objectBackend.objectEmission.path, "direct-wasm");
assert.equal(helloBody.objectBackend.targetFacts.fallbackPolicy, "explicit-direct-never-c-bridge");
assert.equal(helloBody.selfHostRouting.cBridge.required, false);
assert.equal(helloBody.selfHostRouting.cBridge.explicitDirectFallback, "never-c-bridge");
const helloBytes = await readFile(helloBody.artifactPath);
assert.equal(helloBytes[0], 0);
assert.equal(helloBytes[1], 0x61);
assert.equal(helloBytes[2], 0x73);
assert.equal(helloBytes[3], 0x6d);
const helloArtifact = Buffer.from(helloBytes);
assert(helloArtifact.includes(Buffer.from("wasi_snapshot_preview1")));
assert(helloArtifact.includes(Buffer.from("fd_write")));
let helloInstance;
const helloWrites = [];
const helloImports = {
  wasi_snapshot_preview1: {
    fd_write(_fd, iovs, iovsLen, nwritten) {
      const view = new DataView(helloInstance.exports.memory.buffer);
      let total = 0;
      for (let i = 0; i < iovsLen; i += 1) {
        const base = iovs + i * 8;
        const ptr = view.getUint32(base, true);
        const len = view.getUint32(base + 4, true);
        helloWrites.push(new Uint8Array(helloInstance.exports.memory.buffer, ptr, len));
        total += len;
      }
      view.setUint32(nwritten, total, true);
      return 0;
    },
  },
};
helloInstance = (await WebAssembly.instantiate(helloBytes, helloImports)).instance;
helloInstance.exports.main();
const helloText = new TextDecoder().decode(Buffer.concat(helloWrites.map((chunk) => Buffer.from(chunk))));
assert.equal(helloText, "hello from zero\n");

console.log(JSON.stringify({
  ok: true,
  artifactBytes: stageA.bytes.byteLength,
  compileMs: Math.round(compileMs),
  deterministic: true,
}, null, 2));
console.log("browser compiler hardening ok");
