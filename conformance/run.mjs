#!/usr/bin/env node
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { promisify } from "node:util";
import { jsonByteTokenCount } from "../scripts/json-byte-token-count.mjs";

if (process.env.ZERO_NATIVE_TEST_SANDBOX !== "1" && process.env.ZERO_NATIVE_TEST_ALLOW_LOCAL !== "1") {
  console.error("conformance emits native test artifacts; run `npm run conformance` for Vercel Sandbox execution or set ZERO_NATIVE_TEST_ALLOW_LOCAL=1 to opt into local artifacts.");
  process.exit(1);
}

const execFileAsync = promisify(execFile);
const zero = "bin/zero";
const outDir = ".zero/conformance";
const canRunLinuxMuslX64 = process.platform === "linux" && process.arch === "x64";
const runnableDirectTarget =
  process.platform === "darwin" && process.arch === "arm64" ? "darwin-arm64" :
  process.platform === "linux" && process.arch === "x64" ? "linux-musl-x64" :
  null;

function runnableExeArgs(input, out) {
  if (!runnableDirectTarget) return null;
  return ["build", "--emit", "exe", "--target", runnableDirectTarget, input, "--out", out];
}

await mkdir(outDir, { recursive: true });

async function instantiateJsonRuntimeWasm(bytes) {
  let instance;
  const imports = {
    zero_runtime: {
      zero_json_parse_bytes(ptr, len) {
        try {
          return jsonByteTokenCount(new Uint8Array(instance.exports.memory.buffer, ptr, len));
        } catch {
          return -1n;
        }
      },
    },
  };
  const instantiated = await WebAssembly.instantiate(bytes, imports);
  instance = instantiated.instance;
  return instantiated;
}

async function assertBoundsTrap(fixture, name) {
  const out = `${outDir}/${name}`;
  const build = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", fixture, "--out", out]).catch((error) => error);
  if (build.code) {
    const body = JSON.parse(build.stdout);
    assert.equal(body.diagnostics?.[0]?.code, "CGEN004");
    return;
  }
  const body = JSON.parse(build.stdout);
  assert.equal(body.generatedCBytes, 0);
  if (!canRunLinuxMuslX64) return;
  const failedRun = await execFileAsync(out, []).catch((error) => error);
  if (failedRun.stderr) assert.match(failedRun.stderr, /zero bounds check failed/);
}

async function assertDirectRuntimeOrUnsupported(fixture, name, expected) {
  const out = `${outDir}/${name}`;
  const build = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", fixture, "--out", out]).catch((error) => error);
  if (build.code) {
    const body = JSON.parse(build.stdout);
    assert.equal(body.diagnostics?.[0]?.code, "CGEN004");
    return;
  }

  const body = JSON.parse(build.stdout);
  assert.equal(body.generatedCBytes, 0);
  assert.equal(body.legacy, false);
  if (!canRunLinuxMuslX64) return;
  const run = await execFileAsync(out, expected.args ?? [], expected.env ? { env: { ...process.env, ...expected.env } } : {}).catch((error) => error);
  if (run.code || run.signal) return;
  if (expected.stdout instanceof RegExp) assert.match(run.stdout, expected.stdout);
  else assert.equal(run.stdout, expected.stdout);
  if (expected.stderr !== undefined) assert.equal(run.stderr, expected.stderr);
  if (expected.file) assert.equal(await readFile(`${outDir}/${expected.file.name}`, "utf8"), expected.file.text);
}

async function assertElf64Object(path, exportedName) {
  const bytes = await readFile(path);
  assert.equal(bytes[0], 0x7f);
  assert.equal(bytes[1], 0x45);
  assert.equal(bytes[2], 0x4c);
  assert.equal(bytes[3], 0x46);
  assert.equal(bytes[4], 2);
  assert.equal(bytes[5], 1);
  assert.equal(bytes.readUInt16LE(16), 1);
  assert.equal(bytes.readUInt16LE(18), 62);
  const shoff = Number(bytes.readBigUInt64LE(40));
  const shentsize = bytes.readUInt16LE(58);
  const shnum = bytes.readUInt16LE(60);
  const shstrndx = bytes.readUInt16LE(62);
  const cstr = (offset) => {
    let end = offset;
    while (end < bytes.length && bytes[end] !== 0) end++;
    return bytes.toString("utf8", offset, end);
  };
  const shstrHeader = shoff + shstrndx * shentsize;
  const shstrOffset = Number(bytes.readBigUInt64LE(shstrHeader + 24));
  const sections = new Map();
  for (let i = 0; i < shnum; i++) {
    const header = shoff + i * shentsize;
    sections.set(cstr(shstrOffset + bytes.readUInt32LE(header)), {
      index: i,
      offset: Number(bytes.readBigUInt64LE(header + 24)),
      size: Number(bytes.readBigUInt64LE(header + 32)),
      link: bytes.readUInt32LE(header + 40),
      entsize: Number(bytes.readBigUInt64LE(header + 56)),
    });
  }
  assert(sections.get(".text")?.size > 0);
  const symtab = sections.get(".symtab");
  assert(symtab);
  const strtab = [...sections.values()].find((section) => section.index === symtab.link);
  assert(strtab);
  let sawExport = false;
  for (let offset = symtab.offset; offset < symtab.offset + symtab.size; offset += symtab.entsize) {
    const name = cstr(strtab.offset + bytes.readUInt32LE(offset));
    const info = bytes[offset + 4];
    const shndx = bytes.readUInt16LE(offset + 6);
    const size = Number(bytes.readBigUInt64LE(offset + 16));
    if (name === exportedName && info === 0x12 && shndx === sections.get(".text").index && size > 0) sawExport = true;
  }
  assert(sawExport);
  return { bytes, sections };
}

async function assertElfAarch64Object(path, exportedName) {
  const bytes = await readFile(path);
  assert.equal(bytes[0], 0x7f);
  assert.equal(bytes[1], 0x45);
  assert.equal(bytes[2], 0x4c);
  assert.equal(bytes[3], 0x46);
  assert.equal(bytes[4], 2);
  assert.equal(bytes[5], 1);
  assert.equal(bytes.readUInt16LE(16), 1);
  assert.equal(bytes.readUInt16LE(18), 183);
  assert(bytes.includes(Buffer.from([0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6])));
  assert(bytes.includes(Buffer.concat([Buffer.from(exportedName), Buffer.from([0])])));
}

async function assertElf64Executable(path) {
  const bytes = await readFile(path);
  assert.equal(bytes[0], 0x7f);
  assert.equal(bytes[1], 0x45);
  assert.equal(bytes[2], 0x4c);
  assert.equal(bytes[3], 0x46);
  assert.equal(bytes[4], 2);
  assert.equal(bytes[5], 1);
  assert.equal(bytes.readUInt16LE(16), 2);
  assert.equal(bytes.readUInt16LE(18), 62);
  assert(Number(bytes.readBigUInt64LE(24)) > 0);
  assert.equal(Number(bytes.readBigUInt64LE(32)), 64);
  assert.equal(bytes.readUInt16LE(54), 56);
  assert.equal(bytes.readUInt16LE(56), 1);
  const phoff = Number(bytes.readBigUInt64LE(32));
  assert.equal(bytes.readUInt32LE(phoff), 1);
  assert.equal(bytes.readUInt32LE(phoff + 4), 5);
  assert(Number(bytes.readBigUInt64LE(phoff + 32)) > 0);
  assert.equal(Number(bytes.readBigUInt64LE(phoff + 32)), Number(bytes.readBigUInt64LE(phoff + 40)));
}

async function assertElfAarch64Executable(path) {
  const bytes = await readFile(path);
  assert.equal(bytes[0], 0x7f);
  assert.equal(bytes[1], 0x45);
  assert.equal(bytes[2], 0x4c);
  assert.equal(bytes[3], 0x46);
  assert.equal(bytes[4], 2);
  assert.equal(bytes[5], 1);
  assert.equal(bytes.readUInt16LE(16), 2);
  assert.equal(bytes.readUInt16LE(18), 183);
  assert.equal(bytes.readUInt16LE(54), 56);
  assert.equal(bytes.readUInt16LE(56), 1);
  assert(bytes.includes(Buffer.from([0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6])));
  assert(bytes.includes(Buffer.from([0xa8, 0x0b, 0x80, 0xd2, 0x01, 0x00, 0x00, 0xd4])));
}

async function assertMachOArm64Object(path, exportedName) {
  const bytes = await readFile(path);
  assert.equal(bytes.readUInt32LE(0), 0xfeedfacf);
  assert.equal(bytes.readUInt32LE(4), 0x0100000c);
  assert.equal(bytes.readUInt32LE(12), 1);
  assert(bytes.includes(Buffer.concat([Buffer.from(`_${exportedName}`), Buffer.from([0])])));
  return bytes;
}

async function assertMachOArm64Executable(path) {
  const bytes = await readFile(path);
  assert.equal(bytes.readUInt32LE(0), 0xfeedfacf);
  assert.equal(bytes.readUInt32LE(4), 0x0100000c);
  assert.equal(bytes.readUInt32LE(12), 2);
  const ncmds = bytes.readUInt32LE(16);
  let sawUuid = false;
  for (let offset = 32, i = 0; i < ncmds; i++) {
    const cmd = bytes.readUInt32LE(offset);
    const cmdsize = bytes.readUInt32LE(offset + 4);
    assert(cmdsize >= 8);
    assert(offset + cmdsize <= bytes.length);
    if (cmd === 0x1b) {
      assert.equal(cmdsize, 24);
      assert(!bytes.subarray(offset + 8, offset + 24).every((byte) => byte === 0));
      sawUuid = true;
    }
    offset += cmdsize;
  }
  assert(sawUuid);
  assert(bytes.includes(Buffer.from("/usr/lib/dyld")));
  assert(bytes.includes(Buffer.from("/usr/lib/libSystem.B.dylib")));
  assert(bytes.includes(Buffer.from("zero-direct")));
  return bytes;
}

async function assertCoffX64Object(path, exportedName) {
  const bytes = await readFile(path);
  assert.equal(bytes.readUInt16LE(0), 0x8664);
  assert(bytes.readUInt16LE(2) >= 1);
  assert(bytes.readUInt32LE(8) > 0);
  assert(bytes.includes(Buffer.concat([Buffer.from(exportedName), Buffer.from([0])])));
  return bytes;
}

async function assertPeCoffX64Executable(path) {
  const bytes = await readFile(path);
  assert.equal(bytes[0], 0x4d);
  assert.equal(bytes[1], 0x5a);
  const peOffset = bytes.readUInt32LE(0x3c);
  assert.equal(bytes.toString("ascii", peOffset, peOffset + 4), "PE\u0000\u0000");
  assert.equal(bytes.readUInt16LE(peOffset + 4), 0x8664);
  assert.equal(bytes.readUInt16LE(peOffset + 24), 0x20b);
  assert.equal(bytes.readUInt16LE(peOffset + 92), 3);
  assert(bytes.includes(Buffer.from("KERNEL32.dll")));
  assert(bytes.includes(Buffer.from("ExitProcess")));
  assert(bytes.includes(Buffer.from("WriteFile")));
  return bytes;
}

for (const fixture of [
  "conformance/run/pass/hello.0",
  "conformance/native/pass/params.0",
  "conformance/native/pass/shape.0",
  "conformance/native/pass/primitive-stdlib.0",
  "conformance/native/pass/variants-defer-stdlib.0",
  "conformance/native/pass/defer-return-raise-nested.0",
  "conformance/native/pass/payload-match.0",
  "conformance/native/pass/break-continue.0",
  "conformance/native/pass/for-range.0",
  "conformance/native/pass/match-payload-binding.0",
  "conformance/native/pass/choice-payload-reference-return.0",
  "conformance/native/pass/choice-match-payload-reference-origin.0",
  "conformance/native/pass/choice-match-payload-return-origin.0",
  "conformance/native/pass/match-choice-fallback.0",
  "conformance/native/pass/null-maybe.0",
  "conformance/native/pass/meta-typed-target-type.0",
  "conformance/native/pass/std-args.0",
  "conformance/native/pass/std-env.0",
  "conformance/native/pass/std-fs.0",
  "conformance/native/pass/std-fs-bytes.0",
  "conformance/native/pass/std-fs-resource.0",
  "conformance/native/pass/std-fs-readall.0",
  "conformance/native/pass/std-fs-polish.0",
  "conformance/native/pass/std-fs-breadth.0",
  "conformance/native/pass/std-path-io-breadth.0",
  "conformance/native/pass/std-net-http-breadth.0",
  "conformance/native/pass/std-http-metadata-neutral.0",
  "conformance/native/pass/std-http-fetch.0",
  "conformance/native/pass/std-http-errors.0",
  "conformance/native/pass/std-http-response-helpers.0",
  "conformance/native/pass/std-data-formats.0",
  "conformance/native/pass/std-json-bytes.0",
  "conformance/native/pass/std-json-inline-bytes.0",
  "conformance/native/pass/std-json-duplicate-keys.0",
  "conformance/native/pass/std-json-allocator-capacity.0",
  "conformance/native/pass/std-platform-basics.0",
  "conformance/native/pass/std-mem-arrays.0",
  "conformance/native/pass/array-repeat-literal.0",
  "conformance/native/pass/array-repeat-record-field.0",
  "conformance/native/pass/integer-widths.0",
  "conformance/native/pass/std-codec-widths.0",
  "conformance/native/pass/parse-integers.0",
  "conformance/native/pass/explicit-casts.0",
  "conformance/native/pass/float-char-casts.0",
  "conformance/native/pass/radix-suffix-literals.0",
  "conformance/native/pass/char-literals.0",
  "conformance/native/pass/float-primitives.0",
  "conformance/native/pass/wrapping-saturating-arithmetic.0",
  "conformance/native/pass/maybe-error-flow.0",
  "conformance/native/pass/match-scalar-guards.0",
  "conformance/native/pass/indexing-primitives.0",
  "conformance/native/pass/checked-bounds-get.0",
  "conformance/native/pass/check-maybe-fallibility.0",
  "conformance/native/pass/fallibility-error-sets.0",
  "conformance/native/pass/checked-fallible-wrapper.0",
  "conformance/native/pass/checked-fallible-static-method.0",
  "conformance/native/pass/checked-fallible-interface-method.0",
  "conformance/native/pass/fallibility-check-value.0",
  "conformance/native/pass/rescue-check.0",
  "conformance/native/pass/std-fs-fallible.0",
  "conformance/native/pass/std-fs-fallible-resources.0",
  "conformance/native/pass/std-cli-helpers.0",
  "conformance/native/pass/std-mem-copy-fill.0",
  "conformance/native/pass/const-layout.0",
  "conformance/native/pass/c-abi-export.0",
  "conformance/native/pass/range-slices.0",
  "conformance/native/pass/generic-spans.0",
  "conformance/native/pass/open-ended-slices.0",
  "conformance/native/pass/string-slices.0",
  "conformance/native/pass/string-byte-ergonomics.0",
  "conformance/native/pass/indexed-mutation.0",
  "conformance/native/pass/nested-lvalues.0",
  "conformance/native/pass/mutable-spans.0",
  "conformance/native/pass/mutref-indexed-lvalues.0",
  "conformance/native/pass/generic-mem.0",
  "conformance/native/pass/generic-function-basic.0",
  "conformance/native/pass/generic-nested-calls.0",
  "conformance/native/pass/generic-specialization-reuse.0",
  "conformance/native/pass/generic-shape-basic.0",
  "conformance/native/pass/generic-shape-multi.0",
  "conformance/native/pass/generic-shape-methods.0",
  "conformance/native/pass/generic-shape-nested-defaults-alias.0",
  "conformance/native/pass/generic-constructor-expected.0",
  "conformance/native/pass/generic-expected-return.0",
  "conformance/native/pass/generic-literals-arrays.0",
  "conformance/native/pass/receiver-method-calls.0",
  "conformance/native/pass/constructors-defaults.0",
  "conformance/native/pass/static-value-params.0",
  "conformance/native/pass/static-value-types-const-expr.0",
  "conformance/native/pass/compile-time-v1.0",
  "conformance/native/pass/static-interface-basic.0",
  "conformance/native/pass/static-interface-mutref.0",
  "conformance/native/pass/static-interface-static-param.0",
  "conformance/native/pass/top-level-const.0",
  "conformance/native/pass/const-arithmetic.0",
  "conformance/native/pass/type-alias-basic.0",
  "conformance/native/pass/static-method-namespace.0",
  "conformance/native/pass/match-fallback.0",
  "conformance/native/pass/memory-types.0",
  "conformance/native/pass/owned-transfer.0",
  "conformance/native/pass/owned-drop-cleanup.0",
  "conformance/native/pass/owned-drop-move-suppressed.0",
  "conformance/native/pass/borrow-primitives.0",
  "conformance/native/pass/borrow-field-independent-assignment.0",
  "conformance/native/pass/borrow-aggregate-reassignment-same-origin.0",
  "conformance/native/pass/receiver-return-field-origin-clears-other-field.0",
  "conformance/native/pass/borrow-return-param-field-subpath.0",
  "conformance/native/pass/borrow-return-explicit-ref-field-origin.0",
  "conformance/native/pass/borrow-unreachable-return-origin.0",
  "conformance/native/pass/borrow-unreachable-loop-return-origin.0",
  "conformance/native/pass/borrow-unreachable-if-return-origin.0",
  "conformance/native/pass/borrow-unreachable-match-return-origin.0",
  "conformance/native/pass/borrow-unreachable-raise-return-origin.0",
  "conformance/native/pass/borrow-return-ref-alias-field.0",
  "conformance/native/pass/assignment-rhs-side-effect-clears-old-origin.0",
  "conformance/native/pass/shadowed-mutref-side-effect-clears-old-origin.0",
  "conformance/native/pass/mutref-alias-assignment-clears-old-origin.0",
  "conformance/native/pass/mutref-alias-assignment-same-origin.0",
  "conformance/native/pass/function-mutref-reference-store.0",
  "conformance/native/pass/generic-mutref-reference-store.0",
  "conformance/native/pass/receiver-method-reference-store.0",
  "conformance/native/pass/static-interface-mutref-reference-store.0",
  "conformance/native/pass/static-interface-return-reference-origin.0",
  "conformance/native/pass/borrow-return-param-ref.0",
  "conformance/native/pass/borrow-assignment-same-origin.0",
  "conformance/native/pass/borrow-shadowed-root-reassignment.0",
  "conformance/native/pass/borrow-branch-reassignment.0",
  "conformance/native/pass/branch-overwrite-away-reference-origin.0",
  "conformance/native/pass/shape-field-reference-reassignment-clears-origin.0",
  "conformance/native/pass/index-reference-assignment-clears-origin.0",
  "conformance/native/pass/allocator-primitives.0",
  "conformance/native/pass/std-mem-arena.0",
  "conformance/native/pass/std-mem-collections.0",
  "conformance/native/pass/owned-byte-buffer.0",
  "conformance/check/pass/generic-function-basic.0",
  "conformance/native/pass/generic-nested-calls.0",
  "conformance/native/pass/generic-specialization-reuse.0",
  "conformance/check/pass/generic-shape-basic.0",
  "conformance/check/pass/generic-shape-multi.0",
  "conformance/check/pass/generic-shape-methods.0",
  "conformance/native/pass/generic-constructor-expected.0",
  "conformance/native/pass/generic-literals-arrays.0",
  "conformance/check/pass/receiver-method-calls.0",
  "conformance/check/pass/shape-field-defaults.0",
  "conformance/check/pass/static-value-params.0",
  "conformance/check/pass/static-interface-basic.0",
  "conformance/native/pass/static-interface-mutref.0",
  "conformance/native/pass/static-interface-static-param.0",
  "conformance/check/pass/top-level-const.0",
  "conformance/check/pass/const-arithmetic.0",
  "conformance/check/pass/type-alias-basic.0",
  "conformance/check/pass/static-method-namespace.0",
  "conformance/check/pass/fmt-core-usability.0",
  "conformance/check/pass/c-header-import.0",
  "conformance/check/pass/match-fallback.0",
  "conformance/native/pass/match-choice-fallback.0",
  "conformance/check/pass/memory-types.0",
  "conformance/check/pass/checker-type-forms.0",
  "conformance/check/pass/package",
  "conformance/check/pass/imports",
  "examples/memory-package",
  "examples/const-arithmetic.0",
  "examples/generic-pair.0",
  "examples/fixed-vec.0",
  "examples/static-value-params.0",
  "examples/compile-time-v1.0",
  "examples/type-alias.0",
  "examples/static-method.0",
  "examples/static-interface.0",
  "examples/ownership-cleanup.0",
]) {
  await execFileAsync(zero, ["check", fixture]);
}

const checkJsonSuccess = await execFileAsync(zero, ["check", "--json", "conformance/native/pass/explicit-casts.0"]);
const checkJsonSuccessBody = JSON.parse(checkJsonSuccess.stdout);
assert.equal(checkJsonSuccessBody.ok, true);
assert.equal(checkJsonSuccessBody.diagnostics.length, 0);
assert.match(checkJsonSuccessBody.sourceFile, /explicit-casts\.0$/);
assert.ok(checkJsonSuccessBody.metaCache.sourceHash);
assert.ok(checkJsonSuccessBody.metaCache.targetHash);
assert.ok(checkJsonSuccessBody.metaCache.manifestHash);
assert(checkJsonSuccessBody.compilerPhases.some((item) => item.name === "parse" && item.cacheable === true));
assert(checkJsonSuccessBody.compilerPhases.some((item) => item.name === "check" && item.cacheable === true));
assert(checkJsonSuccessBody.compilerCaches.some((item) => item.name === "parseTree" && item.stored === true && item.invalidatesOn === "source"));
assert(checkJsonSuccessBody.compilerCaches.some((item) => item.name === "interface" && item.invalidatesOn.includes("public symbols")));
assert.ok(checkJsonSuccessBody.incrementalInvalidation.targetDependency);
assert.equal(checkJsonSuccessBody.incrementalInvalidation.profileDependency, "release");
assert.equal(checkJsonSuccessBody.incrementalInvalidation.recheckStrategy, "fingerprint changed modules and dependent bodies");

const agentSurfaceClassification = JSON.parse(await readFile("conformance/agent-surface/classification.json", "utf8"));
assert.equal(agentSurfaceClassification.schema, 1);
assert.deepEqual(agentSurfaceClassification.fixtures.map((item) => item.id), [
  "generic-type-shadowing",
  "builtin-generic-type-shadowing",
  "shape-generic-type-shadowing",
  "shape-generic-self-shadowing",
  "method-generic-type-shadowing",
  "method-generic-outer-shadowing",
  "method-generic-self-shadowing",
  "borrow-lexical-lifetime",
  "unresolved-package-import",
  "malformed-use-current",
  "owned-drop-direct-backend-unsupported",
]);

async function assertAgentSurfaceGenericShadowing(path, actualPattern) {
  const result = await execFileAsync(zero, ["check", "--json", path]).catch((error) => error);
  assert.notEqual(result.code, 0);
  const body = JSON.parse(result.stdout);
  assert.equal(body.diagnostics[0].code, "NAM004");
  assert.match(body.diagnostics[0].message, /generic type parameter shadows/);
  assert.match(body.diagnostics[0].actual, actualPattern);
  assert.match(body.diagnostics[0].help, /rename/);
}

await assertAgentSurfaceGenericShadowing("conformance/agent-surface/fixtures/generic-type-shadowing.0", /already names a shape/);
await assertAgentSurfaceGenericShadowing("conformance/agent-surface/fixtures/builtin-generic-type-shadowing.0", /already names a built-in type/);
await assertAgentSurfaceGenericShadowing("conformance/agent-surface/fixtures/shape-generic-type-shadowing.0", /already names a shape/);
await assertAgentSurfaceGenericShadowing("conformance/agent-surface/fixtures/shape-generic-self-shadowing.0", /reserved for method Self types/);
await assertAgentSurfaceGenericShadowing("conformance/agent-surface/fixtures/method-generic-type-shadowing.0", /already names a shape/);
await assertAgentSurfaceGenericShadowing("conformance/agent-surface/fixtures/method-generic-outer-shadowing.0", /outer generic scope/);
await assertAgentSurfaceGenericShadowing("conformance/agent-surface/fixtures/method-generic-self-shadowing.0", /reserved for method Self types/);

const agentSurfaceBorrowLifetime = await execFileAsync(zero, ["check", "--json", "conformance/agent-surface/fixtures/borrow-lexical-lifetime.0"]).catch((error) => error);
assert.notEqual(agentSurfaceBorrowLifetime.code, 0);
const agentSurfaceBorrowLifetimeBody = JSON.parse(agentSurfaceBorrowLifetime.stdout);
assert.equal(agentSurfaceBorrowLifetimeBody.diagnostics[0].code, "BOR001");
assert.match(agentSurfaceBorrowLifetimeBody.diagnostics[0].actual, /data already has shared borrow/);

const agentSurfaceUnresolvedImport = await execFileAsync(zero, ["check", "--json", "conformance/agent-surface/fixtures/unresolved-package-import"]).catch((error) => error);
assert.notEqual(agentSurfaceUnresolvedImport.code, 0);
const agentSurfaceUnresolvedImportBody = JSON.parse(agentSurfaceUnresolvedImport.stdout);
assert.equal(agentSurfaceUnresolvedImportBody.diagnostics[0].code, "IMP001");
assert.match(agentSurfaceUnresolvedImportBody.diagnostics[0].expected, /src\/missing\.utility\.0/);

