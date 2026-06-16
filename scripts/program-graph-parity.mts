#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const execMaxBuffer = 128 * 1024 * 1024;
const zero = process.env.ZERO_BIN || (existsSync(".zero/bin/zero") ? ".zero/bin/zero" : "bin/zero");
const outDir = `/tmp/zero-program-graph-parity-${process.pid}`;
const requireStableNodeIds = process.argv.includes("--require-stable-node-ids");
const graphHashPrime = 1099511628211n;
const graphHashMask = (1n << 64n) - 1n;

function firstDiagnosticCode(diagnostics) {
  return Array.isArray(diagnostics) && diagnostics.length > 0 ? diagnostics[0].code ?? null : null;
}

function targetReadinessSummary(readiness) {
  if (!readiness) return null;
  return {
    languageOk: readiness.languageOk,
    buildable: readiness.buildable,
    stage: readiness.stage,
    code: firstDiagnosticCode(readiness.diagnostics),
  };
}

async function zeroJson(args) {
  const result = await execFileAsync(zero, args, { maxBuffer: execMaxBuffer });
  return JSON.parse(result.stdout);
}

async function zeroJsonFailure(args) {
  try {
    await execFileAsync(zero, args, { maxBuffer: execMaxBuffer });
  } catch (error) {
    return JSON.parse(error.stdout);
  }
  assert.fail(`${zero} ${args.join(" ")} should fail`);
}

async function zeroText(args) {
  const result = await execFileAsync(zero, args, { maxBuffer: execMaxBuffer });
  return result.stdout;
}

async function dumpGraphArtifact(fixture, name) {
  const artifact = `${outDir}/${name}.program-graph`;
  if (fixture.endsWith(".0") && !existsSync(projectionSidecarPath(fixture))) {
    await importProjectionSidecar(fixture);
  }
  await zeroText(["dump", "--out", artifact, fixture]);
  return artifact;
}

function projectionSidecarPath(sourcePath) {
  assert(sourcePath.endsWith(".0"), `${sourcePath}: expected a .0 projection path`);
  return `${sourcePath.slice(0, -2)}.graph`;
}

async function importProjectionSidecar(sourcePath) {
  const sidecar = projectionSidecarPath(sourcePath);
  await zeroText(["import", "--format", "binary", "--out", sidecar, sourcePath]);
  return sidecar;
}

async function graphCompilerInputForFixture(fixture) {
  if (!fixture.endsWith(".0")) return fixture;
  const sidecar = projectionSidecarPath(fixture);
  if (!existsSync(sidecar)) await importProjectionSidecar(fixture);
  return sidecar;
}

function artifactNameForFixture(prefix, fixture) {
  return `${prefix}-${fixture.replace(/[^A-Za-z0-9_.-]+/g, "-")}`;
}

function buildSummary(result) {
  return {
    emit: result.emit,
    target: result.target,
    profile: result.profile,
    compiler: result.compiler,
    generatedCBytes: result.generatedCBytes,
    cBridgeFallback: result.cBridgeFallback,
    objectEmission: result.objectBackend?.objectEmission?.path ?? null,
    linkFormat: result.objectBackend?.linking?.objectFormat ?? null,
    directFacts: result.objectBackend?.directFacts ?? null,
  };
}

function assertGraphCompilerRoute(result, fixture, lowering = "typed-program-graph-mir") {
  const graph = result.graph ?? {
    artifact: result.artifact,
    canonicalSource: result.canonicalSource,
    graphHash: result.graphHash,
    lowering: result.check?.lowering,
  };
  assert(graph.artifact, `${fixture}: graph input should report graph compiler input`);
  const sidecar = fixture.endsWith(".0") ? `${fixture.slice(0, -2)}.graph` : fixture;
  const expectedArtifact = existsSync(sidecar) ? sidecar : fixture;
  assert.equal(graph.artifact, expectedArtifact, `${fixture}: graph artifact should be the active graph input`);
  assert.equal(graph.canonicalSource, false, `${fixture}: graph compiler input should not be canonical source`);
  assert.match(graph.graphHash, /^graph:[0-9a-f]{16}$/, `${fixture}: graph hash`);
  if (result.graph?.lowering) assert.equal(graph.lowering, lowering, `${fixture}: graph lowering`);
}

function compilerCacheKey(result, name) {
  const cache = result.compilerCaches?.find((item) => item.name === name);
  assert(cache, `missing compiler cache ${name}`);
  return cache.key;
}

function compilerCache(result, name) {
  const cache = result.compilerCaches?.find((item) => item.name === name);
  assert(cache, `missing compiler cache ${name}`);
  return cache;
}

const MIR_FUNCTION_COUNT_OFFSET = 16;
const MIR_LOCAL_COUNT_OFFSET = 24;
const MIR_INSTR_COUNT_OFFSET = 32;
const MIR_VALUE_COUNT_OFFSET = 40;
const MIR_VALUE_REF_COUNT_OFFSET = 48;
const MIR_INSTR_REF_COUNT_OFFSET = 56;
const MIR_EXTERNAL_COUNT_OFFSET = 64;
const MIR_EXTERNAL_PARAM_TYPE_COUNT_OFFSET = 72;
const MIR_DATA_SEGMENT_COUNT_OFFSET = 80;
const MIR_DATA_BYTES_OFFSET = 88;
const MIR_STRING_BYTES_OFFSET = 96;
const MIR_GRAPH_HASH_REF_OFFSET = 120;

function corruptMappedMirGraphHashIdentity(bytes) {
  const copy = Buffer.from(bytes);
  const stringBytes = Number(copy.readBigUInt64LE(MIR_STRING_BYTES_OFFSET));
  const graphHashOffset = Number(copy.readBigUInt64LE(MIR_GRAPH_HASH_REF_OFFSET));
  const graphHashLen = Number(copy.readBigUInt64LE(MIR_GRAPH_HASH_REF_OFFSET + 8));
  assert(stringBytes <= copy.length, "mapped MIR cache has invalid string table size");
  assert(graphHashLen > 8, "mapped MIR graph hash identity is too short to corrupt");
  const stringsOffset = copy.length - stringBytes;
  assert(graphHashOffset + 6 < stringBytes, "mapped MIR graph hash identity offset is out of range");
  copy[stringsOffset + graphHashOffset + 6] = 0;
  return copy;
}

function corruptMappedMirU64(bytes, offset, value) {
  const copy = Buffer.from(bytes);
  copy.writeBigUInt64LE(value, offset);
  return copy;
}

function corruptMappedMirFunctionCount(bytes) {
  return corruptMappedMirU64(bytes, MIR_FUNCTION_COUNT_OFFSET, 100_001n);
}

function corruptMappedMirLocalCount(bytes) {
  return corruptMappedMirU64(bytes, MIR_LOCAL_COUNT_OFFSET, 1_000_001n);
}

function corruptMappedMirInstrCount(bytes) {
  return corruptMappedMirU64(bytes, MIR_INSTR_COUNT_OFFSET, 4_000_001n);
}

function corruptMappedMirValueCount(bytes) {
  return corruptMappedMirU64(bytes, MIR_VALUE_COUNT_OFFSET, 4_000_001n);
}

function corruptMappedMirValueRefCount(bytes) {
  return corruptMappedMirU64(bytes, MIR_VALUE_REF_COUNT_OFFSET, 8_000_001n);
}

function corruptMappedMirInstrRefCount(bytes) {
  return corruptMappedMirU64(bytes, MIR_INSTR_REF_COUNT_OFFSET, 8_000_001n);
}

function corruptMappedMirExternalCount(bytes) {
  return corruptMappedMirU64(bytes, MIR_EXTERNAL_COUNT_OFFSET, 100_001n);
}

function corruptMappedMirExternalParamTypeCount(bytes) {
  return corruptMappedMirU64(bytes, MIR_EXTERNAL_PARAM_TYPE_COUNT_OFFSET, 1_000_001n);
}

function corruptMappedMirDataSegmentCount(bytes) {
  return corruptMappedMirU64(bytes, MIR_DATA_SEGMENT_COUNT_OFFSET, 1_000_001n);
}

function corruptMappedMirDataBytes(bytes) {
  return corruptMappedMirU64(bytes, MIR_DATA_BYTES_OFFSET, 256n * 1024n * 1024n + 1n);
}

function corruptMappedMirStringBytes(bytes) {
  return corruptMappedMirU64(bytes, MIR_STRING_BYTES_OFFSET, 256n * 1024n * 1024n + 1n);
}

async function assertMappedMirCacheRegeneratesAfterCorruption(cachePath, corrupt, buildOut, artifact, message) {
  const original = await readFile(cachePath);
  await writeFile(cachePath, corrupt(original));
  const result = await zeroJson(["build", "--json", "--target", "linux-musl-x64", "--out", buildOut, artifact]);
  const cache = compilerCache(result, "mappedFinalMir");
  assert.equal(cache.hit, false, `${message}: corrupted mapped MIR cache must not be reused`);
  assert.equal(cache.written, true, `${message}: corrupted mapped MIR cache should be regenerated`);
  assert.equal(cache.codegenImmediate, false, `${message}: regeneration should not claim immediate codegen from stale MIR`);
  assert.equal(cache.programReconstructed, false, `${message}: graph build should stay graph-native while regenerating MIR`);
  assert.equal(cache.path, cachePath, `${message}: regenerated cache should use the same deterministic path`);
  assert(result.artifactBytes > 0, `${message}: build should still produce an artifact`);
}

const mappedMirCorruptionCases = [
  ["embedded NUL in mapped MIR graph identity", corruptMappedMirGraphHashIdentity],
  ["oversized mapped MIR function count", corruptMappedMirFunctionCount],
  ["oversized mapped MIR local count", corruptMappedMirLocalCount],
  ["oversized mapped MIR instruction count", corruptMappedMirInstrCount],
  ["oversized mapped MIR value count", corruptMappedMirValueCount],
  ["oversized mapped MIR value ref count", corruptMappedMirValueRefCount],
  ["oversized mapped MIR instruction ref count", corruptMappedMirInstrRefCount],
  ["oversized mapped MIR external count", corruptMappedMirExternalCount],
  ["oversized mapped MIR external param type count", corruptMappedMirExternalParamTypeCount],
  ["oversized mapped MIR data segment count", corruptMappedMirDataSegmentCount],
  ["oversized mapped MIR data bytes", corruptMappedMirDataBytes],
  ["oversized mapped MIR string bytes", corruptMappedMirStringBytes],
];

function testSummary(result) {
  return {
    ok: result.ok,
    target: result.target,
    selectedTests: result.selectedTests,
    discoveredTests: result.discoveredTests,
    passedTests: result.passedTests,
    failedTests: result.failedTests,
    expectedFailures: result.expectedFailures,
    unexpectedPasses: result.unexpectedPasses,
    stdout: result.stdout,
  };
}

function findResolutionReference(graph, predicate, message) {
  const reference = graph.resolution?.references?.find(predicate);
  assert(reference, message);
  return reference;
}

function findSemanticCall(graph, predicate, message) {
  const call = graph.semantics?.calls?.find(predicate);
  assert(call, message);
  return call;
}

function assertSemanticCallResolutionMatches(graph, call, message) {
  const reference = graph.resolution?.references?.find((item) => item.kind === "call" && item.node === call.node);
  assert(reference, `${message}: resolver call reference`);
  for (const field of ["qualifiedName", "targetKind", "targetNode", "symbolId"]) {
    assert.equal(call.resolution?.[field], reference[field], `${message}: semantic resolution ${field}`);
  }
}

function resolutionBindings(graph) {
  return graph.resolution?.scopes?.flatMap((scope) => scope.bindings ?? []) ?? [];
}

function hasIncomingGraphEdge(graph, nodeId, kind, order = null) {
  return graph.edges.some((edge) => edge.target === "node" && edge.to === nodeId && edge.kind === kind && (order === null || edge.order === order));
}

function graphHashText(hash, text = "") {
  for (const byte of Buffer.from(text ?? "")) {
    hash ^= BigInt(byte);
    hash = (hash * graphHashPrime) & graphHashMask;
  }
  hash ^= 0xffn;
  return (hash * graphHashPrime) & graphHashMask;
}

function graphHashU64(hash, value) {
  let item = BigInt(value ?? 0);
  for (let i = 0n; i < 8n; i++) {
    hash ^= (item >> (i * 8n)) & 0xffn;
    hash = (hash * graphHashPrime) & graphHashMask;
  }
  return hash;
}

function graphDomainId(domain, hash) {
  return `${domain}:${hash.toString(16).padStart(16, "0")}`;
}

function graphSymbolComponent(text) {
  let out = "";
  for (const ch of text ?? "") {
    const code = ch.charCodeAt(0);
    const allowed = (code >= 48 && code <= 57) ||
      (code >= 65 && code <= 90) ||
      (code >= 97 && code <= 122) ||
      ch === "." || ch === "_" || ch === "-" || ch === "@" || ch === "/";
    out += allowed ? ch : "_";
  }
  return out || "main";
}

function graphIdentityName(identity) {
  if (!identity) return "main";
  if (identity.startsWith("module:")) return identity.slice("module:".length);
  if (identity.startsWith("package:")) return identity.slice("package:".length);
  return identity;
}