const agentSurfaceMalformedUse = await execFileAsync(zero, ["check", "--json", "conformance/agent-surface/fixtures/malformed-use-current.0"]);
const agentSurfaceMalformedUseBody = JSON.parse(agentSurfaceMalformedUse.stdout);
assert.equal(agentSurfaceMalformedUseBody.ok, true);
assert.equal(agentSurfaceMalformedUseBody.diagnostics.length, 0);

const agentSurfaceOwnedDropCheck = await execFileAsync(zero, ["check", "--json", "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.0"]);
const agentSurfaceOwnedDropCheckBody = JSON.parse(agentSurfaceOwnedDropCheck.stdout);
assert.equal(agentSurfaceOwnedDropCheckBody.ok, true);

async function assertAgentSurfaceOwnedDropUnsupported(target, emit, outName, expectedPattern, nativeTarget = true) {
  const build = await execFileAsync(zero, [
    "build",
    "--json",
    "--emit",
    emit,
    "--target",
    target,
    "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.0",
    "--out",
    `${outDir}/${outName}`,
  ]).catch((error) => error);
  assert.notEqual(build.code, 0);
  const body = JSON.parse(build.stdout);
  const diagnostic = body.diagnostics[0];
  assert.equal(diagnostic.code, "CGEN004");
  assert.match(diagnostic.expected, expectedPattern);
  assert.equal(diagnostic.actual, "owned<Tracked>");
  if (nativeTarget) assert(!/wasm/i.test(diagnostic.message));
  else assert.match(diagnostic.expected, /wasm/i);
}

await assertAgentSurfaceOwnedDropUnsupported("linux-musl-x64", "obj", "agent-surface-owned-drop-elf.o", /ELF64/);
await assertAgentSurfaceOwnedDropUnsupported("darwin-arm64", "obj", "agent-surface-owned-drop-macho.o", /Mach-O/);
await assertAgentSurfaceOwnedDropUnsupported("win32-x64.exe", "obj", "agent-surface-owned-drop-coff.obj", /COFF/);
await assertAgentSurfaceOwnedDropUnsupported("wasm32-web", "wasm", "agent-surface-owned-drop.wasm", /wasm/, false);

const compileTimeJson = await execFileAsync(zero, ["check", "--json", "conformance/native/pass/compile-time-v1.0"]);
const compileTimeBody = JSON.parse(compileTimeJson.stdout);
assert.equal(compileTimeBody.ok, true);
assert.equal(compileTimeBody.compileTime.deterministic, true);
assert.equal(compileTimeBody.compileTime.sandbox.filesystem, "denied");
assert.equal(compileTimeBody.compileTime.sandbox.network, "denied");
assert.equal(compileTimeBody.compileTime.limits.maxDepth, 64);
assert.equal(compileTimeBody.compileTime.limits.maxSteps, 1024);
assert.equal(compileTimeBody.compileTime.cacheKeyInputs.algorithm, "fnv1a64-zero-meta-v1");
assert.ok(compileTimeBody.compileTime.cacheKeyInputs.sourceHash);
assert.ok(compileTimeBody.compileTime.meta.supportedFacts.includes("target.abi"));
assert.ok(compileTimeBody.compileTime.meta.supportedFacts.includes("fieldType"));
assert.ok(compileTimeBody.compileTime.meta.supportedFacts.includes("hasEnumCase"));
assert.ok(compileTimeBody.compileTime.staticValues.supported.includes("Bool"));
assert.ok(compileTimeBody.compileTime.staticValues.supported.includes("enum"));
assert.equal(compileTimeBody.compileTime.staticValues.runtimeRegistries, false);
assert.equal(compileTimeBody.compileTime.staticValues.reflectionTables, false);
assert.equal(compileTimeBody.compileTime.reflection.compileTimeOnly, true);
assert.equal(compileTimeBody.compileTime.typedBuilders.status, "limited-v1");
assert.equal(compileTimeBody.compileTime.typedBuilders.rawTokenStrings, false);

const compileTimeGraph = await execFileAsync(zero, ["graph", "--json", "conformance/native/pass/compile-time-v1.0"]);
const compileTimeGraphBody = JSON.parse(compileTimeGraph.stdout);
assert.equal(compileTimeGraphBody.compileTime.deterministic, true);
const readGateGraph = compileTimeGraphBody.functions.find((item) => item.name === "readGate");
assert.ok(readGateGraph.staticParams.some((item) => item.name === "enabledFlag" && item.kind === "bool"));
assert.ok(readGateGraph.staticParams.some((item) => item.name === "selectedMode" && item.kind === "enum"));
const gateGraph = compileTimeGraphBody.shapes.find((item) => item.name === "Gate");
assert.ok(gateGraph.staticParams.some((item) => item.name === "enabledFlag" && item.kind === "bool"));
assert.ok(gateGraph.staticParams.some((item) => item.name === "selectedMode" && item.kind === "enum"));

const buildJsonM6 = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", "--release", "tiny", "examples/hello.0", "--out", `${outDir}/m6-hello`]);
const buildJsonM6Body = JSON.parse(buildJsonM6.stdout);
assert.equal(buildJsonM6Body.legacy, false);
assert.equal(buildJsonM6Body.legacyBackend, null);
assert.equal(buildJsonM6Body.generatedCBytes, 0);
assert.equal(buildJsonM6Body.profileSemantics.canonical, "tiny");
assert.equal(buildJsonM6Body.profileSemantics.panicPolicy, "abort");
assert.equal(buildJsonM6Body.profileSemantics.runtimeMetadataPolicy, "minimum");
assert.equal(buildJsonM6Body.profileSemantics.profileKey, "tiny");
assert.equal(buildJsonM6Body.profileSemantics.unwindPolicy, "no-unwind-abort");
assert.equal(buildJsonM6Body.profileSemantics.profileBudget.generatedCBytes, 0);
assert.equal(buildJsonM6Body.profileBudget.helperBudgetPolicy, "pay-as-used-minimum-runtime");
assert(buildJsonM6Body.profileCatalog.some((item) => item.canonical === "debug" && item.debugInfo === true));
assert(buildJsonM6Body.profileCatalog.some((item) => item.canonical === "release-fast" && item.boundsPolicy.includes("optimizer")));
assert(buildJsonM6Body.profileCatalog.some((item) => item.canonical === "audit" && item.runtimeMetadataPolicy === "maximum"));
assert.equal(buildJsonM6Body.objectBackend.internalIr.callRepresentation, "same-object direct calls for supported direct subsets");
assert.equal(buildJsonM6Body.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(buildJsonM6Body.objectBackend.objectEmission.symbols, true);
assert.ok(buildJsonM6Body.objectBackend.linking.linkerFlavor);
assert.equal(buildJsonM6Body.objectBackend.linking.stripArtifacts, false);

for (const [requestedProfile, canonicalProfile, profileKey] of [
  ["debug", "debug", "debug"],
  ["fast", "release-fast", "fast"],
  ["small", "release-small", "small"],
]) {
  const profileBuild = await execFileAsync(zero, ["build", "--json", "--profile", requestedProfile, "--target", "linux-musl-x64", "examples/hello.0", "--out", `${outDir}/m25-${requestedProfile}-hello`]);
  const profileBody = JSON.parse(profileBuild.stdout);
  assert.equal(profileBody.generatedCBytes, 0);
  assert.equal(profileBody.profileSemantics.canonical, canonicalProfile);
  assert.equal(profileBody.profileSemantics.profileKey, profileKey);
  assert.equal(profileBody.profileSemantics.profileBudget.generatedCBytes, 0);
  assert.equal(profileBody.profileBudget.cBridgeFallback, false);
}

const profileSize = await execFileAsync(zero, ["size", "--json", "--profile", "debug", "--target", "linux-musl-x64", "examples/memory-primitives.0"]);
const profileSizeBody = JSON.parse(profileSize.stdout);
assert.equal(profileSizeBody.generatedCBytes, 0);
assert.equal(profileSizeBody.profileSemantics.profileKey, "debug");
assert.equal(profileSizeBody.sizeBreakdown.profileKey, "debug");
assert(profileSizeBody.sizeBreakdown.functions.some((item) => item.name === "main" && item.retainedBy === "entry point"));
assert(profileSizeBody.sizeBreakdown.sections.some((item) => item.name === "text" && item.retainedBy.includes("retained functions")));
assert(Array.isArray(profileSizeBody.sizeBreakdown.literals.items));
assert(Array.isArray(profileSizeBody.sizeBreakdown.stdlibHelpers));
assert(Array.isArray(profileSizeBody.sizeBreakdown.imports));
assert(Array.isArray(profileSizeBody.sizeBreakdown.runtimeShims));
assert(profileSizeBody.sizeBreakdown.debugMetadata.bytes > 0);
assert(profileSizeBody.retentionReasons.some((item) => item.kind === "function"));
assert(profileSizeBody.optimizationHints.some((item) => item.id === "profile-debug-metadata"));
assert.equal(profileSizeBody.profileBudget.debugMetadataAllowed, true);

const wasiRuntimeSize = await execFileAsync(zero, ["size", "--json", "--target", "wasm32-wasi", "conformance/native/pass/std-fs-resource.0"]);
const wasiRuntimeSizeBody = JSON.parse(wasiRuntimeSize.stdout);
assert.equal(wasiRuntimeSizeBody.portableRuntime.runtimeKind, "wasi");
assert.equal(wasiRuntimeSizeBody.portableRuntime.providerSpecificDeployment, false);
assert.equal(wasiRuntimeSizeBody.portableRuntime.hostedDeployment, "out-of-scope");
assert(wasiRuntimeSizeBody.portableRuntime.imports.functions.includes("path_open"));
assert.equal(wasiRuntimeSizeBody.portableRuntime.memoryFloor.floorBytes, 65536);

const webRuntimeSize = await execFileAsync(zero, ["size", "--json", "--target", "wasm32-web", "conformance/native/pass/std-env.0"]);
const webRuntimeSizeBody = JSON.parse(webRuntimeSize.stdout);
assert.equal(webRuntimeSizeBody.portableRuntime.runtimeKind, "browser-worker");
assert.equal(webRuntimeSizeBody.portableRuntime.providerSpecificDeployment, false);
assert(webRuntimeSizeBody.portableRuntime.imports.functions.includes("environ_get"));
assert.equal(webRuntimeSizeBody.portableRuntime.capabilityRestrictions.filesystem, "denied");

const directObjOut = `${outDir}/direct-obj-add.o`;
const directObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-obj-add.0", "--out", directObjOut]);
const directObjBody = JSON.parse(directObjJson.stdout);
assert.equal(directObjBody.emit, "obj");
assert.equal(directObjBody.compiler, "zero-elf64");
assert.equal(directObjBody.generatedCBytes, 0);
assert(directObjBody.loweredIrBytes > 0);
assert.equal(directObjBody.objectBackend.objectEmission.path, "direct-elf64-object");
await assertElf64Object(directObjOut, "main");

const directI64ObjOut = `${outDir}/direct-i64-return.o`;
const directI64ObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-i64-return.0", "--out", directI64ObjOut]);
const directI64ObjBody = JSON.parse(directI64ObjJson.stdout);
const directI64ObjBytes = await readFile(directI64ObjOut);
assert.equal(directI64ObjBody.emit, "obj");
assert.equal(directI64ObjBody.compiler, "zero-elf64");
assert.equal(directI64ObjBody.generatedCBytes, 0);
assert.equal(directI64ObjBody.objectBackend.objectEmission.path, "direct-elf64-object");
assert(directI64ObjBytes.includes(Buffer.from([0x48, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f])));
assert(directI64ObjBytes.includes(Buffer.from([0x48, 0x01, 0xc8])));

const directIfWasmOut = `${outDir}/direct-if-return.wasm`;
const directIfWasmJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "examples/direct-if-return.0", "--out", directIfWasmOut]);
const directIfWasmBody = JSON.parse(directIfWasmJson.stdout);
assert.equal(directIfWasmBody.emit, "wasm");
assert.equal(directIfWasmBody.generatedCBytes, 0);
assert.equal(directIfWasmBody.objectBackend.objectEmission.path, "direct-wasm");

const directCallWasmOut = `${outDir}/direct-call-branch.wasm`;
const directCallWasmJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "examples/direct-call-branch.0", "--out", directCallWasmOut]);
const directCallWasmBody = JSON.parse(directCallWasmJson.stdout);
assert.equal(directCallWasmBody.emit, "wasm");
assert.equal(directCallWasmBody.generatedCBytes, 0);
assert.equal(directCallWasmBody.objectBackend.objectEmission.path, "direct-wasm");

const directArrayWasmOut = `${outDir}/direct-array-sum.wasm`;
const directArrayWasmJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "examples/direct-array-sum.0", "--out", directArrayWasmOut]);
const directArrayWasmBody = JSON.parse(directArrayWasmJson.stdout);
assert.equal(directArrayWasmBody.emit, "wasm");
assert.equal(directArrayWasmBody.generatedCBytes, 0);
assert(directArrayWasmBody.objectBackend.directFacts.stackBytes > 0);
assert.equal(directArrayWasmBody.objectBackend.objectEmission.path, "direct-wasm");

const directWebWasmOutBase = `${outDir}/direct-web-wasm`;
const directWebWasmJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-wasm-add.0", "--out", directWebWasmOutBase]);
const directWebWasmBody = JSON.parse(directWebWasmJson.stdout);
assert.equal(directWebWasmBody.target, "wasm32-web");
assert.equal(directWebWasmBody.compiler, "zero-wasm");
assert.equal(directWebWasmBody.generatedCBytes, 0);
assert.equal(directWebWasmBody.objectBackend.targetFacts.selectedEmitter, "zero-wasm");
const directWebWasmBytes = await readFile(`${directWebWasmOutBase}.wasm`);
assert.equal(directWebWasmBytes[0], 0);
assert.equal(directWebWasmBytes[1], 0x61);
assert.equal(directWebWasmBytes[2], 0x73);
assert.equal(directWebWasmBytes[3], 0x6d);

const directIfObjOut = `${outDir}/direct-if-return.o`;
const directIfObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-if-return.0", "--out", directIfObjOut]);
const directIfObjBody = JSON.parse(directIfObjJson.stdout);
assert.equal(directIfObjBody.emit, "obj");
assert.equal(directIfObjBody.generatedCBytes, 0);
assert.equal(directIfObjBody.objectBackend.objectEmission.path, "direct-elf64-object");
await assertElf64Object(directIfObjOut, "main");

const directShapeObjOut = `${outDir}/direct-token-shape.o`;
const directShapeObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-token-shape.0", "--out", directShapeObjOut]);
const directShapeObjBody = JSON.parse(directShapeObjJson.stdout);
assert.equal(directShapeObjBody.emit, "obj");
assert.equal(directShapeObjBody.generatedCBytes, 0);
assert.equal(directShapeObjBody.objectBackend.objectEmission.path, "direct-elf64-object");
await assertElf64Object(directShapeObjOut, "main");

const directSpanReadObjOut = `${outDir}/direct-span-read.o`;
const directSpanReadObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-span-read.0", "--out", directSpanReadObjOut]);
const directSpanReadObjBody = JSON.parse(directSpanReadObjJson.stdout);
assert.equal(directSpanReadObjBody.emit, "obj");
assert.equal(directSpanReadObjBody.generatedCBytes, 0);
assert.equal(directSpanReadObjBody.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directSpanReadObjBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directSpanReadObjBody.objectBackend.directFacts.readonlyDataBytes, 6);
await assertElf64Object(directSpanReadObjOut, "main");

const directByteViewLocalsObjOut = `${outDir}/direct-byte-view-reloc.o`;
const directByteViewLocalsObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-byte-view-reloc.0", "--out", directByteViewLocalsObjOut]);
const directByteViewLocalsObjBody = JSON.parse(directByteViewLocalsObjJson.stdout);
assert.equal(directByteViewLocalsObjBody.emit, "obj");
assert.equal(directByteViewLocalsObjBody.generatedCBytes, 0);
assert.equal(directByteViewLocalsObjBody.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directByteViewLocalsObjBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directByteViewLocalsObjBody.objectBackend.directFacts.readonlyDataBytes, 6);
const directByteViewLocalsObject = await assertElf64Object(directByteViewLocalsObjOut, "main");
assert.equal(directByteViewLocalsObject.sections.get(".rodata")?.size, 6);
assert(directByteViewLocalsObject.sections.get(".rela.text")?.size > 0);

const directRescueObjOut = `${outDir}/direct-rescue-basic.o`;
const directRescueObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-rescue-basic.0", "--out", directRescueObjOut]);
const directRescueObjBody = JSON.parse(directRescueObjJson.stdout);
assert.equal(directRescueObjBody.emit, "obj");
assert.equal(directRescueObjBody.generatedCBytes, 0);
assert.equal(directRescueObjBody.objectBackend.objectEmission.path, "direct-elf64-object");
await assertElf64Object(directRescueObjOut, "main");

const directCallObjOut = `${outDir}/direct-call-add.o`;
const directCallObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-call-add.0", "--out", directCallObjOut]);
const directCallObjBody = JSON.parse(directCallObjJson.stdout);
assert.equal(directCallObjBody.emit, "obj");
assert.equal(directCallObjBody.generatedCBytes, 0);
assert.equal(directCallObjBody.objectBackend.objectEmission.path, "direct-elf64-object");
await assertElf64Object(directCallObjOut, "main");

const directLinuxGnuObjOut = `${outDir}/direct-call-add-linux-gnu.o`;
const directLinuxGnuObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-x64", "examples/direct-call-add.0", "--out", directLinuxGnuObjOut]);
const directLinuxGnuObjBody = JSON.parse(directLinuxGnuObjJson.stdout);
assert.equal(directLinuxGnuObjBody.target, "linux-x64");
assert.equal(directLinuxGnuObjBody.compiler, "zero-elf64");
assert.equal(directLinuxGnuObjBody.generatedCBytes, 0);
assert.equal(directLinuxGnuObjBody.objectBackend.targetFacts.selectedEmitter, "zero-elf64");
await assertElf64Object(directLinuxGnuObjOut, "main");

const directMachOObjOut = `${outDir}/direct-call-add-darwin-arm64.o`;
const directMachOObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/direct-call-add.0", "--out", directMachOObjOut]);
const directMachOObjBody = JSON.parse(directMachOObjJson.stdout);
assert.equal(directMachOObjBody.compiler, "zero-macho64");
assert.equal(directMachOObjBody.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(directMachOObjBody.objectBackend.linking.objectFormat, "macho");
const directMachOBytes = await assertMachOArm64Object(directMachOObjOut, "main");
assert(directMachOBytes.includes(Buffer.concat([Buffer.from("_add"), Buffer.from([0])])));
assert(directMachOBytes.includes(Buffer.from([0x00, 0x01, 0x09, 0x0b])));
const directMachOSection = 32 + 72;
const directMachORelocOffset = directMachOBytes.readUInt32LE(directMachOSection + 56);
assert(directMachORelocOffset > 0);
assert(directMachOBytes.readUInt32LE(directMachOSection + 60) > 0);
assert.equal((directMachOBytes.readUInt32LE(directMachORelocOffset + 4) >>> 28) & 15, 2);

const directMachODataObjOut = `${outDir}/direct-byte-view-reloc-darwin-arm64.o`;
const directMachODataObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/direct-byte-view-reloc.0", "--out", directMachODataObjOut]);
const directMachODataObjBody = JSON.parse(directMachODataObjJson.stdout);
assert.equal(directMachODataObjBody.compiler, "zero-macho64");
assert.equal(directMachODataObjBody.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(directMachODataObjBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directMachODataObjBody.objectBackend.directFacts.readonlyDataBytes, 6);
const directMachODataBytes = await assertMachOArm64Object(directMachODataObjOut, "main");
assert(directMachODataBytes.includes(Buffer.from("__const\0")));
assert(directMachODataBytes.includes(Buffer.from("l_.zero_rodata\0")));
assert(directMachODataBytes.includes(Buffer.from("token")));
const directMachODataSection = 32 + 72;
const directMachODataRelocOffset = directMachODataBytes.readUInt32LE(directMachODataSection + 56);
const directMachODataRelocCount = directMachODataBytes.readUInt32LE(directMachODataSection + 60);
assert(directMachODataRelocOffset > 0);
assert(directMachODataRelocCount > 0);
let sawMachOPageReloc = false;
let sawMachOPageoffReloc = false;
for (let i = 0; i < directMachODataRelocCount; i++) {
  const info = directMachODataBytes.readUInt32LE(directMachODataRelocOffset + i * 8 + 4);
  sawMachOPageReloc ||= ((info >>> 28) & 15) === 3;
  sawMachOPageoffReloc ||= ((info >>> 28) & 15) === 4;
}
assert.equal(sawMachOPageReloc, true);
assert.equal(sawMachOPageoffReloc, true);

const directCoffObjOut = `${outDir}/direct-call-add-win-x64.obj`;
const directCoffObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", "examples/direct-call-add.0", "--out", directCoffObjOut]);
const directCoffObjBody = JSON.parse(directCoffObjJson.stdout);
assert.equal(directCoffObjBody.compiler, "zero-coff-x64");
assert.equal(directCoffObjBody.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(directCoffObjBody.objectBackend.linking.objectFormat, "coff");
const directCoffBytes = await assertCoffX64Object(directCoffObjOut, "main");
const directCoffRelocOffset = directCoffBytes.readUInt32LE(20 + 24);
const directCoffRelocCount = directCoffBytes.readUInt16LE(20 + 32);
assert(directCoffBytes.includes(Buffer.concat([Buffer.from("add"), Buffer.from([0])])));
assert(directCoffBytes.includes(Buffer.from([0xe8])));
assert(directCoffRelocOffset > 0);
assert(directCoffRelocCount > 0);
assert.equal(directCoffBytes.readUInt16LE(directCoffRelocOffset + 8), 4);

const directCoffDataObjOut = `${outDir}/direct-byte-view-reloc-win-x64.obj`;
const directCoffDataObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", "examples/direct-byte-view-reloc.0", "--out", directCoffDataObjOut]);
const directCoffDataObjBody = JSON.parse(directCoffDataObjJson.stdout);
assert.equal(directCoffDataObjBody.compiler, "zero-coff-x64");
assert.equal(directCoffDataObjBody.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(directCoffDataObjBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directCoffDataObjBody.objectBackend.directFacts.readonlyDataBytes, 6);
const directCoffDataBytes = await assertCoffX64Object(directCoffDataObjOut, "main");
assert.equal(directCoffDataBytes.readUInt16LE(2), 2);
assert(directCoffDataBytes.includes(Buffer.from(".rdata\0")));
assert(directCoffDataBytes.includes(Buffer.from("token")));
const directCoffDataRelocOffset = directCoffDataBytes.readUInt32LE(20 + 24);
const directCoffDataRelocCount = directCoffDataBytes.readUInt16LE(20 + 32);
assert(directCoffDataRelocOffset > 0);
assert(directCoffDataRelocCount > 0);
let sawCoffAddr64Reloc = false;
for (let i = 0; i < directCoffDataRelocCount; i++) {
  sawCoffAddr64Reloc ||= directCoffDataBytes.readUInt16LE(directCoffDataRelocOffset + i * 10 + 8) === 1;
}
assert.equal(sawCoffAddr64Reloc, true);

const directArm64ElfObjOut = `${outDir}/direct-arm64-return.o`;
const directArm64ElfObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-arm64", "examples/direct-exe-return.0", "--out", directArm64ElfObjOut]);
const directArm64ElfObjBody = JSON.parse(directArm64ElfObjJson.stdout);
assert.equal(directArm64ElfObjBody.compiler, "zero-elf-aarch64");
assert.equal(directArm64ElfObjBody.generatedCBytes, 0);
assert.equal(directArm64ElfObjBody.objectBackend.objectEmission.path, "direct-elf-aarch64-object");
assert.equal(directArm64ElfObjBody.objectBackend.targetFacts.status, "native-exe");
await assertElfAarch64Object(directArm64ElfObjOut, "main");

const directArrayObjOut = `${outDir}/direct-array-fill.o`;
const directArrayObjJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-array-fill.0", "--out", directArrayObjOut]);
const directArrayObjBody = JSON.parse(directArrayObjJson.stdout);
assert.equal(directArrayObjBody.emit, "obj");
assert.equal(directArrayObjBody.generatedCBytes, 0);
assert(directArrayObjBody.objectBackend.directFacts.maxFrameBytes > 0);
assert.equal(directArrayObjBody.objectBackend.objectEmission.path, "direct-elf64-object");
await assertElf64Object(directArrayObjOut, "main");

const directArrayRepeatRecordFieldOut = `${outDir}/direct-array-repeat-record-field.o`;
const directArrayRepeatRecordFieldJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "conformance/native/pass/array-repeat-record-field.0", "--out", directArrayRepeatRecordFieldOut]);
const directArrayRepeatRecordFieldBody = JSON.parse(directArrayRepeatRecordFieldJson.stdout);
assert.equal(directArrayRepeatRecordFieldBody.emit, "obj");
assert.equal(directArrayRepeatRecordFieldBody.generatedCBytes, 0);
assert.equal(directArrayRepeatRecordFieldBody.objectBackend.objectEmission.path, "direct-elf64-object");
await assertElf64Object(directArrayRepeatRecordFieldOut, "main");

const directExeOut = `${outDir}/direct-exe-return`;
const directExeJson = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-exe-return.0", "--out", directExeOut]);
const directExeBody = JSON.parse(directExeJson.stdout);
assert.equal(directExeBody.emit, "exe");
assert.equal(directExeBody.compiler, "zero-elf64");
assert.equal(directExeBody.generatedCBytes, 0);
assert(directExeBody.loweredIrBytes > 0);
assert(directExeBody.artifactBytes < 512);
assert.equal(directExeBody.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(directExeBody.objectBackend.objectEmission.dataSections, false);
await assertElf64Executable(directExeOut);

const directAarch64ExeOut = `${outDir}/direct-aarch64-exe-return`;
const directAarch64ExeJson = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--backend", "zero-elf-aarch64", "--target", "linux-musl-arm64", "examples/direct-exe-return.0", "--out", directAarch64ExeOut]);
const directAarch64ExeBody = JSON.parse(directAarch64ExeJson.stdout);
assert.equal(directAarch64ExeBody.emit, "exe");
assert.equal(directAarch64ExeBody.compiler, "zero-elf-aarch64");
assert.equal(directAarch64ExeBody.generatedCBytes, 0);
assert(directAarch64ExeBody.artifactBytes < 512);
assert.equal(directAarch64ExeBody.objectBackend.objectEmission.path, "direct-elf-aarch64-exe");
assert.equal(directAarch64ExeBody.objectBackend.targetFacts.status, "native-exe");
await assertElfAarch64Executable(directAarch64ExeOut);

const directMachOExeOut = `${outDir}/direct-macho-exe-return`;
await rm(directMachOExeOut, { force: true });
const directMachOExeJson = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--backend", "zero-macho64", "--target", "darwin-arm64", "examples/direct-exe-return.0", "--out", directMachOExeOut]);
const directMachOExeBody = JSON.parse(directMachOExeJson.stdout);
assert.equal(directMachOExeBody.emit, "exe");
assert.equal(directMachOExeBody.compiler, "zero-macho64");
assert.equal(directMachOExeBody.generatedCBytes, 0);
assert.equal(directMachOExeBody.objectBackend.objectEmission.path, "direct-macho64-exe");
assert.equal(directMachOExeBody.objectBackend.targetFacts.status, "native-exe");
await assertMachOArm64Executable(directMachOExeOut);
if (process.platform === "darwin" && process.arch === "arm64") {
  const ran = await execFileAsync(directMachOExeOut).catch((error) => error);
  assert.equal(ran.code, 42);
}

const directCoffExeOut = `${outDir}/direct-coff-exe-return`;
const directCoffExeJson = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--backend", "zero-coff-x64", "--target", "win32-x64.exe", "examples/direct-exe-return.0", "--out", directCoffExeOut]);
const directCoffExeBody = JSON.parse(directCoffExeJson.stdout);
assert.equal(directCoffExeBody.emit, "exe");
assert.equal(directCoffExeBody.compiler, "zero-coff-x64");
assert.equal(directCoffExeBody.generatedCBytes, 0);
assert.equal(directCoffExeBody.objectBackend.objectEmission.path, "direct-coff-x64-exe");
assert.equal(directCoffExeBody.objectBackend.targetFacts.status, "native-exe");
await assertPeCoffX64Executable(`${directCoffExeOut}.exe`);

const directWhileOut = `${outDir}/direct-while-sum`;
const directWhileJson = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-while-sum.0", "--out", directWhileOut]);
const directWhileBody = JSON.parse(directWhileJson.stdout);
assert.equal(directWhileBody.emit, "exe");
assert.equal(directWhileBody.generatedCBytes, 0);
assert.equal(directWhileBody.objectBackend.objectEmission.path, "direct-elf64-exe");
await assertElf64Executable(directWhileOut);

const directCallLoopOut = `${outDir}/direct-call-loop`;
const directCallLoopJson = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-call-loop.0", "--out", directCallLoopOut]);
const directCallLoopBody = JSON.parse(directCallLoopJson.stdout);
assert.equal(directCallLoopBody.emit, "exe");
assert.equal(directCallLoopBody.generatedCBytes, 0);
assert.equal(directCallLoopBody.objectBackend.objectEmission.path, "direct-elf64-exe");
await assertElf64Executable(directCallLoopOut);

const directPackageOut = `${outDir}/direct-package-arrays`;
const directPackageJson = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-package-arrays", "--out", directPackageOut]);
const directPackageBody = JSON.parse(directPackageJson.stdout);
assert.equal(directPackageBody.emit, "exe");
assert.equal(directPackageBody.generatedCBytes, 0);
assert.equal(directPackageBody.objectBackend.directFacts.moduleCount, 2);
assert.equal(directPackageBody.objectBackend.objectEmission.path, "direct-elf64-exe");
await assertElf64Executable(directPackageOut);

const directU8HelperOut = `${outDir}/direct-u8-helper-call`;
const directU8HelperJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-u8-helper-call.0", "--out", directU8HelperOut]);
const directU8HelperBody = JSON.parse(directU8HelperJson.stdout);
assert.equal(directU8HelperBody.emit, "wasm");
assert.equal(directU8HelperBody.generatedCBytes, 0);
const directU8HelperBytes = await readFile(`${directU8HelperOut}.wasm`);
const directU8HelperInstance = await WebAssembly.instantiate(directU8HelperBytes, {});
assert.equal(directU8HelperInstance.instance.exports.main(), 1);

const directArrayBoundsTrapOut = `${outDir}/direct-array-bounds-trap`;
const directArrayBoundsTrapJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-array-bounds-trap.0", "--out", directArrayBoundsTrapOut]);
const directArrayBoundsTrapBody = JSON.parse(directArrayBoundsTrapJson.stdout);
assert.equal(directArrayBoundsTrapBody.generatedCBytes, 0);
assert.equal(directArrayBoundsTrapBody.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(directArrayBoundsTrapBody.objectBackend.directFacts.runtime.boundsTraps, "pay-as-used");
const directArrayBoundsTrapBytes = await readFile(`${directArrayBoundsTrapOut}.wasm`);
const directArrayBoundsTrapInstance = await WebAssembly.instantiate(directArrayBoundsTrapBytes, {});
assert.throws(() => directArrayBoundsTrapInstance.instance.exports.main(), WebAssembly.RuntimeError);

const directStringLenOut = `${outDir}/direct-string-len`;
const directStringLenJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-string-len.0", "--out", directStringLenOut]);
const directStringLenBody = JSON.parse(directStringLenJson.stdout);
assert.equal(directStringLenBody.generatedCBytes, 0);
const directStringLenBytes = await readFile(`${directStringLenOut}.wasm`);
const directStringLenInstance = await WebAssembly.instantiate(directStringLenBytes, {});
assert.equal(directStringLenInstance.instance.exports.main(), 5);

const directStringLiteralOut = `${outDir}/direct-string-literal`;
const directStringLiteralJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-string-literal.0", "--out", directStringLiteralOut]);
const directStringLiteralBody = JSON.parse(directStringLiteralJson.stdout);
assert.equal(directStringLiteralBody.generatedCBytes, 0);
assert.equal(directStringLiteralBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directStringLiteralBody.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(directStringLiteralBody.objectBackend.directFacts.runtime.readonlyDataBytes, 6);
const directStringLiteralBytes = await readFile(`${directStringLiteralOut}.wasm`);
const directStringLiteralInstance = await WebAssembly.instantiate(directStringLiteralBytes, {});
assert.equal(directStringLiteralInstance.instance.exports.main(), 116);

const directSpanReadOut = `${outDir}/direct-span-read`;
const directSpanReadJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-span-read.0", "--out", directSpanReadOut]);
const directSpanReadBody = JSON.parse(directSpanReadJson.stdout);
assert.equal(directSpanReadBody.generatedCBytes, 0);
assert.equal(directSpanReadBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directSpanReadBody.objectBackend.directFacts.runtime.readonlyDataBytes, 6);
const directSpanReadBytes = await readFile(`${directSpanReadOut}.wasm`);
const directSpanReadInstance = await WebAssembly.instantiate(directSpanReadBytes, {});
assert.equal(directSpanReadInstance.instance.exports.main(), 107);

const directStringEqlOut = `${outDir}/direct-string-eql`;
const directStringEqlJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-string-eql.0", "--out", directStringEqlOut]);
const directStringEqlBody = JSON.parse(directStringEqlJson.stdout);
assert.equal(directStringEqlBody.generatedCBytes, 0);
assert.equal(directStringEqlBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directStringEqlBody.objectBackend.directFacts.runtime.readonlyDataBytes, 12);
const directStringEqlBytes = await readFile(`${directStringEqlOut}.wasm`);
const directStringEqlInstance = await WebAssembly.instantiate(directStringEqlBytes, {});
assert.equal(directStringEqlInstance.instance.exports.main(), 1);

const directByteViewLocalsOut = `${outDir}/direct-byte-view-locals`;
const directByteViewLocalsJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-byte-view-locals.0", "--out", directByteViewLocalsOut]);
const directByteViewLocalsBody = JSON.parse(directByteViewLocalsJson.stdout);
assert.equal(directByteViewLocalsBody.generatedCBytes, 0);
assert.equal(directByteViewLocalsBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directByteViewLocalsBody.objectBackend.directFacts.runtime.readonlyDataBytes, 10);
const directByteViewLocalsBytes = await readFile(`${directByteViewLocalsOut}.wasm`);
const directByteViewLocalsInstance = await WebAssembly.instantiate(directByteViewLocalsBytes, {});
assert.equal(directByteViewLocalsInstance.instance.exports.main(), 107);

const directJsonBytesOut = `${outDir}/direct-json-bytes`;
const directJsonBytesJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "conformance/native/pass/std-json-bytes.0", "--out", directJsonBytesOut]);
const directJsonBytesBody = JSON.parse(directJsonBytesJson.stdout);
assert.equal(directJsonBytesBody.generatedCBytes, 0);
assert.equal(directJsonBytesBody.objectBackend.objectEmission.path, "direct-wasm");
assert.equal(directJsonBytesBody.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(directJsonBytesBody.objectBackend.directFacts.runtime.readonlyDataBytes, 9);
const directJsonBytesBytes = await readFile(`${directJsonBytesOut}.wasm`);
assert(WebAssembly.Module.imports(new WebAssembly.Module(directJsonBytesBytes)).some((entry) =>
  entry.module === "zero_runtime" && entry.name === "zero_json_parse_bytes"
));
const directJsonBytesInstance = await instantiateJsonRuntimeWasm(directJsonBytesBytes);
assert.equal(directJsonBytesInstance.instance.exports.main(), 0);

const directJsonInlineBytesOut = `${outDir}/direct-json-inline-bytes`;
const directJsonInlineBytesJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "conformance/native/pass/std-json-inline-bytes.0", "--out", directJsonInlineBytesOut]);
const directJsonInlineBytesBody = JSON.parse(directJsonInlineBytesJson.stdout);
assert.equal(directJsonInlineBytesBody.generatedCBytes, 0);
assert.equal(directJsonInlineBytesBody.objectBackend.objectEmission.path, "direct-wasm");
assert.equal(directJsonInlineBytesBody.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(directJsonInlineBytesBody.objectBackend.directFacts.runtime.readonlyDataBytes, 4);
const directJsonInlineBytesBytes = await readFile(`${directJsonInlineBytesOut}.wasm`);
assert(WebAssembly.Module.imports(new WebAssembly.Module(directJsonInlineBytesBytes)).some((entry) =>
  entry.module === "zero_runtime" && entry.name === "zero_json_parse_bytes"
));
const directJsonInlineBytesInstance = await instantiateJsonRuntimeWasm(directJsonInlineBytesBytes);
assert.equal(directJsonInlineBytesInstance.instance.exports.main(), 0);

const directJsonDuplicateKeysOut = `${outDir}/direct-json-duplicate-keys`;
const directJsonDuplicateKeysJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "conformance/native/pass/std-json-duplicate-keys.0", "--out", directJsonDuplicateKeysOut]);
const directJsonDuplicateKeysBody = JSON.parse(directJsonDuplicateKeysJson.stdout);
assert.equal(directJsonDuplicateKeysBody.generatedCBytes, 0);
assert.equal(directJsonDuplicateKeysBody.objectBackend.objectEmission.path, "direct-wasm");
const directJsonDuplicateKeysBytes = await readFile(`${directJsonDuplicateKeysOut}.wasm`);
const directJsonDuplicateKeysInstance = await instantiateJsonRuntimeWasm(directJsonDuplicateKeysBytes);
assert.equal(directJsonDuplicateKeysInstance.instance.exports.main(), 0);

const directJsonAllocatorCapacityOut = `${outDir}/direct-json-allocator-capacity`;
const directJsonAllocatorCapacityJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "conformance/native/pass/std-json-allocator-capacity.0", "--out", directJsonAllocatorCapacityOut]);
const directJsonAllocatorCapacityBody = JSON.parse(directJsonAllocatorCapacityJson.stdout);
assert.equal(directJsonAllocatorCapacityBody.generatedCBytes, 0);
assert.equal(directJsonAllocatorCapacityBody.objectBackend.objectEmission.path, "direct-wasm");
const directJsonAllocatorCapacityBytes = await readFile(`${directJsonAllocatorCapacityOut}.wasm`);
const directJsonAllocatorCapacityInstance = await instantiateJsonRuntimeWasm(directJsonAllocatorCapacityBytes);
assert.equal(directJsonAllocatorCapacityInstance.instance.exports.main(), 0);

const directMutSpanLenOut = `${outDir}/direct-mutspan-len`;
const directMutSpanLenJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-mutspan-len.0", "--out", directMutSpanLenOut]);
const directMutSpanLenBody = JSON.parse(directMutSpanLenJson.stdout);
assert.equal(directMutSpanLenBody.generatedCBytes, 0);
assert.equal(directMutSpanLenBody.objectBackend.directFacts.runtime.linearMemory, true);
const directMutSpanLenBytes = await readFile(`${directMutSpanLenOut}.wasm`);
const directMutSpanLenInstance = await WebAssembly.instantiate(directMutSpanLenBytes, {});
assert.equal(directMutSpanLenInstance.instance.exports.main(), 5);
assert(directMutSpanLenInstance.instance.exports.memory instanceof WebAssembly.Memory);