function graphStorageModuleName(identity) {
  if (!identity) return "main";
  return identity.startsWith("module:") ? identity.slice("module:".length) : identity;
}

function graphModuleScopeName(graph, module) {
  const identity = graph.moduleIdentity ?? "module:main";
  const moduleName = module?.name || graphIdentityName(identity);
  if (identity.startsWith("package:")) return `${graphSymbolComponent(graphIdentityName(identity))}/${graphSymbolComponent(moduleName)}`;
  return graphSymbolComponent(moduleName);
}

const graphSymbolNodeKinds = new Set([
  "Module",
  "Const",
  "CImport",
  "TypeAlias",
  "Shape",
  "Interface",
  "Enum",
  "Choice",
  "Function",
  "Param",
  "Field",
  "EnumCase",
  "ChoiceCase",
  "Let",
  "ErrorVariant",
]);

const graphOwnerEdgeKinds = new Set([
  "alias", "arg", "arm", "body", "cImport", "case", "choice", "const", "constraint", "declaredType", "default",
  "effect", "else", "enum", "error", "expr", "field", "function", "guard", "import", "interface", "left",
  "method", "param", "rangeEnd", "returnType", "right", "shape", "statement", "target", "then", "type",
  "typeArg", "typeParam", "value", "variant",
]);

function graphNodeDeclaresSymbol(node) {
  return Boolean(node?.name) && graphSymbolNodeKinds.has(node.kind);
}

function graphNodeSymbolNamespace(node, ownerEdge) {
  if (node.kind === "Module") return "module";
  if (node.kind === "Import") return "import";
  if (node.kind === "CImport") return "c-import";
  if (node.kind === "Const") return "value";
  if (node.kind === "Function") return ownerEdge?.kind === "method" ? "method" : "value";
  if (["TypeAlias", "Shape", "Interface", "Enum", "Choice"].includes(node.kind)) return "type";
  if (node.kind === "Param") return "param";
  if (node.kind === "Field") return "field";
  if (["EnumCase", "ChoiceCase", "ErrorVariant"].includes(node.kind)) return "variant";
  if (node.kind === "Let") return "local";
  return null;
}

function graphSymbolName(node) {
  if (!node) return "";
  if (node.kind !== "Import") return node.name ?? "";
  if (node.value) return node.value;
  const lastDot = (node.name ?? "").lastIndexOf(".");
  return lastDot >= 0 ? node.name.slice(lastDot + 1) : node.name ?? "";
}

function graphOwnerEdgeByNode(graph) {
  const nodeIndex = new Map<string, number>(graph.nodes.map((node, index) => [node.id, index] as [string, number]));
  const ownerEdges = Array(graph.nodes.length).fill(null);
  for (const edge of graph.edges) {
    if ((edge.target ?? "node") !== "node" || !graphOwnerEdgeKinds.has(edge.kind)) continue;
    const target = nodeIndex.get(edge.to);
    if (target !== undefined && !ownerEdges[target]) ownerEdges[target] = edge;
  }
  return ownerEdges;
}

function graphSymbolOwnerIndex(graph, ownerEdges, nodeIndex) {
  const ids = new Map<string, number>(graph.nodes.map((node, index) => [node.id, index] as [string, number]));
  let current = nodeIndex;
  for (let depth = 0; depth < graph.nodes.length; depth++) {
    const edge = ownerEdges[current];
    if (!edge) break;
    const ownerIndex = ids.get(edge.from);
    if (ownerIndex === undefined) break;
    const owner = graph.nodes[ownerIndex];
    if (owner.kind === "Module" || graphNodeDeclaresSymbol(owner)) return ownerIndex;
    current = ownerIndex;
  }
  return null;
}

function graphNodeSymbolId(graph, ownerEdges, nodeIndex, memo = new Map(), depth = 0) {
  if (memo.has(nodeIndex)) return memo.get(nodeIndex);
  if (depth > graph.nodes.length) return "";
  const node = graph.nodes[nodeIndex];
  if (!graphNodeDeclaresSymbol(node)) return "";
  const name = graphSymbolName(node);
  if (!name) return "";
  if (node.kind === "Module") {
    const symbol = `symbol:${graphModuleScopeName(graph, node)}::module`;
    memo.set(nodeIndex, symbol);
    return symbol;
  }
  const ownerEdge = ownerEdges[nodeIndex];
  const ownerIndex = graphSymbolOwnerIndex(graph, ownerEdges, nodeIndex);
  const namespace = graphNodeSymbolNamespace(node, ownerEdge);
  if (!namespace) return "";
  let prefix = "";
  if (ownerIndex !== null && graph.nodes[ownerIndex].kind === "Module") {
    prefix = `symbol:${graphModuleScopeName(graph, graph.nodes[ownerIndex])}::`;
  } else if (ownerIndex !== null) {
    prefix = `${graphNodeSymbolId(graph, ownerEdges, ownerIndex, memo, depth + 1)}/`;
  } else {
    prefix = `symbol:${graphSymbolComponent(graphIdentityName(graph.moduleIdentity))}::`;
  }
  let symbol = `${prefix}${graphSymbolComponent(namespace)}.${graphSymbolComponent(name)}`;
  if (node.kind === "Let") symbol += `@${graphSymbolComponent(node.id.split(".").at(-1) ?? node.id)}`;
  memo.set(nodeIndex, symbol);
  return symbol;
}

function graphNodeHash(node) {
  let hash = 1469598103934665603n;
  for (const item of [node.kind, node.name, node.type, node.value]) hash = graphHashText(hash, item ?? "");
  for (const item of [node.public, node.mutable, node.static, node.fallible, node.exportC]) hash = graphHashU64(hash, item ? 1 : 0);
  return graphDomainId("nodehash", hash);
}

function graphTypeId(node) {
  return node.type ? graphDomainId("type", graphHashText(1469598103934665603n, node.type)) : "";
}

function graphEffectId(node) {
  return node.kind === "EffectRef" && node.name ? graphDomainId("effect", graphHashText(1469598103934665603n, node.name)) : "";
}

function finalizeGraphIdentities(graph) {
  const ownerEdges = graphOwnerEdgeByNode(graph);
  const symbolMemo = new Map();
  let graphHash = graphHashU64(1469598103934665603n, graph.schemaVersion ?? 1);
  graphHash = graphHashText(graphHash, graph.moduleIdentity ?? "");
  for (let index = 0; index < graph.nodes.length; index++) {
    const node = graph.nodes[index];
    node.symbolId = graphNodeSymbolId(graph, ownerEdges, index, symbolMemo);
    node.typeId = graphTypeId(node);
    node.effectId = graphEffectId(node);
    node.nodeHash = graphNodeHash(node);
    for (const item of [node.id, node.nodeHash, node.symbolId, node.typeId, node.effectId]) graphHash = graphHashText(graphHash, item ?? "");
  }
  for (const edge of graph.edges) {
    for (const item of [edge.from, edge.to, edge.kind, edge.target ?? "node"]) graphHash = graphHashText(graphHash, item ?? "");
    graphHash = graphHashU64(graphHash, edge.order ?? 0);
  }
  graph.graphHash = graphDomainId("graph", graphHash);
  return graph;
}

function graphDumpQuote(text) {
  return JSON.stringify(text ?? "");
}

function graphNodeDumpLine(node) {
  const parts = [`node ${node.id} ${node.kind}`];
  for (const field of ["name", "type", "value", "path"]) {
    if (node[field]) parts.push(`${field}:${graphDumpQuote(node[field])}`);
  }
  if (node.line > 0) parts.push(`line:${node.line}`);
  if (node.column > 0) parts.push(`column:${node.column}`);
  for (const [field, label] of [["public", "public"], ["mutable", "mutable"], ["static", "static"], ["fallible", "fallible"], ["exportC", "exportC"]]) {
    if (node[field]) parts.push(`${label}:true`);
  }
  return parts.join(" ");
}

function graphDumpText(graph) {
  const lines = [
    "zero-graph v1",
    "origin source-text",
    `module ${graphDumpQuote(graphStorageModuleName(graph.moduleIdentity))}`,
    `hash ${graphDumpQuote(graph.graphHash)}`,
    "",
    ...graph.nodes.map(graphNodeDumpLine),
    ...graph.edges.map((edge) => `edge ${edge.from} ${edge.kind} ${edge.to} target:${edge.target ?? "node"} order:${edge.order ?? 0}`),
    "",
  ];
  return lines.join("\n");
}

async function assertCheckParity(fixture) {
  const sourceInput = await graphCompilerInputForFixture(fixture);
  const source = await zeroJson(["check", "--json", sourceInput]);
  const artifact = await dumpGraphArtifact(fixture, artifactNameForFixture("check", fixture));
  const graph = await zeroJson(["check", "--json", artifact]);

  const sourceGraphArtifact = source.graph?.artifact ?? source.artifact ?? source.sourceFile;
  const sourceCanonical = source.graph?.canonicalSource ?? source.canonicalSource;
  const graphBackedInput = source.sourceFile?.endsWith("/zero.graph") || sourceGraphArtifact?.endsWith(".graph");
  assert.equal(sourceCanonical, !graphBackedInput, `${fixture}: source check should report the expected graph input kind`);
  assert.equal(graph.canonicalSource, false, `${fixture}: graph artifact check should report artifact input`);
  assert.equal(graph.check.phase, "typecheck", `${fixture}: graph artifact check phase`);
  assert.equal(graph.check.lowering, "graph-native-check", `${fixture}: graph artifact lowering`);
  assert.equal(graph.graphCompiler?.graphNativeCheckerUsed, true, `${fixture}: graph artifact native checker`);
  assert.equal(graph.graphCompiler?.graphHirToMirUsed, true, `${fixture}: graph artifact HIR-to-MIR readiness`);
  assert.equal(graph.check.ok, source.ok, `${fixture}: source and graph typecheck should agree`);
  assert.equal(graph.check.target, source.targetReadiness?.target ?? graph.check.target, `${fixture}: target should agree`);
  assert.deepEqual(targetReadinessSummary(graph.targetReadiness), targetReadinessSummary(source.targetReadiness), `${fixture}: target readiness should agree`);
}

async function assertCheckFailureParity(fixture) {
  const sourceInput = await graphCompilerInputForFixture(fixture);
  const source = await zeroJsonFailure(["check", "--json", sourceInput]);
  const artifact = await dumpGraphArtifact(fixture, artifactNameForFixture("check-fail", fixture));
  const graph = await zeroJsonFailure(["check", "--json", artifact]);
  assert.equal(graph.canonicalSource, false, `${fixture}: graph artifact check failure should report artifact input`);
  assert.equal(graph.check.phase, "typecheck", `${fixture}: graph artifact check failure phase`);
  assert.equal(graph.check.lowering, "graph-native-check", `${fixture}: graph artifact check failure lowering`);
  assert.equal(graph.graphCompiler?.graphNativeCheckerUsed, true, `${fixture}: graph artifact failure native checker`);
  assert.equal(graph.check.ok, false, `${fixture}: graph artifact check should fail`);
  const sourceDiag = source.diagnostics?.[0];
  const graphDiag = graph.diagnostics?.[0];
  for (const field of ["code", "message", "path", "line", "column", "expected", "actual", "help"]) {
    assert.equal(graphDiag?.[field], sourceDiag?.[field], `${fixture}: diagnostic ${field} should agree`);
  }
}

async function assertRoundtripStable(fixture) {
  const roundtrip = await zeroJson(["roundtrip", "--json", fixture]);
  assert.equal(roundtrip.ok, true, `${fixture}: graph roundtrip ok`);
  assert.equal(roundtrip.semanticStable, true, `${fixture}: graph roundtrip semantic stability`);
  assert.equal(roundtrip.lowering, "direct-program-graph", `${fixture}: graph roundtrip lowering`);
  assert.equal(roundtrip.roundtripModuleIdentity, roundtrip.moduleIdentity, `${fixture}: module identity`);
  assert.equal(roundtrip.comparison.ok, true, `${fixture}: graph semantic comparison`);
  assert.deepEqual(roundtrip.semanticCounts.original, roundtrip.semanticCounts.roundtrip, `${fixture}: semantic counts`);
}