const directMemoryPeekOut = `${outDir}/direct-memory-peek`;
const directMemoryPeekJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-memory-peek.0", "--out", directMemoryPeekOut]);
const directMemoryPeekBody = JSON.parse(directMemoryPeekJson.stdout);
assert.equal(directMemoryPeekBody.generatedCBytes, 0);
assert.equal(directMemoryPeekBody.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(directMemoryPeekBody.objectBackend.directFacts.exportCount, 3);
const directMemoryPeekBytes = await readFile(`${directMemoryPeekOut}.wasm`);
const directMemoryPeekInstance = await WebAssembly.instantiate(directMemoryPeekBytes, {});
assert(directMemoryPeekInstance.instance.exports.memory instanceof WebAssembly.Memory);
new Uint8Array(directMemoryPeekInstance.instance.exports.memory.buffer)[1024] = 90;
assert.equal(directMemoryPeekInstance.instance.exports.read_at(1024), 90);

const directByteCopyFillOut = `${outDir}/direct-byte-copy-fill`;
const directByteCopyFillJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-byte-copy-fill.0", "--out", directByteCopyFillOut]);
const directByteCopyFillBody = JSON.parse(directByteCopyFillJson.stdout);
assert.equal(directByteCopyFillBody.generatedCBytes, 0);
assert.equal(directByteCopyFillBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directByteCopyFillBody.objectBackend.directFacts.runtime.readonlyDataBytes, 6);
assert.equal(directByteCopyFillBody.objectBackend.directFacts.runtime.linearMemory, true);
const directByteCopyFillBytes = await readFile(`${directByteCopyFillOut}.wasm`);
const directByteCopyFillInstance = await WebAssembly.instantiate(directByteCopyFillBytes, {});
assert.equal(directByteCopyFillInstance.instance.exports.main(), 111);

const directAllocBumpOut = `${outDir}/direct-alloc-bump`;
const directAllocBumpJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-alloc-bump.0", "--out", directAllocBumpOut]);
const directAllocBumpBody = JSON.parse(directAllocBumpJson.stdout);
assert.equal(directAllocBumpBody.generatedCBytes, 0);
assert.equal(directAllocBumpBody.objectBackend.objectEmission.dataSections, true);
assert.equal(directAllocBumpBody.objectBackend.directFacts.allocatorHelperCount, 2);
assert.equal(directAllocBumpBody.objectBackend.directFacts.runtime.heapPolicy, "explicit-fixed-buffer-bump");
assert.equal(directAllocBumpBody.objectBackend.directFacts.runtime.allocator, "FixedBufAlloc");
assert.equal(directAllocBumpBody.objectBackend.directFacts.runtime.linearMemory, true);
const directAllocBumpBytes = await readFile(`${directAllocBumpOut}.wasm`);
const directAllocBumpInstance = await WebAssembly.instantiate(directAllocBumpBytes, {});
assert.equal(directAllocBumpInstance.instance.exports.main(), 114);

const directAllocOverflowOut = `${outDir}/direct-alloc-overflow`;
const directAllocOverflowJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-alloc-overflow.0", "--out", directAllocOverflowOut]);
const directAllocOverflowBody = JSON.parse(directAllocOverflowJson.stdout);
assert.equal(directAllocOverflowBody.generatedCBytes, 0);
assert.equal(directAllocOverflowBody.objectBackend.directFacts.allocatorHelperCount, 2);
assert.equal(directAllocOverflowBody.objectBackend.directFacts.runtime.heapPolicy, "explicit-fixed-buffer-bump");
const directAllocOverflowBytes = await readFile(`${directAllocOverflowOut}.wasm`);
const directAllocOverflowInstance = await WebAssembly.instantiate(directAllocOverflowBytes, {});
assert.equal(directAllocOverflowInstance.instance.exports.main(), 1);

const directTokenShapeOut = `${outDir}/direct-token-shape`;
const directTokenShapeJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-token-shape.0", "--out", directTokenShapeOut]);
const directTokenShapeBody = JSON.parse(directTokenShapeJson.stdout);
assert.equal(directTokenShapeBody.generatedCBytes, 0);
assert.equal(directTokenShapeBody.objectBackend.directFacts.runtime.linearMemory, true);
assert(directTokenShapeBody.objectBackend.directFacts.maxFrameBytes > 0);
const directTokenShapeBytes = await readFile(`${directTokenShapeOut}.wasm`);
const directTokenShapeInstance = await WebAssembly.instantiate(directTokenShapeBytes, {});
assert.equal(directTokenShapeInstance.instance.exports.main(), 65);

const directEnumMatchOut = `${outDir}/direct-enum-match`;
const directEnumMatchJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-enum-match.0", "--out", directEnumMatchOut]);
const directEnumMatchBody = JSON.parse(directEnumMatchJson.stdout);
assert.equal(directEnumMatchBody.generatedCBytes, 0);
assert.equal(directEnumMatchBody.objectBackend.directFacts.runtime.linearMemory, true);
const directEnumMatchBytes = await readFile(`${directEnumMatchOut}.wasm`);
const directEnumMatchInstance = await WebAssembly.instantiate(directEnumMatchBytes, {});
assert.equal(directEnumMatchInstance.instance.exports.main(), 2);

const directRaisesBasicOut = `${outDir}/direct-raises-basic`;
const directRaisesBasicJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-raises-basic.0", "--out", directRaisesBasicOut]);
const directRaisesBasicBody = JSON.parse(directRaisesBasicJson.stdout);
assert.equal(directRaisesBasicBody.generatedCBytes, 0);
assert.equal(directRaisesBasicBody.objectBackend.targetFacts.selectedEmitter, "zero-wasm");
const directRaisesBasicBytes = await readFile(`${directRaisesBasicOut}.wasm`);
const directRaisesBasicInstance = await WebAssembly.instantiate(directRaisesBasicBytes, {});
assert.equal(directRaisesBasicInstance.instance.exports.main(), 7);

const directRescueBasicOut = `${outDir}/direct-rescue-basic`;
const directRescueBasicJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-rescue-basic.0", "--out", directRescueBasicOut]);
const directRescueBasicBody = JSON.parse(directRescueBasicJson.stdout);
assert.equal(directRescueBasicBody.generatedCBytes, 0);
assert.equal(directRescueBasicBody.objectBackend.targetFacts.selectedEmitter, "zero-wasm");
const directRescueBasicBytes = await readFile(`${directRescueBasicOut}.wasm`);
const directRescueBasicInstance = await WebAssembly.instantiate(directRescueBasicBytes, {});
assert.equal(directRescueBasicInstance.instance.exports.main(), 9);

const directByteBufOut = `${outDir}/direct-byte-buf`;
const directByteBufJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-byte-buf.0", "--out", directByteBufOut]);
const directByteBufBody = JSON.parse(directByteBufJson.stdout);
assert.equal(directByteBufBody.generatedCBytes, 0);
assert.equal(directByteBufBody.objectBackend.directFacts.bufferHelperCount, 3);
assert.equal(directByteBufBody.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(directByteBufBody.objectBackend.directFacts.runtime.buffer, "Vec<u8>");
const directByteBufBytes = await readFile(`${directByteBufOut}.wasm`);
const directByteBufInstance = await WebAssembly.instantiate(directByteBufBytes, {});
assert.equal(directByteBufInstance.instance.exports.main(), 66);

const directGenericIdentityOut = `${outDir}/direct-generic-identity`;
const directGenericIdentityJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-generic-identity.0", "--out", directGenericIdentityOut]);
const directGenericIdentityBody = JSON.parse(directGenericIdentityJson.stdout);
assert.equal(directGenericIdentityBody.generatedCBytes, 0);
assert.equal(directGenericIdentityBody.objectBackend.directFacts.functionCount, 2);
const directGenericIdentityBytes = await readFile(`${directGenericIdentityOut}.wasm`);
const directGenericIdentityInstance = await WebAssembly.instantiate(directGenericIdentityBytes, {});
assert.equal(directGenericIdentityInstance.instance.exports.main(), 42);

const directGenericFixedBufOut = `${outDir}/direct-generic-fixedbuf`;
const directGenericFixedBufJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-generic-fixedbuf.0", "--out", directGenericFixedBufOut]);
const directGenericFixedBufBody = JSON.parse(directGenericFixedBufJson.stdout);
assert.equal(directGenericFixedBufBody.generatedCBytes, 0);
assert.equal(directGenericFixedBufBody.objectBackend.directFacts.runtime.linearMemory, true);
const directGenericFixedBufBytes = await readFile(`${directGenericFixedBufOut}.wasm`);
const directGenericFixedBufInstance = await WebAssembly.instantiate(directGenericFixedBufBytes, {});
assert.equal(directGenericFixedBufInstance.instance.exports.main(), 55);

const directGenericVecOut = `${outDir}/direct-generic-vec`;
const directGenericVecJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-generic-vec.0", "--out", directGenericVecOut]);
const directGenericVecBody = JSON.parse(directGenericVecJson.stdout);
assert.equal(directGenericVecBody.generatedCBytes, 0);
assert.equal(directGenericVecBody.objectBackend.directFacts.runtime.linearMemory, true);
const directGenericVecBytes = await readFile(`${directGenericVecOut}.wasm`);
const directGenericVecInstance = await WebAssembly.instantiate(directGenericVecBytes, {});
assert.equal(directGenericVecInstance.instance.exports.main(), 61);

const directPackageCallOrderOut = `${outDir}/direct-package-call-order`;
const directPackageCallOrderJson = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-package-call-order", "--out", directPackageCallOrderOut]);
const directPackageCallOrderBody = JSON.parse(directPackageCallOrderJson.stdout);
assert.equal(directPackageCallOrderBody.emit, "wasm");
assert.equal(directPackageCallOrderBody.generatedCBytes, 0);
assert.equal(directPackageCallOrderBody.objectBackend.directFacts.moduleCount, 3);
const directPackageCallOrderBytes = await readFile(`${directPackageCallOrderOut}.wasm`);
const directPackageCallOrderInstance = await WebAssembly.instantiate(directPackageCallOrderBytes, {});
assert.equal(directPackageCallOrderInstance.instance.exports.main(), 27);

const compilerZeroStage0Out = `${outDir}/compiler-zero-stage0`;
const compilerZeroStage0Json = await execFileAsync(zero, ["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "compiler-zero", "--out", compilerZeroStage0Out]);
const compilerZeroStage0Body = JSON.parse(compilerZeroStage0Json.stdout);
assert.equal(compilerZeroStage0Body.emit, "wasm");
assert.equal(compilerZeroStage0Body.target, "wasm32-web");
assert.equal(compilerZeroStage0Body.generatedCBytes, 0);
assert.equal(compilerZeroStage0Body.objectBackend.directFacts.moduleCount, 10);
assert.equal(compilerZeroStage0Body.objectBackend.directFacts.bufferHelperCount, 3);
assert.equal(compilerZeroStage0Body.objectBackend.targetFacts.selectedEmitter, "zero-wasm");
const compilerZeroStage0Bytes = await readFile(`${compilerZeroStage0Out}.wasm`);
const compilerZeroStage0Instance = await WebAssembly.instantiate(compilerZeroStage0Bytes, {});
assert.equal(compilerZeroStage0Instance.instance.exports.main(), 48);
assert(compilerZeroStage0Instance.instance.exports.memory instanceof WebAssembly.Memory);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_runtime_arena_plan(64, 16), 54);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_source_input_mode(1, 1, 0), 61);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_source_input_mode(2, 0, 1), 62);
const compilerZeroSourceBytes = new TextEncoder().encode("pub fun main() -> Void {}\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroSourceBytes, 1024);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_accept_source_buffer(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 63);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_token_summary(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 1025);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_source_hash(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength) >>> 0, 3863097883);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_source_location_at(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 18), 1019);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_source_location_at(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 26), 2001);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_source_order_key(1, 2, 3863097883, 3863097884), 64);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_source_order_key(2, 1, 3863097883, 3863097884), 65);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_source_order_key(1, 1, 3863097883, 3863097884), 66);
const compilerZeroManifestBytes = await readFile("compiler-zero/zero.json");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroManifestBytes, 4096);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_manifest_summary(4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 52);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_hosted_package_source_plan(4096, compilerZeroManifestBytes.length, 1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1), 68);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_hosted_package_source_plan(4096, compilerZeroManifestBytes.length, 1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 0), 0);
const compilerZeroRuntimeAbiBytes = new TextEncoder().encode('export c fun add(a: i32, b: i32) -> i32 { return a + b }\npub fun main(world: World, args: Args, env: Env, alloc: Alloc) -> Void raises { Bad } { let bytes: [4]u8 = [1, 2, 3, 4] check world.out.write("ok") check world.err.write("bad") raise Bad }\n');
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroRuntimeAbiBytes, 34816);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_runtime_abi_summary(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1), 1023);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_runtime_memory_layout(64, 4, 1), 6400041);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_runtime_shim_cost(1, 1, 1, 1, 1), 11610);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_runtime_helper_summary(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1), 249);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_runtime_helper_summary(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 2), 250);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_runtime_helper_summary(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 3), 252);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_browser_compile_request(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1, 2, 3), 1020363);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_browser_compile_response(511, 255, 65535, 141), 800141);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_browser_artifact_plan(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1, 3), 1311);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_source_graph_plan(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1), 52680001);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_compile_request(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1, 2, 3), 1821673);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_module_graph_packet(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1), 13171111);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_source_diag_code(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1), 0);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_write_minimal_wasm(8192, 8, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 8);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_minimal_wasm_hash(8192, 8, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 1836278016);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_write_return42_wasm(14336, 64, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 38);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_command_response(1, 52680001, 1821673, 1836278016), 4010001);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_command_response(2, 52680001, 1821673, 1836278016), 4020008);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_command_response(3, 52680001, 1821673, 1836278016), 4030001);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_command_response(4, 52680001, 1821673, 1836278016), 4040008);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_fixed_point_packet(2, 4020008, 7009121, 1836278016), 2203300);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_fixed_point_packet(3, 4020008, 7009121, 1836278016), 3303300);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_native_artifact_plan(1, 1, 0, 0), 801101);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_native_artifact_plan(2, 2, 0, 0), 802202);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_native_artifact_plan(2, 3, 0, 0), 802303);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_native_artifact_plan(2, 2, 1, 0), 0);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_target_capability_diag(2, 1, 1, 3), 0);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_target_capability_diag(5, 1, 0, 2), 6002);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_required_capabilities(), 8191);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_supported_capabilities(), 8191);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_gap_mask(), 0);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_gap_diag_code(512), 7003);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_gap_diag_code(1024), 7004);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_gap_diag_code(2048), 7005);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_gap_diag_code(4096), 7006);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_stage_plan(3, 8191, 8191, 0, 0), 3300);
const compilerZeroUnsupportedSelfHostBytes = new TextEncoder().encode("pub extern c fun main() -> i32\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroUnsupportedSelfHostBytes, 12288);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_source_diag_code(12288, compilerZeroUnsupportedSelfHostBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1), 7009);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_self_host_diagnostic_packet(7009, 1, 4, 2), 7009121);
const compilerZeroModuleSourceBytes = new TextEncoder().encode("use lib\npub fun main() -> Void {}\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroModuleSourceBytes, 8192);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_module_import_summary(8192, compilerZeroModuleSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 2), 73);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_module_import_summary(8192, compilerZeroModuleSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1), 0);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_module_import_diag_code(8192, compilerZeroModuleSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1), 3003);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_module_graph_reject_code(1, 1, 0, 0, 0), 3003);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_module_graph_reject_code(2, 1, 1, 0, 0), 3008);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_module_graph_reject_code(2, 1, 0, 1, 0), 3009);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_module_graph_reject_code(2, 1, 0, 0, 1), 3010);
const compilerZeroScopeBytes = new TextEncoder().encode("pub shape Point { x: i32 }\npub fun main() -> Void {\n let x = 1\n let y = x\n}\nfun helper() -> Void {}\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroScopeBytes, 32768);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_scope_summary(32768, compilerZeroScopeBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 40202);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_name_resolution_packet(32768, compilerZeroScopeBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 0, 0), 0);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_name_resolution_packet(32768, compilerZeroScopeBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1, 0), 3003);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_name_resolution_packet(32768, compilerZeroScopeBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 0, 1), 3009);
const compilerZeroMemberNameBytes = new TextEncoder().encode("type BytePair = Pair<u8, u8>\nshape Pair<T, static N: usize> { left: T }\npub fun main() -> Void { pair.left }\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroMemberNameBytes, 40960);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_member_name_summary(40960, compilerZeroMemberNameBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 1010201);
const compilerZeroTypeSurfaceBytes = new TextEncoder().encode("shape Box { items: [4]u8, name: String, next: ref<Box> } enum Color { red } choice Event { quit } pub fun main(flag: Bool) -> Void { let n: usize = 1 }\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroTypeSurfaceBytes, 53248);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_type_surface_summary(53248, compilerZeroTypeSurfaceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 63);
const compilerZeroExpressionSurfaceBytes = new TextEncoder().encode("let mut pair: Pair = Pair { left: 1, right: 2 } pair.left = pair.items[0]");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroExpressionSurfaceBytes, 57344);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_expression_surface_summary(57344, compilerZeroExpressionSurfaceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 63);
const compilerZeroControlFlowBytes = new TextEncoder().encode("if ok { check call() } while ok { return } rescue err { return }");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroControlFlowBytes, 61440);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_control_flow_summary(61440, compilerZeroControlFlowBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 15);
const compilerZeroGenericStaticBytes = new TextEncoder().encode("interface Readable<T> { fun read(self: ref<T>) -> i32 } shape FixedVec<T, static N: usize> { items: [N]T fun push(self: mutref<Self>, value: T) -> Void {} } fun first<T, static N: usize>(vec: ref<FixedVec<T,N>>) -> T { return vec.items[0] } pub fun main() -> Void { vec.push(1) }\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroGenericStaticBytes, 45056);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_generic_static_summary(45056, compilerZeroGenericStaticBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 255);
const compilerZeroFallibilityBytes = new TextEncoder().encode("fun fail() -> Void raises { BadInput } { raise BadInput } pub fun main() -> Void raises { check fail() rescue err { return } }\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroFallibilityBytes, 47104);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_fallibility_summary(47104, compilerZeroFallibilityBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 255);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_fallibility_diag_code(0, 0, 0, 0), 0);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_fallibility_diag_code(1, 0, 0, 0), 1003);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_fallibility_diag_code(0, 1, 0, 0), 1002);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_fallibility_diag_code(0, 0, 1, 0), 1002);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_fallibility_diag_code(0, 0, 0, 1), 1001);
const compilerZeroCapabilityBytes = new TextEncoder().encode('pub fun main(world: World, net: Net) -> Void raises { let fs = std.fs.host() let env = std.env.get("X") let proc = std.proc.spawn("noop") check world.out.write("target web") }\n');
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroCapabilityBytes, 59392);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_capability_summary(59392, compilerZeroCapabilityBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 255);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_capability_diag_code(0, 0), 0);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_capability_diag_code(1, 0), 6002);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_capability_diag_code(1, 1), 0);
const compilerZeroOwnershipBytes = new TextEncoder().encode("shape Receiver { value: i32 fun add(self: mutref<Self>) -> Void { self.value = self.value + 1 } } pub fun main() -> Void { let mut bytes: [2]u8 = [0, 0] let span: MutSpan<u8> = bytes bytes[0] = 1 let owned: owned<Receiver> = Receiver { value: 1 } consume(owned) Receiver.add(&mut owned) }\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroOwnershipBytes, 6144);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_ownership_summary(6144, compilerZeroOwnershipBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 255);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_ownership_diag_code(1, 0, 0, 0), 1009);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_ownership_diag_code(0, 1, 0, 0), 3013);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_ownership_diag_code(0, 0, 1, 0), 3048);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_ownership_diag_code(0, 0, 0, 1), 3109);
const compilerZeroMirBytes = new TextEncoder().encode('pub const LIMIT: usize = 4\nshape Pair { left: i32, right: i32 }\nenum Color { red }\nchoice Result { ok: i32, bad }\npub fun main(world: World) -> i32 raises { Bad } { let local: [4]u8 = [1, 2, 3, 4] let msg: String = "ok" let span: Span<u8> = local if local[0] == 1 { return helper(span.len) } raise Bad }\n');
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroMirBytes, 32768);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_mir_lowering_summary(32768, compilerZeroMirBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 65535);
const compilerZeroMirHashA = compilerZeroStage0Instance.instance.exports.compiler_mir_hash(32768, compilerZeroMirBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 1);
const compilerZeroMirHashB = compilerZeroStage0Instance.instance.exports.compiler_mir_hash(32768, compilerZeroMirBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 2);
assert(compilerZeroMirHashA > 0);
assert(compilerZeroMirHashB > 0);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_mir_module_order_hash(1, compilerZeroMirHashA, 2, compilerZeroMirHashB), compilerZeroStage0Instance.instance.exports.compiler_mir_module_order_hash(2, compilerZeroMirHashB, 1, compilerZeroMirHashA));
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_mir_json_contract(65535, 1, 2), 167535);
const compilerZeroParseItemBytes = new TextEncoder().encode('import math\nuse lib\nconst answer: i32 = 42\nshape Point { x: i32 }\nenum Color { red }\nchoice Event { quit }\nextern c "x.h" as x\npub fun main() -> Void {}\ntest "ok" {}\n');
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroParseItemBytes, 16384);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_parse_item_summary(16384, compilerZeroParseItemBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 511);
const compilerZeroParseStmtBytes = new TextEncoder().encode('pub fun main() -> Void raises {\n let x = 1\n if x { check world.out.write("x") }\n while x { return }\n match x { ._ { raise Bad } }\n let y = call() rescue err { return }\n}\n');
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroParseStmtBytes, 20480);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_parse_stmt_expr_summary(20480, compilerZeroParseStmtBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 255);
const compilerZeroParseRootFixture = "conformance/parse/compiler-smoke.0";
const compilerZeroParseRootJson = await execFileAsync(zero, ["parse", "--json", compilerZeroParseRootFixture]);
const compilerZeroParseRootBody = JSON.parse(compilerZeroParseRootJson.stdout);
const compilerZeroParseRootExpected =
  compilerZeroParseRootBody.root.shapeCount * 1000000 +
  compilerZeroParseRootBody.root.enumCount * 10000 +
  compilerZeroParseRootBody.root.choiceCount * 100 +
  compilerZeroParseRootBody.root.functionCount;
const compilerZeroParseRootBytes = await readFile(compilerZeroParseRootFixture);
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroParseRootBytes, 24576);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_parse_root_summary(24576, compilerZeroParseRootBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), compilerZeroParseRootExpected);
const compilerZeroParsePublicBytes = new TextEncoder().encode('pub const answer: i32 = 42\npub shape Point { x: i32 }\npub enum Color { red }\npub choice Event { quit }\npub fun main() -> Void {}\nfun helper() -> Void {}\n');
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroParsePublicBytes, 28672);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_parse_public_api_summary(28672, compilerZeroParsePublicBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 11111);
const compilerZeroParseAstBytes = new TextEncoder().encode("pub const answer: i32 = 42\nshape Point { x: i32 }\npub fun main() -> Void { let x = 1 return }\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroParseAstBytes, 49152);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_parse_ast_summary(49152, compilerZeroParseAstBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength), 1010081);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_byte_buffer_plan(8, 2), 71);
const compilerZeroDiagSourceBytes = new TextEncoder().encode("pub fun main() -> Void {}\nmissing\n");
new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(compilerZeroDiagSourceBytes, 36864);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_diagnostic_packet_build(36864, compilerZeroDiagSourceBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength, 26, 3003, 1, 2), 2014203);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_diagnostic_detail_summary(1, 2, 3, 4, 5, 6), 123456);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_related_span_summary(2, 3, 7, 11, 4), 2037114);
const compilerZeroDiagFixture = "conformance/native/fail/unknown-std-helper.0";
const compilerZeroDiagJson = await execFileAsync(zero, ["check", "--json", compilerZeroDiagFixture]).catch((error) => error);
const compilerZeroDiagBody = JSON.parse(compilerZeroDiagJson.stdout);
const compilerZeroDiag = compilerZeroDiagBody.diagnostics[0];
assert.equal(compilerZeroDiag.code, "STD002");
assert.equal(compilerZeroDiag.fixSafety, "behavior-preserving");
assert.match(compilerZeroDiag.message, /unknown std helper/);
assert.match(compilerZeroDiag.expected, /known std helper/);
assert.match(compilerZeroDiag.actual, /unknown std helper/);
assert.match(compilerZeroDiag.help, /documented std helper/);
assert.equal(compilerZeroStage0Instance.instance.exports.compiler_diagnostic_detail_summary(1, 2, 3, 4, 5, 2), 123452);
assert.equal(compilerZeroDiag.related.length, 1);
assert.equal(
  compilerZeroStage0Instance.instance.exports.compiler_related_span_summary(compilerZeroDiag.line, compilerZeroDiag.column, compilerZeroDiag.related[0].line, compilerZeroDiag.related[0].column, 4),
  compilerZeroDiag.line * 1000000 + compilerZeroDiag.column * 10000 + compilerZeroDiag.related[0].line * 1000 + compilerZeroDiag.related[0].column * 10 + 4,
);
for (const fixture of [
  "conformance/check/fail/parse-missing-brace.0",
  "conformance/check/fail/parse-missing-comma.0",
  "conformance/check/fail/parse-bad-type-args.0",
  "conformance/check/pass/shape.0",
  "conformance/native/pass/parse-integers.0",
  "conformance/parse/compiler-smoke.0",
]) {
  const tokenJson = await execFileAsync(zero, ["tokens", "--json", fixture]);
  const tokenBody = JSON.parse(tokenJson.stdout);
  const expectedTokenSummary =
    tokenBody.tokens.length * 100 +
    tokenBody.tokens.filter((token) => token.kind === "keyword").length * 10 +
    tokenBody.tokens.filter((token) => token.kind === "symbol").length;
  const fixtureBytes = await readFile(fixture);
  new Uint8Array(compilerZeroStage0Instance.instance.exports.memory.buffer).set(fixtureBytes, 12288);
  assert.equal(
    compilerZeroStage0Instance.instance.exports.compiler_token_summary(12288, fixtureBytes.length, compilerZeroStage0Instance.instance.exports.memory.buffer.byteLength),
    expectedTokenSummary,
  );
}

const metaJsonSuccess = await execFileAsync(zero, ["check", "--json", "conformance/native/pass/meta-typed-target-type.0"]);
const metaJsonSuccessBody = JSON.parse(metaJsonSuccess.stdout);
assert.equal(metaJsonSuccessBody.ok, true);
assert.ok(metaJsonSuccessBody.metaCache.hits >= 1);
assert.ok(metaJsonSuccessBody.metaCache.misses >= 1);

const checkJsonFailure = await execFileAsync(zero, ["check", "--json", "conformance/native/fail/cast-bool-to-int.0"]).catch((error) => error);
assert.notEqual(checkJsonFailure.code, 0);
const checkJsonFailureBody = JSON.parse(checkJsonFailure.stdout);
assert.equal(checkJsonFailureBody.ok, false);
assert.equal(checkJsonFailureBody.diagnostics[0].code, "TYP017");
assert.match(checkJsonFailureBody.diagnostics[0].message, /cast requires/);
assert.match(checkJsonFailureBody.diagnostics[0].expected, /integer/);
assert.match(checkJsonFailureBody.diagnostics[0].actual, /Bool as i32/);
assert.match(checkJsonFailureBody.diagnostics[0].help, /cast only/);
assert.equal(checkJsonFailureBody.diagnostics[0].fixSafety, "requires-human-review");
assert.equal(checkJsonFailureBody.diagnostics[0].repair.id, "manual-review");

for (const [fixture, message] of [
  ["conformance/check/fail/parse-missing-brace.0", /expected '\}'/],
  ["conformance/check/fail/parse-missing-comma.0", /expected '\{'/],
  ["conformance/check/fail/parse-bad-type-args.0", /expected '\}'/],
]) {
  const parseFailure = await execFileAsync(zero, ["check", "--json", fixture]).catch((error) => error);
  assert.notEqual(parseFailure.code, 0);
  const parseFailureBody = JSON.parse(parseFailure.stdout);
  assert.equal(parseFailureBody.diagnostics[0].code, "PAR100");
  assert.match(parseFailureBody.diagnostics[0].message, message);
  assert.ok(parseFailureBody.diagnostics[0].line > 0);
  assert.ok(parseFailureBody.diagnostics[0].column > 0);
  assert.equal(parseFailureBody.diagnostics[0].fixSafety, "requires-human-review");
  assert.equal(parseFailureBody.diagnostics[0].repair.id, "repair-syntax");
}

const missingImportJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/missing-import"]).catch((error) => error);
assert.notEqual(missingImportJson.code, 0);
const missingImportBody = JSON.parse(missingImportJson.stdout);
assert.equal(missingImportBody.diagnostics[0].code, "IMP001");
assert.match(missingImportBody.diagnostics[0].message, /unknown package-local import/);
assert.equal(missingImportBody.diagnostics[0].fixSafety, "requires-human-review");

const importLineMapJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/import-line-map"]).catch((error) => error);
assert.notEqual(importLineMapJson.code, 0);
const importLineMapBody = JSON.parse(importLineMapJson.stdout);
assert.equal(importLineMapBody.diagnostics[0].code, "PAR100");
assert.equal(importLineMapBody.diagnostics[0].path, "conformance/check/fail/import-line-map/src/main.0");
assert.equal(importLineMapBody.diagnostics[0].line, 4);
assert.equal(importLineMapBody.diagnostics[0].column, 1);

const importedLineMapJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/imported-line-map"]).catch((error) => error);
assert.notEqual(importedLineMapJson.code, 0);
const importedLineMapBody = JSON.parse(importedLineMapJson.stdout);
assert.equal(importedLineMapBody.diagnostics[0].code, "TYP003");
assert.equal(importedLineMapBody.diagnostics[0].path, "conformance/check/fail/imported-line-map/src/helper.0");
assert.equal(importedLineMapBody.diagnostics[0].line, 2);
assert.equal(importedLineMapBody.diagnostics[0].column, 5);

const importFixLineMapPatch = await execFileAsync(zero, ["fix", "--patch", "--json", "conformance/check/fail/import-fix-line-map"]);
const importFixLineMapPatchBody = JSON.parse(importFixLineMapPatch.stdout);
assert.equal(importFixLineMapPatchBody.diagnostics[0].code, "TYP009");
assert.equal(importFixLineMapPatchBody.diagnostics[0].path, "conformance/check/fail/import-fix-line-map/src/helper.0");
assert.equal(importFixLineMapPatchBody.patches[0].path, "conformance/check/fail/import-fix-line-map/src/helper.0");
assert.equal(importFixLineMapPatchBody.patches[0].line, 2);
assert.match(importFixLineMapPatchBody.patches[0].new, /let mut values/);

const importCycleJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/import-cycle"]).catch((error) => error);
assert.notEqual(importCycleJson.code, 0);
const importCycleBody = JSON.parse(importCycleJson.stdout);
assert.equal(importCycleBody.diagnostics[0].code, "IMP002");
assert.match(importCycleBody.diagnostics[0].message, /import cycle/);
assert.match(importCycleBody.diagnostics[0].actual, /a -> b -> a/);

const duplicatePublicJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/duplicate-public"]).catch((error) => error);
assert.notEqual(duplicatePublicJson.code, 0);
const duplicatePublicBody = JSON.parse(duplicatePublicJson.stdout);
assert.equal(duplicatePublicBody.diagnostics[0].code, "IMP003");
assert.match(duplicatePublicBody.diagnostics[0].actual, /also exported/);

const unresolvedNestedImportJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/unresolved-nested-import"]).catch((error) => error);
assert.notEqual(unresolvedNestedImportJson.code, 0);
const unresolvedNestedImportBody = JSON.parse(unresolvedNestedImportJson.stdout);
assert.equal(unresolvedNestedImportBody.diagnostics[0].code, "IMP001");
assert.match(unresolvedNestedImportBody.diagnostics[0].expected, /src\/missing\.0/);

const duplicatePublicLargeJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/duplicate-public-large"]).catch((error) => error);
assert.notEqual(duplicatePublicLargeJson.code, 0);
const duplicatePublicLargeBody = JSON.parse(duplicatePublicLargeJson.stdout);
assert.equal(duplicatePublicLargeBody.diagnostics[0].code, "IMP003");
assert.match(duplicatePublicLargeBody.diagnostics[0].actual, /duplicateName/);

const genericConflictJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/generic-inference-conflict.0"]).catch((error) => error);
assert.notEqual(genericConflictJson.code, 0);
const genericConflictBody = JSON.parse(genericConflictJson.stdout);
assert.equal(genericConflictBody.diagnostics[0].code, "TYP024");
assert.match(genericConflictBody.diagnostics[0].message, /generic inference/);
assert.match(genericConflictBody.diagnostics[0].expected, /one concrete type/);
assert.match(genericConflictBody.diagnostics[0].actual, /u8/);

const genericArgCountJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/generic-type-arg-count.0"]).catch((error) => error);
assert.notEqual(genericArgCountJson.code, 0);
const genericArgCountBody = JSON.parse(genericArgCountJson.stdout);
assert.equal(genericArgCountBody.diagnostics[0].code, "TYP023");
assert.equal(genericArgCountBody.diagnostics[0].repair.id, "match-generic-type-arguments");

const genericCannotInferJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/generic-cannot-infer.0"]).catch((error) => error);
assert.notEqual(genericCannotInferJson.code, 0);
const genericCannotInferBody = JSON.parse(genericCannotInferJson.stdout);
assert.equal(genericCannotInferBody.diagnostics[0].code, "TYP025");
assert.equal(genericCannotInferBody.diagnostics[0].repair.id, "add-explicit-generic-type-arguments");

const constMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/const-type-mismatch.0"]).catch((error) => error);
assert.notEqual(constMismatchJson.code, 0);
const constMismatchBody = JSON.parse(constMismatchJson.stdout);
assert.equal(constMismatchBody.diagnostics[0].code, "TYP016");
assert.match(constMismatchBody.diagnostics[0].message, /out of range/);

const metaUnsupportedJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/meta-unsupported.0"]).catch((error) => error);
assert.notEqual(metaUnsupportedJson.code, 0);
const metaUnsupportedBody = JSON.parse(metaUnsupportedJson.stdout);
assert.equal(metaUnsupportedBody.diagnostics[0].code, "MET001");
assert.equal(metaUnsupportedBody.diagnostics[0].repair.id, "remove-unsupported-meta-expression");

const metaCycleJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/meta-cycle.0"]).catch((error) => error);
assert.notEqual(metaCycleJson.code, 0);
const metaCycleBody = JSON.parse(metaCycleJson.stdout);
assert.equal(metaCycleBody.diagnostics[0].code, "MET001");
assert.match(metaCycleBody.diagnostics[0].actual, /limit\/cycle/);

const aliasCycleJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/type-alias-cycle.0"]).catch((error) => error);
assert.notEqual(aliasCycleJson.code, 0);
const aliasCycleBody = JSON.parse(aliasCycleJson.stdout);
assert.equal(aliasCycleBody.diagnostics[0].code, "TYP026");
assert.match(aliasCycleBody.diagnostics[0].message, /alias cycle/);

const publicConstMissingTypeJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/public-const-missing-type.0"]).catch((error) => error);
assert.notEqual(publicConstMissingTypeJson.code, 0);
const publicConstMissingTypeBody = JSON.parse(publicConstMissingTypeJson.stdout);
assert.equal(publicConstMissingTypeBody.diagnostics[0].code, "PUB001");
assert.equal(publicConstMissingTypeBody.diagnostics[0].repair.id, "add-public-api-type");

const genericOwnedContainerJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/generic-owned-container.0"]).catch((error) => error);
assert.notEqual(genericOwnedContainerJson.code, 0);
const genericOwnedContainerBody = JSON.parse(genericOwnedContainerJson.stdout);
assert.equal(genericOwnedContainerBody.diagnostics[0].code, "OWN001");
assert.match(genericOwnedContainerBody.diagnostics[0].message, /generic containers cannot own generic payloads/);

const interfaceUnknownJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/interface-unknown.0"]).catch((error) => error);
assert.notEqual(interfaceUnknownJson.code, 0);
const interfaceUnknownBody = JSON.parse(interfaceUnknownJson.stdout);
assert.equal(interfaceUnknownBody.diagnostics[0].code, "IFC001");
assert.equal(interfaceUnknownBody.diagnostics[0].repair.id, "declare-or-use-static-interface");

const interfaceMissingMethodJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/interface-missing-method.0"]).catch((error) => error);
assert.notEqual(interfaceMissingMethodJson.code, 0);
const interfaceMissingMethodBody = JSON.parse(interfaceMissingMethodJson.stdout);
assert.equal(interfaceMissingMethodBody.diagnostics[0].code, "IFC002");
assert.match(interfaceMissingMethodBody.diagnostics[0].message, /Readable\.read/);
assert.match(interfaceMissingMethodBody.diagnostics[0].expected, /matching static method/);
assert.match(interfaceMissingMethodBody.diagnostics[0].actual, /method not found/);
assert.equal(interfaceMissingMethodBody.diagnostics[0].repair.id, "add-required-interface-method");

const interfaceArityMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/interface-arity-mismatch.0"]).catch((error) => error);
assert.notEqual(interfaceArityMismatchJson.code, 0);
const interfaceArityMismatchBody = JSON.parse(interfaceArityMismatchJson.stdout);
assert.equal(interfaceArityMismatchBody.diagnostics[0].code, "IFC003");

const interfaceReturnMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/interface-return-mismatch.0"]).catch((error) => error);
assert.notEqual(interfaceReturnMismatchJson.code, 0);
const interfaceReturnMismatchBody = JSON.parse(interfaceReturnMismatchJson.stdout);
assert.equal(interfaceReturnMismatchBody.diagnostics[0].code, "IFC004");

const interfaceParamMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/interface-param-mismatch.0"]).catch((error) => error);
assert.notEqual(interfaceParamMismatchJson.code, 0);
const interfaceParamMismatchBody = JSON.parse(interfaceParamMismatchJson.stdout);
assert.equal(interfaceParamMismatchBody.diagnostics[0].code, "IFC005");
assert.match(interfaceParamMismatchBody.diagnostics[0].expected, /ref<Counter>/);
assert.match(interfaceParamMismatchBody.diagnostics[0].actual, /i32/);

const staticUnsupportedTypeJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/static-value-unsupported-type.0"]).catch((error) => error);
assert.notEqual(staticUnsupportedTypeJson.code, 0);
const staticUnsupportedTypeBody = JSON.parse(staticUnsupportedTypeJson.stdout);
assert.equal(staticUnsupportedTypeBody.diagnostics[0].code, "STC001");
assert.equal(staticUnsupportedTypeBody.diagnostics[0].repair.id, "use-supported-static-value-type");

const staticNonConstantJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/static-value-non-constant.0"]).catch((error) => error);
assert.notEqual(staticNonConstantJson.code, 0);
const staticNonConstantBody = JSON.parse(staticNonConstantJson.stdout);
assert.equal(staticNonConstantBody.diagnostics[0].code, "STC002");
assert.equal(staticNonConstantBody.diagnostics[0].repair.id, "pass-constant-static-value");

const staticMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/static-value-mismatch.0"]).catch((error) => error);
assert.notEqual(staticMismatchJson.code, 0);
const staticMismatchBody = JSON.parse(staticMismatchJson.stdout);
assert.equal(staticMismatchBody.diagnostics[0].code, "STC003");
assert.match(staticMismatchBody.diagnostics[0].expected, /FixedVec<u8,8>/);
assert.match(staticMismatchBody.diagnostics[0].actual, /FixedVec<u8,cap>/);
assert.equal(staticMismatchBody.diagnostics[0].repair.id, "match-static-value-argument");

const staticBoolEnumMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/static-value-bool-enum-mismatch.0"]).catch((error) => error);
assert.notEqual(staticBoolEnumMismatchJson.code, 0);
const staticBoolEnumMismatchBody = JSON.parse(staticBoolEnumMismatchJson.stdout);
assert.equal(staticBoolEnumMismatchBody.diagnostics[0].code, "STC003");
assert.match(staticBoolEnumMismatchBody.diagnostics[0].expected, /Gate<false,Mode\.fast>/);
assert.match(staticBoolEnumMismatchBody.diagnostics[0].actual, /Gate<true,Mode\.fast>/);
assert.equal(staticBoolEnumMismatchBody.diagnostics[0].repair.id, "match-static-value-argument");

const shapeMethodCannotInferJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/generic-shape-method-cannot-infer.0"]).catch((error) => error);
assert.notEqual(shapeMethodCannotInferJson.code, 0);
const shapeMethodCannotInferBody = JSON.parse(shapeMethodCannotInferJson.stdout);
assert.equal(shapeMethodCannotInferBody.diagnostics[0].code, "SHM001");
assert.equal(shapeMethodCannotInferBody.diagnostics[0].repair.id, "bind-generic-shape-method");

const shapeMethodSelfMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/generic-shape-method-self-mismatch.0"]).catch((error) => error);
assert.notEqual(shapeMethodSelfMismatchJson.code, 0);
const shapeMethodSelfMismatchBody = JSON.parse(shapeMethodSelfMismatchJson.stdout);
assert.equal(shapeMethodSelfMismatchBody.diagnostics[0].code, "SHM002");
assert.equal(shapeMethodSelfMismatchBody.diagnostics[0].repair.id, "match-shape-method-self");

const selfOutsideShapeJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/self-outside-shape.0"]).catch((error) => error);
assert.notEqual(selfOutsideShapeJson.code, 0);
const selfOutsideShapeBody = JSON.parse(selfOutsideShapeJson.stdout);
assert.equal(selfOutsideShapeBody.diagnostics[0].code, "SHM001");
assert.match(selfOutsideShapeBody.diagnostics[0].message, /Self is only valid/);

const arrayLengthMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/array-literal-length-mismatch.0"]).catch((error) => error);
assert.notEqual(arrayLengthMismatchJson.code, 0);
const arrayLengthMismatchBody = JSON.parse(arrayLengthMismatchJson.stdout);
assert.equal(arrayLengthMismatchBody.diagnostics[0].code, "TYP002");
assert.match(arrayLengthMismatchBody.diagnostics[0].message, /array literal length/);
assert.match(arrayLengthMismatchBody.diagnostics[0].actual, /3 element/);

const arrayElementMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/array-literal-element-mismatch.0"]).catch((error) => error);
assert.notEqual(arrayElementMismatchJson.code, 0);
const arrayElementMismatchBody = JSON.parse(arrayElementMismatchJson.stdout);
assert.equal(arrayElementMismatchBody.diagnostics[0].code, "TYP002");
assert.match(arrayElementMismatchBody.diagnostics[0].message, /array literal element/);
assert.equal(arrayElementMismatchBody.diagnostics[0].expected, "u8");

const receiverMethodImmutableJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/receiver-method-immutable.0"]).catch((error) => error);
assert.notEqual(receiverMethodImmutableJson.code, 0);
const receiverMethodImmutableBody = JSON.parse(receiverMethodImmutableJson.stdout);
assert.equal(receiverMethodImmutableBody.diagnostics[0].code, "RCV002");
assert.equal(receiverMethodImmutableBody.diagnostics[0].repair.id, "make-receiver-addressable-or-mutable");

const receiverMethodUnknownJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/receiver-method-unknown.0"]).catch((error) => error);
assert.notEqual(receiverMethodUnknownJson.code, 0);
const receiverMethodUnknownBody = JSON.parse(receiverMethodUnknownJson.stdout);
assert.equal(receiverMethodUnknownBody.diagnostics[0].code, "RCV001");
assert.equal(receiverMethodUnknownBody.diagnostics[0].repair.id, "call-declared-receiver-method");

const shapeDefaultMissingRequiredJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/shape-default-missing-required.0"]).catch((error) => error);
assert.notEqual(shapeDefaultMissingRequiredJson.code, 0);
const shapeDefaultMissingRequiredBody = JSON.parse(shapeDefaultMissingRequiredJson.stdout);
assert.equal(shapeDefaultMissingRequiredBody.diagnostics[0].code, "FLD002");
assert.match(shapeDefaultMissingRequiredBody.diagnostics[0].help, /field/);

const shapeDefaultTypeMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/shape-default-type-mismatch.0"]).catch((error) => error);
assert.notEqual(shapeDefaultTypeMismatchJson.code, 0);
const shapeDefaultTypeMismatchBody = JSON.parse(shapeDefaultTypeMismatchJson.stdout);
assert.equal(shapeDefaultTypeMismatchBody.diagnostics[0].code, "TYP002");
assert.match(shapeDefaultTypeMismatchBody.diagnostics[0].message, /default/);

const matchFallbackPayloadJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/match-fallback-payload.0"]).catch((error) => error);
assert.notEqual(matchFallbackPayloadJson.code, 0);
const matchFallbackPayloadBody = JSON.parse(matchFallbackPayloadJson.stdout);
assert.equal(matchFallbackPayloadBody.diagnostics[0].code, "MAT004");
assert.match(matchFallbackPayloadBody.diagnostics[0].message, /fallback/);

const matchDuplicateFallbackJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/match-duplicate-fallback.0"]).catch((error) => error);
assert.notEqual(matchDuplicateFallbackJson.code, 0);
const matchDuplicateFallbackBody = JSON.parse(matchDuplicateFallbackJson.stdout);
assert.equal(matchDuplicateFallbackBody.diagnostics[0].code, "MAT003");
assert.match(matchDuplicateFallbackBody.diagnostics[0].message, /duplicate match fallback/);

const matchBoolNonExhaustiveJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/match-bool-non-exhaustive.0"]).catch((error) => error);
assert.notEqual(matchBoolNonExhaustiveJson.code, 0);
const matchBoolNonExhaustiveBody = JSON.parse(matchBoolNonExhaustiveJson.stdout);
assert.equal(matchBoolNonExhaustiveBody.diagnostics[0].code, "MAT002");
assert.match(matchBoolNonExhaustiveBody.diagnostics[0].message, /Bool/);

const matchU8NonExhaustiveJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/match-u8-non-exhaustive.0"]).catch((error) => error);
assert.notEqual(matchU8NonExhaustiveJson.code, 0);
const matchU8NonExhaustiveBody = JSON.parse(matchU8NonExhaustiveJson.stdout);
assert.equal(matchU8NonExhaustiveBody.diagnostics[0].code, "MAT002");
assert.match(matchU8NonExhaustiveBody.diagnostics[0].message, /u8/);

const matchGuardNonBoolJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/match-guard-non-bool.0"]).catch((error) => error);
assert.notEqual(matchGuardNonBoolJson.code, 0);
const matchGuardNonBoolBody = JSON.parse(matchGuardNonBoolJson.stdout);
assert.equal(matchGuardNonBoolBody.diagnostics[0].code, "MAT005");
assert.match(matchGuardNonBoolBody.diagnostics[0].message, /guard/);

const badTargetNameJson = await execFileAsync(zero, ["check", "--json", "--target", "not-a-target", "examples/hello.0"]).catch((error) => error);
assert.notEqual(badTargetNameJson.code, 0);
const badTargetNameBody = JSON.parse(badTargetNameJson.stdout);
assert.equal(badTargetNameBody.diagnostics[0].code, "TAR001");
assert.match(badTargetNameBody.diagnostics[0].message, /unknown target/);
assert.match(badTargetNameBody.diagnostics[0].expected, /zero targets/);
assert.equal(badTargetNameBody.diagnostics[0].actual, "not-a-target");
assert.match(badTargetNameBody.diagnostics[0].help, /zero targets/);
assert.equal(badTargetNameBody.diagnostics[0].fixSafety, "requires-human-review");
assert.equal(badTargetNameBody.diagnostics[0].repair.id, "manual-review");
assert.match(badTargetNameBody.diagnostics[0].related[0].message, /not-a-target/);

const explainTar002 = await execFileAsync(zero, ["explain", "--json", "TAR002"]);
const explainTar002Body = JSON.parse(explainTar002.stdout);
assert.equal(explainTar002Body.schemaVersion, 1);
assert.equal(explainTar002Body.code, "TAR002");
assert.equal(explainTar002Body.repair.id, "choose-target-with-required-capability");

const explainText = await execFileAsync(zero, ["explain", "TYP009"]);
assert.match(explainText.stdout, /Mutable storage required/);

const fixPlanJson = await execFileAsync(zero, ["fix", "--plan", "--json", "conformance/native/fail/mem-copy-immutable-dst.0"]);
const fixPlanBody = JSON.parse(fixPlanJson.stdout);
assert.equal(fixPlanBody.schemaVersion, 1);
assert.equal(fixPlanBody.mode, "plan");
assert.equal(fixPlanBody.appliesEdits, false);
assert.equal(fixPlanBody.fixes[0].id, "make-binding-mutable");
assert.equal(fixPlanBody.fixes[0].diagnosticCode, "TYP009");
assert.equal(fixPlanBody.fixes[0].safety, "behavior-preserving");
assert.equal(fixPlanBody.diagnostics[0].repair.id, "make-binding-mutable");

const genericFixPlanJson = await execFileAsync(zero, ["fix", "--plan", "--json", "conformance/check/fail/generic-cannot-infer.0"]);
const genericFixPlanBody = JSON.parse(genericFixPlanJson.stdout);
assert.equal(genericFixPlanBody.fixes[0].id, "add-explicit-generic-type-arguments");
assert.equal(genericFixPlanBody.fixes[0].diagnosticCode, "TYP025");
assert.equal(genericFixPlanBody.fixes[0].safety, "behavior-preserving");
assert.equal(genericFixPlanBody.diagnostics[0].repair.id, "add-explicit-generic-type-arguments");

const fixPatchJson = await execFileAsync(zero, ["fix", "--patch", "--json", "conformance/native/fail/mem-copy-immutable-dst.0"]);
const fixPatchBody = JSON.parse(fixPatchJson.stdout);
assert.equal(fixPatchBody.mode, "patch");
assert.equal(fixPatchBody.appliesEdits, false);
assert.equal(fixPatchBody.fixes[0].appliesEdits, true);
assert.match(fixPatchBody.patches[0].new, /let mut dst/);

const fixApplyFixture = `${outDir}/fix-apply-mutable.0`;
await writeFile(fixApplyFixture, await readFile("conformance/native/fail/mem-copy-immutable-dst.0", "utf8"));
const fixApplyJson = await execFileAsync(zero, ["fix", "--apply", "--json", fixApplyFixture]);
const fixApplyBody = JSON.parse(fixApplyJson.stdout);
assert.equal(fixApplyBody.mode, "apply");
assert.equal(fixApplyBody.applied, true);
assert.match(await readFile(fixApplyFixture, "utf8"), /let mut dst/);
await execFileAsync(zero, ["check", fixApplyFixture]);

const importGraph = await execFileAsync(zero, ["graph", "--json", "conformance/check/pass/imports"]);
const importGraphBody = JSON.parse(importGraph.stdout);
assert.deepEqual(importGraphBody.imports, ["math", "types"]);
assert.equal(importGraphBody.sourceFiles.length, 3);
assert.equal(importGraphBody.sourceMaps.length, 3);
assert(importGraphBody.sourceMaps.every((item) => item.columnUnit === "utf8-byte"));
assert.deepEqual(importGraphBody.targets.map((item) => item.name), ["cli"]);
assert.deepEqual(importGraphBody.modules.map((item) => item.name), ["math", "types", "main"]);
assert.deepEqual(importGraphBody.importEdges.map((item) => `${item.from}->${item.to}`), ["main->math", "main->types"]);
assert(importGraphBody.symbols.some((item) => item.module === "math" && item.name === "add_one"));
assert(importGraphBody.functions.some((item) => item.name === "main" && item.returnType === "Void" && item.effects.includes("world")));

const resourceGraph = await execFileAsync(zero, ["graph", "--json", "examples/resource-cli"]);
const resourceGraphBody = JSON.parse(resourceGraph.stdout);
assert(resourceGraphBody.targets.some((item) => item.name === "cli" && item.kind === "exe"));
assert.deepEqual(resourceGraphBody.importEdges.map((item) => `${item.from}->${item.to}`), ["main->config", "main->payload"]);
assert(resourceGraphBody.requiresCapabilities.includes("fs"));
assert(resourceGraphBody.functions.some((item) => item.name === "outputDir" && item.effects.includes("env")));
const resourceMainSymbol = resourceGraphBody.symbols.find((item) => item.name === "main");
assert(resourceMainSymbol.effects.includes("fs"));
assert.equal(resourceMainSymbol.allocationBehavior, "no heap allocation");
assert.equal(resourceMainSymbol.ownership.params[0].type, "World");

const memoryGraph = await execFileAsync(zero, ["graph", "--json", "--target", "linux-musl-x64", "examples/memory-package"]);
const memoryGraphBody = JSON.parse(memoryGraph.stdout);
assert.deepEqual(memoryGraphBody.importEdges.map((item) => `${item.from}->${item.to}`), ["main->buffer", "main->checksum"]);
assert(memoryGraphBody.requiresCapabilities.includes("memory"));
assert(!memoryGraphBody.requiresCapabilities.includes("fs"));
assert.equal(memoryGraphBody.targetSupport.fsAvailable, true);
assert.equal(memoryGraphBody.targetSupport.requiredCapabilitySupport.status, "supported");
assert.equal(memoryGraphBody.symbolCounts.public, 5);
assert(memoryGraphBody.stdlibHelpers.some((helper) => helper.name === "std.mem.copy" && helper.targetSupport === "target-neutral"));
assert(memoryGraphBody.stdlibHelpers.some((helper) => helper.name === "std.fs.createOrRaise" && helper.targetSupport === "host"));
const memCopyHelper = memoryGraphBody.stdlibHelpers.find((helper) => helper.name === "std.mem.copy");
assert.equal(memCopyHelper.module, "std.mem");
assert(memCopyHelper.effects.includes("memory"));
assert.equal(memCopyHelper.errorBehavior, "infallible");
assert.match(memCopyHelper.ownershipNotes, /caller-owned storage/);
assert.equal(memCopyHelper.example, "examples/memory-primitives.0");
assert.equal(memCopyHelper.apiStability, "bootstrap-stable");

const lexerTokens = await execFileAsync(zero, ["tokens", "--json", "conformance/lexer/compiler-smoke.0"]);
const lexerTokensBody = JSON.parse(lexerTokens.stdout);
assert.equal(lexerTokensBody.schemaVersion, 1);
assert.deepEqual(lexerTokensBody.tokens.slice(0, 4).map((token) => `${token.kind}:${token.text}`), [
  "keyword:use",
  "keyword:pub",
  "keyword:fun",
  "ident:main",
]);
assert.deepEqual(lexerTokensBody.tokens.slice(4, 8).map((token) => token.text), ["123", "0xff", "0b101", "42_u8"]);
assert.deepEqual(lexerTokensBody.tokens.slice(8, 12).map((token) => `${token.kind}:${token.text}`), [
  "string:hi",
  "char:120",
  "symbol:(",
  "symbol:)",
]);
assert.equal(lexerTokensBody.tokens[0].line, 1);
assert.equal(lexerTokensBody.tokens[0].column, 1);
assert.equal(lexerTokensBody.tokens[0].offset, 0);
assert.equal(lexerTokensBody.tokens[0].length, 3);
assert.equal(lexerTokensBody.tokens[5].offset, 21);
assert.equal(lexerTokensBody.tokens[5].length, 4);
assert.equal(lexerTokensBody.tokens[12].kind, "ident");
assert.equal(lexerTokensBody.tokens[12].text, "main");
assert.equal(lexerTokensBody.tokens[12].line, 2);
assert.equal(lexerTokensBody.tokens[12].column, 1);
assert.equal(lexerTokensBody.tokens.at(-1).kind, "eof");
assert.equal(lexerTokensBody.tokens.at(-1).length, 0);

const parseTree = await execFileAsync(zero, ["parse", "--json", "conformance/parse/compiler-smoke.0"]);
const parseTreeBody = JSON.parse(parseTree.stdout);
assert.equal(parseTreeBody.schemaVersion, 1);
assert.equal(parseTreeBody.root.kind, "module");
assert.equal(parseTreeBody.root.shapeCount, 1);
assert.equal(parseTreeBody.root.enumCount, 1);
assert.equal(parseTreeBody.root.choiceCount, 1);
assert.equal(parseTreeBody.root.functionCount, 1);
assert.equal(parseTreeBody.shapes[0].name, "Point");
assert.equal(parseTreeBody.enums[0].caseCount, 2);
assert.equal(parseTreeBody.choices[0].caseCount, 2);
assert.equal(parseTreeBody.functions[0].name, "main");
assert.equal(parseTreeBody.functions[0].paramCount, 1);
assert.deepEqual(parseTreeBody.functions[0].bodyKinds, ["if", "while", "check", "return"]);

const constGraph = await execFileAsync(zero, ["graph", "--json", "examples/const-arithmetic.0"]);
const constGraphBody = JSON.parse(constGraph.stdout);
assert(constGraphBody.consts.some((item) => item.name === "answer" && item.type === "i32"));
assert(constGraphBody.symbols.some((item) => item.name === "answer" && item.kind === "const" && item.public === false));

const genericPairGraph = await execFileAsync(zero, ["graph", "--json", "examples/generic-pair.0"]);
const genericPairGraphBody = JSON.parse(genericPairGraph.stdout);
assert(genericPairGraphBody.functions.some((item) => item.name === "makePair" && item.generic === true && item.returnType === "Pair<T,U>"));
assert(genericPairGraphBody.shapes.some((item) => item.name === "Pair" && item.generic === true && item.typeParams.join(",") === "T,U"));

const staticValueGraph = await execFileAsync(zero, ["graph", "--json", "examples/static-value-params.0"]);
const staticValueGraphBody = JSON.parse(staticValueGraph.stdout);
const fixedVecShape = staticValueGraphBody.shapes.find((item) => item.name === "FixedVec");
assert(fixedVecShape);
assert(fixedVecShape.staticParams.some((item) => item.name === "N" && item.type === "usize" && item.staticDispatch === true));
assert(staticValueGraphBody.functions.some((item) => item.name === "first" && item.staticParams.some((param) => param.name === "N")));

const fixedVecGraph = await execFileAsync(zero, ["graph", "--json", "examples/fixed-vec.0"]);
const fixedVecGraphBody = JSON.parse(fixedVecGraph.stdout);
const fixedVecMethodsShape = fixedVecGraphBody.shapes.find((item) => item.name === "FixedVec");
assert(fixedVecMethodsShape);
const pushMethod = fixedVecMethodsShape.methods.find((item) => item.name === "push");
assert(pushMethod);
assert.equal(pushMethod.inheritedShapeParams, true);
assert.deepEqual(pushMethod.shapeTypeParams, ["T", "N"]);
assert(pushMethod.shapeStaticParams.some((item) => item.name === "N" && item.staticDispatch === true));

const aliasGraph = await execFileAsync(zero, ["graph", "--json", "examples/type-alias.0"]);
const aliasGraphBody = JSON.parse(aliasGraph.stdout);
assert(aliasGraphBody.aliases.some((item) => item.name === "BytePair" && item.target === "Pair<u8,u8>"));
assert(aliasGraphBody.symbols.some((item) => item.name === "BytePair" && item.kind === "alias"));

const staticMethodGraph = await execFileAsync(zero, ["graph", "--json", "examples/static-method.0"]);
const staticMethodGraphBody = JSON.parse(staticMethodGraph.stdout);
const counterShape = staticMethodGraphBody.shapes.find((item) => item.name === "Counter");
assert(counterShape);
assert(counterShape.methods.some((item) => item.name === "add" && item.staticDispatch === true && item.returnType === "i32"));

const staticInterfaceGraph = await execFileAsync(zero, ["graph", "--json", "examples/static-interface.0"]);
const staticInterfaceGraphBody = JSON.parse(staticInterfaceGraph.stdout);
assert(staticInterfaceGraphBody.interfaces.some((item) => item.name === "Readable" && item.staticOnly === true));
assert(staticInterfaceGraphBody.functions.some((item) => item.name === "readValue" && item.constraints.some((constraint) => constraint.interface === "Readable<T>" && constraint.staticDispatch === true)));
assert(staticInterfaceGraphBody.symbols.some((item) => item.name === "Readable" && item.kind === "interface"));

const memorySize = await execFileAsync(zero, ["size", "--json", "--target", "linux-musl-x64", "examples/memory-package"]);
const memorySizeBody = JSON.parse(memorySize.stdout);
assert.equal(memorySizeBody.target, "linux-musl-x64");
assert.equal(memorySizeBody.targetSupport.fsAvailable, true);
assert(memorySizeBody.requiresCapabilities.includes("memory"));
assert(!memorySizeBody.requiresCapabilities.includes("fs"));
assert.equal(memorySizeBody.generatedCBytes, 0);
assert.equal(memorySizeBody.cBridgeFallback, false);
assert(memorySizeBody.loweredIrBytes > 0);
assert(memorySizeBody.sections.some((section) => section.name === "direct-size-metadata" && section.kind === "metadata"));
assert(memorySizeBody.objectBackend.objectEmission.path.includes("direct-"));
assert.equal(memorySizeBody.objectBackend.emitKind, "size");
assert(memorySizeBody.stdlibHelpers.some((helper) => helper.name === "std.mem.copy"));
assert(memorySizeBody.stdlibHelperAttribution.some((helper) => helper.name === "std.mem.copy" && helper.estimatedDirectBytes > 0));
assert(memorySizeBody.usedStdlibHelpers.every((helper) => helper.module && helper.effects?.length && helper.errorBehavior && helper.ownershipNotes && helper.example));
assert(memorySizeBody.runtimeShims.some((shim) => shim.name === "bounds-checks" && shim.payAsUsed === true));