async function assertCommandStateContracts() {
  const sourceDump = await zeroJson(["dump", "--json", "examples/hello.0"]);
  const sidecarDump = await zeroJson(["validate", "--json", "examples/hello.graph"]);
  assert.equal(sourceDump.canonicalSource, false, "graph dump from a projection should use its graph sidecar");
  assert.equal(sourceDump.graphHash, sidecarDump.graphHash, "graph dump from a projection should read the sidecar graph");
  assert.equal(sourceDump.validation.state, "shape-valid", "graph dump should produce a shape-valid graph");
  assert.equal(sourceDump.validation.ok, true, "graph dump validation should pass");
  assert.equal(sourceDump.resolution.state, "resolved", "graph dump should expose name resolution state");
  assert.equal(sourceDump.resolution.ok, true, "graph dump resolution should pass");
  const worldRef = findResolutionReference(sourceDump, (item) => item.kind === "identifier" && item.name === "world", "graph dump should resolve parameter identifiers");
  assert.equal(worldRef.targetKind, "param", "graph dump should classify parameter references");
  const writeRef = findResolutionReference(sourceDump, (item) => item.kind === "call" && item.qualifiedName === "world.out.write", "graph dump should resolve member calls to their base binding");
  assert.equal(writeRef.targetKind, "member", "graph dump should classify member calls");

  const artifact = await dumpGraphArtifact("examples/hello.0", "state-contracts");
  const validate = await zeroJson(["validate", "--json", artifact]);
  assert.equal(validate.ok, true, "graph validate should accept the artifact");
  assert.equal(validate.canonicalSource, false, "graph validate should report artifact input");
  assert.equal(validate.validation.state, "shape-valid", "graph validate should promise shape-valid state");
  assert.equal(validate.validation.ok, true, "graph validate state should be ok");

  const emptyLiteralArtifact = await dumpGraphArtifact("benchmarks/rosetta/empty-string.0", "empty-string-literal");
  const emptyLiteralDump = await readFile(emptyLiteralArtifact, "utf8");
  assert.match(emptyLiteralDump, /node #[^ ]+ Literal[^\n]* value:""/, "graph dump should preserve empty literal values");
  const emptyLiteralValidate = await zeroJson(["validate", "--json", emptyLiteralArtifact]);
  assert.equal(emptyLiteralValidate.ok, true, "graph validate should accept stored empty string literal artifacts");

  const view = await zeroJson(["view", "--json", artifact]);
  assert.equal(view.ok, true, "graph view should render a valid source projection");
  assert.equal(view.canonicalSource, false, "graph view should report artifact input");
  assert.match(view.view, /pub fn main/, "graph view should include canonical source text");

  const check = await zeroJson(["check", "--json", artifact]);
  assert.equal(check.ok, true, "graph check should typecheck the artifact");
  assert.equal(check.canonicalSource, false, "graph check should report artifact input");
  assert.equal(check.check.phase, "typecheck", "graph check should promise typecheck state");
  assert.equal(check.check.lowering, "graph-native-check", "graph check should use graph-native semantic checks");
  assert.equal(check.graphCompiler.graphNativeCheckerUsed, true, "graph check should report graph-native checker use");
  assert.equal(check.graphCompiler.graphHirToMirUsed, true, "graph check should report graph HIR-to-MIR readiness");
  assert.equal(check.targetReadiness.languageOk, true, "graph check should include language readiness");

  const size = await zeroJson(["size", "--json", "--target", "linux-musl-x64", artifact]);
  assert.equal(size.graph.canonicalSource, false, "graph size should report artifact input");
  assert.equal(size.graph.lowering, "mapped-final-mir", "graph size should lower through mapped final MIR");
  assert.equal(size.generatedCBytes, 0, "graph size should stay on the direct backend");
  assert.equal(size.cBridgeFallback, false, "graph size should not use C bridge fallback");
  const sizeMirCache = size.compilerCaches.find((cache) => cache.name === "mappedFinalMir");
  assert.equal(sizeMirCache.codegenImmediate, false, "graph size should compute report facts after a cold mapped-MIR write");
  assert.equal(sizeMirCache.programReconstructed, false, "graph size should derive report facts from graph/IR metadata instead of reconstructing checked Program facts");
  const build = await zeroJson(["build", "--json", "--target", "linux-musl-x64", "--out", `${outDir}/state-contracts-build`, artifact]);
  const buildMirCache = build.compilerCaches.find((cache) => cache.name === "mappedFinalMir");
  assert.equal(buildMirCache.hit, true, "graph build should reuse the mapped final MIR warmed by size");
  assert.equal(buildMirCache.codegenImmediate, true, "graph build should codegen immediately from mapped final MIR");
  assert.equal(buildMirCache.programReconstructed, false, "graph build should not reconstruct checked program facts on a mapped MIR hit");
  for (const [index, [label, corrupt]] of mappedMirCorruptionCases.entries()) {
    await assertMappedMirCacheRegeneratesAfterCorruption(
      sizeMirCache.path,
      corrupt,
      `${outDir}/state-contracts-build-corrupt-${index}`,
      artifact,
      label,
    );
  }

  const stdArgsArtifact = await dumpGraphArtifact("conformance/native/pass/std-args.0", "std-args-mir");
  const stdArgsSize = await zeroJson(["size", "--json", "--target", "linux-musl-x64", stdArgsArtifact]);
  assert.equal(stdArgsSize.graph.lowering, "mapped-final-mir", "graph size should lower std args through mapped final MIR");
  assert.equal(stdArgsSize.generatedCBytes, 0, "graph MIR std args should stay on the direct backend");
  assert.equal(stdArgsSize.objectBackend.directFacts.functionCount, 1, "graph MIR std args should retain the main function");
  const stdArgsRun = await zeroText(["run", "--out", `${outDir}/std-args-run`, stdArgsArtifact, "one", "two"]);
  assert.equal(stdArgsRun, "one\n", "graph run should execute std args from typed graph MIR");

  const roundtrip = await zeroJson(["roundtrip", "--json", artifact]);
  assert.equal(roundtrip.ok, true, "graph roundtrip should accept the artifact");
  assert.equal(roundtrip.canonicalSource, false, "graph roundtrip should report artifact input");
  assert.equal(roundtrip.semanticStable, true, "graph roundtrip should promise semantic stability");
  assert.equal(roundtrip.lowering, "direct-program-graph", "graph roundtrip should lower through ProgramGraph");

  const sourceMap = await zeroJson(["source-map", "--json", "examples/hello.0"]);
  assert.equal(sourceMap.ok, true, "graph source-map should succeed");
  assert.equal(sourceMap.artifact, "examples/hello.graph", "graph source-map should read the projection sidecar");
  assert.equal(sourceMap.canonicalSource, false, "graph source-map should report graph artifact input for projections");
  const mainMapping = sourceMap.mappings.find((mapping) => mapping.kind === "Function" && mapping.name === "main");
  assert(mainMapping && mainMapping.sourceRange.path === "hello.0", "graph source-map should map function nodes to stored graph source ranges");
  assert.equal(mainMapping.sourceAvailable, false, "graph source-map should not tokenize the projection when the sidecar is active");
  assert.deepEqual(mainMapping.sourceRange.start, { line: 1, column: 1 }, "graph source-map should preserve stored graph source locations");
  assert.deepEqual(mainMapping.sourceRange.end, { line: 1, column: 2 }, "graph source-map should preserve stored graph source ranges");
  assert.equal(await zeroText(["source-map", "examples/hello.0"]), `program graph source map ok: ${sourceMap.counts.mappings} mappings\n`, "graph source-map text output");

  const repeatedTypesFixture = `${outDir}/source-map-repeated-types.0`;
  await writeFile(repeatedTypesFixture, [
    "fn double(value: i32) -> i32 {",
    "    return value + value",
    "}",
    "",
    "pub fn main(world: World) -> Void raises {",
    "    check world.out.write(\"source map repeated types\\n\")",
    "}",
    "",
  ].join("\n"));
  await importProjectionSidecar(repeatedTypesFixture);
  const repeatedTypesMap = await zeroJson(["source-map", "--json", repeatedTypesFixture]);
  const repeatedTypeRanges = repeatedTypesMap.mappings
    .filter((mapping) => mapping.kind === "TypeRef" && mapping.type === "i32" && mapping.sourceRange.start.line === 1)
    .map((mapping) => mapping.sourceRange.start);
  assert.deepEqual(repeatedTypeRanges, [{ line: 1, column: 11 }, { line: 1, column: 1 }], "graph source-map should use stored graph ranges without reparsing projection text");
  assert.equal(repeatedTypesMap.mappings.every((mapping) => mapping.sourceAvailable === false), true, "graph source-map should not tokenize sidecar-backed projection text");
}

async function assertResolutionFacts() {
  const stdStr = await zeroJson(["dump", "--json", "examples/std-str.0"]);
  assert.equal(stdStr.resolution.ok, true, "std-str graph resolution");
  const grepScan = await zeroJson(["dump", "--json", "examples/grep-scan.0"]);
  assert.equal(grepScan.resolution.ok, true, "grep-scan graph resolution");
  const nextLine = findResolutionReference(grepScan, (item) => item.kind === "call" && item.qualifiedName === "std.io.nextLine", "graph-backed std call should resolve");
  assert.equal(nextLine.targetKind, "graphBackedStdlib", "graph-backed std call target kind");
  assert.match(nextLine.symbolId, /^symbol:std\.io::value\.__zero_std_io_next_line$/, "graph-backed std call symbol");
  const memEql = findResolutionReference(stdStr, (item) => item.kind === "call" && item.qualifiedName === "std.mem.eql", "table std helper should resolve");
  assert.equal(memEql.targetKind, "stdlib", "table std helper target kind");
  assert.equal(memEql.symbolId, "stdlib:std.mem.eql", "table std helper symbol");

  const packageGraph = await zeroJson(["dump", "--json", "examples/direct-package-arrays"]);
  assert.equal(packageGraph.resolution.ok, true, "package graph resolution");
  const record = findResolutionReference(packageGraph, (item) => item.kind === "call" && item.qualifiedName === "record", "package import call should resolve");
  assert.equal(record.targetKind, "function", "package import call target kind");
  assert.equal(record.symbolId, "symbol:direct-package-arrays@0.1.0/arrays::value.record", "package import call target symbol");
  assert.equal(record.viaImport, "symbol:main::import.arrays", "package import call should record import binding");

  const cImport = await zeroJson(["dump", "--json", "conformance/native/pass/c-import-alias-later-local.0"]);
  assert.equal(cImport.resolution.ok, true, "C import graph resolution");
  const cCall = findResolutionReference(cImport, (item) => item.kind === "call" && item.qualifiedName === "c.zero_c_add", "C import call should resolve");
  assert.equal(cCall.targetKind, "cFunction", "C import call target kind");
  assert.match(cCall.symbolId, /^symbol:c-import-alias-later-local::c-import\.c$/, "C import call symbol");
  const localC = findResolutionReference(cImport, (item) => item.kind === "identifier" && item.name === "c" && item.targetKind === "local", "later local should shadow C import after declaration");
  assert.match(localC.symbolId, /local\.c@/, "local shadow symbol");

  const cImportTypeShadow = await zeroJson(["dump", "--json", "conformance/native/pass/c-import-type-shadowing.0"]);
  assert.equal(cImportTypeShadow.resolution.ok, true, "C import/type shadow graph resolution");
  const shadowedCounterCall = findResolutionReference(cImportTypeShadow, (item) => item.kind === "call" && item.qualifiedName === "Counter.zero_c_add", "static method should resolve when a C import alias shares the type name");
  assert.equal(shadowedCounterCall.targetKind, "method", "C import/type shadow static method target kind");
  assert.equal(shadowedCounterCall.symbolId, "symbol:c-import-type-shadowing::type.Counter/method.zero_c_add", "C import/type shadow static method symbol");
  assert.equal(shadowedCounterCall.viaImport, "", "same-module static method should not report an import binding");
  const counterIdentifier = findResolutionReference(cImportTypeShadow, (item) => item.kind === "identifier" && item.name === "Counter", "call-chain base should resolve to the type namespace");
  assert.equal(counterIdentifier.targetKind, "shape", "call-chain base should prefer the type binding for static method chains");

  const staticInterface = await zeroJson(["dump", "--json", "examples/static-interface.0"]);
  assert.equal(staticInterface.resolution.ok, true, "static interface graph resolution");
  const interfaceCall = findResolutionReference(staticInterface, (item) => item.kind === "call" && item.qualifiedName === "T.read", "constrained interface method call should resolve");
  assert.equal(interfaceCall.targetKind, "interfaceMethod", "constrained interface call target kind");
  assert.equal(interfaceCall.symbolId, "symbol:static-interface::type.Readable/method.read", "constrained interface call target symbol");

  const genericFunction = await zeroJson(["dump", "--json", "conformance/native/pass/generic-function-basic.0"]);
  assert.equal(genericFunction.resolution.ok, true, "generic function graph resolution");
  const identityReturnType = findResolutionReference(genericFunction, (item) => item.kind === "type" && item.name === "T" && item.symbolId === "symbol:generic-function-basic::value.identity/param.T" && hasIncomingGraphEdge(genericFunction, item.node, "returnType"), "generic return type should resolve to its type parameter");
  assert.equal(identityReturnType.targetKind, "type", "generic return type target kind");

  const staticValues = await zeroJson(["dump", "--json", "conformance/native/pass/static-value-params.0"]);
  assert.equal(staticValues.resolution.ok, true, "static value parameter graph resolution");
  const staticParamBinding = resolutionBindings(staticValues).find((item) => item.name === "N" && item.kind === "staticParam" && item.symbolId === "symbol:static-value-params::value.first/param.N");
  assert(staticParamBinding, "function static parameter should be classified as a staticParam binding");
  const capTypeArg = findResolutionReference(staticValues, (item) => item.kind === "type" && item.name === "cap" && hasIncomingGraphEdge(staticValues, item.node, "typeArg"), "static value type argument should resolve through value lookup");
  assert.equal(capTypeArg.targetKind, "const", "static value type argument target kind");
  assert.equal(capTypeArg.symbolId, "symbol:static-value-params::value.cap", "static value type argument symbol");

  const staticExplicitFixture = `${outDir}/resolution-static-explicit-args.0`;
  await writeFile(staticExplicitFixture, [
    "const Foo: usize = 3",
    "",
    "type Foo {",
    "    value: i32,",
    "}",
    "",
    "type Gate<static enabledFlag: Bool, static selectedMode: Mode> {",
    "    value: i32,",
    "}",
    "",
    "enum Mode: u8 {",
    "    fast,",
    "    tiny,",
    "}",
    "",
    "fn readGate<static enabledFlag: Bool, static selectedMode: Mode>(gate: ref<Gate<enabledFlag, selectedMode>>) -> i32 {",
    "    if enabledFlag {",
    "        return gate.value",
    "    }",
    "    return 0",
    "}",
    "",
    "fn pick<T: Type, static N: usize>(value: T, items: ref<[N]i32>) -> T {",
    "    return value",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let items: [Foo]i32 = [1, 2, 3]",
    "    let value: Foo = pick<Foo, Foo>(Foo { value: 7 }, &items)",
    "    let gate: Gate<true, Mode.fast> = Gate { value: 9 }",
    "    let gated: i32 = readGate<true, Mode.fast>(&gate)",
    "    expect (value.value == 7 && gated == 9)",
    "}",
    "",
  ].join("\n"));
  await importProjectionSidecar(staticExplicitFixture);
  const staticExplicit = await zeroJson(["dump", "--json", staticExplicitFixture]);
  assert.equal(staticExplicit.resolution.ok, true, "explicit static generic argument graph resolution");
  const pickTypeArg = findResolutionReference(staticExplicit, (item) => item.kind === "type" && item.name === "Foo" && hasIncomingGraphEdge(staticExplicit, item.node, "typeArg", 0), "type argument in a mixed generic call should resolve as a type");
  assert.equal(pickTypeArg.targetKind, "type", "mixed generic type argument target kind");
  assert.equal(pickTypeArg.symbolId, "symbol:resolution-static-explicit-args::type.Foo", "mixed generic type argument symbol");
  const pickStaticArg = findResolutionReference(staticExplicit, (item) => item.kind === "type" && item.name === "Foo" && hasIncomingGraphEdge(staticExplicit, item.node, "typeArg", 1), "static argument with a type-name collision should resolve as a value");
  assert.equal(pickStaticArg.targetKind, "const", "static argument with a type-name collision target kind");
  assert.equal(pickStaticArg.symbolId, "symbol:resolution-static-explicit-args::value.Foo", "static argument with a type-name collision symbol");
  const boolStaticArg = findResolutionReference(staticExplicit, (item) => item.kind === "type" && item.qualifiedName === "true" && hasIncomingGraphEdge(staticExplicit, item.node, "typeArg", 0), "literal bool static argument should resolve");
  assert.equal(boolStaticArg.targetKind, "staticLiteral", "literal bool static argument target kind");
  assert.equal(boolStaticArg.symbolId, "literal:true", "literal bool static argument symbol");
  const enumStaticArg = findResolutionReference(staticExplicit, (item) => item.kind === "type" && item.qualifiedName === "Mode.fast" && hasIncomingGraphEdge(staticExplicit, item.node, "typeArg", 1), "enum-case static argument should resolve");
  assert.equal(enumStaticArg.targetKind, "variant", "enum-case static argument target kind");
  assert.equal(enumStaticArg.symbolId, "symbol:resolution-static-explicit-args::type.Mode/variant.fast", "enum-case static argument symbol");

  const valueTypeCollisionFixture = `${outDir}/resolution-value-type-collision.0`;
  await writeFile(valueTypeCollisionFixture, [
    "const Foo: usize = 3",
    "",
    "type Foo {",
    "    value: i32,",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let n: usize = Foo",
    "    expect n == 3",
    "}",
    "",
  ].join("\n"));
  await importProjectionSidecar(valueTypeCollisionFixture);
  const valueTypeCollision = await zeroJson(["dump", "--json", valueTypeCollisionFixture]);
  assert.equal(valueTypeCollision.resolution.ok, true, "value/type collision graph resolution");
  const valueFoo = findResolutionReference(valueTypeCollision, (item) => item.kind === "identifier" && item.name === "Foo", "ordinary identifiers should prefer value bindings over same-name type bindings");
  assert.equal(valueFoo.targetKind, "const", "value/type collision identifier target kind");
  assert.equal(valueFoo.symbolId, "symbol:resolution-value-type-collision::value.Foo", "value/type collision identifier symbol");

  const builtinShadowFixture = `${outDir}/resolution-builtin-shadow.0`;
  await writeFile(builtinShadowFixture, [
    "type World {",
    "    value: i32,",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let item: World = World { value: 1 }",
    "    expect item.value == 1",
    "}",
    "",
  ].join("\n"));
  await importProjectionSidecar(builtinShadowFixture);
  const builtinShadow = await zeroJson(["dump", "--json", builtinShadowFixture]);
  assert.equal(builtinShadow.resolution.ok, true, "builtin type shadow graph resolution");
  const shadowedWorld = findResolutionReference(builtinShadow, (item) => item.kind === "type" && item.name === "World" && hasIncomingGraphEdge(builtinShadow, item.node, "declaredType"), "declared types should shadow builtin type names in resolution facts");
  assert.equal(shadowedWorld.targetKind, "type", "builtin shadow type target kind");
  assert.equal(shadowedWorld.symbolId, "symbol:resolution-builtin-shadow::type.World", "builtin shadow type symbol");

  const forRange = await zeroJson(["dump", "--json", "conformance/native/pass/for-range.0"]);
  assert.equal(forRange.resolution.ok, true, "for range graph resolution");
  const forIndexBinding = resolutionBindings(forRange).find((item) => item.name === "index" && item.kind === "local");
  assert(forIndexBinding, "for range iterator should create a local binding");
  assert.match(forIndexBinding.symbolId, /local\.index@_stmt_/, "for range iterator symbol should include its node id");
  const forIndexRef = findResolutionReference(forRange, (item) => item.kind === "identifier" && item.name === "index" && item.targetKind === "local", "for range body should resolve iterator references");
  assert.equal(forIndexRef.symbolId, forIndexBinding.symbolId, "for range iterator reference symbol");

  const resultChoice = await zeroJson(["dump", "--json", "examples/result-choice.0"]);
  assert.equal(resultChoice.resolution.ok, true, "choice constructor graph resolution");
  const choiceConstructor = findResolutionReference(resultChoice, (item) => item.kind === "call" && item.qualifiedName === "Result.ok", "choice constructor should resolve to its case binding");
  assert.equal(choiceConstructor.targetKind, "variant", "choice constructor target kind");
  assert.equal(choiceConstructor.symbolId, "symbol:result-choice::type.Result/variant.ok", "choice constructor target symbol");
  assert.equal(choiceConstructor.viaImport, "", "same-module choice constructor should not report an import binding");
  const patternBinding = resolutionBindings(resultChoice).find((item) => item.name === "value" && item.kind === "pattern");
  assert(patternBinding, "choice match payload should create a pattern binding");
  assert.match(patternBinding.symbolId, /pattern\.value@_stmt_/, "choice match payload symbol should include its node id");

  const testBlocks = await zeroJson(["dump", "--json", "conformance/native/pass/test-blocks.0"]);
  assert.equal(testBlocks.resolution.ok, true, "test block graph resolution");
  const expectCall = findResolutionReference(testBlocks, (item) => item.kind === "call" && item.qualifiedName === "expect", "test expect call should resolve as a language builtin");
  assert.equal(expectCall.targetKind, "testExpect", "test expect call target kind");
  assert.equal(expectCall.symbolId, "builtin:expect", "test expect call symbol");

  const compileTime = await zeroJson(["dump", "--json", "conformance/native/pass/compile-time-v1.0"]);
  assert.equal(compileTime.resolution.ok, true, "compile-time facts graph resolution");
  const targetNamespace = findResolutionReference(compileTime, (item) => item.kind === "identifier" && item.name === "target", "target meta namespace should resolve");
  assert.equal(targetNamespace.targetKind, "targetNamespace", "target namespace target kind");
  for (const fact of ["hasField", "fieldCount", "fieldType", "enumCaseCount", "hasEnumCase", "choiceCaseCount", "hasChoiceCase"]) {
    const metaFact = findResolutionReference(compileTime, (item) => item.kind === "call" && item.qualifiedName === fact, `${fact} meta call should resolve`);
    assert.equal(metaFact.targetKind, "metaFact", `${fact} meta call target kind`);
    assert.equal(metaFact.symbolId, `meta:${fact}`, `${fact} meta call symbol`);
  }
  const targetFacts = await zeroJson(["dump", "--json", "conformance/native/pass/meta-typed-target-type.0"]);
  assert.equal(targetFacts.resolution.ok, true, "target fact graph resolution");
  const hasCapability = findResolutionReference(targetFacts, (item) => item.kind === "call" && item.qualifiedName === "target.hasCapability", "target fact call should resolve");
  assert.equal(hasCapability.targetKind, "targetFact", "target fact call target kind");
  assert.equal(hasCapability.symbolId, "meta:target.hasCapability", "target fact call symbol");

  const fixedVec = await zeroJson(["dump", "--json", "examples/fixed-vec.0"]);
  assert.equal(fixedVec.resolution.ok, true, "Self graph resolution");
  const selfReturn = findResolutionReference(fixedVec, (item) => item.kind === "type" && item.name === "Self" && hasIncomingGraphEdge(fixedVec, item.node, "returnType"), "Self return type should resolve to the enclosing type");
  assert.equal(selfReturn.targetKind, "type", "Self return type target kind");
  assert.equal(selfReturn.symbolId, "symbol:fixed-vec::type.FixedVec", "Self return type symbol");
  const receiverPush = findResolutionReference(fixedVec, (item) => item.kind === "call" && item.qualifiedName === "vec.push", "receiver method should resolve to the method binding");
  assert.equal(receiverPush.targetKind, "method", "receiver method target kind");
  assert.equal(receiverPush.symbolId, "symbol:fixed-vec::type.FixedVec/method.push", "receiver method target symbol");

  const systemsPackage = await zeroJson(["dump", "--json", "examples/systems-package"]);
  assert.equal(systemsPackage.resolution.ok, true, "package-local module graph resolution");
  const statusType = findResolutionReference(systemsPackage, (item) => item.kind === "type" && item.name === "Status" && item.symbolId === "symbol:systems-package@0.1.0/types::type.Status", "package-local type should resolve across loaded modules");
  assert.equal(statusType.targetKind, "type", "package-local type target kind");
  const statusVariant = findResolutionReference(systemsPackage, (item) => item.kind === "identifier" && item.name === "Status" && item.symbolId === "symbol:systems-package@0.1.0/types::type.Status", "package-local enum namespace should resolve across loaded modules");
  assert.equal(statusVariant.targetKind, "enum", "package-local enum namespace target kind");

  const hostedTypes = await zeroJson(["dump", "--json", "examples/readall-cli"]);
  assert.equal(hostedTypes.resolution.ok, true, "hosted std-backed type graph resolution");

  const stdShadowFixture = `${outDir}/std-shadow.0`;
  await writeFile(stdShadowFixture, [
    "pub fn main(world: World) -> Void raises {",
    "    let std: i32 = 1",
    "    if std == 1 {",
    "        check world.out.write(\"std shadow ok\\n\")",
    "    }",
    "}",
    "",
  ].join("\n"));
  await importProjectionSidecar(stdShadowFixture);
  const stdShadow = await zeroJson(["dump", "--json", stdShadowFixture]);
  assert.equal(stdShadow.resolution.ok, true, "local std shadow graph resolution");
  const stdRef = findResolutionReference(stdShadow, (item) => item.kind === "identifier" && item.name === "std", "local std identifier should be present");
  assert.equal(stdRef.targetKind, "local", "local std should shadow the stdlib namespace");
  assert.match(stdRef.symbolId, /local\.std@/, "local std shadow symbol");
}

async function assertStdGraphDependencyMerge() {
  const fixture = `${outDir}/stdlib-codec-http-merge.0`;
  const artifact = `${outDir}/stdlib-codec-http-merge.graph`;
  await writeFile(fixture, [
    "export c fn main() -> i32 {",
    "    var ok: Bool = true",
    "",
    "    let bad_hex: Maybe<usize> = std.codec.hexDecodedLen(\"41z\")",
    "    var base64_buf: [2]u8 = [0_u8; 2]",
    "    let bad_base64: Maybe<Span<u8>> = std.codec.base64Decode(base64_buf, \"AAB=\")",
    "    if bad_hex.has || bad_base64.has {",
    "        ok = false",
    "    }",
    "",
    "    var request_buf: [128]u8 = [0_u8; 128]",
    "    let request: Maybe<Span<u8>> = std.http.writeJsonRequest(request_buf, \"POST https://example.com/api?name=zero\", \"{\\\"ping\\\":1}\")",
    "    if request.has {",
    "        let missing: Maybe<Span<u8>> = std.http.requestQueryValue(request.value, \"missing\")",
    "        if missing.has {",
    "            ok = false",
    "        }",
    "    }",
    "",
    "    if ok {",
    "        return 0",
    "    }",
    "    return 1",
    "}",
    "",
  ].join("\n"));

  await zeroText(["import", "--format", "text", "--out", artifact, fixture]);
  assert.equal(await zeroText(["validate", artifact]), "program graph ok\n", "merged std dependency graph should validate");
  assert.equal(await zeroText(["check", artifact]), "ok\n", "merged std dependency graph should check");

  const graph = await zeroJson(["dump", "--json", artifact]);
  assert.equal(graph.validation.ok, true, "merged std dependency graph dump validation");
  const stdUrl = graph.nodes.find((node) => node.kind === "Module" && node.name === "std.url");
  assert(stdUrl, "std graph dependency merge should retain the shared std.url module");
  const nodeById = new Map<string, any>(graph.nodes.map((node: any) => [node.id, node]));
  const functionEdges = graph.edges.filter((edge) => edge.target === "node" && edge.from === stdUrl.id && edge.kind === "function");
  const orders = functionEdges.map((edge) => edge.order).sort((a, b) => a - b);
  assert.deepEqual(orders, [...orders.keys()], "shared std module function edges should be compact after merging partial std graphs");
  const functionNames = new Set(functionEdges.map((edge) => nodeById.get(edge.to)?.name));
  assert(functionNames.has("__zero_std_url_percent_encode"), "std.codec dependency should contribute URL encoding helpers");
  assert(functionNames.has("__zero_std_url_query_value"), "std.http dependency should contribute URL query helpers");
}

async function assertSemanticFacts() {
  const hello = await zeroJson(["dump", "--json", "examples/hello.0"]);
  assert.equal(hello.semantics.state, "typed-facts", "hello graph semantic fact state");
  assert.equal(hello.semantics.ok, true, "hello graph semantic facts");
  assert.equal(hello.semantics.counts.functions, 1, "hello semantic function count");
  assert.equal(hello.semantics.counts.calls, 1, "hello semantic call count");
  assert.equal(hello.semantics.counts.fallibleCalls, 1, "hello semantic fallible call count");
  assert.equal(hello.semantics.counts.resources, hello.semantics.resources.length, "hello semantic resource count");
  assert.equal(hello.semantics.counts.targetRequirements, hello.semantics.targetRequirements.length, "hello semantic target requirement count");
  assert.equal(hello.semantics.counts.repairs, hello.semantics.repairs.length, "hello semantic repair count");
  const helloMain = hello.semantics.functions.find((item) => item.name === "main");
  assert(helloMain, "hello semantic function fact");
  assert.equal(helloMain.returnType, "Void", "hello function return type");
  assert.equal(helloMain.fallible, true, "hello function fallibility");
  assert.deepEqual(helloMain.params.map((item) => [item.name, item.type]), [["world", "World"]], "hello function params");
  assert.equal(helloMain.sourceRange.path, "hello.0", "hello function semantic source range should use the stored graph path");
  assert.deepEqual(helloMain.sourceRange.start, { line: 1, column: 1 }, "hello function semantic source range should come from stored graph metadata");
  assert.deepEqual(helloMain.sourceRange.end, { line: 1, column: 2 }, "hello function semantic source range should come from stored graph metadata");
  assert(hello.semantics.ownership.some((item) => item.name === "world" && item.ownership === "resource-handle" && item.resource === true), "world parameter should be represented as a resource handle");
  const write = findSemanticCall(hello, (item) => item.qualifiedName === "world.out.write", "world write semantic call fact");
  assert.equal(write.returnType, "Void", "world write return type");
  assert.equal(write.contract.kind, "worldStreamWrite", "world write contract kind");
  assert.equal(write.contract.capability, "io", "world write capability");
  assert.equal(write.contract.targetSupport, "world-io", "world write target support");
  assert.match(write.contract.targetNode, /^#param_/, "world write contract target node");
  assert.equal(write.contract.symbolId, "symbol:hello::value.main/param.world", "world write contract symbol");
  assertSemanticCallResolutionMatches(hello, write, "world write semantic resolution");
  assert.equal(write.resolution.targetKind, "member", "world write semantic resolution kind");
  assert.equal(write.resolution.symbolId, "symbol:hello::value.main/param.world", "world write semantic resolution symbol");
  assert.equal(write.fallible, true, "world write fallibility");
  assert.equal(write.checked, true, "world write checked state");
  assert.equal(write.contract.requiresCheck, false, "checked world write should not require repair");
  assert.equal(write.contract.repair.id, "check-fallible-call", "world write repair shape");
  assert.equal(write.sourceRange.path, "hello.0", "world write source range should use the stored graph path");
  assert.deepEqual(write.sourceRange.start, { line: 2, column: 26 }, "world write semantic source range should come from stored graph metadata");
  assert.deepEqual(write.sourceRange.end, { line: 2, column: 27 }, "world write semantic source range should come from stored graph metadata");
  assert(hello.semantics.resources.some((item) => item.kind === "capabilityUse" && item.resourceKind === "world-io" && item.qualifiedName === "world.out.write"), "world write resource fact");
  assert(hello.semantics.targetRequirements.some((item) => item.qualifiedName === "world.out.write" && item.capability === "io" && item.targetSupport === "world-io"), "world write target requirement fact");
  assert(hello.semantics.repairs.some((item) => item.qualifiedName === "world.out.write" && item.requiresCheck === false && item.repair.id === "check-fallible-call"), "world write top-level repair fact");

  const grepScan = await zeroJson(["dump", "--json", "examples/grep-scan.0"]);
  const nextLine = findSemanticCall(grepScan, (item) => item.qualifiedName === "std.io.nextLine", "std io nextLine semantic call fact");
  assert.equal(nextLine.contract.kind, "graphBackedStdlib", "graph-backed std contract kind");
  assert.equal(nextLine.contract.sourceModule, "std.io", "graph-backed std module");
  assert.equal(nextLine.contract.returnType, "Maybe<Span<u8>>", "graph-backed std return type");
  assert.equal(nextLine.contract.capability, "memory", "graph-backed std capability");
  assert.equal(nextLine.contract.targetSupport, "target-neutral", "graph-backed std target support");
  assert.equal(nextLine.contract.expectedArgCount, 2, "graph-backed std arg count");
  assert.deepEqual(nextLine.contract.expectedArgTypes, ["Span<u8>", "usize"], "graph-backed std arg types");
  assertSemanticCallResolutionMatches(grepScan, nextLine, "graph-backed std semantic resolution");
  assert.equal(nextLine.resolution.targetKind, "graphBackedStdlib", "graph-backed std semantic resolution kind");
  assert.equal(nextLine.resolution.symbolId, "symbol:std.io::value.__zero_std_io_next_line", "graph-backed std semantic symbol");
  assert.deepEqual(nextLine.args.map((item) => item.type), ["Span<u8>", "usize"], "graph-backed std actual arg types");

  const stdFs = await zeroJson(["dump", "--json", "conformance/native/pass/std-fs-fallible.0"]);
  const stdFsMain = stdFs.semantics.functions.find((item) => item.name === "main");
  assert(stdFsMain, "std fs main semantic function fact");
  assert.deepEqual(stdFsMain.errors, ["NotFound", "TooLarge", "Io"], "named error set facts");
  const readAll = findSemanticCall(stdFs, (item) => item.qualifiedName === "std.fs.readAllOrRaise", "fallible std helper semantic call fact");
  assert.equal(readAll.contract.kind, "stdlib", "fallible std helper contract kind");
  assert.equal(readAll.contract.fallible, true, "fallible std helper contract fallibility");
  assert.equal(readAll.contract.checked, true, "fallible std helper checked state");
  assert.deepEqual(readAll.contract.errors, ["NotFound", "TooLarge", "Io"], "fallible std helper errors");
  assert.equal(readAll.contract.capability, "fs", "fallible std helper capability");
  assert.equal(readAll.contract.targetSupport, "host", "fallible std helper target support");
  assert.equal(readAll.contract.repair.id, "check-fallible-call", "fallible std helper repair shape");
  assertSemanticCallResolutionMatches(stdFs, readAll, "fallible std helper semantic resolution");
  assert.equal(readAll.resolution.targetKind, "stdlib", "fallible std helper semantic resolution kind");
  assert.equal(readAll.resolution.symbolId, "stdlib:std.fs.readAllOrRaise", "fallible std helper semantic symbol");
  assert(stdFs.semantics.ownership.some((item) => item.name === "body" && item.type === "owned<ByteBuf>" && item.ownership === "owned"), "owned ByteBuf ownership fact");
  assert(stdFs.semantics.borrowing.some((item) => item.type === "ref<ByteBuf>" && item.borrowKind === "borrow" && item.target), "borrowed ByteBuf fact");
  assert(stdFs.semantics.resources.some((item) => item.kind === "capabilityUse" && item.qualifiedName === "std.fs.readAllOrRaise" && item.capability === "fs"), "std fs resource-use fact");
  assert(stdFs.semantics.targetRequirements.some((item) => item.qualifiedName === "std.fs.readAllOrRaise" && item.capability === "fs" && item.targetSupport === "host"), "std fs target requirement fact");
  assert(stdFs.semantics.repairs.some((item) => item.qualifiedName === "std.fs.readAllOrRaise" && item.requiresCheck === false), "std fs top-level repair fact");

  const cImport = await zeroJson(["dump", "--json", "conformance/native/pass/c-import-alias-later-local.0"]);
  const cCall = findSemanticCall(cImport, (item) => item.qualifiedName === "c.zero_c_add", "C import semantic call fact");
  assert.equal(cCall.contract.kind, "cAbi", "C import contract kind");
  assert.equal(cCall.contract.capability, "c-abi", "C import capability");
  assert.equal(cCall.contract.targetSupport, "host-c-abi", "C import target support");
  assert.match(cCall.contract.targetNode, /^#cimp_/, "C import contract target node");
  assert.match(cCall.contract.symbolId, /^symbol:c-import-alias-later-local::c-import\.c$/, "C import contract symbol");
  assertSemanticCallResolutionMatches(cImport, cCall, "C import semantic resolution");
  assert.equal(cCall.resolution.targetKind, "cFunction", "C import semantic resolution kind");
  assert.equal(cCall.returnType, "i32", "C import return type");
  assert.deepEqual(cCall.args.map((item) => item.type), ["i32", "i32"], "C import actual arg types");
  assert(cImport.semantics.targetRequirements.some((item) => item.qualifiedName === "c.zero_c_add" && item.capability === "c-abi" && item.targetSupport === "host-c-abi"), "C import target requirement fact");
  assert(cImport.semantics.resources.some((item) => item.kind === "capabilityUse" && item.qualifiedName === "c.zero_c_add" && item.resourceKind === "c-abi"), "C import resource fact");

  const borrowGraph = await zeroJson(["dump", "--json", "conformance/native/pass/borrow-return-explicit-ref-field-origin.0"]);
  assert(borrowGraph.semantics.ownership.some((item) => item.name === "current" && item.ownership === "borrow"), "borrowed local ownership fact");
  assert(borrowGraph.semantics.borrowing.some((item) => item.borrowKind === "mut-borrow" && item.mutable === true && item.target), "mutable borrow target fact");
  assert.equal(borrowGraph.semantics.resources.length, 0, "borrow-only fixture should not invent resource facts");

  const userResourceNameFixture = `${outDir}/semantic-user-resource-names.0`;
  await writeFile(userResourceNameFixture, [
    "type File {",
    "    fd: i32,",
    "}",
    "",
    "type UserFileRecord {",
    "    value: i32,",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let record: UserFileRecord = UserFileRecord { value: 1 }",
    "    let file: File = File { fd: record.value }",
    "    let maybe_file: Maybe<File> = null",
    "    expect file.fd == 1",
    "}",
    "",
  ].join("\n"));
  await importProjectionSidecar(userResourceNameFixture);
  const userResourceNames = await zeroJson(["dump", "--json", userResourceNameFixture]);
  assert.equal(userResourceNames.semantics.resources.length, 0, "user-defined type names containing resource words should not emit resource facts");
  assert(!userResourceNames.semantics.ownership.some((item) => item.type === "File" || item.type === "UserFileRecord" || item.type === "Maybe<File>"), "user-defined type names containing resource words should not emit ownership resource facts");

  const shadowedStdResourceFixture = `${outDir}/semantic-shadowed-stdlib-resource.0`;
  await writeFile(shadowedStdResourceFixture, [
    "type File {",
    "    fd: i32,",
    "}",
    "",
    "pub fn main() -> Void raises [NotFound, TooLarge, Io] {",
    "    let fs: Fs = std.fs.host()",
    "    let owned_file: owned<File> = check std.fs.createOrRaise(fs, \".zero/semantic-shadowed-stdlib-resource.txt\")",
    "    let user_file: File = File { fd: 1 }",
    "    expect user_file.fd == 1",
    "}",
    "",
  ].join("\n"));
  await importProjectionSidecar(shadowedStdResourceFixture);
  const shadowedStdResource = await zeroJson(["dump", "--json", shadowedStdResourceFixture]);
  assert(shadowedStdResource.semantics.ownership.some((item) => item.name === "owned_file" && item.type === "owned<File>" && item.ownership === "owned" && item.resource === true), "stdlib File resource facts should survive a user-defined File type");
  assert(!shadowedStdResource.semantics.ownership.some((item) => item.name === "user_file"), "user-defined File binding should not become an ownership fact");
  assert(shadowedStdResource.semantics.resources.some((item) => item.kind === "binding" && item.type === "owned<File>" && item.resourceKind === "file"), "stdlib File resource binding should survive a user-defined File type");

  const stdIoLines = await zeroJson(["dump", "--json", "conformance/native/pass/std-io-lines.graph"]);
  assert(stdIoLines.semantics.ownership.some((item) => item.name === "line_reader" && item.type === "FixedReader" && item.ownership === "resource-handle" && item.resource === true), "FixedReader bindings should remain resource handles despite stdlib shape declarations");
  assert(stdIoLines.semantics.ownership.some((item) => item.name === "writer" && item.type === "FixedWriter" && item.ownership === "resource-handle" && item.resource === true), "FixedWriter bindings should remain resource handles despite stdlib shape declarations");
  assert(stdIoLines.semantics.ownership.some((item) => item.type === "mutref<FixedReader>" && item.ownership === "mut-borrow" && item.resource === true), "FixedReader borrows should preserve resource facts");
  assert(stdIoLines.semantics.ownership.some((item) => item.type === "mutref<FixedWriter>" && item.ownership === "mut-borrow" && item.resource === true), "FixedWriter borrows should preserve resource facts");
  assert(stdIoLines.semantics.resources.some((item) => item.kind === "binding" && item.type === "FixedReader" && item.resourceKind === "resource"), "FixedReader bindings should emit resource facts");
  assert(stdIoLines.semantics.resources.some((item) => item.kind === "binding" && item.type === "FixedWriter" && item.resourceKind === "resource"), "FixedWriter bindings should emit resource facts");

  const searchSort = await zeroJson(["dump", "--json", "conformance/native/pass/std-search-sort-widths.0"]);
  assert(!searchSort.semantics.resources.some((item) => item.qualifiedName === "std.search.binaryU32" || item.qualifiedName === "std.search.binaryUsize"), "no-allocation search helpers should not emit resource facts");

  const callResolution = await zeroJson(["dump", "--json", "conformance/check/pass/call-resolution-inspection.0"]);
  const resolverCallReferences = callResolution.resolution.references.filter((item) => item.kind === "call");
  assert.equal(callResolution.semantics.calls.length, resolverCallReferences.length, "semantic calls should mirror resolver call references and skip operators");
  for (const call of callResolution.semantics.calls) assertSemanticCallResolutionMatches(callResolution, call, `call resolution semantic fact ${call.qualifiedName}`);
  const receiverCall = findSemanticCall(callResolution, (item) => item.qualifiedName === "counter.checkedRead", "receiver method semantic call fact");
  assert.equal(receiverCall.args.length, 0, "receiver method source-visible args");
  assert.equal(receiverCall.contract.expectedArgCount, 0, "receiver method expected arg count should exclude implicit self");
  assert.equal(receiverCall.receiver?.implicit, true, "receiver method should expose implicit receiver");
  assert.equal(receiverCall.receiver?.type, "Counter", "receiver method receiver type");
  const staticRead = findSemanticCall(callResolution, (item) => item.qualifiedName === "Counter.read", "static method call semantic fact");
  assert.equal(staticRead.receiver, null, "static method namespace calls should not report implicit receivers");
  assert.equal(staticRead.args.length, 1, "static method call source-visible args");
  assert.equal(staticRead.contract.expectedArgCount, 1, "static method expected arg count should keep explicit self arg");
  const interfaceRead = findSemanticCall(callResolution, (item) => item.qualifiedName === "T.read", "interface method semantic call fact");
  assert.equal(interfaceRead.resolution.targetKind, "interfaceMethod", "interface method semantic target kind");
  assert.equal(interfaceRead.resolution.symbolId, "symbol:call-resolution-inspection::type.Readable/method.read", "interface method semantic symbol");
  const eventKey = findSemanticCall(callResolution, (item) => item.qualifiedName === "Event.key", "choice variant semantic call fact");
  assert.equal(eventKey.resolution.targetKind, "variant", "choice variant semantic target kind");
  assert.equal(eventKey.resolution.symbolId, "symbol:call-resolution-inspection::type.Event/variant.key", "choice variant semantic symbol");

}

async function assertUnconstrainedGenericTypeParams() {
  const fixture = `${outDir}/generic-no-constraint.0`;
  await writeFile(fixture, [
    "type Box<T> {",
    "    value: T,",
    "}",
    "",
    "fn id<T>(value: T) -> T {",
    "    return value",
    "}",
    "",
    "pub fn main(world: World) -> Void raises {",
    "    let box: Box<i32> = Box { value: id<i32>(1) }",
    "    if box.value == 1 {",
    "        check world.out.write(\"generic no constraint ok\\n\")",
    "    }",
    "}",
    "",
  ].join("\n"));
  const fixtureGraph = await importProjectionSidecar(fixture);
  const check = await zeroJson(["check", "--json", fixtureGraph]);
  assert.equal(check.ok, true, "source check should accept unconstrained generic parameters");
  const dump = await zeroJson(["dump", "--json", fixture]);
  assert.equal(dump.validation.ok, true, "graph dump should validate unconstrained generic parameters");
  assert(dump.nodes.some((node) => node.kind === "Param" && node.name === "T" && node.type === ""), "graph dump should preserve an unconstrained type parameter");
  const artifact = await dumpGraphArtifact(fixture, "generic-no-constraint");
  const validate = await zeroJson(["validate", "--json", artifact]);
  assert.equal(validate.ok, true, "graph validate should accept unconstrained generic parameter artifacts");
}

async function assertBuildParity(fixture, name) {
  const sourceInput = await graphCompilerInputForFixture(fixture);
  const artifact = await dumpGraphArtifact(fixture, `${name}-build-input`);
  const sourceOut = `${outDir}/${name}.source-build`;
  const graphOut = `${outDir}/${name}.graph-build`;
  const source = await zeroJson(["build", "--json", "--target", "linux-musl-x64", "--out", sourceOut, sourceInput]);
  const graph = await zeroJson(["build", "--json", "--target", "linux-musl-x64", "--out", graphOut, artifact]);

  assert.equal(graph.graph.artifact, artifact, `${fixture}: graph build artifact`);
  assert.equal(graph.graph.canonicalSource, false, `${fixture}: graph build should use artifact input`);
  assert.equal(graph.graph.lowering, "mapped-final-mir", `${fixture}: graph build lowering`);
  assert.deepEqual(buildSummary(graph), buildSummary(source), `${fixture}: source and graph build summaries should agree`);
  assert(source.artifactBytes > 0, `${fixture}: source build should write an artifact`);
  assert(graph.artifactBytes > 0, `${fixture}: graph build should write an artifact`);
}

async function assertRunParity(fixture, name, args = []) {
  const sourceInput = await graphCompilerInputForFixture(fixture);
  const artifact = await dumpGraphArtifact(fixture, `${name}-run-input`);
  const sourceOut = `${outDir}/${name}.source-run`;
  const graphOut = `${outDir}/${name}.graph-run`;
  const source = await zeroText(["run", "--out", sourceOut, sourceInput, "--", ...args]);
  const graph = await zeroText(["run", "--out", graphOut, artifact, "--", ...args]);

  assert.equal(graph, source, `${fixture}: source and graph run output should agree`);
}

async function assertTestParity(fixture, name) {
  const sourceInput = await graphCompilerInputForFixture(fixture);
  const artifact = await dumpGraphArtifact(fixture, `${name}-test-input`);
  const source = await zeroJson(["test", "--json", sourceInput]);
  const graph = await zeroJson(["test", "--json", artifact]);

  assert.equal(graph.graph.artifact, artifact, `${fixture}: graph test artifact`);
  assert.equal(graph.graph.canonicalSource, false, `${fixture}: graph test should use artifact input`);
  assert.equal(graph.graph.lowering, "direct-program-graph", `${fixture}: graph test lowering`);
  assertGraphCompilerRoute(source, sourceInput, "direct-program-graph");
  assert.equal(source.testBackend, "direct-program-graph", `${fixture}: source test backend`);
  assert.equal(graph.testBackend, "direct-program-graph", `${fixture}: graph test backend`);
  assert.deepEqual(testSummary(graph), testSummary(source), `${fixture}: source and graph test summaries should agree`);
}

async function assertGraphCommandCompilerPath() {
  const helloCheck = await zeroJson(["check", "--json", "examples/hello.graph"]);
  assertGraphCompilerRoute(helloCheck, "examples/hello.graph");
  if (helloCheck.compilerCaches) assert.equal(helloCheck.compilerCaches[0].sourceKind, "program-graph", "graph check should use graph cache identity");
  assert.deepEqual(targetReadinessSummary(helloCheck.targetReadiness), {
    languageOk: true,
    buildable: true,
    stage: "ready",
    code: null,
  }, "graph check target readiness");

  const stdPathCheck = await zeroJson(["check", "--json", "std/path.graph"]);
  assertGraphCompilerRoute(stdPathCheck, "std/path.graph", "typed-program-graph-mir");
  assert.equal(stdPathCheck.ok, true, "stdlib graph check should preserve library entrypoint rules");
  if (stdPathCheck.compilerCaches) assert.equal(stdPathCheck.compilerCaches[0].sourceKind, "program-graph", "stdlib graph check should use graph cache identity");

  const helloBuildOut = `${outDir}/graph-command-build`;
  const helloBuild = await zeroJson(["build", "--json", "--target", "linux-musl-x64", "--out", helloBuildOut, "examples/hello.graph"]);
  assertGraphCompilerRoute(helloBuild, "examples/hello.graph", "mapped-final-mir");
  assert.equal(helloBuild.generatedCBytes, 0, "graph build should stay on direct backend");
  assert.equal(helloBuild.compilerCaches[0].sourceKind, "program-graph", "graph build should use graph cache identity");
  assert.equal(helloBuild.incrementalInvalidation.sourceKind, "program-graph", "graph build invalidation source kind");
  assert.equal(helloBuild.incrementalInvalidation.graphInput.artifact, "examples/hello.graph", "graph build graph input");
  assert(helloBuild.artifactBytes > 0, "graph build should write an artifact");

  const helloSize = await zeroJson(["size", "--json", "--target", "linux-musl-x64", "examples/hello.graph"]);
  assertGraphCompilerRoute(helloSize, "examples/hello.graph", "mapped-final-mir");
  assert.equal(helloSize.generatedCBytes, 0, "graph size should stay on direct backend");
  assert.equal(helloSize.cBridgeFallback, false, "graph size should not use C bridge fallback");
  assert.equal(helloSize.compilerCaches[0].sourceKind, "program-graph", "graph size should use graph cache identity");

  const helloArtifact = await dumpGraphArtifact("examples/hello.0", "source-command-cache-key");
  const helloGraphSize = await zeroJson(["size", "--json", "--target", "linux-musl-x64", helloArtifact]);
  assert.equal(compilerCacheKey(helloSize, "parseTree"), compilerCacheKey(helloGraphSize, "parseTree"), "graph size and graph artifact size should share graph parse cache key");
  assert.equal(compilerCacheKey(helloSize, "checkedBody"), compilerCacheKey(helloGraphSize, "checkedBody"), "graph size and graph artifact size should share graph check cache key");

  const helloMem = await zeroJson(["mem", "--json", "examples/hello.graph"]);
  assertGraphCompilerRoute(helloMem, "examples/hello.graph", "mapped-final-mir");
  assert.equal(helloMem.compilerCaches[0].sourceKind, "program-graph", "graph mem should use graph cache identity");
  assert.equal(helloMem.incrementalInvalidation.sourceKind, "program-graph", "graph mem invalidation source kind");

  const packageCheck = await zeroJson(["check", "--json", "conformance/packages/test-app"]);
  assert(packageCheck.graph, "package command should report graph compiler input");
  assert.equal(packageCheck.graph.artifact, "conformance/packages/test-app/zero.graph", "package should route through repository graph store");
  assert.equal(packageCheck.graph.canonicalSource, false, "package graph should not report canonical source");
  assert.match(packageCheck.graph.graphHash, /^graph:[0-9a-f]{16}$/, "package graph hash");
  assert.equal(packageCheck.graph.lowering, "graph-native-check", "package graph lowering");
  assert.equal(packageCheck.graph.moduleIdentity, "package:test-app@0.1.0", "package graph identity");
  assert.equal(packageCheck.package.name, "test-app", "package metadata");

  const packageTest = await zeroJson(["test", "--json", "conformance/packages/test-app"]);
  assert(packageTest.graph, "package test should report graph compiler input");
  assert.equal(packageTest.graph.artifact, "conformance/packages/test-app/zero.graph", "package test should route through repository graph store");
  assert.equal(packageTest.graph.canonicalSource, false, "package test graph should not report canonical source");
  assert.match(packageTest.graph.graphHash, /^graph:[0-9a-f]{16}$/, "package test graph hash");
  assert.equal(packageTest.graph.lowering, "direct-program-graph", "package test graph lowering");
  assert.equal(packageTest.testBackend, "direct-program-graph", "graph test backend");
  assert.equal(packageTest.testDiscovery.mode, "package-graph", "package tests should report graph discovery");
  assert.equal(packageTest.generatedCBytes, 0, "source graph tests should not use generated C");
  assert.equal(packageTest.cBridgeFallback, false, "source graph tests should not use C bridge fallback");
}

async function assertPatchRecomputesNestedSymbolOwners() {
  const graphPath = `${outDir}/reordered-owner.program-graph`;
  const patchPath = `${outDir}/reordered-owner.patch`;
  const patchedPath = `${outDir}/reordered-owner.patched.program-graph`;
  const baseNode = {
    value: "",
    path: `${outDir}/reordered-owner.0`,
    line: 1,
    column: 1,
    public: false,
    mutable: false,
    static: false,
    fallible: false,
    exportC: false,
  };
  const graph = finalizeGraphIdentities({
    schemaVersion: 1,
    moduleIdentity: "module:hello",
    nodes: [
      { ...baseNode, id: "#param_p", kind: "Param", name: "world", type: "World", column: 13 },
      { ...baseNode, id: "#type_w", kind: "TypeRef", name: "", type: "World", column: 20 },
      { ...baseNode, id: "#type_v", kind: "TypeRef", name: "", type: "Void", column: 30 },
      { ...baseNode, id: "#block_b", kind: "Block", name: "body", type: "" },
      { ...baseNode, id: "#mod_m", kind: "Module", name: "hello", type: "" },
      { ...baseNode, id: "#decl_f", kind: "Function", name: "main", type: "Void", public: true },
    ],
    edges: [
      { from: "#decl_f", kind: "param", to: "#param_p", target: "node", order: 0 },
      { from: "#param_p", kind: "type", to: "#type_w", target: "node", order: 0 },
      { from: "#decl_f", kind: "returnType", to: "#type_v", target: "node", order: 0 },
      { from: "#decl_f", kind: "body", to: "#block_b", target: "node", order: 0 },
      { from: "#mod_m", kind: "function", to: "#decl_f", target: "node", order: 0 },
    ],
  });
  await writeFile(graphPath, graphDumpText(graph));
  const before = await zeroJson(["source-map", "--json", graphPath]);
  const beforeParam = before.mappings.find((item) => item.nodeId === "#param_p");
  assert.equal(beforeParam?.symbolId, "symbol:hello::value.main/param.world", "reordered graph starts with nested owner symbol");
  await writeFile(patchPath, [
    "zero-program-graph-patch v1",
    `expect graphHash "${graph.graphHash}"`,
    'set node="#decl_f" field="name" expect="main" value="entry"',
    "",
  ].join("\n"));

  const patch = await zeroJson(["patch", "--json", "--out", patchedPath, graphPath, patchPath]);
  assert.equal(patch.ok, true, "patch should save a reordered graph after owner rename");
  const validate = await zeroJson(["validate", "--json", patchedPath]);
  assert.equal(validate.ok, true, "patched reordered graph should read back with matching identities");
  const after = await zeroJson(["source-map", "--json", patchedPath]);
  const afterParam = after.mappings.find((item) => item.nodeId === "#param_p");
  assert.equal(afterParam?.symbolId, "symbol:hello::value.entry/param.world", "nested symbol owner should be recomputed after owner rename");
}

async function assertGraphSidecarPatchParity() {
  const fixture = `${outDir}/graph-sidecar-patch.0`;
  const original = await readFile("examples/hello.0", "utf8");
  await writeFile(fixture, original);
  const sidecar = await importProjectionSidecar(fixture);

  const beforeGraph = await zeroJson(["dump", "--json", fixture]);
  const beforeSidecar = await zeroJson(["validate", "--json", sidecar]);
  assert.equal(beforeGraph.canonicalSource, false, "projection handle should read the graph sidecar for graph queries");
  assert.equal(beforeGraph.graphHash, beforeSidecar.graphHash, "projection handle should match the graph sidecar");
  assert.equal(beforeGraph.validation.state, "shape-valid", "sidecar graph should be shape-valid");
  const literal = findStringLiteral(beforeGraph, "hello from zero\n");

  const patch = await zeroJson([
    "patch",
    "--json",
    "--out",
    sidecar,
    sidecar,
    "--expect-graph-hash",
    beforeGraph.graphHash,
    "--op",
    `set node="${literal.id}" field="value" expect="hello from zero\\n" value="hello sidecar graph\\n"`,
  ]);
  assert.equal(patch.ok, true, "graph sidecar patch should succeed");
  assert.equal(patch.canonicalSource, false, "graph sidecar patch should report graph input");
  assert.equal(patch.saved.path, sidecar, "graph sidecar patch should save to the graph sidecar");
  assert.equal(patch.originalGraphHash, beforeGraph.graphHash, "graph sidecar patch should check the expected graph hash");
  assert.match(patch.patchedGraphHash, /^graph:[0-9a-f]{16}$/, "graph sidecar patch should report a graph hash");
  assert.notEqual(patch.patchedGraphHash, beforeGraph.graphHash, "graph sidecar patch should change graph hash");
  assert.equal(patch.operationCount, 1, "graph sidecar patch should report one operation");
  assert.equal(patch.operations[0].ok, true, "graph sidecar patch operation should pass");
  assert.equal(patch.operations[0].node, literal.id, "graph sidecar patch should target the requested node");

  await zeroText(["view", "--out", fixture, sidecar]);
  const patchedSource = await readFile(fixture, "utf8");
  assert.match(patchedSource, /hello sidecar graph\\n/, "graph sidecar patch should export updated source text");
  assert.equal(await zeroText(["check", sidecar]), "ok\n", "patched sidecar should check through first-class check command");

  const artifact = await dumpGraphArtifact(fixture, "graph-sidecar-patch-run");
  const sourceOut = `${outDir}/graph-sidecar-patch.source-run`;
  const graphOut = `${outDir}/graph-sidecar-patch.graph-run`;
  const source = await zeroText(["run", "--out", sourceOut, sidecar]);
  const graph = await zeroText(["run", "--out", graphOut, artifact]);
  assert.equal(source, "hello sidecar graph\n", "patched projection handle run output");
  assert.equal(graph, source, "patched graph artifact run output should match source");
}

async function assertGraphPatchPreservesNodeIds() {
  const artifact = await dumpGraphArtifact("examples/hello.0", "patch-preserves-id");
  const patchedArtifact = `${outDir}/patch-preserves-id.patched.program-graph`;
  const beforeGraph = await zeroJson(["dump", "--json", "examples/hello.0"]);
  const beforeLiteral = findStringLiteral(beforeGraph, "hello from zero\n");
  const beforeMain = beforeGraph.nodes.find((node) => node.kind === "Function" && node.name === "main");
  assert(beforeMain, "missing main function");

  const patch = await zeroJson([
    "patch",
    "--json",
    "--out",
    patchedArtifact,
    artifact,
    "--expect-graph-hash",
    beforeGraph.graphHash,
    "--op",
    `replace node="${beforeLiteral.id}" expect="${beforeLiteral.nodeHash}" kind="Literal" type="String" value="hello preserved\\n"`,
  ]);
  assert.equal(patch.ok, true, "graph artifact patch should succeed");
  assert.equal(patch.operations[0].node, beforeLiteral.id, "graph patch should target the inspected literal ID");
  assert.notEqual(patch.patchedGraphHash, beforeGraph.graphHash, "graph patch should update graphHash");

  const patchedText = await readFile(patchedArtifact, "utf8");
  assert.match(patchedText, new RegExp(`node ${beforeLiteral.id.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")} Literal[^\\n]*value:"hello preserved\\\\n"`), "graph patch should preserve literal node ID");
  assert.match(patchedText, new RegExp(`node ${beforeMain.id.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")} Function[^\\n]*name:"main"`), "graph patch should not churn unrelated function ID");
  assert.equal(await zeroText(["check", patchedArtifact]), "ok\n", "patched graph artifact should check");
}

function findStringLiteral(graph, value) {
  const literal = graph.nodes.find((node) => node.kind === "Literal" && node.type === "String" && node.value === value);
  assert(literal, `missing string literal ${JSON.stringify(value)}`);
  return literal;
}

function findNodeById(graph, id) {
  const node = graph.nodes.find((item) => item.id === id);
  assert(node, `missing graph node ${id}`);
  return node;
}

function assertMissingNodeId(graph, id, message) {
  assert(!graph.nodes.some((item) => item.id === id), message);
}

function findOwnerNode(graph, nodeId, edgeKind) {
  const edge = graph.edges.find((item) => item.target === "node" && item.to === nodeId && item.kind === edgeKind);
  assert(edge, `missing ${edgeKind} owner edge for ${nodeId}`);
  return findNodeById(graph, edge.from);
}

function findCheckForStringLiteral(graph, value) {
  const literal = findStringLiteral(graph, value);
  const call = findOwnerNode(graph, literal.id, "arg");
  const check = findOwnerNode(graph, call.id, "expr");
  assert.equal(check.kind, "Check", `expected check owner for ${JSON.stringify(value)}`);
  return { check, literal };
}

function findNodeByKindAndName(graph, kind, name) {
  const node = graph.nodes.find((item) => item.kind === kind && item.name === name);
  assert(node, `missing ${kind} node ${name}`);
  return node;
}

async function assertDeclarationSiblingIdentity() {
  const declarationFixture = `${outDir}/identity-declarations.0`;
  const declarations = [
    "type Point {",
    "    x: i32,",
    "}",
    "",
    "type Other {",
    "    y: i32,",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let point: Point = Point { x: 1 }",
    "    expect point.x == 1",
    "}",
    "",
  ].join("\n");
  await writeFile(declarationFixture, declarations);
  await importProjectionSidecar(declarationFixture);
  const beforeDeclarations = await zeroJson(["dump", "--json", declarationFixture]);
  const beforePoint = findNodeByKindAndName(beforeDeclarations, "Shape", "Point");
  const beforeOther = findNodeByKindAndName(beforeDeclarations, "Shape", "Other");

  await writeFile(declarationFixture, declarations.replace("type Point", "type Added {\n    z: i32,\n}\n\ntype Point"));
  await importProjectionSidecar(declarationFixture);
  const prependedDeclarations = await zeroJson(["dump", "--json", declarationFixture]);
  assert.equal(findNodeByKindAndName(prependedDeclarations, "Shape", "Point").id, beforePoint.id, "prepending a declaration should not churn existing shape IDs");
  assert.equal(findNodeByKindAndName(prependedDeclarations, "Shape", "Other").id, beforeOther.id, "prepending a declaration should not churn later shape IDs");

  const methodFixture = `${outDir}/identity-methods.0`;
  const methods = [
    "type Counter {",
    "    value: i32,",
    "    fn read(self: ref<Self>) -> i32 {",
    "        return self.value",
    "    }",
    "    fn done(self: ref<Self>) -> Bool {",
    "        return true",
    "    }",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let counter: Counter = Counter { value: 42 }",
    "    expect Counter.read(&counter) == 42",
    "}",
    "",
  ].join("\n");
  await writeFile(methodFixture, methods);
  await importProjectionSidecar(methodFixture);
  const beforeMethods = await zeroJson(["dump", "--json", methodFixture]);
  const beforeRead = findNodeByKindAndName(beforeMethods, "Function", "read");
  const beforeDone = findNodeByKindAndName(beforeMethods, "Function", "done");

  await writeFile(methodFixture, methods.replace("    fn read", "    fn zero(self: ref<Self>) -> u32 {\n        return 0_u32\n    }\n    fn read"));
  await importProjectionSidecar(methodFixture);
  const prependedMethods = await zeroJson(["dump", "--json", methodFixture]);
  assert.equal(findNodeByKindAndName(prependedMethods, "Function", "read").id, beforeRead.id, "prepending a distinct method should not churn existing method IDs");
  assert.equal(findNodeByKindAndName(prependedMethods, "Function", "done").id, beforeDone.id, "prepending a distinct method should not churn later method IDs");

  await writeFile(methodFixture, methods.replace("    fn read", "    fn count(self: ref<Self>) -> i32 {\n        return 1\n    }\n    fn read"));
  await importProjectionSidecar(methodFixture);
  const sameShapeMethods = await zeroJson(["dump", "--json", methodFixture]);
  const sameShapeRead = findNodeByKindAndName(sameShapeMethods, "Function", "read");
  const countMethod = findNodeByKindAndName(sameShapeMethods, "Function", "count");
  assert.notEqual(sameShapeRead.id, beforeRead.id, "same-shape method collision should retire the old ambiguous method ID");
  assert.notEqual(countMethod.id, beforeRead.id, "prepended same-shape method should not steal the old method ID");
  assertMissingNodeId(sameShapeMethods, beforeRead.id, "old ambiguous method ID should not target any same-shape method");
}

async function assertSourceEditIdentityBaseline() {
  const fixture = `${outDir}/identity-edit.0`;
  const original = [
    "fn helper() -> i32 {",
    "    return 1",
    "}",
    "",
    "pub fn main(world: World) -> Void raises {",
    "    check world.out.write(\"hello from zero\\n\")",
    "}",
    "",
  ].join("\n");
  await writeFile(fixture, original);
  await importProjectionSidecar(fixture);

  const beforeGraph = await zeroJson(["dump", "--json", fixture]);
  const before = findStringLiteral(beforeGraph, "hello from zero\n");
  const beforeHelper = beforeGraph.nodes.find((node) => node.kind === "Function" && node.name === "helper");
  const beforeCheck = beforeGraph.nodes.find((node) => node.kind === "Check");
  assert(beforeHelper, "missing helper function before rename");
  assert(beforeCheck, "missing check statement before insertion");

  await writeFile(fixture, original.replace("hello from zero\\n", "hello from graph\\n"));
  await importProjectionSidecar(fixture);
  const afterGraph = await zeroJson(["dump", "--json", fixture]);
  const after = findStringLiteral(afterGraph, "hello from graph\n");

  assert.notEqual(after.nodeHash, before.nodeHash, "editing literal content should change nodeHash");
  assert.notEqual(afterGraph.graphHash, beforeGraph.graphHash, "editing literal content should change graphHash");
  assert.equal(after.id, before.id, "source-imported node id should survive a local content edit");

  await writeFile(fixture, original.replace("fn helper", "fn renamedHelper"));
  await importProjectionSidecar(fixture);
  const renamedGraph = await zeroJson(["dump", "--json", fixture]);
  const renamedHelper = renamedGraph.nodes.find((node) => node.kind === "Function" && node.name === "renamedHelper");
  assert(renamedHelper, "missing renamed function");
  assert.equal(renamedHelper.id, beforeHelper.id, "source-imported declaration id should survive a rename");
  assert.notEqual(renamedHelper.nodeHash, beforeHelper.nodeHash, "renaming a declaration should change nodeHash");
  assert.notEqual(renamedHelper.symbolId, beforeHelper.symbolId, "renaming a declaration should change symbolId");
  if (requireStableNodeIds) assert.equal(renamedHelper.id, beforeHelper.id, "strict stable node id check");

  await writeFile(fixture, original.replace("\npub fn main", "\nfn appendedHelper() -> i32 {\n    return 2\n}\n\npub fn main"));
  await importProjectionSidecar(fixture);
  const sameShapeSiblingGraph = await zeroJson(["dump", "--json", fixture]);
  const sameShapeHelper = sameShapeSiblingGraph.nodes.find((node) => node.kind === "Function" && node.name === "helper");
  const insertedHelper = sameShapeSiblingGraph.nodes.find((node) => node.kind === "Function" && node.name === "appendedHelper");
  assert(sameShapeHelper, "missing helper after same-shape sibling insertion");
  assert(insertedHelper, "missing inserted same-shape sibling");
  assert.notEqual(sameShapeHelper.id, beforeHelper.id, "same-shape sibling collision should retire the old ambiguous declaration ID");
  assert.notEqual(insertedHelper.id, beforeHelper.id, "same-shape sibling should not steal the old declaration ID");
  assertMissingNodeId(sameShapeSiblingGraph, beforeHelper.id, "old ambiguous declaration ID should not target any same-shape sibling");

  await writeFile(fixture, original.replace("fn helper", "fn prependedHelper() -> i32 {\n    return 2\n}\n\nfn helper"));
  await importProjectionSidecar(fixture);
  const prependedSiblingGraph = await zeroJson(["dump", "--json", fixture]);
  const prependedExistingHelper = prependedSiblingGraph.nodes.find((node) => node.kind === "Function" && node.name === "helper");
  const prependedHelper = prependedSiblingGraph.nodes.find((node) => node.kind === "Function" && node.name === "prependedHelper");
  assert(prependedExistingHelper, "missing helper after prepended same-shape sibling");
  assert(prependedHelper, "missing prepended same-shape sibling");
  assert.notEqual(prependedExistingHelper.id, beforeHelper.id, "prepending a same-shape sibling should retire the old ambiguous declaration ID");
  assert.notEqual(prependedHelper.id, beforeHelper.id, "prepended same-shape sibling should not steal the old declaration ID");
  assertMissingNodeId(prependedSiblingGraph, beforeHelper.id, "old ambiguous declaration ID should not target any prepended sibling");

  await writeFile(fixture, original.replace("    check world.out.write", "    let marker: i32 = 1\n    check world.out.write"));
  await importProjectionSidecar(fixture);
  const insertedGraph = await zeroJson(["dump", "--json", fixture]);
  const insertedCheck = insertedGraph.nodes.find((node) => node.kind === "Check");
  const insertedLiteral = findStringLiteral(insertedGraph, "hello from zero\n");
  assert.equal(insertedCheck?.id, beforeCheck.id, "inserting before a statement should not churn the existing statement ID");
  assert.equal(insertedLiteral.id, before.id, "inserting before a statement should not churn nested expression IDs");

  await writeFile(fixture, original.replace("    check world.out.write(\"hello from zero\\n\")", "    check world.out.write(\"hello from zero\\n\")\n    check world.out.write(\"inserted\\n\")"));
  await importProjectionSidecar(fixture);
  const sameKindStatementGraph = await zeroJson(["dump", "--json", fixture]);
  const sameKindExisting = findCheckForStringLiteral(sameKindStatementGraph, "hello from zero\n");
  const sameKindInserted = findCheckForStringLiteral(sameKindStatementGraph, "inserted\n");
  assert.notEqual(sameKindExisting.check.id, beforeCheck.id, "same-kind statement collision should retire the old ambiguous statement ID");
  assert.notEqual(sameKindExisting.literal.id, before.id, "same-kind statement collision should retire nested ambiguous expression IDs");
  assert.notEqual(sameKindInserted.check.id, beforeCheck.id, "appended same-kind statement should not steal the old statement ID");
  assert.notEqual(sameKindInserted.literal.id, before.id, "appended same-kind expression should not steal the old expression ID");
  assertMissingNodeId(sameKindStatementGraph, beforeCheck.id, "old ambiguous statement ID should not target any same-kind statement");
  assertMissingNodeId(sameKindStatementGraph, before.id, "old ambiguous expression ID should not target any same-kind expression");

  await writeFile(fixture, original.replace("    check world.out.write", "    check world.out.write(\"inserted\\n\")\n    check world.out.write"));
  await importProjectionSidecar(fixture);
  const prependedStatementGraph = await zeroJson(["dump", "--json", fixture]);
  const prependedExisting = findCheckForStringLiteral(prependedStatementGraph, "hello from zero\n");
  const prependedInserted = findCheckForStringLiteral(prependedStatementGraph, "inserted\n");
  assert.notEqual(prependedExisting.check.id, beforeCheck.id, "prepending a same-kind statement should retire the old ambiguous statement ID");
  assert.notEqual(prependedExisting.literal.id, before.id, "prepending a same-kind statement should retire nested ambiguous expression IDs");
  assert.notEqual(prependedInserted.check.id, beforeCheck.id, "prepended same-kind statement should not steal the old statement ID");
  assert.notEqual(prependedInserted.literal.id, before.id, "prepended same-kind expression should not steal the old expression ID");
  assertMissingNodeId(prependedStatementGraph, beforeCheck.id, "old ambiguous statement ID should not target any prepended statement");
  assertMissingNodeId(prependedStatementGraph, before.id, "old ambiguous expression ID should not target any prepended expression");
}

async function assertSourceEditReconcile() {
  const fixture = `${outDir}/identity-reconcile.0`;
  const original = [
    "fn helper() -> i32 {",
    "    return 1",
    "}",
    "",
    "pub fn main(world: World) -> Void raises {",
    "    check world.out.write(\"hello from zero\\n\")",
    "}",
    "",
  ].join("\n");
  await writeFile(fixture, original);
  const baseArtifact = await dumpGraphArtifact(fixture, "identity-reconcile-base");

  await writeFile(fixture, original.replace("hello from zero\\n", "hello from graph\\n"));
  const edited = await zeroJson(["reconcile", "--json", baseArtifact, "--source", fixture]);
  assert.equal(edited.ok, true, "reconcile should accept an unambiguous literal edit");
  assert.equal(edited.identity.edited > 0, true, "reconcile should report edited nodes");
  assert.equal(edited.identity.ambiguous, 0, "literal edit should not be ambiguous");
  assert.equal(edited.graphPatch.available, true, "literal edit should produce a graph patch");
  assert.match(edited.graphPatch.text, /set node="#expr_[^"]+" field="value"/, "literal edit patch should target a graph node");
  const literalDecision = edited.decisions.find((decision) => decision.status === "edited" && decision.kind === "Literal");
  assert.deepEqual(literalDecision?.sourceRange.start, { line: 6, column: 27 }, "reconcile should map edited literal decisions to the source token");
  assert.deepEqual(literalDecision?.sourceRange.end, { line: 6, column: 47 }, "reconcile should include the full edited literal token");
  assert.equal(await zeroText(["reconcile", baseArtifact, "--source", fixture]), "program graph reconcile ok\n", "reconcile text output");

  await writeFile(fixture, original.replace("\npub fn main", "\nfn appendedHelper() -> i32 {\n    return 2\n}\n\npub fn main"));
  const appended = await zeroJson(["reconcile", "--json", baseArtifact, "--source", fixture]);
  assert.equal(appended.ok, true, "reconcile should accept unambiguous appended declarations");
  assert.equal(appended.identity.ambiguous, 0, "named appended declaration should not be ambiguous");
  assert.equal(appended.identity.inserted > 0, true, "reconcile should report inserted declaration nodes");
  assert(appended.decisions.some((decision) => decision.status === "unchanged" && decision.kind === "Function" && decision.name === "helper"), "existing helper declaration should keep its identity");
  assert(appended.decisions.some((decision) => decision.status === "inserted" && decision.kind === "Function" && decision.name === "appendedHelper"), "appended helper declaration should receive a new identity");

  const copiedFixture = `${outDir}/identity-reconcile-copy.0`;
  await writeFile(copiedFixture, original);
  const moduleMismatch = await zeroJsonFailure(["reconcile", "--json", baseArtifact, "--source", copiedFixture]);
  assert.equal(moduleMismatch.ok, false, "reconcile should reject different module identities");
  assert.equal(moduleMismatch.identity.moduleIdentityChanged, true, "reconcile should flag module identity changes");
  assert.equal(moduleMismatch.diagnostics[0].code, "GRC003", "reconcile should explain module identity mismatch");
}

try {
  await mkdir(outDir, { recursive: true });

  for (const fixture of [
    "examples/hello.0",
    "examples/type-alias.0",
    "examples/static-interface.0",
    "examples/direct-rescue-basic.0",
    "examples/std-math.0",
    "examples/systems-package",
    "examples/readall-cli",
    "examples/direct-package-arrays",
    "conformance/check/pass/c-header-import.0",
    "conformance/native/pass/borrow-field-independent-assignment.0",
    "conformance/native/pass/open-ended-slices.0",
    "conformance/native/pass/allocator-primitives.0",
    "conformance/native/pass/std-fs-fallible.0",
    "benchmarks/rosetta/fibonacci-sequence.0",
  ]) {
    await assertCheckParity(fixture);
  }

  for (const fixture of [
    "examples/hello.0",
    "examples/type-alias.0",
    "examples/static-interface.0",
    "examples/direct-rescue-basic.0",
    "examples/std-math.0",
    "examples/systems-package",
    "examples/readall-cli",
    "conformance/check/pass/c-header-import.0",
    "conformance/native/pass/borrow-field-independent-assignment.0",
    "conformance/native/pass/open-ended-slices.0",
    "conformance/native/pass/allocator-primitives.0",
    "conformance/native/pass/std-fs-fallible.0",
    "benchmarks/rosetta/fibonacci-sequence.0",
  ]) {
    await assertRoundtripStable(fixture);
  }

  await assertCommandStateContracts();
  await assertResolutionFacts();
  await assertStdGraphDependencyMerge();
  await assertSemanticFacts();
  await assertUnconstrainedGenericTypeParams();
  await assertGraphCommandCompilerPath();
  await assertPatchRecomputesNestedSymbolOwners();
  await assertBuildParity("examples/hello.0", "hello");
  await assertRunParity("examples/hello.0", "hello");
  await assertRunParity("conformance/native/pass/std-args.0", "std-args", ["alpha", "beta"]);
  await assertTestParity("conformance/native/pass/test-blocks.0", "test-blocks");
  await assertGraphSidecarPatchParity();
  await assertGraphPatchPreservesNodeIds();

  await assertSourceEditIdentityBaseline();
  await assertSourceEditReconcile();
  await assertDeclarationSiblingIdentity();
  console.log("program graph parity ok");
} finally {
  await rm(outDir, { force: true, recursive: true });
}