const genericPairSize = await execFileAsync(zero, ["size", "--json", "examples/generic-pair.0"]);
const genericPairSizeBody = JSON.parse(genericPairSize.stdout);
assert(genericPairSizeBody.genericSpecializations.some((item) => item.name === "z_Pair_i32_u8_"));
assert(genericPairSizeBody.genericSpecializations.some((item) => item.name === "z_makePair__i32__u8"));

const targetsJson = await execFileAsync(zero, ["targets"]);
const targetsBody = JSON.parse(targetsJson.stdout);
const linuxMuslTarget = targetsBody.targets.find((item) => item.name === "linux-musl-x64");
const windowsMsvcTarget = targetsBody.targets.find((item) => item.aliases.includes("x86_64-windows-msvc"));
const wasiTarget = targetsBody.targets.find((item) => item.name === "wasm32-wasi");
const wasmWebTarget = targetsBody.targets.find((item) => item.name === "wasm32-web");
const linuxGnuTarget = targetsBody.targets.find((item) => item.name === "linux-x64");
const darwinArm64Target = targetsBody.targets.find((item) => item.name === "darwin-arm64");
const linuxArm64Target = targetsBody.targets.find((item) => item.name === "linux-arm64");
assert(linuxMuslTarget.capabilityFacts.some((item) => item.name === "fs" && item.available === true));
assert.equal(linuxMuslTarget.abi, "musl");
assert.equal(linuxMuslTarget.objectFormat, "elf");
assert.equal(linuxMuslTarget.toolchain.crossCompiler, "target-capable C compiler");
assert.equal(linuxMuslTarget.libcFacts.mode, "bundled-libc");
assert.equal(linuxMuslTarget.directBackend.status, "native-exe");
assert.equal(linuxMuslTarget.directBackend.objectSupported, true);
assert.equal(linuxMuslTarget.directBackend.exeSupported, true);
assert.equal(linuxMuslTarget.directBackend.objectEmitter, "zero-elf64");
assert.equal(linuxMuslTarget.directBackend.exeEmitter, "zero-elf64-exe");
assert.equal(linuxMuslTarget.directBackend.explicitDirectFallback, "never-c-bridge");
assert.equal(linuxMuslTarget.httpRuntime.status, "unsupported");
assert.equal(linuxMuslTarget.httpRuntime.provider, null);
assert.match(linuxMuslTarget.httpRuntime.reason, /lacks net/i);
assert.equal(windowsMsvcTarget.objectFormat, "coff");
assert.equal(windowsMsvcTarget.libcFacts.mode, "sysroot");
assert.equal(windowsMsvcTarget.libcFacts.sysrootStatus, "missing");
assert.equal(windowsMsvcTarget.directBackend.objectSupported, true);
assert.equal(windowsMsvcTarget.directBackend.exeSupported, true);
assert.equal(windowsMsvcTarget.directBackend.objectEmitter, "zero-coff-x64");
assert.equal(windowsMsvcTarget.directBackend.exeEmitter, "zero-coff-x64-exe");
assert.equal(wasiTarget.objectFormat, "wasm");
assert.equal(wasiTarget.libcFacts.mode, "bundled-libc");
assert.equal(wasiTarget.directBackend.status, "wasm-module");
assert.equal(wasmWebTarget.directBackend.objectEmitter, "zero-wasm");
assert.equal(linuxGnuTarget.directBackend.objectEmitter, "zero-elf64");
assert.equal(darwinArm64Target.directBackend.objectEmitter, "zero-macho64");
assert.equal(darwinArm64Target.directBackend.exeSupported, true);
assert.equal(darwinArm64Target.directBackend.exeEmitter, "zero-macho64-exe");
assert.equal(darwinArm64Target.httpRuntime.provider, targetsBody.host === "darwin-arm64" ? "curl" : null);
assert.equal(darwinArm64Target.httpRuntime.tlsVerification, targetsBody.host === "darwin-arm64");
if (targetsBody.host === "darwin-arm64") {
  assert.equal(darwinArm64Target.httpRuntime.customCa.env, "ZERO_HTTP_TEST_CA_BUNDLE");
}
assert.equal(linuxArm64Target.directBackend.status, "native-exe");
assert.equal(linuxArm64Target.directBackend.objectEmitter, "zero-elf-aarch64");
assert.equal(linuxArm64Target.directBackend.exeEmitter, "zero-elf-aarch64-exe");
assert.match(linuxArm64Target.directBackend.reason, /direct object and executable backend available/);

const zlsSelfTest = await execFileAsync("node", ["scripts/zls.mjs", "--self-test"]);
assert.match(zlsSelfTest.stdout, /zls self-test ok/);

const targetUnsupportedJson = await execFileAsync(zero, ["check", "--json", "--target", "wasm32-web", "conformance/native/fail/std-fs-target-unsupported.0"]).catch((error) => error);
assert.notEqual(targetUnsupportedJson.code, 0);
const targetUnsupportedBody = JSON.parse(targetUnsupportedJson.stdout);
assert.equal(targetUnsupportedBody.diagnostics[0].code, "TAR002");
assert.match(targetUnsupportedBody.diagnostics[0].expected, /Fs capability/);
assert.match(targetUnsupportedBody.diagnostics[0].actual, /wasm32-web lacks Fs/);
assert.match(targetUnsupportedBody.diagnostics[0].help, /remove hosted std\.fs/);
assert.equal(targetUnsupportedBody.diagnostics[0].fixSafety, "requires-human-review");
assert.equal(targetUnsupportedBody.diagnostics[0].repair.id, "choose-target-with-required-capability");
assert.match(targetUnsupportedBody.diagnostics[0].related[0].message, /lacks Fs/);

const targetUnsupportedFixPlan = await execFileAsync(zero, ["fix", "--plan", "--json", "--target", "wasm32-web", "conformance/native/fail/std-fs-target-unsupported.0"]);
const targetUnsupportedFixPlanBody = JSON.parse(targetUnsupportedFixPlan.stdout);
assert.equal(targetUnsupportedFixPlanBody.fixes[0].id, "choose-target-with-required-capability");
assert.equal(targetUnsupportedFixPlanBody.fixes[0].diagnosticCode, "TAR002");
assert.equal(targetUnsupportedFixPlanBody.fixes[0].safety, "requires-human-review");
assert.equal(targetUnsupportedFixPlanBody.diagnostics[0].repair.id, "choose-target-with-required-capability");

const targetNetUnsupportedJson = await execFileAsync(zero, ["check", "--json", "--target", "linux-musl-x64", "conformance/check/fail/target-net-unsupported.0"]).catch((error) => error);
assert.notEqual(targetNetUnsupportedJson.code, 0);
const targetNetUnsupportedBody = JSON.parse(targetNetUnsupportedJson.stdout);
assert.equal(targetNetUnsupportedBody.diagnostics[0].code, "TAR002");
assert.match(targetNetUnsupportedBody.diagnostics[0].actual, /lacks Net/);

const targetProcUnsupportedJson = await execFileAsync(zero, ["check", "--json", "--target", "linux-musl-x64", "conformance/check/fail/target-proc-unsupported.0"]).catch((error) => error);
assert.notEqual(targetProcUnsupportedJson.code, 0);
const targetProcUnsupportedBody = JSON.parse(targetProcUnsupportedJson.stdout);
assert.equal(targetProcUnsupportedBody.diagnostics[0].code, "TAR002");
assert.match(targetProcUnsupportedBody.diagnostics[0].actual, /lacks Proc/);

const routesJson = await execFileAsync(zero, ["routes", "--json", "examples/web/hello"]);
const routesBody = JSON.parse(routesJson.stdout);
assert.equal(routesBody.runtime, "wasm32-web");
assert.equal(routesBody.routeCount, 1);
assert.equal(routesBody.ownership.wasi.requestBody, "host-owned stream");
assert.equal(routesBody.ownership.browserWasm.filesystem, "unavailable");
assert.equal(routesBody.handlerAbi.request.type, "Request");
assert.equal(routesBody.handlerAbi.memory.requestAllocation, "zero by default");
assert.equal(routesBody.measurements.compressedSizeBudgetBytes, 10240);
assert.equal(routesBody.measurements.perRequestAllocationBudgetBytes, 0);
assert.equal(routesBody.artifact.available, true);
assert.equal(routesBody.artifact.generatedCBytes, 0);
assert.equal(routesBody.webBundle.javascriptFrameworkTaxBytes, 0);
assert(routesBody.webSurfaces.response.includes("stream"));
assert.equal(routesBody.artifactAudit.filesystem, "denied for browser wasm");
assert.equal(routesBody.localRuntime.runtimeKind, "browser-worker");
assert.equal(routesBody.localRuntime.providerSpecificDeployment, false);
assert.equal(routesBody.localRuntime.productionLikeImports, true);
assert.equal(routesBody.localRuntime.frameworkTaxBytes, 0);
assert.equal(routesBody.webBundle.deployment.providerSpecific, false);
assert.equal(routesBody.webBundle.deployment.vercel, "out-of-scope");

const cHeaderGraph = await execFileAsync(zero, ["graph", "--json", "conformance/check/pass/c-header-import.0"]);
const cHeaderGraphBody = JSON.parse(cHeaderGraph.stdout);
assert(cHeaderGraphBody.cImports.some((item) => item.header === "conformance/c/simple.h" && item.imports.functions >= 1 && item.cacheKey));
const simpleHeaderImport = cHeaderGraphBody.cImports.find((item) => item.header === "conformance/c/simple.h");
assert(simpleHeaderImport.typedModel.functions.some((item) => item.name === "zero_c_add" && item.params.length === 2));
assert(simpleHeaderImport.typedModel.constants.some((item) => item.name === "ZERO_C_ANSWER" && item.value === "42"));
assert(simpleHeaderImport.typedModel.structs.some((item) => item.name === "zero_c_point" && item.fields.some((field) => field.name === "y")));
assert(simpleHeaderImport.typedModel.enums.some((item) => item.name === "zero_c_color" && item.cases.some((entry) => entry.name === "ZERO_C_BLUE")));
assert(simpleHeaderImport.typedModel.typedefs.some((item) => item.name === "zero_c_int" && item.target === "int"));
assert.equal(typeof simpleHeaderImport.cache.target, "string");

const cInteropGraph = await execFileAsync(zero, ["graph", "--json", "examples/c-interop"]);
const cInteropGraphBody = JSON.parse(cInteropGraph.stdout);
const mathLib = cInteropGraphBody.cLibraries.find((item) => item.name === "math");
assert(mathLib);
assert.equal(mathLib.linkMode, "static");
assert.equal(mathLib.targetValidation.vendoredLibraries, true);
assert.equal(mathLib.targetValidation.vendoredHeaders, true);
assert.equal(mathLib.targetValidation.pkgConfigTargetSafe, true);
const cInteropCrossGraph = await execFileAsync(zero, ["graph", "--json", "--target", "linux-musl-x64", "examples/c-interop"]);
const cInteropCrossGraphBody = JSON.parse(cInteropCrossGraph.stdout);
assert.equal(cInteropCrossGraphBody.cLibraries[0].targetValidation.pkgConfigTargetSafe, true);
assert.equal(cInteropCrossGraphBody.cLibraries[0].targetValidation.implicitHostDiscovery, false);
assert.equal(cInteropCrossGraphBody.cLibraries[0].linkPlan.hostDiscovery, "none");
const hostLeakGraph = await execFileAsync(zero, ["graph", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package"]);
const hostLeakGraphBody = JSON.parse(hostLeakGraph.stdout);
assert.equal(hostLeakGraphBody.cLibraries[0].targetValidation.hostHeaderLeakage, true);
assert.equal(hostLeakGraphBody.cLibraries[0].targetValidation.implicitHostDiscovery, true);
assert.equal(hostLeakGraphBody.cLibraries[0].targetValidation.status, "blocked");
const hostLeakBuild = await execFileAsync(zero, ["build", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package", "--out", ".zero/out/host-leak-package"], { encoding: "utf8" }).catch((error) => error);
assert.notEqual(hostLeakBuild.code, 0);
const hostLeakBuildBody = JSON.parse(hostLeakBuild.stdout);
assert.equal(hostLeakBuildBody.diagnostics[0].code, "CIMP003");
assert.match(hostLeakBuildBody.diagnostics[0].help, /target sysroot|vendored/);

const depGraph = await execFileAsync(zero, ["graph", "--json", "--target", "linux-musl-x64", "conformance/packages/dep-app"]);
const depGraphBody = JSON.parse(depGraph.stdout);
assert.equal(depGraphBody.package.name, "dep-app");
assert.equal(depGraphBody.package.version, "0.1.0");
assert.equal(depGraphBody.package.resolver.deterministic, true);
assert.match(depGraphBody.package.lockfile.path, /\.zero\/package-locks\/[0-9a-f]+\.lock\.json/);
assert(depGraphBody.package.dependencies.some((item) => item.name === "dep-lib" && item.status === "path-resolved" && item.targetCompatible === true));
assert(depGraphBody.package.dependencies.some((item) => item.name === "remote-tools" && item.status === "registry-reference" && item.version === "1.2.3"));
assert.equal(depGraphBody.packageCache.cacheKeyInputs.compilerVersion, "0.1.2");
assert.equal(depGraphBody.packageCache.cacheKeyInputs.packageVersion, "0.1.0");
assert.match(depGraphBody.packageCache.cacheKeyInputs.dependencyGraphHash, /^[0-9a-f]{16}$/);
const depDoc = await execFileAsync(zero, ["doc", "--json", "conformance/packages/dep-app"]);
const depDocBody = JSON.parse(depDoc.stdout);
assert.equal(depDocBody.package.name, "dep-app");
assert.equal(depDocBody.publicationGate.requiresExamplesForPublicApi, true);
assert.equal(depDocBody.packageCache.cacheKeyInputs.packageVersion, "0.1.0");
const depBuild = await execFileAsync(zero, ["build", "--json", "--target", "linux-musl-x64", "conformance/packages/dep-app", "--out", ".zero/out/dep-app"]);
const depBuildBody = JSON.parse(depBuild.stdout);
assert.equal(depBuildBody.package.dependencies.length, 2);
assert.equal(depBuildBody.packageCache.invalidationReasons.includes("dependency graph changed"), true);
assert.match(depBuildBody.compilerCaches[0].dependencyGraphHash, /^[0-9a-f]{16}$/);
const targetGraph = await execFileAsync(zero, ["graph", "--json", "--target", "linux-musl-x64", "conformance/packages/target-incompatible-app"]);
const targetGraphBody = JSON.parse(targetGraph.stdout);
assert.equal(targetGraphBody.package.dependencies[0].targetCompatible, false);
const missingDep = await execFileAsync(zero, ["check", "--json", "conformance/packages/missing-dep-app"]).catch((error) => error);
assert.notEqual(missingDep.code, 0);
assert.equal(JSON.parse(missingDep.stdout).diagnostics[0].code, "PKG001");
const cycleDep = await execFileAsync(zero, ["check", "--json", "conformance/packages/cycle-a"]).catch((error) => error);
assert.notEqual(cycleDep.code, 0);
assert.equal(JSON.parse(cycleDep.stdout).diagnostics[0].code, "PKG002");
const conflictDep = await execFileAsync(zero, ["check", "--json", "conformance/packages/conflict-app"]).catch((error) => error);
assert.notEqual(conflictDep.code, 0);
assert.equal(JSON.parse(conflictDep.stdout).diagnostics[0].code, "PKG003");
const targetIncompatibleDep = await execFileAsync(zero, ["check", "--json", "--target", "linux-musl-x64", "conformance/packages/target-incompatible-app"]).catch((error) => error);
assert.notEqual(targetIncompatibleDep.code, 0);
assert.equal(JSON.parse(targetIncompatibleDep.stdout).diagnostics[0].code, "PKG004");

const cHeaderUnsupportedJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/c-header-unsupported.0"]).catch((error) => error);
assert.notEqual(cHeaderUnsupportedJson.code, 0);
const cHeaderUnsupportedBody = JSON.parse(cHeaderUnsupportedJson.stdout);
assert.equal(cHeaderUnsupportedBody.diagnostics[0].code, "CIMP002");
assert.match(cHeaderUnsupportedBody.diagnostics[0].actual, /variadic/);

const errorSetFixPlan = await execFileAsync(zero, ["fix", "--plan", "--json", "conformance/native/fail/std-fs-error-set-mismatch.0"]);
const errorSetFixPlanBody = JSON.parse(errorSetFixPlan.stdout);
assert.equal(errorSetFixPlanBody.fixes[0].id, "add-missing-error-name");
assert.equal(errorSetFixPlanBody.fixes[0].diagnosticCode, "ERR002");
assert.equal(errorSetFixPlanBody.fixes[0].safety, "api-changing");
assert.equal(errorSetFixPlanBody.diagnostics[0].repair.id, "add-missing-error-name");

const parseFixPlan = await execFileAsync(zero, ["fix", "--plan", "--json", "conformance/check/fail/parse-missing-brace.0"]);
const parseFixPlanBody = JSON.parse(parseFixPlan.stdout);
assert.equal(parseFixPlanBody.fixes[0].id, "repair-syntax");
assert.equal(parseFixPlanBody.fixes[0].diagnosticCode, "PAR100");

const nameFixPlan = await execFileAsync(zero, ["fix", "--plan", "--json", "conformance/check/fail/unknown-name.0"]);
const nameFixPlanBody = JSON.parse(nameFixPlan.stdout);
assert.equal(nameFixPlanBody.fixes[0].id, "declare-missing-symbol");
assert.equal(nameFixPlanBody.fixes[0].diagnosticCode, "NAM003");

const typeFixPlan = await execFileAsync(zero, ["fix", "--plan", "--json", "conformance/check/fail/wrong-return-type.0"]);
const typeFixPlanBody = JSON.parse(typeFixPlan.stdout);
assert.equal(typeFixPlanBody.fixes[0].id, "match-return-type");
assert.equal(typeFixPlanBody.fixes[0].diagnosticCode, "TYP003");

const ownershipFixPlan = await execFileAsync(zero, ["fix", "--plan", "--json", "conformance/native/fail/owned-use-after-move.0"]);
const ownershipFixPlanBody = JSON.parse(ownershipFixPlan.stdout);
assert.equal(ownershipFixPlanBody.fixes[0].id, "avoid-use-after-move");
assert.equal(ownershipFixPlanBody.fixes[0].diagnosticCode, "OWN001");

const abiFixPlan = await execFileAsync(zero, ["fix", "--plan", "--json", "conformance/check/fail/export-c-raises.0"]);
const abiFixPlanBody = JSON.parse(abiFixPlan.stdout);
assert.equal(abiFixPlanBody.fixes[0].id, "make-c-abi-safe");
assert.equal(abiFixPlanBody.fixes[0].diagnosticCode, "ABI001");

const uncheckedFallibleJson = await execFileAsync(zero, ["check", "--json", "conformance/native/fail/unchecked-fallible-call.0"]).catch((error) => error);
assert.notEqual(uncheckedFallibleJson.code, 0);
const uncheckedFallibleBody = JSON.parse(uncheckedFallibleJson.stdout);
assert.equal(uncheckedFallibleBody.diagnostics[0].code, "ERR003");
assert.equal(uncheckedFallibleBody.diagnostics[0].fixSafety, "api-changing");

const errorSetMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/native/fail/error-set-mismatch.0"]).catch((error) => error);
assert.notEqual(errorSetMismatchJson.code, 0);
const errorSetMismatchBody = JSON.parse(errorSetMismatchJson.stdout);
assert.equal(errorSetMismatchBody.diagnostics[0].code, "ERR002");
assert.match(errorSetMismatchBody.diagnostics[0].expected, /caller raises/);
assert.equal(errorSetMismatchBody.diagnostics[0].fixSafety, "api-changing");

const stdFsErrorMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/native/fail/std-fs-error-set-mismatch.0"]).catch((error) => error);
assert.notEqual(stdFsErrorMismatchJson.code, 0);
const stdFsErrorMismatchBody = JSON.parse(stdFsErrorMismatchJson.stdout);
assert.equal(stdFsErrorMismatchBody.diagnostics[0].code, "ERR002");
assert.match(stdFsErrorMismatchBody.diagnostics[0].actual, /std fs call may raise/);

const stdFsCreateErrorMismatchJson = await execFileAsync(zero, ["check", "--json", "conformance/native/fail/std-fs-create-error-set-mismatch.0"]).catch((error) => error);
assert.notEqual(stdFsCreateErrorMismatchJson.code, 0);
const stdFsCreateErrorMismatchBody = JSON.parse(stdFsCreateErrorMismatchJson.stdout);
assert.equal(stdFsCreateErrorMismatchBody.diagnostics[0].code, "ERR002");
assert.match(stdFsCreateErrorMismatchBody.diagnostics[0].help, /raises/);

const stdFsUncheckedResourceJson = await execFileAsync(zero, ["check", "--json", "conformance/native/fail/std-fs-unchecked-resource-fallible.0"]).catch((error) => error);
assert.notEqual(stdFsUncheckedResourceJson.code, 0);
const stdFsUncheckedResourceBody = JSON.parse(stdFsUncheckedResourceJson.stdout);
assert.equal(stdFsUncheckedResourceBody.diagnostics[0].code, "ERR003");
assert.equal(stdFsUncheckedResourceBody.diagnostics[0].fixSafety, "api-changing");
assert.equal(stdFsUncheckedResourceBody.diagnostics[0].repair.id, "check-or-rescue-fallible-call");

const badManifestKindJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/bad-manifest-kind"]).catch((error) => error);
assert.notEqual(badManifestKindJson.code, 0);
assert.equal(JSON.parse(badManifestKindJson.stdout).diagnostics[0].code, "BLD002");

const missingMainJson = await execFileAsync(zero, ["check", "--json", "conformance/check/fail/missing-main-src"]).catch((error) => error);
assert.notEqual(missingMainJson.code, 0);
assert.equal(JSON.parse(missingMainJson.stdout).diagnostics[0].code, "BLD002");

const readAllInvalidAllocJson = await execFileAsync(zero, ["check", "--json", "conformance/native/fail/fs-readall-invalid-alloc.0"]).catch((error) => error);
assert.notEqual(readAllInvalidAllocJson.code, 0);
const readAllInvalidAllocBody = JSON.parse(readAllInvalidAllocJson.stdout);
assert.equal(readAllInvalidAllocBody.diagnostics[0].code, "STD003");
assert.match(readAllInvalidAllocBody.diagnostics[0].message, /readAll expects an allocator/);

const zeroTestRun = await execFileAsync(zero, ["test", "conformance/native/pass/test-blocks.0"]);
assert.equal(zeroTestRun.stdout, "1 test(s) ok\n");

const zeroTestJsonRun = await execFileAsync(zero, ["test", "--json", "--filter", "addition", "conformance/native/pass/test-blocks.0"]);
const zeroTestJsonBody = JSON.parse(zeroTestJsonRun.stdout);
assert.equal(zeroTestJsonBody.ok, true);
assert.equal(zeroTestJsonBody.discoveredTests, 1);
assert.equal(zeroTestJsonBody.selectedTests, 1);
assert.equal(zeroTestJsonBody.passedTests, 1);
assert.equal(zeroTestJsonBody.testDiscovery.filter, "addition");
assert.equal(zeroTestJsonBody.fixtures.snapshotKey, "zero-test-direct-frontend-v1");
assert.equal(zeroTestJsonBody.results[0].status, "passed");

const zeroPackageTestJsonRun = await execFileAsync(zero, ["test", "--json", "conformance/packages/test-app"]);
const zeroPackageTestBody = JSON.parse(zeroPackageTestJsonRun.stdout);
assert.equal(zeroPackageTestBody.ok, true);
assert.equal(zeroPackageTestBody.testDiscovery.mode, "package");
assert.equal(zeroPackageTestBody.discoveredTests, 3);
assert.equal(zeroPackageTestBody.selectedTests, 3);
assert.equal(zeroPackageTestBody.expectedFailures, 1);
assert(zeroPackageTestBody.fixtures.sourceFiles.some((path) => path.endsWith("helper.0")));

const zeroExpectedFailJsonRun = await execFileAsync(zero, ["test", "--json", "conformance/native/pass/test-expected-fail.0"]);
const zeroExpectedFailBody = JSON.parse(zeroExpectedFailJsonRun.stdout);
assert.equal(zeroExpectedFailBody.ok, true);
assert.equal(zeroExpectedFailBody.expectedFailures, 1);
assert.equal(zeroExpectedFailBody.failedTests, 0);
assert.equal(zeroExpectedFailBody.results[0].status, "expected-fail");

const zeroUnexpectedPassJsonRun = await execFileAsync(zero, ["test", "--json", "conformance/native/fail/test-unexpected-pass.0"]).catch((error) => error);
assert.notEqual(zeroUnexpectedPassJsonRun.code, 0);
const zeroUnexpectedPassBody = JSON.parse(zeroUnexpectedPassJsonRun.stdout);
assert.equal(zeroUnexpectedPassBody.ok, false);
assert.equal(zeroUnexpectedPassBody.unexpectedPasses, 1);
assert.equal(zeroUnexpectedPassBody.results[0].status, "unexpected-pass");

const zeroTestFailureRun = await execFileAsync(zero, ["test", "conformance/native/fail/test-expect-runtime-fail.0"]).catch((error) => error);
assert.notEqual(zeroTestFailureRun.code, 0);
assert.match(zeroTestFailureRun.stderr, /zero test expectation failed/);
assert.match(zeroTestFailureRun.stderr, /expect runtime failure exits nonzero/);

const fmtRun = await execFileAsync(zero, ["fmt", "conformance/native/pass/test-blocks.0"]);
assert.equal(fmtRun.stdout, await readFile("conformance/native/pass/test-blocks.0", "utf8"));

const fmtFallibilityRun = await execFileAsync(zero, ["fmt", "conformance/native/pass/fallibility-check-value.0"]);
assert.equal(fmtFallibilityRun.stdout, await readFile("conformance/native/pass/fallibility-check-value.0", "utf8"));

const fmtImportsRun = await execFileAsync(zero, ["fmt", "conformance/check/pass/imports/src/main.0"]);
assert.match(fmtImportsRun.stdout, /use math/);
assert.match(fmtImportsRun.stdout, /use types/);

const fmtCoreRun = await execFileAsync(zero, ["fmt", "conformance/check/pass/fmt-core-usability.0"]);
assert.equal(fmtCoreRun.stdout, `type ByteCount = usize

shape Counter {
    value: i32,
    fun add(self: ref<Self>, amount: i32) -> i32 {
        return self.value + amount
    }
}

enum State {
    ready,
    done,
}

pub fun main() -> Void {
    let count: ByteCount = 42_usize
    let counter: Counter = Counter { value: 40 }
    let state: State = State.ready
    match state {
        .ready {
            expect(Counter.add(&counter, 2) == 42)
        }
        ._ {
            expect(count == 42_usize)
        }
    }
}
`);

const fmtMessyRun = await execFileAsync(zero, ["fmt", "conformance/format/messy.0"]);
assert.equal(fmtMessyRun.stdout, `pub fun main(world: World) -> Void raises {
    check world.out.write("format me\\n")
}
`);

const fmtSyntaxLocalRun = await execFileAsync(zero, ["fmt", "conformance/format/syntax-local.0"]);
assert.equal(fmtSyntaxLocalRun.stdout, `pub fun main() -> Void {
    let text = "{not a block}"
    // comment { also not a block }
    unknown_name(text)
}
`);
const fmtIdempotentPath = `${outDir}/fmt-idempotent.0`;
await writeFile(fmtIdempotentPath, fmtSyntaxLocalRun.stdout);
const fmtIdempotentRun = await execFileAsync(zero, ["fmt", fmtIdempotentPath]);
assert.equal(fmtIdempotentRun.stdout, fmtSyntaxLocalRun.stdout);

const fmtFunctionsBlocksRun = await execFileAsync(zero, ["fmt", "conformance/format/functions-blocks.0"]);
assert.equal(fmtFunctionsBlocksRun.stdout, `fun helper(value: i32) -> i32 {
    if value == 0 {
        return 1
    } else {
        return value
    }
}
pub fun main() -> Void {
    let count = helper(1)
    while count != 0 {
        break
    }
}
`);

const fmtDataTypesRun = await execFileAsync(zero, ["fmt", "conformance/format/data-types.0"]);
assert.equal(fmtDataTypesRun.stdout, `shape Point {
    x: i32,
    y: i32,
}
enum State {
    ready,
    done,
}
choice Event {
    key: i32,
    quit,
}
`);

const fmtGenericsStaticRun = await execFileAsync(zero, ["fmt", "conformance/format/generics-static.0"]);
assert.equal(fmtGenericsStaticRun.stdout, `shape Box<T, static N: usize> {
    items: [N]T,
}
fun first<T, static N: usize>(box: ref<Box<T, N>>) -> T {
    return box.items[0]
}
pub fun main() -> Void {
    let value: Box<u8, 4> = Box { items: [1, 2, 3, 4] }
}
`);

const fmtMatchRun = await execFileAsync(zero, ["fmt", "conformance/format/match-expressions.0"]);
assert.equal(fmtMatchRun.stdout, `choice Event {
    key: i32,
    quit,
}
pub fun main() -> Void {
    let event: Event = Event.key(7)
    match event {
        .key => code {
            expect(code == 7)
        }
        ._ {
            expect(false == true)
        }
    }
}
`);

const helloRunArgs = runnableExeArgs("conformance/run/pass/hello.0", `${outDir}/hello`);
if (helloRunArgs) {
  await rm(`${outDir}/hello`, { force: true });
  await execFileAsync(zero, helloRunArgs);
  const run = await execFileAsync(`${outDir}/hello`, []);
  assert.match(run.stdout, /hello conformance/);
}

const packageGraphJson = await execFileAsync(zero, ["graph", "--json", "conformance/check/pass/package"]);
const packageGraph = JSON.parse(packageGraphJson.stdout);
assert.deepEqual(packageGraph.sourceFiles.sort(), [
  "conformance/check/pass/package/src/main.0",
  "conformance/check/pass/package/src/types.0",
]);
assert(packageGraph.requiresCapabilities.includes("codec"));
assert(packageGraph.requiresCapabilities.includes("parse"));
assert(packageGraph.requiresCapabilities.includes("time"));
assert.equal(packageGraph.selfHostRouting.cBridge.policy, "removed");

for (const runtimeFixture of [
  ["conformance/native/pass/break-continue.0", "break-continue", { stdout: "loop tick\nloop tick\n" }],
  ["conformance/native/pass/for-range.0", "for-range", { stdout: "range tick\nrange tick\nrange tick\n" }],
  ["conformance/native/pass/match-payload-binding.0", "match-payload-binding", { stdout: /payload binding ok/ }],
  ["conformance/native/pass/match-fallback.0", "match-fallback", { stdout: "match fallback ok\n" }],
  ["conformance/native/pass/match-choice-fallback.0", "match-choice-fallback", { stdout: "choice fallback ok\n" }],
  ["conformance/native/pass/null-maybe.0", "null-maybe", { stdout: /null maybe ok/ }],
  ["conformance/native/pass/std-args.0", "std-args", { stdout: "alpha\n", args: ["alpha", "beta"] }],
  ["conformance/native/pass/std-env.0", "std-env", { stdout: "env ok\n", env: { ZERO_CONFORMANCE_ENV: "agent-env" } }],
  ["conformance/native/pass/std-fs.0", "std-fs", { stdout: "fs ok\n", file: { name: "std-fs-write.txt", text: "zero write\n" } }],
  ["conformance/native/pass/std-fs-bytes.0", "std-fs-bytes", { stdout: "fs bytes ok\n", stderr: "fs bytes err ok\n" }],
  ["conformance/native/pass/std-fs-resource.0", "std-fs-resource", { stdout: "fs resource ok\n", file: { name: "std-fs-resource.txt", text: "zero file\n" } }],
  ["conformance/native/pass/integer-widths.0", "integer-widths", { stdout: "integer widths ok\n" }],
  ["conformance/native/pass/std-codec-widths.0", "std-codec-widths", { stdout: "codec widths ok\n" }],
  ["conformance/native/pass/parse-integers.0", "parse-integers", { stdout: "parse integers ok\n" }],
  ["conformance/native/pass/explicit-casts.0", "explicit-casts", { stdout: "explicit casts ok\n" }],
  ["conformance/native/pass/float-char-casts.0", "float-char-casts", { stdout: "float char casts ok\n" }],
  ["conformance/native/pass/radix-suffix-literals.0", "radix-suffix-literals", { stdout: "radix suffix literals ok\n" }],
  ["conformance/native/pass/char-literals.0", "char-literals", { stdout: "char literals ok\n" }],
  ["conformance/native/pass/float-primitives.0", "float-primitives", { stdout: "float primitives ok\n" }],
  ["conformance/native/pass/checked-bounds-get.0", "checked-bounds-get", { stdout: "checked bounds get ok\n" }],
  ["conformance/native/pass/check-maybe-fallibility.0", "check-maybe-fallibility", { stdout: "check maybe fallibility ok\n" }],
  ["conformance/native/pass/fallibility-error-sets.0", "fallibility-error-sets", { stdout: "fallibility error sets ok\n" }],
  ["conformance/native/pass/rescue-check.0", "rescue-check", { stdout: "rescue ok\n" }],
  ["conformance/native/pass/std-fs-fallible.0", "std-fs-fallible", { stdout: "fs named errors ok\n" }],
  ["conformance/native/pass/std-fs-fallible-resources.0", "std-fs-fallible-resources", { stdout: "fs fallible resources ok\n" }],
  ["conformance/native/pass/std-cli-helpers.0", "std-cli-helpers", { stdout: "cli helpers ok\n" }],
  ["conformance/native/pass/std-mem-copy-fill.0", "std-mem-copy-fill", { stdout: "mem copy fill ok\n" }],
  ["conformance/native/pass/const-layout.0", "const-layout", { stdout: "const layout ok\n" }],
  ["conformance/native/pass/c-abi-export.0", "c-abi-export", { stdout: "c abi export ok\n" }],
]) {
  await assertDirectRuntimeOrUnsupported(...runtimeFixture);
}

const abiDump = await execFileAsync(zero, ["abi", "dump", "--json", "conformance/native/pass/const-layout.0"]);
const abiDumpBody = JSON.parse(abiDump.stdout);
assert.equal(abiDumpBody.schemaVersion, 1);
assert.equal(abiDumpBody.pointerSize, 8);
assert(abiDumpBody.externShapes.some((item) => item.name === "CPoint" && item.size === 8 && item.fields[1].offset === 4));
assert(abiDumpBody.primitiveLayouts.some((item) => item.name === "usize" && item.size === 8));
const cAbiDump = await execFileAsync(zero, ["abi", "dump", "--json", "conformance/native/pass/c-abi-export.0"]);
const cAbiDumpBody = JSON.parse(cAbiDump.stdout);
assert(cAbiDumpBody.cExports.some((item) => item.name === "zero_add" && item.cReturnType === "int32_t"));
assert.equal(cAbiDumpBody.generatedHeader.available, true);
assert.match(cAbiDumpBody.generatedHeader.text, /int32_t zero_add\(int32_t a, int32_t b\);/);
const abiCheck = await execFileAsync(zero, ["abi", "check", "--json", "conformance/native/pass/const-layout.0"]);
assert.equal(JSON.parse(abiCheck.stdout).ok, true);

for (const runtimeFixture of [
  ["conformance/native/pass/indexing-primitives.0", "indexing-primitives", { stdout: "indexing primitives ok\n" }],
  ["conformance/native/pass/range-slices.0", "range-slices", { stdout: "range slices ok\n" }],
  ["conformance/native/pass/generic-spans.0", "generic-spans", { stdout: "generic spans ok\n" }],
  ["conformance/native/pass/open-ended-slices.0", "open-ended-slices", { stdout: "open ended slices ok\n" }],
  ["conformance/native/pass/string-slices.0", "string-slices", { stdout: "string slices ok\n" }],
  ["conformance/native/pass/indexed-mutation.0", "indexed-mutation", { stdout: "indexed mutation ok\n" }],
  ["conformance/native/pass/nested-lvalues.0", "nested-lvalues", { stdout: "nested lvalues ok\n" }],
  ["conformance/native/pass/mutable-spans.0", "mutable-spans", { stdout: "mutable spans ok\n" }],
  ["conformance/native/pass/mutref-indexed-lvalues.0", "mutref-indexed-lvalues", { stdout: "mutref indexed lvalues ok\n" }],
  ["conformance/native/pass/generic-mem.0", "generic-mem", { stdout: "generic mem ok\n" }],
  ["conformance/native/pass/generic-nested-calls.0", "generic-nested-calls", { stdout: "generic nested calls ok\n" }],
  ["conformance/native/pass/static-interface-mutref.0", "static-interface-mutref", { stdout: "static interface mutref ok\n" }],
  ["conformance/native/pass/owned-transfer.0", "owned-transfer", { stdout: "owned transfer ok\n" }],
  ["conformance/native/pass/owned-drop-cleanup.0", "owned-drop-cleanup", { stdout: "owned drop cleanup ok\n" }],
  ["conformance/native/pass/owned-drop-move-suppressed.0", "owned-drop-move-suppressed", { stdout: "owned drop move suppressed ok\n" }],
  ["conformance/native/pass/borrow-primitives.0", "borrow-primitives", { stdout: "borrow primitives ok\n" }],
  ["conformance/native/pass/allocator-primitives.0", "allocator-primitives", { stdout: "allocator primitives ok\n" }],
  ["conformance/native/pass/owned-byte-buffer.0", "owned-byte-buffer", { stdout: "owned byte buffer ok\n" }],
]) {
  await assertDirectRuntimeOrUnsupported(...runtimeFixture);
}

await assertBoundsTrap("conformance/native/fail/bounds-array-index.0", "bounds-array-index");
await assertBoundsTrap("conformance/native/fail/bounds-span-index.0", "bounds-span-index");
await assertBoundsTrap("conformance/native/fail/bounds-slice-end.0", "bounds-slice-end");
await assertBoundsTrap("conformance/native/fail/bounds-slice-order.0", "bounds-slice-order");
await assertBoundsTrap("conformance/native/fail/bounds-open-slice-start.0", "bounds-open-slice-start");
await assertBoundsTrap("conformance/native/fail/index-string.0", "index-string");
await assertBoundsTrap("conformance/native/fail/slice-string.0", "slice-string");
await assertBoundsTrap("conformance/native/fail/indexed-mutation-oob.0", "indexed-mutation-oob");

const failed = await execFileAsync(zero, ["check", "conformance/check/fail/unknown-name.0"]).catch((error) => error);
assert.notEqual(failed.code, 0);
assert.match(failed.stderr, /NAM003/);

const unknownField = await execFileAsync(zero, ["check", "conformance/native/fail/unknown-field.0"]).catch((error) => error);
assert.notEqual(unknownField.code, 0);
assert.match(unknownField.stderr, /FLD001/);

const wrongReturnType = await execFileAsync(zero, ["check", "conformance/native/fail/bad-return.0"]).catch((error) => error);
assert.notEqual(wrongReturnType.code, 0);
assert.match(wrongReturnType.stderr, /TYP003/);

const immutableAssignment = await execFileAsync(zero, ["check", "conformance/native/fail/immutable-assignment.0"]).catch((error) => error);
assert.notEqual(immutableAssignment.code, 0);
assert.match(immutableAssignment.stderr, /TYP009/);

const indexedMutationImmutable = await execFileAsync(zero, ["check", "conformance/native/fail/indexed-mutation-immutable.0"]).catch((error) => error);
assert.notEqual(indexedMutationImmutable.code, 0);
assert.match(indexedMutationImmutable.stderr, /TYP009/);

const indexedMutationType = await execFileAsync(zero, ["check", "conformance/native/fail/indexed-mutation-type.0"]).catch((error) => error);
assert.notEqual(indexedMutationType.code, 0);
assert.match(indexedMutationType.stderr, /TYP002/);

const indexedMutationNonInteger = await execFileAsync(zero, ["check", "conformance/native/fail/indexed-mutation-non-integer.0"]).catch((error) => error);
assert.notEqual(indexedMutationNonInteger.code, 0);
assert.match(indexedMutationNonInteger.stderr, /TYP022/);

const indexedMutationSpan = await execFileAsync(zero, ["check", "conformance/native/fail/indexed-mutation-span.0"]).catch((error) => error);
assert.notEqual(indexedMutationSpan.code, 0);
assert.match(indexedMutationSpan.stderr, /TYP021/);

const indexedMutationString = await execFileAsync(zero, ["check", "conformance/native/fail/indexed-mutation-string.0"]).catch((error) => error);
assert.notEqual(indexedMutationString.code, 0);
assert.match(indexedMutationString.stderr, /TYP021/);
assert.match(indexedMutationString.stderr, /byte-oriented/);

const nestedLvalueImmutableField = await execFileAsync(zero, ["check", "conformance/native/fail/nested-lvalue-immutable-field.0"]).catch((error) => error);
assert.notEqual(nestedLvalueImmutableField.code, 0);
assert.match(nestedLvalueImmutableField.stderr, /TYP009/);

const nestedLvalueFieldType = await execFileAsync(zero, ["check", "conformance/native/fail/nested-lvalue-field-type.0"]).catch((error) => error);
assert.notEqual(nestedLvalueFieldType.code, 0);
assert.match(nestedLvalueFieldType.stderr, /TYP002/);

const nestedLvalueUnknownField = await execFileAsync(zero, ["check", "conformance/native/fail/nested-lvalue-unknown-field.0"]).catch((error) => error);
assert.notEqual(nestedLvalueUnknownField.code, 0);
assert.match(nestedLvalueUnknownField.stderr, /FLD001/);

const nestedLvalueNonIntegerIndex = await execFileAsync(zero, ["check", "conformance/native/fail/nested-lvalue-non-integer-index.0"]).catch((error) => error);
assert.notEqual(nestedLvalueNonIntegerIndex.code, 0);
assert.match(nestedLvalueNonIntegerIndex.stderr, /TYP022/);

const nestedLvalueSpanIndex = await execFileAsync(zero, ["check", "conformance/native/fail/nested-lvalue-span-index.0"]).catch((error) => error);
assert.notEqual(nestedLvalueSpanIndex.code, 0);
assert.match(nestedLvalueSpanIndex.stderr, /TYP021/);

const nestedLvalueStringIndex = await execFileAsync(zero, ["check", "conformance/native/fail/nested-lvalue-string-index.0"]).catch((error) => error);
assert.notEqual(nestedLvalueStringIndex.code, 0);
assert.match(nestedLvalueStringIndex.stderr, /TYP021/);
assert.match(nestedLvalueStringIndex.stderr, /byte-oriented/);

const mutspanImmutableArray = await execFileAsync(zero, ["check", "conformance/native/fail/mutspan-immutable-array.0"]).catch((error) => error);
assert.notEqual(mutspanImmutableArray.code, 0);
assert.match(mutspanImmutableArray.stderr, /TYP009/);

const memCopyImmutableDst = await execFileAsync(zero, ["check", "conformance/native/fail/mem-copy-immutable-dst.0"]).catch((error) => error);
assert.notEqual(memCopyImmutableDst.code, 0);
assert.match(memCopyImmutableDst.stderr, /TYP009/);

const memCopyImmutableDstJson = await execFileAsync(zero, ["check", "--json", "conformance/native/fail/mem-copy-immutable-dst.0"]).catch((error) => error);
assert.notEqual(memCopyImmutableDstJson.code, 0);
const memCopyImmutableDstBody = JSON.parse(memCopyImmutableDstJson.stdout);
assert.equal(memCopyImmutableDstBody.diagnostics[0].code, "TYP009");
assert.equal(memCopyImmutableDstBody.diagnostics[0].repair.id, "make-binding-mutable");

const mutspanFromSpan = await execFileAsync(zero, ["check", "conformance/native/fail/mutspan-from-span.0"]).catch((error) => error);
assert.notEqual(mutspanFromSpan.code, 0);
assert.match(mutspanFromSpan.stderr, /TYP002/);

const mutspanFromString = await execFileAsync(zero, ["check", "conformance/native/fail/mutspan-from-string.0"]).catch((error) => error);
assert.notEqual(mutspanFromString.code, 0);
assert.match(mutspanFromString.stderr, /TYP002/);

const mutspanReadonlyIndex = await execFileAsync(zero, ["check", "conformance/native/fail/mutspan-readonly-index.0"]).catch((error) => error);
assert.notEqual(mutspanReadonlyIndex.code, 0);
assert.match(mutspanReadonlyIndex.stderr, /TYP021/);

const mutspanAssignmentType = await execFileAsync(zero, ["check", "conformance/native/fail/mutspan-assignment-type.0"]).catch((error) => error);
assert.notEqual(mutspanAssignmentType.code, 0);
assert.match(mutspanAssignmentType.stderr, /TYP002/);

const mutspanNonIntegerIndex = await execFileAsync(zero, ["check", "conformance/native/fail/mutspan-non-integer-index.0"]).catch((error) => error);
assert.notEqual(mutspanNonIntegerIndex.code, 0);
assert.match(mutspanNonIntegerIndex.stderr, /TYP022/);

const duplicateFunction = await execFileAsync(zero, ["check", "conformance/native/fail/duplicate-function.0"]).catch((error) => error);
assert.notEqual(duplicateFunction.code, 0);
assert.match(duplicateFunction.stderr, /NAM004/);

const wrongArity = await execFileAsync(zero, ["check", "conformance/native/fail/wrong-arity.0"]).catch((error) => error);
assert.notEqual(wrongArity.code, 0);
assert.match(wrongArity.stderr, /NAM004/);

const unknownEnumCase = await execFileAsync(zero, ["check", "conformance/check/fail/unknown-enum-case.0"]).catch((error) => error);
assert.notEqual(unknownEnumCase.code, 0);
assert.match(unknownEnumCase.stderr, /VAR001/);

const badStdCall = await execFileAsync(zero, ["check", "conformance/native/fail/bad-std-call.0"]).catch((error) => error);
assert.notEqual(badStdCall.code, 0);
assert.match(badStdCall.stderr, /STD002/);

const stdHttpErrorRawInt = await execFileAsync(zero, ["check", "conformance/native/fail/std-http-error-raw-int.0"]).catch((error) => error);
assert.notEqual(stdHttpErrorRawInt.code, 0);
assert.match(stdHttpErrorRawInt.stderr, /TYP002/);

const stdHttpFetchRawTimeout = await execFileAsync(zero, ["check", "conformance/native/fail/std-http-fetch-raw-timeout.0"]).catch((error) => error);
assert.notEqual(stdHttpFetchRawTimeout.code, 0);
assert.match(stdHttpFetchRawTimeout.stderr, /STD003/);

const stdJsonParseBytesRawAlloc = await execFileAsync(zero, ["check", "conformance/native/fail/std-json-parsebytes-raw-alloc.0"]).catch((error) => error);
assert.notEqual(stdJsonParseBytesRawAlloc.code, 0);
assert.match(stdJsonParseBytesRawAlloc.stderr, /STD003/);

const stdJsonParseBytesImmutableAlloc = await execFileAsync(zero, ["check", "conformance/native/fail/std-json-parsebytes-immutable-alloc.0"]).catch((error) => error);
assert.notEqual(stdJsonParseBytesImmutableAlloc.code, 0);
assert.match(stdJsonParseBytesImmutableAlloc.stderr, /STD003/);

const genericMemLenNonSpan = await execFileAsync(zero, ["check", "conformance/native/fail/generic-mem-len-non-span.0"]).catch((error) => error);
assert.notEqual(genericMemLenNonSpan.code, 0);
assert.match(genericMemLenNonSpan.stderr, /STD003/);

const genericMemEqlMismatch = await execFileAsync(zero, ["check", "conformance/native/fail/generic-mem-eql-mismatch.0"]).catch((error) => error);
assert.notEqual(genericMemEqlMismatch.code, 0);
assert.match(genericMemEqlMismatch.stderr, /STD003/);

const genericMemEqlNonSpan = await execFileAsync(zero, ["check", "conformance/native/fail/generic-mem-eql-non-span.0"]).catch((error) => error);
assert.notEqual(genericMemEqlNonSpan.code, 0);
assert.match(genericMemEqlNonSpan.stderr, /STD003/);

const badMemoryType = await execFileAsync(zero, ["check", "conformance/native/fail/bad-memory-type.0"]).catch((error) => error);
assert.notEqual(badMemoryType.code, 0);
assert.match(badMemoryType.stderr, /MEM001/);

const checkerArrayElementType = await execFileAsync(zero, ["check", "conformance/check/fail/checker-array-element-type.0"]).catch((error) => error);
assert.notEqual(checkerArrayElementType.code, 0);
assert.match(checkerArrayElementType.stderr, /TYP002/);

const checkerRefMismatch = await execFileAsync(zero, ["check", "conformance/check/fail/checker-ref-mismatch.0"]).catch((error) => error);
assert.notEqual(checkerRefMismatch.code, 0);
assert.match(checkerRefMismatch.stderr, /TYP001/);

const checkerMutrefImmutable = await execFileAsync(zero, ["check", "conformance/check/fail/checker-mutref-immutable.0"]).catch((error) => error);
assert.notEqual(checkerMutrefImmutable.code, 0);
assert.match(checkerMutrefImmutable.stderr, /TYP009/);

const nonexhaustiveMatch = await execFileAsync(zero, ["check", "conformance/native/fail/nonexhaustive-match.0"]).catch((error) => error);
assert.notEqual(nonexhaustiveMatch.code, 0);
assert.match(nonexhaustiveMatch.stderr, /MAT002/);

const badChoicePayload = await execFileAsync(zero, ["check", "conformance/native/fail/bad-choice-payload.0"]).catch((error) => error);
assert.notEqual(badChoicePayload.code, 0);
assert.match(badChoicePayload.stderr, /VAR004/);

const allocatorInvalid = await execFileAsync(zero, ["check", "conformance/native/fail/allocator-invalid.0"]).catch((error) => error);
assert.notEqual(allocatorInvalid.code, 0);
assert.match(allocatorInvalid.stderr, /STD003/);

const allocatorImmutableFixedBuf = await execFileAsync(zero, ["check", "conformance/native/fail/allocator-immutable-fixedbuf.0"]).catch((error) => error);
assert.notEqual(allocatorImmutableFixedBuf.code, 0);
assert.match(allocatorImmutableFixedBuf.stderr, /STD003/);

const byteBufferImmutableAlloc = await execFileAsync(zero, ["check", "conformance/native/fail/byte-buffer-immutable-alloc.0"]).catch((error) => error);
assert.notEqual(byteBufferImmutableAlloc.code, 0);
assert.match(byteBufferImmutableAlloc.stderr, /STD003/);

const ownedUseAfterMove = await execFileAsync(zero, ["check", "conformance/native/fail/owned-use-after-move.0"]).catch((error) => error);
assert.notEqual(ownedUseAfterMove.code, 0);
assert.match(ownedUseAfterMove.stderr, /OWN001/);

const unsupportedDrop = await execFileAsync(zero, ["check", "conformance/native/fail/unsupported-drop.0"]).catch((error) => error);
assert.notEqual(unsupportedDrop.code, 0);
assert.match(unsupportedDrop.stderr, /OWN002/);

const invalidDropSignature = await execFileAsync(zero, ["check", "conformance/native/fail/invalid-drop-signature.0"]).catch((error) => error);
assert.notEqual(invalidDropSignature.code, 0);
assert.match(invalidDropSignature.stderr, /OWN002/);

const borrowMutrefImmutable = await execFileAsync(zero, ["check", "conformance/native/fail/borrow-mutref-immutable.0"]).catch((error) => error);
assert.notEqual(borrowMutrefImmutable.code, 0);
assert.match(borrowMutrefImmutable.stderr, /TYP009/);

const borrowConflict = await execFileAsync(zero, ["check", "conformance/native/fail/borrow-conflict.0"]).catch((error) => error);
assert.notEqual(borrowConflict.code, 0);
assert.match(borrowConflict.stderr, /BOR001/);

const borrowAssignWhileBorrowed = await execFileAsync(zero, ["check", "conformance/native/fail/borrow-assign-while-borrowed.0"]).catch((error) => error);
assert.notEqual(borrowAssignWhileBorrowed.code, 0);
assert.match(borrowAssignWhileBorrowed.stderr, /BOR001/);

const borrowAssignThroughRef = await execFileAsync(zero, ["check", "conformance/native/fail/borrow-assign-through-ref.0"]).catch((error) => error);
assert.notEqual(borrowAssignThroughRef.code, 0);
assert.match(borrowAssignThroughRef.stderr, /TYP009/);

const borrowReturnLocal = await execFileAsync(zero, ["check", "conformance/native/fail/borrow-return-local.0"]).catch((error) => error);
assert.notEqual(borrowReturnLocal.code, 0);
assert.match(borrowReturnLocal.stderr, /BOR002/);

const borrowWrongType = await execFileAsync(zero, ["check", "conformance/native/fail/borrow-wrong-type.0"]).catch((error) => error);
assert.notEqual(borrowWrongType.code, 0);
assert.match(borrowWrongType.stderr, /TYP001/);

const refIndexedAssignment = await execFileAsync(zero, ["check", "conformance/native/fail/ref-indexed-assignment.0"]).catch((error) => error);
assert.notEqual(refIndexedAssignment.code, 0);
assert.match(refIndexedAssignment.stderr, /TYP009/);

const breakOutsideLoop = await execFileAsync(zero, ["check", "conformance/native/fail/break-outside-loop.0"]).catch((error) => error);
assert.notEqual(breakOutsideLoop.code, 0);
assert.match(breakOutsideLoop.stderr, /TYP012/);

const continueOutsideLoop = await execFileAsync(zero, ["check", "conformance/native/fail/continue-outside-loop.0"]).catch((error) => error);
assert.notEqual(continueOutsideLoop.code, 0);
assert.match(continueOutsideLoop.stderr, /TYP013/);

const nonBoolCondition = await execFileAsync(zero, ["check", "conformance/native/fail/non-bool-condition.0"]).catch((error) => error);
assert.notEqual(nonBoolCondition.code, 0);
assert.match(nonBoolCondition.stderr, /TYP010/);

const badMatchPayloadBinding = await execFileAsync(zero, ["check", "conformance/native/fail/bad-match-payload-binding.0"]).catch((error) => error);
assert.notEqual(badMatchPayloadBinding.code, 0);
assert.match(badMatchPayloadBinding.stderr, /MAT004/);

const badNull = await execFileAsync(zero, ["check", "conformance/native/fail/bad-null.0"]).catch((error) => error);
assert.notEqual(badNull.code, 0);
assert.match(badNull.stderr, /TYP011/);

const badForRange = await execFileAsync(zero, ["check", "conformance/native/fail/bad-for-range.0"]).catch((error) => error);
assert.notEqual(badForRange.code, 0);
assert.match(badForRange.stderr, /TYP014/);

const badStdArgs = await execFileAsync(zero, ["check", "conformance/native/fail/bad-std-args.0"]).catch((error) => error);
assert.notEqual(badStdArgs.code, 0);
assert.match(badStdArgs.stderr, /STD002/);

const badStdFs = await execFileAsync(zero, ["check", "conformance/native/fail/bad-std-fs.0"]).catch((error) => error);
assert.notEqual(badStdFs.code, 0);
assert.match(badStdFs.stderr, /STD003/);

const fsOpenWithoutCapability = await execFileAsync(zero, ["check", "conformance/native/fail/fs-open-without-capability.0"]).catch((error) => error);
assert.notEqual(fsOpenWithoutCapability.code, 0);
assert.match(fsOpenWithoutCapability.stderr, /STD003/);

const fsReadWithoutMutref = await execFileAsync(zero, ["check", "conformance/native/fail/fs-read-without-mutref.0"]).catch((error) => error);
assert.notEqual(fsReadWithoutMutref.code, 0);
assert.match(fsReadWithoutMutref.stderr, /STD003/);

const unknownStdHelper = await execFileAsync(zero, ["check", "conformance/native/fail/unknown-std-helper.0"]).catch((error) => error);
assert.notEqual(unknownStdHelper.code, 0);
assert.match(unknownStdHelper.stderr, /STD002/);

const integerU8Overflow = await execFileAsync(zero, ["check", "conformance/native/fail/integer-u8-overflow.0"]).catch((error) => error);
assert.notEqual(integerU8Overflow.code, 0);
assert.match(integerU8Overflow.stderr, /TYP016/);

const integerI8Overflow = await execFileAsync(zero, ["check", "conformance/native/fail/integer-i8-overflow.0"]).catch((error) => error);
assert.notEqual(integerI8Overflow.code, 0);
assert.match(integerI8Overflow.stderr, /TYP016/);

const integerU64Overflow = await execFileAsync(zero, ["check", "conformance/native/fail/integer-u64-overflow.0"]).catch((error) => error);
assert.notEqual(integerU64Overflow.code, 0);
assert.match(integerU64Overflow.stderr, /TYP016/);

const malformedIntegerLiteral = await execFileAsync(zero, ["check", "conformance/native/fail/malformed-integer-literal.0"]).catch((error) => error);
assert.notEqual(malformedIntegerLiteral.code, 0);
assert.match(malformedIntegerLiteral.stderr, /TYP015/);

const integerNonliteralNarrowing = await execFileAsync(zero, ["check", "conformance/native/fail/integer-nonliteral-narrowing.0"]).catch((error) => error);
assert.notEqual(integerNonliteralNarrowing.code, 0);
assert.match(integerNonliteralNarrowing.stderr, /TYP002/);

const stdCodecReadU16Width = await execFileAsync(zero, ["check", "conformance/native/fail/std-codec-read-u16-width.0"]).catch((error) => error);
assert.notEqual(stdCodecReadU16Width.code, 0);
assert.match(stdCodecReadU16Width.stderr, /TYP002/);

const castStringToInt = await execFileAsync(zero, ["check", "conformance/native/fail/cast-string-to-int.0"]).catch((error) => error);
assert.notEqual(castStringToInt.code, 0);
assert.match(castStringToInt.stderr, /TYP017/);

const castBoolToInt = await execFileAsync(zero, ["check", "conformance/native/fail/cast-bool-to-int.0"]).catch((error) => error);
assert.notEqual(castBoolToInt.code, 0);
assert.match(castBoolToInt.stderr, /TYP017/);

const castIntToSpan = await execFileAsync(zero, ["check", "conformance/native/fail/cast-int-to-span.0"]).catch((error) => error);
assert.notEqual(castIntToSpan.code, 0);
assert.match(castIntToSpan.stderr, /TYP017/);

const castIntToString = await execFileAsync(zero, ["check", "conformance/native/fail/cast-int-to-string.0"]).catch((error) => error);
assert.notEqual(castIntToString.code, 0);
assert.match(castIntToString.stderr, /TYP017/);

const radixInvalidBinary = await execFileAsync(zero, ["check", "conformance/native/fail/radix-invalid-binary.0"]).catch((error) => error);
assert.notEqual(radixInvalidBinary.code, 0);
assert.match(radixInvalidBinary.stderr, /TYP015/);

const radixEmptyHex = await execFileAsync(zero, ["check", "conformance/native/fail/radix-empty-hex.0"]).catch((error) => error);
assert.notEqual(radixEmptyHex.code, 0);
assert.match(radixEmptyHex.stderr, /TYP015/);

const literalBadSeparator = await execFileAsync(zero, ["check", "conformance/native/fail/literal-bad-separator.0"]).catch((error) => error);
assert.notEqual(literalBadSeparator.code, 0);
assert.match(literalBadSeparator.stderr, /TYP015/);

const literalTrailingSeparator = await execFileAsync(zero, ["check", "conformance/native/fail/literal-trailing-separator.0"]).catch((error) => error);
assert.notEqual(literalTrailingSeparator.code, 0);
assert.match(literalTrailingSeparator.stderr, /TYP015/);

const literalUnknownSuffix = await execFileAsync(zero, ["check", "conformance/native/fail/literal-unknown-suffix.0"]).catch((error) => error);
assert.notEqual(literalUnknownSuffix.code, 0);
assert.match(literalUnknownSuffix.stderr, /TYP015/);

const literalSuffixMismatch = await execFileAsync(zero, ["check", "conformance/native/fail/literal-suffix-mismatch.0"]).catch((error) => error);
assert.notEqual(literalSuffixMismatch.code, 0);
assert.match(literalSuffixMismatch.stderr, /TYP002/);

const literalSuffixOverflow = await execFileAsync(zero, ["check", "conformance/native/fail/literal-suffix-overflow.0"]).catch((error) => error);
assert.notEqual(literalSuffixOverflow.code, 0);
assert.match(literalSuffixOverflow.stderr, /TYP016/);

const charEmpty = await execFileAsync(zero, ["check", "conformance/native/fail/char-empty.0"]).catch((error) => error);
assert.notEqual(charEmpty.code, 0);
assert.match(charEmpty.stderr, /TYP018/);

const charMultiple = await execFileAsync(zero, ["check", "conformance/native/fail/char-multiple.0"]).catch((error) => error);
assert.notEqual(charMultiple.code, 0);
assert.match(charMultiple.stderr, /TYP018/);

const charBadEscape = await execFileAsync(zero, ["check", "conformance/native/fail/char-bad-escape.0"]).catch((error) => error);
assert.notEqual(charBadEscape.code, 0);
assert.match(charBadEscape.stderr, /TYP018/);

const charToString = await execFileAsync(zero, ["check", "conformance/native/fail/char-to-string.0"]).catch((error) => error);
assert.notEqual(charToString.code, 0);
assert.match(charToString.stderr, /TYP002/);

const stringToChar = await execFileAsync(zero, ["check", "conformance/native/fail/string-to-char.0"]).catch((error) => error);
assert.notEqual(stringToChar.code, 0);
assert.match(stringToChar.stderr, /TYP002/);

const charIntegerArithmetic = await execFileAsync(zero, ["check", "conformance/native/fail/char-integer-arithmetic.0"]).catch((error) => error);
assert.notEqual(charIntegerArithmetic.code, 0);
assert.match(charIntegerArithmetic.stderr, /TYP002/);

const malformedFloatLiteral = await execFileAsync(zero, ["check", "conformance/native/fail/malformed-float-literal.0"]).catch((error) => error);
assert.notEqual(malformedFloatLiteral.code, 0);
assert.match(malformedFloatLiteral.stderr, /TYP019/);

const floatF32Overflow = await execFileAsync(zero, ["check", "conformance/native/fail/float-f32-overflow.0"]).catch((error) => error);
assert.notEqual(floatF32Overflow.code, 0);
assert.match(floatF32Overflow.stderr, /TYP020/);

const integerFromFloat = await execFileAsync(zero, ["check", "conformance/native/fail/integer-from-float.0"]).catch((error) => error);
assert.notEqual(integerFromFloat.code, 0);
assert.match(integerFromFloat.stderr, /TYP002/);

const floatFromInteger = await execFileAsync(zero, ["check", "conformance/native/fail/float-from-integer.0"]).catch((error) => error);
assert.notEqual(floatFromInteger.code, 0);
assert.match(floatFromInteger.stderr, /TYP002/);

const floatMixedWidth = await execFileAsync(zero, ["check", "conformance/native/fail/float-mixed-width.0"]).catch((error) => error);
assert.notEqual(floatMixedWidth.code, 0);
assert.match(floatMixedWidth.stderr, /TYP002/);

const floatIntArithmetic = await execFileAsync(zero, ["check", "conformance/native/fail/float-int-arithmetic.0"]).catch((error) => error);
assert.notEqual(floatIntArithmetic.code, 0);
assert.match(floatIntArithmetic.stderr, /TYP002/);

const indexNonInteger = await execFileAsync(zero, ["check", "conformance/native/fail/index-non-integer.0"]).catch((error) => error);
assert.notEqual(indexNonInteger.code, 0);
assert.match(indexNonInteger.stderr, /TYP022/);

const indexNonIndexable = await execFileAsync(zero, ["check", "conformance/native/fail/index-non-indexable.0"]).catch((error) => error);
assert.notEqual(indexNonIndexable.code, 0);
assert.match(indexNonIndexable.stderr, /TYP021/);

const checkedGetNonIndexable = await execFileAsync(zero, ["check", "conformance/native/fail/checked-get-non-indexable.0"]).catch((error) => error);
assert.notEqual(checkedGetNonIndexable.code, 0);
assert.match(checkedGetNonIndexable.stderr, /STD003/);

const checkMaybeVoid = await execFileAsync(zero, ["check", "conformance/native/fail/check-maybe-void.0"]).catch((error) => error);
assert.notEqual(checkMaybeVoid.code, 0);
assert.match(checkMaybeVoid.stderr, /ERR001/);

const raiseWithoutRaises = await execFileAsync(zero, ["check", "conformance/native/fail/raise-without-raises.0"]).catch((error) => error);
assert.notEqual(raiseWithoutRaises.code, 0);
assert.match(raiseWithoutRaises.stderr, /ERR001/);

const raiseUndeclaredError = await execFileAsync(zero, ["check", "conformance/native/fail/raise-undeclared-error.0"]).catch((error) => error);
assert.notEqual(raiseUndeclaredError.code, 0);
assert.match(raiseUndeclaredError.stderr, /ERR002/);

const uncheckedFallibleCall = await execFileAsync(zero, ["check", "conformance/native/fail/unchecked-fallible-call.0"]).catch((error) => error);
assert.notEqual(uncheckedFallibleCall.code, 0);
assert.match(uncheckedFallibleCall.stderr, /ERR003/);

const errorSetMismatch = await execFileAsync(zero, ["check", "conformance/native/fail/error-set-mismatch.0"]).catch((error) => error);
assert.notEqual(errorSetMismatch.code, 0);
assert.match(errorSetMismatch.stderr, /ERR002/);

const rescueFallbackTypeMismatch = await execFileAsync(zero, ["check", "conformance/native/fail/rescue-fallback-type-mismatch.0"]).catch((error) => error);
assert.notEqual(rescueFallbackTypeMismatch.code, 0);
assert.match(rescueFallbackTypeMismatch.stderr, /TYP003|TYP002/);

const constFieldAssignment = await execFileAsync(zero, ["check", "conformance/native/fail/const-field-assignment.0"]).catch((error) => error);
assert.notEqual(constFieldAssignment.code, 0);
assert.match(constFieldAssignment.stderr, /TYP009/);

const constMutBorrow = await execFileAsync(zero, ["check", "conformance/native/fail/const-mut-borrow.0"]).catch((error) => error);
assert.notEqual(constMutBorrow.code, 0);
assert.match(constMutBorrow.stderr, /TYP009/);

const badExternLayout = await execFileAsync(zero, ["check", "conformance/native/fail/bad-extern-layout.0"]).catch((error) => error);
assert.notEqual(badExternLayout.code, 0);
assert.match(badExternLayout.stderr, /ABI001/);

const badPackedLayout = await execFileAsync(zero, ["check", "conformance/native/fail/bad-packed-layout.0"]).catch((error) => error);
assert.notEqual(badPackedLayout.code, 0);
assert.match(badPackedLayout.stderr, /ABI001/);

const badCExport = await execFileAsync(zero, ["check", "conformance/native/fail/bad-c-export.0"]).catch((error) => error);
assert.notEqual(badCExport.code, 0);
assert.match(badCExport.stderr, /ABI001/);

const badCExportRaises = await execFileAsync(zero, ["check", "conformance/native/fail/bad-c-export-raises.0"]).catch((error) => error);
assert.notEqual(badCExportRaises.code, 0);
assert.match(badCExportRaises.stderr, /ABI001/);

const byteBufferUseAfterMove = await execFileAsync(zero, ["check", "conformance/native/fail/byte-buffer-use-after-move.0"]).catch((error) => error);
assert.notEqual(byteBufferUseAfterMove.code, 0);
assert.match(byteBufferUseAfterMove.stderr, /OWN001/);

const testExpectNonBool = await execFileAsync(zero, ["check", "conformance/native/fail/test-expect-non-bool.0"]).catch((error) => error);
assert.notEqual(testExpectNonBool.code, 0);
assert.match(testExpectNonBool.stderr, /TYP001/);

const sliceNonIntegerBound = await execFileAsync(zero, ["check", "conformance/native/fail/slice-non-integer-bound.0"]).catch((error) => error);
assert.notEqual(sliceNonIntegerBound.code, 0);
assert.match(sliceNonIntegerBound.stderr, /TYP022/);

const sliceNonSliceable = await execFileAsync(zero, ["check", "conformance/native/fail/slice-non-sliceable.0"]).catch((error) => error);
assert.notEqual(sliceNonSliceable.code, 0);
assert.match(sliceNonSliceable.stderr, /TYP021/);

for (const [fixture, code] of [
  ["missing-return-path.0", /TYP003/],
  ["check-non-fallible-value.0", /ERR001/],
  ["unchecked-fallible-wrapper.0", /ERR003/],
  ["unchecked-fallible-wrapper-declared-later.0", /ERR003/],
  ["unchecked-fallible-receiver-wrapper.0", /ERR003/],
  ["unchecked-fallible-generic-call.0", /ERR003/],
  ["unchecked-fallible-static-method.0", /ERR003/],
  ["unchecked-fallible-interface-method.0", /ERR003/],
  ["unchecked-fallible-interface-wrapper.0", /ERR003/],
  ["error-set-wrapper-mismatch.0", /ERR002/],
  ["error-set-wrapper-nested-check-mismatch.0", /ERR002/],
  ["error-set-interface-wrapper-mismatch.0", /ERR002/],
  ["unknown-member-on-non-shape.0", /TYP021/],
  ["function-used-as-value.0", /TYP001/],
  ["call-local-value.0", /TYP001/],
  ["call-shadowed-function-value.0", /TYP001/],
  ["shape-literal-expected-type.0", /TYP002/],
  ["heterogeneous-array-literal.0", /TYP002/],
  ["unknown-function-parameter-type.0", /NAM003/],
  ["unknown-local-generic-type.0", /NAM003/],
  ["unknown-return-reference-origin.0", /NAM003/],
  ["duplicate-shape-field.0", /NAM004/],
  ["duplicate-enum-case.0", /NAM004/],
  ["duplicate-shape-literal-field.0", /NAM004/],
  ["type-name-used-as-value.0", /TYP001/],
  ["read-while-mutably-borrowed.0", /BOR001/],
  ["array-reference-borrow-origin.0", /BOR001/],
  ["return-array-reference-escape.0", /BOR002/],
  ["check-array-reference-origin.0", /BOR001/],
  ["check-maybe-reference-origin.0", /BOR001/],
  ["rescue-reference-origin.0", /BOR001/],
  ["local-reference-escape-call.0", /BOR002/],
  ["local-reference-escape-binding.0", /BOR002/],
  ["local-reference-escape-method.0", /BOR002/],
  ["local-reference-escape-param.0", /BOR002/],
  ["local-reference-escape-param-call.0", /BOR002/],
  ["borrow-assignment-origin-tracking.0", /BOR001/],
  ["borrow-assignment-shorter-lived-root.0", /BOR002/],
  ["borrow-branch-origin-merge.0", /BOR001/],
  ["aggregate-if-reference-origin.0", /BOR001/],
  ["aggregate-match-reference-origin.0", /BOR001/],
  ["choice-payload-local-reference-escape.0", /BOR002/],
  ["choice-match-payload-reference-origin.0", /BOR001/],
  ["choice-match-payload-return-escape.0", /BOR002/],
  ["array-if-reference-origin.0", /BOR001/],
  ["array-while-reference-origin.0", /BOR001/],
  ["array-for-reference-origin.0", /BOR001/],
  ["borrow-call-result-multiple-origins.0", /BOR001/],
  ["borrow-self-assignment-origin-tracking.0", /BOR001/],
  ["generic-reference-borrow-origin.0", /BOR001/],
  ["generic-reference-return-escape.0", /BOR002/],
  ["aggregate-call-reference-borrow-origin.0", /BOR001/],
  ["aggregate-call-reference-escape.0", /BOR002/],
  ["aggregate-param-reference-return-origin.0", /BOR001/],
  ["call-branch-reference-origin.0", /BOR001/],
  ["generic-aggregate-reference-origin.0", /BOR001/],
  ["generic-receiver-method-reference-origin.0", /BOR001/],
  ["function-mutref-side-effect-reference-origin.0", /BOR001/],
  ["function-mutref-local-reference-escape.0", /BOR002/],
  ["function-mutref-local-shape-reference-escape.0", /BOR002/],
  ["function-mutref-local-array-reference-escape.0", /BOR002/],
  ["function-mutref-stored-reference-copy-origin.0", /BOR001/],
  ["static-interface-mutref-reference-origin.0", /BOR001/],
  ["nested-generic-mutref-reference-origin.0", /BOR001/],
  ["static-interface-mutref-local-reference-escape.0", /BOR002/],
  ["static-interface-return-reference-origin.0", /BOR002/],
  ["recursive-mutref-summary-incomplete.0", /BOR002/],
  ["assignment-rhs-side-effect-reference-origin.0", /BOR001/],
  ["function-rescue-side-effect-reference-origin.0", /BOR001/],
  ["function-short-circuit-side-effect-reference-origin.0", /BOR001/],
  ["nested-receiver-side-effect-reference-origin.0", /BOR001/],
  ["receiver-method-local-reference-escape.0", /BOR002/],
  ["receiver-union-side-effect-reference-origin.0", /BOR001/],
  ["mutref-alias-assignment-reference-origin.0", /BOR001/],
  ["mutref-alias-assignment-shorter-lived-origin.0", /BOR002/],
  ["rescue-side-effect-reference-origin.0", /BOR001/],
  ["short-circuit-side-effect-reference-origin.0", /BOR001/],
  ["shadowed-mutref-side-effect-reference-origin.0", /BOR001/],
  ["shape-field-reference-escape.0", /BOR002/],
  ["shape-field-reference-borrow-origin.0", /BOR001/],
  ["return-shape-reference-escape.0", /BOR002/],
  ["shape-field-reference-call-return-escape.0", /BOR002/],
  ["shape-field-reference-assignment-origin.0", /BOR001/],
  ["shape-field-reference-assignment-preserves-other-origin.0", /BOR001/],
  ["shape-array-reference-index-origin.0", /BOR001/],
  ["index-reference-assignment-preserves-other-origin.0", /BOR001/],
  ["shape-array-reference-call-origin.0", /BOR001/],
  ["receiver-return-field-origin.0", /BOR001/],
  ["receiver-return-partial-field-origin.0", /BOR001/],
  ["receiver-return-partial-index-origin.0", /BOR001/],
  ["receiver-method-side-effect-reference-origin.0", /BOR001/],
  ["world-stream-used-as-value.0", /TYP001/],
]) {
  const result = await execFileAsync(zero, ["check", `conformance/native/fail/${fixture}`]).catch((error) => error);
  assert.notEqual(result.code, 0);
  assert.match(result.stderr, code);
}

console.log("conformance ok");
