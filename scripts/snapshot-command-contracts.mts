#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { join } from "node:path";

if (process.env.ZERO_NATIVE_TEST_SANDBOX !== "1" && process.env.ZERO_NATIVE_TEST_ALLOW_LOCAL !== "1") {
  console.error("command contract snapshots emit native test artifacts; run `pnpm run command-contracts` for Vercel Sandbox execution or set ZERO_NATIVE_TEST_ALLOW_LOCAL=1 to opt into local artifacts.");
  process.exit(1);
}

const outDir = ".zero/command-contracts";
mkdirSync(outDir, { recursive: true });

function zero(args, options: { allowFailure?: boolean } = {}) {
  try {
    const stdout = execFileSync("bin/zero", args, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
    return { code: 0, stdout };
  } catch (error) {
    if (!options.allowFailure) throw error;
    return {
      code: error.status ?? 1,
      stdout: error.stdout?.toString() ?? "",
      stderr: error.stderr?.toString() ?? "",
    };
  }
}

function json(args, options = {}) {
  const result = zero(args, options);
  return { ...result, body: JSON.parse(result.stdout) };
}

function sha256File(path) {
  return createHash("sha256").update(readFileSync(path)).digest("hex");
}

const graphHashPrime = 1099511628211n;
const graphHashMask = (1n << 64n) - 1n;

function graphHashText(hash, text) {
  for (const byte of Buffer.from(text ?? "")) {
    hash ^= BigInt(byte);
    hash = (hash * graphHashPrime) & graphHashMask;
  }
  hash ^= 0xffn;
  return (hash * graphHashPrime) & graphHashMask;
}

function graphHashU64(hash, value) {
  let item = BigInt(value);
  for (let i = 0n; i < 8n; i++) {
    hash ^= (item >> (i * 8n)) & 0xffn;
    hash = (hash * graphHashPrime) & graphHashMask;
  }
  return hash;
}

function graphQuotedField(line, field) {
  const match = line.match(new RegExp(`(?:^| )${field}:"((?:\\\\.|[^"])*)"`));
  return match ? JSON.parse(`"${match[1]}"`) : "";
}

function graphBareField(line, field, fallback = "") {
  const match = line.match(new RegExp(`(?:^| )${field}:([^ ]+)`));
  return match ? match[1] : fallback;
}

function graphTopLevelQuoted(line, field) {
  const match = line.match(new RegExp(`^${field} "([^"]*)"$`));
  return match ? match[1] : "";
}

function graphNodeHandle(line) {
  const match = line.match(/^node (#[A-Za-z0-9_.-]+)/);
  return match ? match[1] : "";
}

function graphNodeKind(line) {
  const match = line.match(/^node #[A-Za-z0-9_.-]+ ([A-Za-z_][A-Za-z0-9_-]*)/);
  return match ? match[1] : "";
}

function graphStoredModuleIdentity(text) {
  if (!text) return "module:main";
  if (text.startsWith("module:") || text.startsWith("package:")) return text;
  return `module:${text}`;
}

function graphEdgeRecord(line) {
  const match = line.match(/^edge (#[A-Za-z0-9_.-]+) ([A-Za-z_][A-Za-z0-9_-]*) ([^ ]+)/);
  return {
    line,
    from: match?.[1] ?? "",
    kind: match?.[2] ?? "",
    to: match?.[3] ?? "",
    target: graphBareField(line, "target", "node"),
    order: graphBareField(line, "order", "0"),
  };
}

function graphDomainId(domain, hash) {
  return `${domain}:${hash.toString(16).padStart(16, "0")}`;
}

function graphNodeDeclaresSymbol(node) {
  if (!node?.name) return false;
  return [
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
  ].includes(node.kind);
}

function graphNodeHash(line) {
  let hash = 1469598103934665603n;
  hash = graphHashText(hash, graphNodeKind(line));
  hash = graphHashText(hash, graphQuotedField(line, "name"));
  hash = graphHashText(hash, graphQuotedField(line, "type"));
  hash = graphHashText(hash, graphQuotedField(line, "value"));
  hash = graphHashU64(hash, graphBareField(line, "public", "false") === "true" ? 1n : 0n);
  hash = graphHashU64(hash, graphBareField(line, "mutable", "false") === "true" ? 1n : 0n);
  hash = graphHashU64(hash, graphBareField(line, "static", "false") === "true" ? 1n : 0n);
  hash = graphHashU64(hash, graphBareField(line, "fallible", "false") === "true" ? 1n : 0n);
  hash = graphHashU64(hash, graphBareField(line, "exportC", "false") === "true" ? 1n : 0n);
  return graphDomainId("nodehash", hash);
}

function graphSymbolComponent(text) {
  let out = "";
  for (const ch of text ?? "") out += /^[A-Za-z0-9._@/-]$/.test(ch) ? ch : "_";
  return out || "main";
}

function graphIdentityName(identity) {
  if (identity?.startsWith("module:")) return identity.slice("module:".length);
  if (identity?.startsWith("package:")) return identity.slice("package:".length);
  return identity || "main";
}

function graphModuleScopeName(moduleIdentity, moduleNode) {
  const moduleName = moduleNode?.name || graphIdentityName(moduleIdentity);
  if (moduleIdentity?.startsWith("package:")) return `${graphSymbolComponent(graphIdentityName(moduleIdentity))}/${graphSymbolComponent(moduleName)}`;
  return graphSymbolComponent(moduleName);
}

function graphImportBindingName(node) {
  if (node.value) return node.value;
  const parts = (node.name ?? "").split(".");
  return parts.at(-1) || node.name || "";
}

function graphSymbolName(node) {
  return node.kind === "Import" ? graphImportBindingName(node) : (node.name ?? "");
}

const graphOwnerKinds = new Set([
  "alias",
  "arg",
  "arm",
  "body",
  "cImport",
  "case",
  "choice",
  "const",
  "constraint",
  "declaredType",
  "default",
  "effect",
  "else",
  "enum",
  "error",
  "expr",
  "field",
  "function",
  "guard",
  "import",
  "interface",
  "left",
  "method",
  "param",
  "rangeEnd",
  "returnType",
  "right",
  "shape",
  "statement",
  "target",
  "then",
  "type",
  "typeArg",
  "typeParam",
  "value",
  "variant",
]);

function graphOwnerEdgeForNode(edges, nodeId) {
  return edges.find((edge) => edge.target === "node" && edge.to === nodeId && graphOwnerKinds.has(edge.kind)) ?? null;
}

function graphSymbolOwner(nodesById, edges, node) {
  let current = node?.id ?? "";
  for (let depth = 0; current && depth < nodesById.size; depth++) {
    const ownerEdge = graphOwnerEdgeForNode(edges, current);
    if (!ownerEdge) return null;
    const owner = nodesById.get(ownerEdge.from);
    if (!owner) return null;
    if (owner.kind === "Module" || graphNodeDeclaresSymbol(owner)) return owner;
    current = owner.id;
  }
  return null;
}

function graphNodeSymbolNamespace(node, ownerEdge) {
  switch (node.kind) {
    case "Module": return "module";
    case "Import": return "import";
    case "CImport": return "c-import";
    case "Const": return "value";
    case "Function": return ownerEdge?.kind === "method" ? "method" : "value";
    case "TypeAlias":
    case "Shape":
    case "Interface":
    case "Enum":
    case "Choice":
      return "type";
    case "Param": return "param";
    case "Field": return "field";
    case "EnumCase":
    case "ChoiceCase":
    case "ErrorVariant":
      return "variant";
    case "Let": return "local";
    default: return "";
  }
}

function graphLocalSymbolSuffix(node) {
  if (node.kind !== "Let") return "";
  const suffix = node.id.split(".").at(-1) || node.id;
  return suffix ? `@${graphSymbolComponent(suffix)}` : "";
}

function graphNodeSymbolIdFromGraph(moduleIdentity, nodesById, edges, node, depth = 0) {
  if (!graphNodeDeclaresSymbol(node) || depth > nodesById.size) return "";
  const name = graphSymbolName(node);
  if (!name) return "";
  if (node.kind === "Module") return `symbol:${graphModuleScopeName(moduleIdentity, node)}::module`;
  const ownerEdge = graphOwnerEdgeForNode(edges, node.id);
  const owner = graphSymbolOwner(nodesById, edges, node);
  const namespace = graphNodeSymbolNamespace(node, ownerEdge);
  if (!namespace) return "";
  if (owner?.kind === "Module") {
    return `symbol:${graphModuleScopeName(moduleIdentity, owner)}::${graphSymbolComponent(namespace)}.${graphSymbolComponent(name)}${graphLocalSymbolSuffix(node)}`;
  }
  if (owner) {
    const ownerSymbol = graphNodeSymbolIdFromGraph(moduleIdentity, nodesById, edges, owner, depth + 1);
    if (ownerSymbol) return `${ownerSymbol}/${graphSymbolComponent(namespace)}.${graphSymbolComponent(name)}${graphLocalSymbolSuffix(node)}`;
  }
  return `symbol:${graphSymbolComponent(graphIdentityName(moduleIdentity))}::${graphSymbolComponent(namespace)}.${graphSymbolComponent(name)}${graphLocalSymbolSuffix(node)}`;
}

function graphNodeTypeId(line) {
  const type = graphQuotedField(line, "type");
  return type ? graphDomainId("type", graphHashText(1469598103934665603n, type)) : "";
}

function graphNodeEffectId(line) {
  const kind = graphNodeKind(line);
  const name = graphQuotedField(line, "name");
  return kind === "EffectRef" && name ? graphDomainId("effect", graphHashText(1469598103934665603n, name)) : "";
}

function recomputeGraphHash(text) {
  const lines = text.trimEnd().split("\n");
  const moduleIdentity = graphStoredModuleIdentity(graphTopLevelQuoted(lines.find((line) => line.startsWith("module ")) ?? "", "module"));
  const nodeRecords = lines.filter((item) => item.startsWith("node ")).map((line) => ({
    line,
    id: graphNodeHandle(line),
    kind: graphNodeKind(line),
    name: graphQuotedField(line, "name"),
    type: graphQuotedField(line, "type"),
    value: graphQuotedField(line, "value"),
  }));
  const edgeRecords = lines.filter((item) => item.startsWith("edge ")).map(graphEdgeRecord);
  const nodesById = new Map(nodeRecords.map((node) => [node.id, node]));
  let hash = 1469598103934665603n;
  hash = graphHashU64(hash, 1n);
  hash = graphHashText(hash, moduleIdentity);
  for (const node of nodeRecords) {
    const line = node.line;
    hash = graphHashText(hash, node.id);
    hash = graphHashText(hash, graphQuotedField(line, "nodeHash") || graphNodeHash(line));
    hash = graphHashText(hash, graphQuotedField(line, "symbolId") || graphNodeSymbolIdFromGraph(moduleIdentity, nodesById, edgeRecords, node));
    hash = graphHashText(hash, graphQuotedField(line, "typeId") || graphNodeTypeId(line));
    hash = graphHashText(hash, graphQuotedField(line, "effectId") || graphNodeEffectId(line));
  }
  for (const edge of edgeRecords) {
    hash = graphHashText(hash, edge.from);
    hash = graphHashText(hash, edge.to);
    hash = graphHashText(hash, edge.kind);
    hash = graphHashText(hash, edge.target);
    hash = graphHashU64(hash, edge.order);
  }
  return `graph:${hash.toString(16).padStart(16, "0")}`;
}

function repeatBuildHash(args, firstPath, repeatOut, repeatPath = repeatOut) {
  const repeatArgs = [...args];
  const outIndex = repeatArgs.indexOf("--out");
  assert(outIndex >= 0, "repeat build args should include --out");
  repeatArgs[outIndex + 1] = repeatOut;
  rmSync(repeatPath, { force: true, recursive: true });
  rmSync(`${repeatOut}.exe`, { force: true });
  const repeatReport = json(repeatArgs).body;
  assert.equal(repeatReport.generatedCBytes, 0);
  assert.equal(repeatReport.cBridgeFallback ?? false, false);
  assert.equal(sha256File(repeatPath), sha256File(firstPath));
  return repeatReport;
}

function hasAarch64Instruction(bytes, expected) {
  for (let offset = 0; offset + 4 <= bytes.length; offset++) {
    if (bytes.readUInt32LE(offset) === expected) return true;
  }
  return false;
}

function hasAarch64VoidReturnEpilogue(bytes) {
  for (let offset = 0; offset + 12 <= bytes.length; offset++) {
    if (bytes.readUInt32LE(offset) !== 0x52800000) continue;
    let cursor = offset + 4;
    const maybeAddSp = bytes.readUInt32LE(cursor);
    if (((maybeAddSp & 0xffc003ff) >>> 0) === 0x910003ff) cursor += 4;
    if (cursor + 8 <= bytes.length &&
        bytes.readUInt32LE(cursor) === 0xa8c17bfd &&
        bytes.readUInt32LE(cursor + 4) === 0xd65f03c0) return true;
  }
  return false;
}

function assertMachOLoadCommand(bytes, expectedCommand, expectedSize) {
  const ncmds = bytes.readUInt32LE(16);
  for (let offset = 32, i = 0; i < ncmds; i++) {
    const cmd = bytes.readUInt32LE(offset);
    const cmdsize = bytes.readUInt32LE(offset + 4);
    assert(cmdsize >= 8);
    assert(offset + cmdsize <= bytes.length);
    if (cmd === expectedCommand) {
      if (expectedSize !== undefined) assert.equal(cmdsize, expectedSize);
      return bytes.subarray(offset, offset + cmdsize);
    }
    offset += cmdsize;
  }
  assert.fail(`missing Mach-O load command 0x${expectedCommand.toString(16)}`);
}

function elfPackedErrorBytes(code) {
  const bytes = Buffer.alloc(10);
  bytes[0] = 0x48;
  bytes[1] = 0xb8;
  bytes.writeBigUInt64LE(BigInt(code) << 32n, 2);
  return bytes;
}

function hasAnsiControlBytes(text) {
  return /\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b\][^\x07]*(?:\x07|\x1b\\)/.test(text);
}

function removeInlineTests(path) {
  if (!existsSync(path)) return;
  const source = readFileSync(path, "utf8");
  const testStart = source.indexOf("\n\ntest ");
  if (testStart >= 0) writeFileSync(path, `${source.slice(0, testStart)}\n`);
}

function assertTemplateManifest(kind, manifest, readme) {
  assert.equal(manifest.package.version, "0.1.0");
  assert.equal(manifest.targets.cli.defaultTarget, "linux-musl-x64");
  assert.equal(manifest.targets.cli.devTarget, "host");
  assert.equal(manifest.targets.cli.releaseProfile, "release-small");
  assert.equal(manifest.docs.readme, "README.md");
  assert.deepEqual(manifest.docs.examples, [manifest.targets.cli.main]);
  assert.match(readme, /zero check/);
  assert.match(readme, /zero test/);
  assert.match(readme, /zero dev --json/);
  if (kind === "lib") {
    assert.equal(manifest.targets.cli.main, "src/lib.0");
    assert.match(readme, /zero doc --json/);
  } else {
    assert.equal(manifest.targets.cli.main, "src/main.0");
    assert.match(readme, /zero run \./);
    assert.match(readme, /zero build --target linux-musl-x64/);
    assert.match(readme, /zero ship --target linux-musl-x64/);
  }
}

function assertDevReport(report, kind) {
  assert.equal(report.schemaVersion, 1);
  assert.equal(report.ok, true);
  assert.equal(report.mode, "watch-plan");
  assert.equal(report.generatedCBytes, 0);
  assert.equal(report.cBridgeFallback, false);
  assert.equal(report.watch.planOnly, true);
  assert.equal(report.watch.manifest, "zero.json");
  assert.deepEqual(report.watch.rerun, ["check", "test", "examples"]);
  assert(Array.isArray(report.watch.packageLocks));
  assert(Array.isArray(report.watch.generatedBindingInputs));
  assert.equal(report.watch.restartOnSuccess, kind !== "lib");
  assert.equal(report.restart.runnableCli, kind !== "lib");
  assert.equal(report.trace.enabled, true);
  assert.equal(report.trace.requested, false);
  assert.equal(report.trace.phaseTiming, true);
  assert.equal(report.trace.cacheFacts, true);
  assert.equal(report.trace.diagnosticsPassthrough, true);
  assert.equal(report.partialDiagnostics.stable, true);
  assert.equal(report.partialDiagnostics.whileCodegenPending, true);
  assert.equal(report.interfaceFingerprints.algorithm, "fnv1a64-zero-interface-v1");
  assert.match(report.interfaceFingerprints.targetFactsHash, /^[0-9a-f]{16}$/);
  assert(report.interfaceFingerprints.modules.length >= 1);
  assert.equal(report.incrementalInvalidation.partialDiagnosticsStable, true);
  assert.match(String(report.incrementalInvalidation.cacheHits), /^\d+$/);
  assert.match(String(report.incrementalInvalidation.cacheMisses), /^\d+$/);
  assert(report.actions.some((action) => action.kind === "check"));
  assert(report.actions.some((action) => action.kind === "test"));
  assert(report.actions.some((action) => action.kind === "examples"));
  assert(report.actions.some((action) => action.kind === "restart" && action.enabled === (kind !== "lib")));
}

function assertShipReport(report, outPath) {
  assert.equal(report.schemaVersion, 1);
  assert.equal(report.ok, true);
  assert.equal(report.command, "ship");
  assert.equal(report.emit, "exe");
  assert.equal(report.target, "linux-musl-x64");
  assert.equal(report.generatedCBytes, 0);
  assert.equal(report.cBridgeFallback, false);
  assert.equal(report.releasePreview.deterministic, true);
  assertReleaseTargetContract(report, {
    target: "linux-musl-x64",
    emit: "exe",
    objectFormat: "elf",
    artifactKind: "native-executable",
    linkerFlavor: "elf64",
    targetLibcMode: "bundled-libc",
  });
  assert.deepEqual(report.releasePreview.targetContract, report.releaseTargetContract);
  assert.equal(report.artifactPath, outPath);
  assert.equal(statSync(report.artifactPath).size, report.artifactBytes);
  const artifactKinds = new Set(report.artifacts.map((artifact) => artifact.kind));
  for (const kind of ["binary", "stripped-binary", "checksum", "archive", "debug-symbol-metadata", "size-report", "sbom-placeholder"]) {
    assert(artifactKinds.has(kind), `ship report should include ${kind}`);
  }
  for (const artifact of report.artifacts) {
    assert(existsSync(artifact.path), `${artifact.kind} should exist at ${artifact.path}`);
  }
  assert.equal(JSON.parse(readFileSync(report.releasePreview.sizeReport, "utf8")).generatedCBytes, 0);
  assert.equal(JSON.parse(readFileSync(report.releasePreview.debugSymbols, "utf8")).kind, "zero-debug-symbol-metadata");
  assert.equal(JSON.parse(readFileSync(report.releasePreview.sbom, "utf8")).kind, "zero-sbom-placeholder");
  assert.match(readFileSync(report.releasePreview.archive, "utf8"), /zero archive manifest v1/);
}

function assertReleaseTargetContract(report, expected) {
  const contract = report.releaseTargetContract ?? report.releasePreview?.targetContract;
  assert(contract, "release target contract should be present");
  assert.equal(contract.schemaVersion, 1);
  assert.equal(contract.target, expected.target);
  assert.equal(contract.emit, expected.emit);
  assert.equal(contract.artifactKind, expected.artifactKind);
  assert.equal(contract.objectFormat, expected.objectFormat);
  assert.equal(contract.linkerFlavor, expected.linkerFlavor);
  assert.equal(contract.libc.targetMode, expected.targetLibcMode);
  assert.equal(contract.libc.artifactMode, "none");
  assert.equal(contract.generatedCBytes, 0);
  assert.equal(contract.cBridgeFallback, false);
  assert.equal(contract.fallbackPolicy, "explicit-direct-never-c-bridge");
  assert.equal(contract.readiness.status, "supported");
  assert.equal(contract.readiness.directArtifact, true);
  assert.equal(contract.sysroot.requiredByArtifact, false);
  assert.equal(contract.determinism.repeatBuildHash, "checked-by-command-contracts");
  assert(contract.capabilityFacts.some((capability) => capability.name === "memory" && capability.available === true));
}

const generatedCBytesBeforeReadOnlyCommands = json(["size", "--json", "examples/memory-package"]).body.generatedCBytes;

assert.equal(zero(["--version"]).stdout, "zero 0.1.4\n");

const version = json(["--version", "--json"]).body;
assert.equal(version.schemaVersion, 1);
assert.equal(version.version, "0.1.4");
assert.equal(version.backend, "zero-c");
assert.equal(typeof version.host, "string");
assert(version.targets.includes("darwin-arm64"));
assert(version.targets.includes("linux-musl-x64"));
assert(version.targets.includes("win32-x64.exe"));
assert.equal(typeof version.targetCompiler.available, "boolean");

const doctor = json(["doctor", "--json"]).body;
assert.equal(doctor.schemaVersion, 1);
assert(["ok", "warning", "error"].includes(doctor.status));
assert(doctor.checks.some((check) => check.name === "native-c-compiler"));
assert(doctor.checks.some((check) => check.name === "target-c-compiler"));
assert(doctor.checks.some((check) => check.name === "cross-executable-builds" && /non-host executable builds|target-capable C compiler available/.test(check.message)));
assert(doctor.checks.some((check) => check.name === "path" && /PATH/.test(check.message)));
assert(doctor.checks.some((check) => check.name === "host-target" && /host target/.test(check.message)));
assert(doctor.checks.some((check) => check.name === "target-sdk-sysroot" && /sysroot|target-capable C compiler/.test(check.message)));
assert(doctor.checks.some((check) => check.name === "docs-examples"));

for (const [command, expected] of [
  [["--help"], /zero new cli hello/],
  [["check", "--help"], /Usage: zero check/],
  [["build", "--help"], /Usage: zero build/],
  [["run", "--help"], /Usage: zero run/],
  [["test", "--help"], /Usage: zero test/],
  [["fmt", "--help"], /Usage: zero fmt/],
  [["new", "--help"], /Usage: zero new/],
  [["skills", "--help"], /Usage: zero skills/],
  [["ship", "--help"], /Usage: zero ship/],
  [["targets", "--help"], /Usage: zero targets/],
  [["tokens", "--help"], /Usage: zero tokens/],
  [["parse", "--help"], /Usage: zero parse/],
  [["graph", "--help"], /Usage: zero graph/],
  [["size", "--help"], /Usage: zero size/],
  [["explain", "--help"], /Usage: zero explain/],
  [["fix", "--help"], /Usage: zero fix/],
] as Array<[string[], RegExp]>) {
  assert.match(zero(command).stdout, expected);
}
const graphHelp = zero(["graph", "--help"]).stdout;
assert.match(graphHelp, /zero graph \[dump\|import\|validate\|roundtrip\] \[--json\] --out <program-graph-artifact> <input>/);
assert.match(graphHelp, /zero graph view \[--json\] --out <file\.zero> <program-graph-artifact>/);
assert.match(graphHelp, /zero graph size \[--json\] \[--target <target>\] --out <artifact> <input>/);
assert.match(graphHelp, /zero graph patch \[--json\] --out <program-graph-artifact> <input> \(<patch-file>\|--op <operation>\)/);
assert.match(graphHelp, /zero graph build \[--json\] \[--emit exe\|obj\].*<program-graph-or-package>/);
assert.match(graphHelp, /zero graph run \[--target <host-target>\].*<program-graph-or-package> \[-- args\.\.\.\]/);
assert.match(graphHelp, /zero graph test \[--json\] \[--filter <name>\] \[--target <target>\] <program-graph-or-package>/);
assert.doesNotMatch(graphHelp, /zero graph check[^\n]*--out/);
const rootHelp = zero(["--help"]).stdout;
assert.match(rootHelp, /zero graph \[dump\|import\|validate\|roundtrip\] \[--json\] --out <program-graph-artifact> <input>/);
assert.match(rootHelp, /zero graph view \[--json\] --out <file\.zero> <program-graph-artifact>/);
assert.match(rootHelp, /zero graph size \[--json\] \[--target <target>\] --out <artifact> <program-graph-or-package>/);
assert.match(rootHelp, /zero graph patch \[--json\] --out <program-graph-artifact> <program-graph-or-package> \(<patch-file>\|--op <operation>\)/);
assert.match(rootHelp, /zero graph build \[--json\] \[--emit exe\|obj\].*<program-graph-or-package>/);
assert.match(rootHelp, /zero graph run \[--target <host-target>\].*<program-graph-or-package> \[-- args\.\.\.\]/);
assert.match(rootHelp, /zero graph test \[--json\] \[--filter <name>\] \[--target <target>\] <program-graph-or-package>/);

const graphDump = zero(["graph", "dump", "examples/hello.0"]).stdout;
const graphDumpAgain = zero(["graph", "dump", "examples/hello.0"]).stdout;
assert.equal(graphDumpAgain, graphDump);
assert.match(graphDump, /^zero-graph v1\n/);
assert.match(graphDump, /origin source-text/);
assert.match(graphDump, /module "hello"/);
assert.match(graphDump, /hash "graph:[0-9a-f]{16}"/);
assert.doesNotMatch(graphDump, /idStrategy/);
assert.doesNotMatch(graphDump, /canonicalSource/);
assert.doesNotMatch(graphDump, /moduleIdentity/);
assert.doesNotMatch(graphDump, /graphHash/);
assert.doesNotMatch(graphDump, /counts nodes=/);
assert.doesNotMatch(graphDump, /validation "shape-valid" ok/);
assert.doesNotMatch(graphDump, / nodeHash:/);
assert.doesNotMatch(graphDump, / path:/);
assert.doesNotMatch(graphDump, / line:/);
assert.doesNotMatch(graphDump, / column:/);
assert.doesNotMatch(graphDump, / public:false/);
assert.doesNotMatch(graphDump, / target:node order:0/);
assert.doesNotMatch(graphDump, /node id=/);
const graphDumpJson = json(["graph", "dump", "--json", "examples/hello.0"]).body;
assert.equal(graphDumpJson.schemaVersion, 1);
assert.equal(graphDumpJson.moduleIdentity, "module:hello");
assert.equal(graphDumpJson.validation.ok, true);
assert.match(graphDumpJson.graphHash, /^graph:[0-9a-f]{16}$/);
const graphNodeBy = (predicate) => {
  const node = graphDumpJson.nodes.find(predicate);
  assert(node);
  return node;
};
const graphModuleNode = graphNodeBy((node) => node.kind === "Module" && node.name === "hello");
const graphMainFunctionNode = graphNodeBy((node) => node.kind === "Function" && node.name === "main");
const graphMainBodyNode = graphNodeBy((node) => node.kind === "Block" && node.name === "body");
const graphHelloLiteralNode = graphNodeBy((node) => node.kind === "Literal" && node.type === "String" && node.value === "hello from zero\n");
assert.match(graphModuleNode.id, /^#[0-9a-f]{8}$/);
assert.equal(graphModuleNode.symbolId, "symbol:hello::module");
assert.equal(graphMainFunctionNode.symbolId, "symbol:hello::value.main");
assert.doesNotMatch(graphDump, /node #000001/);
assert(graphDumpJson.edges.some((edge) => edge.from === graphModuleNode.id && edge.to === graphMainFunctionNode.id && edge.kind === "function" && edge.order === 0));
const graphDumpPath = join(outDir, "hello.program-graph");
const graphDumpJsonPath = join(outDir, "hello.dump-json.program-graph");
const graphStableSiblingSourcePath = join(outDir, "hello-stable-sibling.0");
const graphBareOutPath = join(outDir, "hello.bare-graph-out");
const graphImportPath = join(outDir, "hello.imported.program-graph");
const graphImportJsonPath = join(outDir, "hello.imported-json.program-graph");
const graphInspectOutPath = join(outDir, "hello.inspect.json");
const graphCanonicalPath = join(outDir, "hello.canonical.program-graph");
const graphSourceTextOutPath = join(outDir, "hello.source-output.0");
const graphSourceRowOutPath = join(outDir, "hello.source-output.row");
const graphValidateSourceTextOutPath = join(outDir, "hello.validate-source-output.0");
const checkedInGraphSourcePath = "conformance/program-graph/hello.0";
const checkedInGraphPackageDir = "conformance/program-graph";
const checkedInGraphBuildPath = join(outDir, "checked-in-graph-build");
const checkedInGraphRunPath = join(outDir, "checked-in-graph-run");
const checkedInGraphShipPath = join(outDir, "checked-in-graph-ship");
const graphViewPath = join(outDir, "hello.program-graph.zero");
const graphViewWrongOutPath = join(outDir, "hello.program-graph.0");
const graphCheckViewPath = join(outDir, "hello.checked.program-graph.zero");
const graphSizePath = join(outDir, "hello.program-graph.size.json");
const graphBuildPath = join(outDir, "hello.program-graph-build");
const graphObjPath = join(outDir, "hello.program-graph.o");
const graphRunPath = join(outDir, "hello.program-graph-run");
const graphArgsDumpPath = join(outDir, "std-args.program-graph");
const graphArgsRunPath = join(outDir, "std-args.program-graph-run");
const graphTestDumpPath = join(outDir, "test-blocks.program-graph");
const graphPackageTestDumpPath = join(outDir, "test-app.program-graph");
const graphManifestPackageDir = join(outDir, "graph-manifest-package");
const graphManifestArtifactPath = join(graphManifestPackageDir, "artifacts", "test-app.program-graph");
const graphManifestBuildPath = join(outDir, "graph-manifest-package-build");
const graphManifestRunPath = join(outDir, "graph-manifest-package-run");
const directGraphManifestBuildPath = join(outDir, "direct-graph-manifest-package-build");
const directGraphManifestRunPath = join(outDir, "direct-graph-manifest-package-run");
const directGraphManifestShipPath = join(outDir, "direct-graph-manifest-package-ship");
const directGraphTargetGateRoot = join(outDir, "direct-graph-target-gate");
const directGraphTargetGatePackageDir = join(directGraphTargetGateRoot, "app");
const directGraphTargetGateDepDir = join(directGraphTargetGateRoot, "target-webbits");
const directGraphTargetGateArtifactPath = join(directGraphTargetGatePackageDir, "artifacts", "app.program-graph");
const directGraphHostLeakPackageDir = join(outDir, "direct-graph-host-leak-package");
const directGraphHostLeakArtifactPath = join(directGraphHostLeakPackageDir, "artifacts", "app.program-graph");
const directGraphHostLeakBuildPath = join(outDir, "direct-graph-host-leak-build");
const graphSizeNoisePatchPath = join(outDir, "hello.program-graph.size-noise.patch");
const graphSizeNoisePath = join(outDir, "hello.program-graph.size-noise.program-graph");
const graphSourceRoundtripPath = join(outDir, "hello.source-roundtrip.program-graph");
const graphSourceRoundtripSourceTextPath = join(outDir, "hello.source-roundtrip.0");
const graphArtifactRoundtripPath = join(outDir, "hello.roundtrip.program-graph");
const graphArtifactRoundtripSourceTextPath = join(outDir, "hello.roundtrip.0");
const graphPatchPath = join(outDir, "hello.program-graph.patch");
const graphPatchedPath = join(outDir, "hello.patched.program-graph");
const graphInlinePatchedPath = join(outDir, "hello.inline-patched.program-graph");
const graphPatchInsertPath = join(outDir, "hello.insert.program-graph.patch");
const graphInsertedPath = join(outDir, "hello.insert.program-graph");
const graphPatchDeletePath = join(outDir, "hello.delete.program-graph.patch");
const graphDeletedPath = join(outDir, "hello.delete.program-graph");
const graphPatchDeleteThenInsertPath = join(outDir, "hello.delete-then-insert.program-graph.patch");
const graphDeleteThenInsertedPath = join(outDir, "hello.delete-then-insert.program-graph");
const graphPatchDeleteNodeFactPath = join(outDir, "hello.delete-node-fact.program-graph.patch");
const graphDeletedNodeFactPath = join(outDir, "hello.delete-node-fact.program-graph");
const graphPatchDeleteExternalRootRefPath = join(outDir, "hello.delete-external-root-ref.program-graph.patch");
const graphPatchDeleteExtraOwnerPath = join(outDir, "hello.delete-extra-owner.program-graph.patch");
const graphPatchReplacePath = join(outDir, "hello.replace.program-graph.patch");
const graphReplacedPath = join(outDir, "hello.replace.program-graph");
const graphPatchStaleReplacePath = join(outDir, "hello.stale-replace.program-graph.patch");
const graphPatchInsertEdgePath = join(outDir, "hello.insert-edge.program-graph.patch");
const graphInsertedEdgePath = join(outDir, "hello.insert-edge.program-graph");
const graphPatchEmptyTypeEdgePath = join(outDir, "hello.empty-type-edge.program-graph.patch");
const graphPatchRenamePath = join(outDir, "hello.rename.program-graph.patch");
const graphRenamedPath = join(outDir, "hello.rename.program-graph");
const graphPatchInvalidRenamePath = join(outDir, "hello.invalid-rename.program-graph.patch");
const graphUncheckedPatchPath = join(outDir, "hello.unchecked.program-graph.patch");
const graphUncheckedPath = join(outDir, "hello.unchecked.program-graph");
const graphBorrowDumpPath = join(outDir, "borrow.program-graph");
const graphBorrowConflictPatchPath = join(outDir, "borrow-conflict.program-graph.patch");
const graphBorrowConflictPath = join(outDir, "borrow-conflict.program-graph");
const graphPatchEmptyPath = join(outDir, "hello.empty.program-graph.patch");
const graphPatchControlPath = join(outDir, "hello.control.program-graph.patch");
const graphPatchHighBytePath = join(outDir, "hello.high-byte.program-graph.patch");
const graphPatchBadEscapePath = join(outDir, "hello.bad-escape.program-graph.patch");
const graphPatchNullEscapePath = join(outDir, "hello.null-escape.program-graph.patch");
const graphPatchRawNullPath = join(outDir, "hello.raw-null.program-graph.patch");
const graphPatchInvalidNamePath = join(outDir, "hello.invalid-name.program-graph.patch");
const graphPatchInvalidTypePath = join(outDir, "hello.invalid-type.program-graph.patch");
const graphPatchReservedParamPath = join(outDir, "hello.reserved-param.program-graph.patch");
const graphReservedParamPath = join(outDir, "hello.reserved-param.program-graph");
const graphPatchInternalFunctionPath = join(outDir, "hello.internal-function.program-graph.patch");
const graphInternalFunctionPath = join(outDir, "hello.internal-function.program-graph");
const graphPayloadDumpPath = join(outDir, "payload-match.program-graph");
const graphPatchInvalidMatchReplacePath = join(outDir, "payload-match.invalid-replace.program-graph.patch");
const graphPatchInvalidMatchInsertPath = join(outDir, "payload-match.invalid-insert.program-graph.patch");
const graphPackageDumpPath = join(outDir, "systems-package.program-graph");
const graphPackagePathMismatchPatchPath = join(outDir, "systems-package.path-mismatch.program-graph.patch");
const graphPackagePathMismatchPath = join(outDir, "systems-package.path-mismatch.program-graph");
const graphPatchInvalidImportAliasPath = join(outDir, "systems-package.invalid-import-alias.program-graph.patch");
const graphPatchInvalidImportNamePath = join(outDir, "systems-package.invalid-import-name.program-graph.patch");
const graphInvalidImportNamePath = join(outDir, "systems-package.invalid-import-name.program-graph");
const graphPatchMissingImportPath = join(outDir, "systems-package.missing-import.program-graph.patch");
const graphMissingImportPath = join(outDir, "systems-package.missing-import.program-graph");
const graphPatchMismatchPath = join(outDir, "hello.mismatch.program-graph.patch");
const graphPatchBadHashPath = join(outDir, "hello.bad-hash.program-graph.patch");
const graphSparseOrderPath = join(outDir, "hello.sparse-order.program-graph");
const graphSparseArgPath = join(outDir, "hello.sparse-arg.program-graph");
rmSync(graphDumpPath, { force: true });
rmSync(graphDumpJsonPath, { force: true });
rmSync(graphStableSiblingSourcePath, { force: true });
rmSync(graphBareOutPath, { force: true });
rmSync(graphImportPath, { force: true });
rmSync(graphImportJsonPath, { force: true });
rmSync(graphCanonicalPath, { force: true });
rmSync(graphSourceTextOutPath, { force: true });
rmSync(graphSourceRowOutPath, { force: true });
rmSync(graphValidateSourceTextOutPath, { force: true });
rmSync(checkedInGraphBuildPath, { force: true });
rmSync(checkedInGraphRunPath, { force: true });
rmSync(checkedInGraphShipPath, { force: true });
rmSync(graphViewPath, { force: true });
rmSync(graphViewWrongOutPath, { force: true });
rmSync(graphCheckViewPath, { force: true });
rmSync(graphSizePath, { force: true });
rmSync(graphBuildPath, { force: true });
rmSync(graphObjPath, { force: true });
rmSync(graphRunPath, { force: true });
rmSync(graphArgsDumpPath, { force: true });
rmSync(graphArgsRunPath, { force: true });
rmSync(graphTestDumpPath, { force: true });
rmSync(graphPackageTestDumpPath, { force: true });
rmSync(graphManifestPackageDir, { force: true, recursive: true });
rmSync(graphManifestBuildPath, { force: true });
rmSync(graphManifestRunPath, { force: true });
rmSync(directGraphManifestBuildPath, { force: true });
rmSync(directGraphManifestRunPath, { force: true });
rmSync(directGraphManifestShipPath, { force: true });
rmSync(directGraphTargetGateRoot, { force: true, recursive: true });
rmSync(directGraphHostLeakPackageDir, { force: true, recursive: true });
rmSync(directGraphHostLeakBuildPath, { force: true });
rmSync(graphSizeNoisePatchPath, { force: true });
rmSync(graphSizeNoisePath, { force: true });
rmSync(graphSourceRoundtripPath, { force: true });
rmSync(graphSourceRoundtripSourceTextPath, { force: true });
rmSync(graphArtifactRoundtripPath, { force: true });
rmSync(graphArtifactRoundtripSourceTextPath, { force: true });
rmSync(graphPatchPath, { force: true });
rmSync(graphPatchedPath, { force: true });
rmSync(graphInlinePatchedPath, { force: true });
rmSync(graphPatchInsertPath, { force: true });
rmSync(graphInsertedPath, { force: true });
rmSync(graphPatchDeletePath, { force: true });
rmSync(graphDeletedPath, { force: true });
rmSync(graphPatchDeleteThenInsertPath, { force: true });
rmSync(graphDeleteThenInsertedPath, { force: true });
rmSync(graphPatchDeleteNodeFactPath, { force: true });
rmSync(graphDeletedNodeFactPath, { force: true });
rmSync(graphPatchDeleteExternalRootRefPath, { force: true });
rmSync(graphPatchDeleteExtraOwnerPath, { force: true });
rmSync(graphPatchReplacePath, { force: true });
rmSync(graphReplacedPath, { force: true });
rmSync(graphPatchStaleReplacePath, { force: true });
rmSync(graphPatchInsertEdgePath, { force: true });
rmSync(graphInsertedEdgePath, { force: true });
rmSync(graphPatchEmptyTypeEdgePath, { force: true });
rmSync(graphPatchRenamePath, { force: true });
rmSync(graphRenamedPath, { force: true });
rmSync(graphPatchInvalidRenamePath, { force: true });
rmSync(graphUncheckedPatchPath, { force: true });
rmSync(graphUncheckedPath, { force: true });
rmSync(graphBorrowDumpPath, { force: true });
rmSync(graphBorrowConflictPatchPath, { force: true });
rmSync(graphBorrowConflictPath, { force: true });
rmSync(graphPatchEmptyPath, { force: true });
rmSync(graphPatchControlPath, { force: true });
rmSync(graphPatchHighBytePath, { force: true });
rmSync(graphPatchBadEscapePath, { force: true });
rmSync(graphPatchNullEscapePath, { force: true });
rmSync(graphPatchRawNullPath, { force: true });
rmSync(graphPatchInvalidNamePath, { force: true });
rmSync(graphPatchInvalidTypePath, { force: true });
rmSync(graphPatchReservedParamPath, { force: true });
rmSync(graphReservedParamPath, { force: true });
rmSync(graphPatchInternalFunctionPath, { force: true });
rmSync(graphInternalFunctionPath, { force: true });
rmSync(graphPayloadDumpPath, { force: true });
rmSync(graphPatchInvalidMatchReplacePath, { force: true });
rmSync(graphPatchInvalidMatchInsertPath, { force: true });
rmSync(graphPackageDumpPath, { force: true });
rmSync(graphPackagePathMismatchPatchPath, { force: true });
rmSync(graphPackagePathMismatchPath, { force: true });
rmSync(graphPatchInvalidImportAliasPath, { force: true });
rmSync(graphPatchInvalidImportNamePath, { force: true });
rmSync(graphInvalidImportNamePath, { force: true });
rmSync(graphPatchMissingImportPath, { force: true });
rmSync(graphMissingImportPath, { force: true });
rmSync(graphPatchMismatchPath, { force: true });
rmSync(graphPatchBadHashPath, { force: true });
rmSync(graphSparseOrderPath, { force: true });
rmSync(graphSparseArgPath, { force: true });
writeFileSync(graphStableSiblingSourcePath, `fn helper u32\n  ret 1\n\n${readFileSync("examples/hello.0", "utf8")}`);
const graphStableSiblingJson = json(["graph", "dump", "--json", graphStableSiblingSourcePath]).body;
assert.equal(graphStableSiblingJson.nodes.find((node) => node.kind === "Function" && node.name === "main")?.id, graphMainFunctionNode.id);
assert.equal(graphStableSiblingJson.nodes.find((node) => node.kind === "Literal" && node.type === "String" && node.value === "hello from zero\n")?.id, graphHelloLiteralNode.id);
assert.equal(zero(["graph", "dump", "--out", graphDumpPath, "examples/hello.0"]).stdout, "");
assert.equal(readFileSync(graphDumpPath, "utf8"), graphDump);
const graphDumpOutJson = json(["graph", "dump", "--json", "--out", graphDumpJsonPath, "examples/hello.0"]).body;
assert.deepEqual(graphDumpOutJson, graphDumpJson);
assert.equal(readFileSync(graphDumpJsonPath, "utf8"), graphDump);
assert.equal(zero(["graph", "validate", graphDumpJsonPath]).stdout, "program graph ok\n");
assert.equal(zero(["graph", "dump", "--out", graphDumpPath, "examples/hello.0"]).stdout, "");
assert.equal(readFileSync(graphDumpPath, "utf8"), graphDump);
assert.equal(zero(["graph", "import", "examples/hello.0"]).stdout, graphDump);
assert.equal(zero(["graph", "import", "--out", graphImportPath, "examples/hello.0"]).stdout, "");
assert.equal(readFileSync(graphImportPath, "utf8"), graphDump);
const graphImportJson = json(["graph", "import", "--json", "examples/hello.0"]).body;
assert.equal(graphImportJson.moduleIdentity, graphDumpJson.moduleIdentity);
assert.equal(graphImportJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphImportJson.validation.ok, true);
const graphImportOutJson = json(["graph", "import", "--json", "--out", graphImportJsonPath, "examples/hello.0"]).body;
assert.equal(graphImportOutJson.ok, true);
assert.equal(graphImportOutJson.moduleIdentity, graphDumpJson.moduleIdentity);
assert.equal(graphImportOutJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphImportOutJson.validation.ok, true);
assert.equal(graphImportOutJson.saved.path, graphImportJsonPath);
assert.equal(readFileSync(graphImportJsonPath, "utf8"), graphDump);
assert.equal(zero(["graph", "validate", graphImportJsonPath]).stdout, "program graph ok\n");
const graphInspectJson = json(["graph", "inspect", "--json", "examples/hello.0"]).body;
assert.equal(graphInspectJson.schemaVersion, 1);
assert.equal(graphInspectJson.programGraph.graphHash, graphDumpJson.graphHash);
assert.equal(graphInspectJson.programGraph.canonicalSource, false);
assert.equal(graphInspectJson.programGraph.validation.ok, true);
assert.equal(graphInspectJson.callResolution.schemaVersion, 1);
const graphInspectOutJson = json(["graph", "inspect", "--json", "--out", graphInspectOutPath, "examples/hello.0"], { allowFailure: true });
assert.notEqual(graphInspectOutJson.code, 0);
assert.equal(graphInspectOutJson.body.diagnostics[0].message, "graph inspect does not support --out");
assert.equal(graphInspectOutJson.body.diagnostics[0].expected, "zero graph inspect [--json] <file.0|file.row|project|zero.json>");
const graphBareOutJson = json(["graph", "--json", "--out", graphBareOutPath, "examples/hello.0"], { allowFailure: true });
assert.notEqual(graphBareOutJson.code, 0);
assert.equal(graphBareOutJson.body.diagnostics[0].message, "graph requires an output-capable subcommand for --out");
assert.equal(graphBareOutJson.body.diagnostics[0].expected, "zero graph dump|import|validate|patch|roundtrip --out <program-graph-artifact> <input>");
assert.equal(existsSync(graphBareOutPath), false);
assert.equal(zero(["graph", "validate", graphDumpPath]).stdout, "program graph ok\n");
const graphValidateJson = json(["graph", "validate", "--json", "--out", graphCanonicalPath, graphDumpPath]).body;
assert.equal(graphValidateJson.ok, true);
assert.equal(graphValidateJson.moduleIdentity, "module:hello");
assert.equal(graphValidateJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphValidateJson.saved.path, graphCanonicalPath);
assert.equal(readFileSync(graphCanonicalPath, "utf8"), graphDump);
const graphSourceTextOutJson = json(["graph", "dump", "--json", "--out", graphSourceTextOutPath, "examples/hello.0"], { allowFailure: true });
assert.notEqual(graphSourceTextOutJson.code, 0);
assert.equal(graphSourceTextOutJson.body.diagnostics[0].message, "program graph output must not use source text extension");
assert.equal(graphSourceTextOutJson.body.diagnostics[0].expected, "zero graph dump --out <program-graph-artifact> <input>");
assert.equal(graphSourceTextOutJson.body.diagnostics[0].help, ".0 and .row files are canonical source text; write derived ProgramGraph artifacts to a non-source path");
assert.equal(existsSync(graphSourceTextOutPath), false);
const graphSourceRowOutJson = json(["graph", "import", "--json", "--out", graphSourceRowOutPath, "examples/hello.0"], { allowFailure: true });
assert.notEqual(graphSourceRowOutJson.code, 0);
assert.equal(graphSourceRowOutJson.body.diagnostics[0].message, "program graph output must not use source text extension");
assert.equal(graphSourceRowOutJson.body.diagnostics[0].expected, "zero graph dump --out <program-graph-artifact> <input>");
assert.equal(existsSync(graphSourceRowOutPath), false);
const graphValidateSourceTextOutJson = json(["graph", "validate", "--json", "--out", graphValidateSourceTextOutPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphValidateSourceTextOutJson.code, 0);
assert.equal(graphValidateSourceTextOutJson.body.diagnostics[0].message, "program graph output must not use source text extension");
assert.equal(graphValidateSourceTextOutJson.body.diagnostics[0].expected, "zero graph validate --out <program-graph-artifact> <program-graph-artifact>");
assert.equal(existsSync(graphValidateSourceTextOutPath), false);
const checkedInGraphSource = readFileSync(checkedInGraphSourcePath, "utf8");
assert.equal(checkedInGraphSource, readFileSync("examples/hello.0", "utf8"));
const checkedInGraphValidateJson = json(["graph", "validate", "--json", checkedInGraphSourcePath], { allowFailure: true });
assert.notEqual(checkedInGraphValidateJson.code, 0);
assert.equal(checkedInGraphValidateJson.body.diagnostics[0].message, "expected zero-graph v1 header");
const checkedInGraphViewJson = json(["graph", "view", "--json", checkedInGraphSourcePath], { allowFailure: true });
assert.notEqual(checkedInGraphViewJson.code, 0);
assert.equal(checkedInGraphViewJson.body.diagnostics[0].message, "expected zero-graph v1 header");
const checkedInGraphPackageCheckJson = json(["check", "--json", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageCheckJson.ok, true);
assert.equal(checkedInGraphPackageCheckJson.sourceFile, checkedInGraphSourcePath);
assert.equal(checkedInGraphPackageCheckJson.package.name, "program-graph-fixture");
assert.equal(checkedInGraphPackageCheckJson.package.manifestPath, join(checkedInGraphPackageDir, "zero.json"));
assert.equal(checkedInGraphPackageCheckJson.graph, undefined);
const checkedInGraphPackageSizeJson = json(["size", "--json", "--target", "linux-musl-x64", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageSizeJson.sourceFile, checkedInGraphSourcePath);
assert.equal(checkedInGraphPackageSizeJson.graph, undefined);
const checkedInGraphPackageBuildJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", checkedInGraphBuildPath, checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageBuildJson.sourceFile, checkedInGraphSourcePath);
assert.equal(checkedInGraphPackageBuildJson.graph, undefined);
assert.equal(checkedInGraphPackageBuildJson.artifactPath, checkedInGraphBuildPath);
assert.equal(zero(["run", "--out", checkedInGraphRunPath, checkedInGraphPackageDir]).stdout, "hello from zero\n");
const checkedInGraphPackageTestJson = json(["test", "--json", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageTestJson.ok, true);
assert.equal(checkedInGraphPackageTestJson.sourceFile, checkedInGraphSourcePath);
assert.equal(checkedInGraphPackageTestJson.graph, undefined);
assert.equal(checkedInGraphPackageTestJson.testDiscovery.mode, "package");
assert.equal(checkedInGraphPackageTestJson.selectedTests, 0);
const checkedInGraphPackageShipJson = json(["ship", "--json", "--target", "linux-musl-x64", "--out", checkedInGraphShipPath, checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageShipJson.ok, true);
assert.equal(checkedInGraphPackageShipJson.sourceFile, checkedInGraphSourcePath);
assert.equal(checkedInGraphPackageShipJson.graph, undefined);
assert.equal(checkedInGraphPackageShipJson.artifactPath, checkedInGraphShipPath);
const graphView = zero(["graph", "view", graphDumpPath]).stdout;
assert.equal(zero(["graph", "view", graphDumpPath]).stdout, graphView);
assert.match(graphView, /^# Generated \.zero preview by zero graph view\. Do not edit\.\n# Canonical source is \.0 text, not this projection\.\n/);
assert.match(graphView, /# Source graph: graph:[0-9a-f]{16}/);
assert.match(graphView, /# graph origin source-text/);
assert.match(graphView, /pub fn main Void world World !/);
assert.match(graphView, /check world\.out\.write "hello from zero\\n"/);
const graphViewJson = json(["graph", "view", "--json", graphDumpPath]).body;
assert.equal(graphViewJson.ok, true);
assert.equal(graphViewJson.canonicalSource, false);
assert.equal(graphViewJson.moduleIdentity, "module:hello");
assert.equal(graphViewJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphViewJson.view, graphView);
assert.equal(zero(["graph", "view", "--out", graphViewPath, graphDumpPath]).stdout, "");
assert.equal(readFileSync(graphViewPath, "utf8"), graphView);
const graphViewOutJson = json(["graph", "view", "--json", "--out", graphViewPath, graphDumpPath]).body;
assert.equal(graphViewOutJson.ok, true);
assert.equal(graphViewOutJson.saved.path, graphViewPath);
assert.equal(graphViewOutJson.view, null);
assert.equal(readFileSync(graphViewPath, "utf8"), graphView);
const graphViewWrongOutJson = json(["graph", "view", "--json", "--out", graphViewWrongOutPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphViewWrongOutJson.code, 0);
assert.equal(graphViewWrongOutJson.body.diagnostics[0].message, "graph view output must use .zero extension");
assert.equal(graphViewWrongOutJson.body.diagnostics[0].expected, "zero graph view --out <file.zero> <program-graph-artifact>");
assert.equal(existsSync(graphViewWrongOutPath), false);
assert.equal(zero(["graph", "check", graphDumpPath]).stdout, "program graph check ok\n");
const graphCheckJson = json(["graph", "check", "--json", graphDumpPath]).body;
assert.equal(graphCheckJson.ok, true);
assert.equal(graphCheckJson.canonicalSource, false);
assert.equal(graphCheckJson.moduleIdentity, "module:hello");
assert.equal(graphCheckJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphCheckJson.check.ok, true);
assert.equal(graphCheckJson.check.phase, "typecheck");
assert.match(graphCheckJson.check.target, /^(darwin|linux|win32)-/);
assert.equal(graphCheckJson.check.lowering, "direct-program-graph");
assert.equal(graphCheckJson.check.sourcePath, null);
assert.equal(graphCheckJson.targetReadiness.ok, true);
assert.equal(graphCheckJson.targetReadiness.languageOk, true);
assert.equal(graphCheckJson.targetReadiness.buildable, true);
assert.deepEqual(graphCheckJson.diagnostics, []);
assert.equal(graphCheckJson.saved, null);
assert.equal(graphCheckJson.view, null);
const graphCheckOutJson = json(["graph", "check", "--json", "--out", graphCheckViewPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphCheckOutJson.code, 0);
assert.equal(graphCheckOutJson.body.diagnostics[0].message, "graph check does not write generated previews");
assert.equal(graphCheckOutJson.body.diagnostics[0].expected, "zero graph view --out <file.zero> <program-graph-artifact>");
const graphSizeJson = json(["graph", "size", "--json", "--target", "linux-musl-x64", graphDumpPath]).body;
assert.equal(graphSizeJson.schemaVersion, 1);
assert.equal(graphSizeJson.sourceFile, graphDumpPath);
assert.equal(graphSizeJson.graph.artifact, graphDumpPath);
assert.equal(graphSizeJson.graph.canonicalSource, false);
assert.equal(graphSizeJson.graph.moduleIdentity, "module:hello");
assert.equal(graphSizeJson.graph.graphHash, graphDumpJson.graphHash);
assert.equal(graphSizeJson.graph.lowering, "direct-program-graph");
assert.equal(graphSizeJson.target, "linux-musl-x64");
assert.equal(graphSizeJson.generatedCBytes, 0);
assert.equal(graphSizeJson.cBridgeFallback, false);
assert.equal(graphSizeJson.sizeBreakdown.target, "linux-musl-x64");
assert.equal(graphSizeJson.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(graphSizeJson.artifactPath, null);
assert(graphSizeJson.compilerCaches.every((cache) => cache.sourceKind === "program-graph" && cache.graphHash === graphDumpJson.graphHash));
assert.equal(graphSizeJson.compilerCaches.find((cache) => cache.name === "parseTree").invalidatesOn, "ProgramGraph input");
assert.equal(graphSizeJson.incrementalInvalidation.sourceKind, "program-graph");
assert.equal(graphSizeJson.incrementalInvalidation.graphInput.artifact, graphDumpPath);
assert.equal(graphSizeJson.incrementalInvalidation.graphInput.graphHash, graphDumpJson.graphHash);
assert.equal(graphSizeJson.incrementalInvalidation.changedInputs.graphArtifact, graphDumpPath);
assert.equal(graphSizeJson.incrementalInvalidation.interfaceFingerprints.sourceKind, "program-graph");
assert.equal(graphSizeJson.incrementalInvalidation.interfaceFingerprints.graphHash, graphDumpJson.graphHash);
const graphSizeHelloInterface = graphSizeJson.incrementalInvalidation.interfaceFingerprints.modules.find((item) => item.name === "hello");
assert(graphSizeHelloInterface);
assert.equal(graphSizeHelloInterface.publicSymbolCount, 1);
assert.deepEqual(graphSizeHelloInterface.publicSymbols, [{ name: "main", kind: "function" }]);
const graphSizeOutJson = json(["graph", "size", "--json", "--target", "linux-musl-x64", "--out", graphSizePath, graphDumpPath]).body;
assert.equal(graphSizeOutJson.graph.graphHash, graphDumpJson.graphHash);
assert.equal(graphSizeOutJson.artifactPath, graphSizePath);
assert.equal(graphSizeOutJson.artifactBytes, statSync(graphSizePath).size);
assert.match(readFileSync(graphSizePath, "utf8"), /"kind": "zero-size-metadata"/);
const graphBuildJson = json(["graph", "build", "--json", "--target", "linux-musl-x64", "--out", graphBuildPath, graphDumpPath]).body;
assert.equal(graphBuildJson.schemaVersion, 1);
assert.equal(graphBuildJson.sourceFile, graphDumpPath);
assert.equal(graphBuildJson.graph.artifact, graphDumpPath);
assert.equal(graphBuildJson.graph.canonicalSource, false);
assert.equal(graphBuildJson.graph.moduleIdentity, "module:hello");
assert.equal(graphBuildJson.graph.graphHash, graphDumpJson.graphHash);
assert.equal(graphBuildJson.graph.lowering, "direct-program-graph");
assert.equal(graphBuildJson.emit, "exe");
assert.equal(graphBuildJson.target, "linux-musl-x64");
assert.equal(graphBuildJson.generatedCBytes, 0);
assert.equal(graphBuildJson.artifactPath, graphBuildPath);
assert.equal(graphBuildJson.artifactBytes, statSync(graphBuildPath).size);
assert(graphBuildJson.compilerCaches.every((cache) => cache.sourceKind === "program-graph" && cache.graphHash === graphDumpJson.graphHash));
assert.equal(graphBuildJson.incrementalInvalidation.sourceKind, "program-graph");
assert.equal(graphBuildJson.incrementalInvalidation.graphInput.artifact, graphDumpPath);
assert.equal(graphBuildJson.incrementalInvalidation.graphInput.graphHash, graphDumpJson.graphHash);
assert.equal(graphBuildJson.incrementalInvalidation.changedInputs.graphArtifact, graphDumpPath);
const graphObjJson = json(["graph", "build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "--out", graphObjPath, graphDumpPath]).body;
assert.equal(graphObjJson.sourceFile, graphDumpPath);
assert.equal(graphObjJson.graph.graphHash, graphDumpJson.graphHash);
assert.equal(graphObjJson.emit, "obj");
assert.equal(graphObjJson.target, "linux-musl-x64");
assert.equal(graphObjJson.generatedCBytes, 0);
assert.equal(graphObjJson.artifactPath, graphObjPath);
assert.equal(graphObjJson.artifactBytes, statSync(graphObjPath).size);
assert.equal(zero(["graph", "run", "--out", graphRunPath, graphDumpPath]).stdout, "hello from zero\n");
assert.equal(existsSync(graphRunPath), true);
assert.equal(zero(["graph", "dump", "--out", graphArgsDumpPath, "conformance/native/pass/std-args.0"]).stdout, "");
assert.equal(zero(["graph", "run", "--out", graphArgsRunPath, graphArgsDumpPath, "--", "alpha", "beta"]).stdout, "alpha\n");
assert.equal(existsSync(graphArgsRunPath), true);
const graphRunJson = json(["graph", "run", "--json", graphDumpPath], { allowFailure: true });
assert.equal(graphRunJson.code, 1);
assert.equal(graphRunJson.body.diagnostics[0].message, "zero graph run does not support --json");
assert.equal(zero(["graph", "dump", "--out", graphTestDumpPath, "conformance/native/pass/test-blocks.0"]).stdout, "");
assert.equal(zero(["graph", "test", graphTestDumpPath]).stdout, "1 test(s) ok\n");
const graphTestOutJson = json(["graph", "test", "--json", "--out", graphCheckViewPath, graphTestDumpPath], { allowFailure: true });
assert.notEqual(graphTestOutJson.code, 0);
assert.equal(graphTestOutJson.body.diagnostics[0].message, "graph test does not support --out");
assert.equal(graphTestOutJson.body.diagnostics[0].expected, "zero graph test [--json] [--filter <name>] [--target <target>] <program-graph-or-package>");
const graphTestJson = json(["graph", "test", "--json", "--filter", "addition", graphTestDumpPath]).body;
assert.equal(graphTestJson.ok, true);
assert.equal(graphTestJson.sourceFile, graphTestDumpPath);
assert.equal(graphTestJson.graph.artifact, graphTestDumpPath);
assert.equal(graphTestJson.graph.canonicalSource, false);
assert.equal(graphTestJson.graph.lowering, "direct-program-graph");
assert.match(graphTestJson.graph.graphHash, /^graph:[0-9a-f]{16}$/);
assert.equal(graphTestJson.testBackend, "direct-frontend");
assert.equal(graphTestJson.testDiscovery.mode, "program-graph");
assert.equal(graphTestJson.testDiscovery.filter, "addition");
assert.equal(graphTestJson.selectedTests, 1);
assert.equal(graphTestJson.results[0].status, "passed");
assert.equal(graphTestJson.results[0].location.sourceFile, graphTestDumpPath);
assert.equal(graphTestJson.results[0].location.line, 18);
assert.equal(zero(["graph", "dump", "--out", graphPackageTestDumpPath, "conformance/packages/test-app"]).stdout, "");
const graphPackageTestJson = json(["graph", "test", "--json", graphPackageTestDumpPath]).body;
assert.equal(graphPackageTestJson.ok, true);
assert.equal(graphPackageTestJson.graph.artifact, graphPackageTestDumpPath);
assert.equal(graphPackageTestJson.graph.moduleIdentity, "package:test-app@0.1.0");
assert.equal(graphPackageTestJson.testDiscovery.mode, "package-graph");
assert.equal(graphPackageTestJson.testDiscovery.sourceFileCount, 2);
assert.equal(graphPackageTestJson.selectedTests, 3);
assert(graphPackageTestJson.fixtures.sourceFiles.includes(`${graphPackageTestDumpPath}#helper`));
assert(graphPackageTestJson.fixtures.sourceFiles.includes(`${graphPackageTestDumpPath}#main`));
const graphPackageHelperTest = graphPackageTestJson.results.find((item) => item.name === "package helper double");
assert(graphPackageHelperTest);
assert.equal(graphPackageHelperTest.location.sourceFile, `${graphPackageTestDumpPath}#helper`);
assert.equal(graphPackageHelperTest.location.line, 18);
const graphPackageExpectedFail = graphPackageTestJson.results.find((item) => item.status === "expected-fail");
assert(graphPackageExpectedFail);
assert.equal(graphPackageExpectedFail.location.sourceFile, `${graphPackageTestDumpPath}#helper`);
assert.equal(graphPackageExpectedFail.location.line, 29);
assert.equal(graphPackageExpectedFail.failure.sourceFile, `${graphPackageTestDumpPath}#helper`);
assert.equal(graphPackageExpectedFail.failure.line, 33);
mkdirSync(join(graphManifestPackageDir, "src"), { recursive: true });
mkdirSync(join(graphManifestPackageDir, "artifacts"), { recursive: true });
writeFileSync(join(graphManifestPackageDir, "src", "main.0"), readFileSync("conformance/packages/test-app/src/main.0", "utf8"));
writeFileSync(join(graphManifestPackageDir, "src", "helper.0"), readFileSync("conformance/packages/test-app/src/helper.0", "utf8"));
writeFileSync(join(graphManifestPackageDir, "zero.json"), `${JSON.stringify({
  package: { name: "graph-manifest-package", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0", graph: "artifacts/test-app.program-graph" } },
}, null, 2)}\n`);
assert.equal(zero(["graph", "import", "--out", graphManifestArtifactPath, "conformance/packages/test-app"]).stdout, "");
assert.equal(zero(["graph", "validate", graphManifestPackageDir]).stdout, "program graph ok\n");
const graphManifestCheckJson = json(["graph", "check", "--json", graphManifestPackageDir]).body;
assert.equal(graphManifestCheckJson.ok, true);
assert.equal(graphManifestCheckJson.artifact, graphManifestArtifactPath);
assert.equal(graphManifestCheckJson.moduleIdentity, "package:test-app@0.1.0");
assert.equal(graphManifestCheckJson.check.lowering, "direct-program-graph");
const graphManifestSizeJson = json(["graph", "size", "--json", "--target", "linux-musl-x64", graphManifestPackageDir]).body;
assert.equal(graphManifestSizeJson.graph.artifact, graphManifestArtifactPath);
assert.equal(graphManifestSizeJson.graph.lowering, "direct-program-graph");
const graphManifestBuildJson = json(["graph", "build", "--json", "--target", "linux-musl-x64", "--out", graphManifestBuildPath, graphManifestPackageDir]).body;
assert.equal(graphManifestBuildJson.graph.artifact, graphManifestArtifactPath);
assert.equal(graphManifestBuildJson.graph.lowering, "direct-program-graph");
assert.equal(zero(["graph", "run", "--out", graphManifestRunPath, graphManifestPackageDir]).stdout, "package tests\n");
const graphManifestTestJson = json(["graph", "test", "--json", graphManifestPackageDir]).body;
assert.equal(graphManifestTestJson.ok, true);
assert.equal(graphManifestTestJson.graph.artifact, graphManifestArtifactPath);
assert.equal(graphManifestTestJson.testDiscovery.mode, "package-graph");
const graphManifestRoundtripJson = json(["graph", "roundtrip", "--json", graphManifestPackageDir]).body;
assert.equal(graphManifestRoundtripJson.ok, true);
assert.equal(graphManifestRoundtripJson.artifact, graphManifestArtifactPath);
assert.equal(graphManifestRoundtripJson.semanticStable, true);
assert.equal(graphManifestRoundtripJson.lowering, "direct-program-graph");
assert.equal(graphManifestRoundtripJson.moduleIdentity, "package:test-app@0.1.0");
assert.equal(graphManifestRoundtripJson.roundtripModuleIdentity, "package:test-app@0.1.0");
assert.equal(graphManifestRoundtripJson.view, null);
const directGraphManifestCheckJson = json(["check", "--json", graphManifestPackageDir]).body;
assert.equal(directGraphManifestCheckJson.ok, true);
assert.equal(directGraphManifestCheckJson.graph, undefined);
assert.equal(directGraphManifestCheckJson.sourceFile, join(graphManifestPackageDir, "src", "main.0"));
assert.equal(directGraphManifestCheckJson.package.name, "graph-manifest-package");
assert.equal(directGraphManifestCheckJson.package.manifestPath, join(graphManifestPackageDir, "zero.json"));
assert.notEqual(directGraphManifestCheckJson.package.manifestHash, "0000000000000000");
assert(directGraphManifestCheckJson.compilerCaches.every((cache) => cache.sourceKind !== "program-graph"));
assert.equal(directGraphManifestCheckJson.compilerCaches.find((cache) => cache.name === "parseTree").invalidatesOn, "source");
assert.equal(directGraphManifestCheckJson.incrementalInvalidation.changedInputs.graphArtifact, undefined);
assert.equal(directGraphManifestCheckJson.incrementalInvalidation.changedInputs.manifestPath, join(graphManifestPackageDir, "zero.json"));
const directGraphManifestSizeJson = json(["size", "--json", "--target", "linux-musl-x64", graphManifestPackageDir]).body;
assert.equal(directGraphManifestSizeJson.graph, undefined);
assert.equal(directGraphManifestSizeJson.sourceFile, join(graphManifestPackageDir, "src", "main.0"));
const directGraphManifestBuildJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", directGraphManifestBuildPath, graphManifestPackageDir]).body;
assert.equal(directGraphManifestBuildJson.graph, undefined);
assert.equal(directGraphManifestBuildJson.sourceFile, join(graphManifestPackageDir, "src", "main.0"));
assert.equal(zero(["run", "--out", directGraphManifestRunPath, graphManifestPackageDir]).stdout, "package tests\n");
const directGraphManifestTestJson = json(["test", "--json", graphManifestPackageDir]).body;
assert.equal(directGraphManifestTestJson.ok, true);
assert.equal(directGraphManifestTestJson.graph, undefined);
assert.equal(directGraphManifestTestJson.testDiscovery.mode, "package");
const directGraphManifestShipJson = json(["ship", "--json", "--target", "linux-musl-x64", "--out", directGraphManifestShipPath, graphManifestPackageDir]).body;
assert.equal(directGraphManifestShipJson.ok, true);
assert.equal(directGraphManifestShipJson.sourceFile, join(graphManifestPackageDir, "src", "main.0"));
assert.equal(directGraphManifestShipJson.graph, undefined);
assert.equal(directGraphManifestShipJson.artifactPath, directGraphManifestShipPath);
assert.equal(existsSync(directGraphManifestShipPath), true);
assert(directGraphManifestShipJson.compilerCaches.every((cache) => cache.sourceKind !== "program-graph"));
assert.equal(directGraphManifestShipJson.incrementalInvalidation.changedInputs.graphArtifact, undefined);
const sourcePackageCheckJson = json(["check", "--json", "conformance/packages/test-app"]).body;
assert.equal(sourcePackageCheckJson.ok, true);
assert.equal(sourcePackageCheckJson.graph, undefined);
mkdirSync(join(directGraphTargetGatePackageDir, "src"), { recursive: true });
mkdirSync(join(directGraphTargetGatePackageDir, "artifacts"), { recursive: true });
mkdirSync(join(directGraphTargetGateDepDir, "src"), { recursive: true });
writeFileSync(join(directGraphTargetGatePackageDir, "src", "main.0"), "pub fn main Void world World !\n  check world.out.write \"target gate\\n\"\n");
writeFileSync(join(directGraphTargetGateDepDir, "src", "main.0"), "pub fn main Void world World !\n  check world.out.write \"webbits\\n\"\n");
writeFileSync(join(directGraphTargetGateDepDir, "zero.json"), `${JSON.stringify({
  package: { name: "target-webbits", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
}, null, 2)}\n`);
writeFileSync(join(directGraphTargetGatePackageDir, "zero.json"), `${JSON.stringify({
  package: { name: "direct-graph-target-gate", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0", graph: "artifacts/app.program-graph" } },
  dependencies: {
    "target-webbits": {
      path: "../target-webbits",
      version: "0.1.0",
      targets: ["win32-x64.exe"],
    },
  },
}, null, 2)}\n`);
assert.equal(zero(["graph", "import", "--out", directGraphTargetGateArtifactPath, directGraphTargetGatePackageDir]).stdout, "");
const directGraphTargetGateJson = json(["check", "--json", "--target", "linux-musl-x64", directGraphTargetGatePackageDir], { allowFailure: true });
assert.equal(directGraphTargetGateJson.code, 1);
assert.equal(directGraphTargetGateJson.body.diagnostics[0].code, "PKG004");
assert.equal(directGraphTargetGateJson.body.diagnostics[0].message, "package dependency is not compatible with target");
mkdirSync(join(directGraphHostLeakPackageDir, "src"), { recursive: true });
mkdirSync(join(directGraphHostLeakPackageDir, "artifacts"), { recursive: true });
writeFileSync(join(directGraphHostLeakPackageDir, "src", "main.0"), "pub fn main Void world World !\n  check world.out.write \"host leak\\n\"\n");
writeFileSync(join(directGraphHostLeakPackageDir, "zero.json"), `${JSON.stringify({
  package: { name: "direct-graph-host-leak", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0", graph: "artifacts/app.program-graph" } },
  c: {
    libs: {
      hostonly: {
        headers: ["hostonly.h"],
        include: ["/usr/include"],
        lib: ["/usr/lib"],
        link: ["hostonly"],
        pkg_config: "hostonly",
      },
    },
  },
}, null, 2)}\n`);
assert.equal(zero(["graph", "import", "--out", directGraphHostLeakArtifactPath, directGraphHostLeakPackageDir]).stdout, "");
const directGraphHostLeakJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", directGraphHostLeakBuildPath, directGraphHostLeakPackageDir], { allowFailure: true });
assert.equal(directGraphHostLeakJson.code, 1);
assert.equal(directGraphHostLeakJson.body.diagnostics[0].code, "CIMP003");
assert.equal(directGraphHostLeakJson.body.diagnostics[0].message, "foreign target C dependency would use host discovery");
const graphManifestMissingJson = json(["graph", "check", "--json", "conformance/packages/test-app"], { allowFailure: true });
assert.equal(graphManifestMissingJson.code, 1);
assert.equal(graphManifestMissingJson.body.diagnostics[0].message, "zero.json is missing targets.cli.graph");
assert.equal(graphManifestMissingJson.body.diagnostics[0].expected, "targets.cli.graph pointing at a derived ProgramGraph artifact");
writeFileSync(graphSizeNoisePatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insert node="#patch_size_noise" kind="Function" parent="${graphModuleNode.id}" edge="backlink" order="0" name="ghost" type="Void" public="true" path="examples/hello.0" line="1" column="1"`,
  "",
].join("\n"));
const graphSizeNoisePatchJson = json(["graph", "patch", "--json", "--out", graphSizeNoisePath, graphDumpPath, graphSizeNoisePatchPath]).body;
assert.equal(graphSizeNoisePatchJson.ok, true);
assert.equal(zero(["graph", "check", graphSizeNoisePath]).stdout, "program graph check ok\n");
const graphSizeNoiseJson = json(["graph", "size", "--json", "--target", "linux-musl-x64", graphSizeNoisePath]).body;
const graphSizeNoiseInterface = graphSizeNoiseJson.incrementalInvalidation.interfaceFingerprints.modules.find((item) => item.name === "hello");
assert(graphSizeNoiseInterface);
assert.deepEqual(graphSizeNoiseInterface.publicSymbols, [{ name: "main", kind: "function" }]);
writeFileSync(graphPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="hello patched\\n"`,
  "",
].join("\n"));
assert.equal(zero(["graph", "patch", "--out", graphPatchedPath, graphDumpPath, graphPatchPath]).stdout, "program graph patch ok\n");
const graphPatchedStdout = zero(["graph", "patch", graphDumpPath, graphPatchPath]).stdout;
assert.match(graphPatchedStdout, /^zero-graph v1\n/);
assert.match(graphPatchedStdout, /value:"hello patched\\n"/);
assert.equal(readFileSync(graphPatchedPath, "utf8"), graphPatchedStdout);
const graphPatchJson = json(["graph", "patch", "--json", "--out", graphPatchedPath, graphDumpPath, graphPatchPath]).body;
assert.equal(graphPatchJson.ok, true);
assert.equal(graphPatchJson.canonicalSource, false);
assert.equal(graphPatchJson.patch, graphPatchPath);
assert.equal(graphPatchJson.originalGraphHash, graphDumpJson.graphHash);
assert.match(graphPatchJson.patchedGraphHash, /^graph:[0-9a-f]{16}$/);
assert.notEqual(graphPatchJson.patchedGraphHash, graphDumpJson.graphHash);
assert.equal(graphPatchJson.operationCount, 1);
assert.equal(graphPatchJson.operations[0].ok, true);
assert.equal(graphPatchJson.operations[0].node, graphHelloLiteralNode.id);
assert.equal(graphPatchJson.operations[0].field, "value");
assert.equal(graphPatchJson.operations[0].actual, "hello from zero\n");
assert.equal(graphPatchJson.operations[0].value, "hello patched\n");
assert.equal(graphPatchJson.diagnostic, null);
assert.equal(graphPatchJson.saved.path, graphPatchedPath);
assert.equal(zero(["graph", "validate", graphPatchedPath]).stdout, "program graph ok\n");
assert.match(zero(["graph", "view", graphPatchedPath]).stdout, /check world\.out\.write "hello patched\\n"/);
assert.equal(zero(["graph", "check", graphPatchedPath]).stdout, "program graph check ok\n");
const graphInlinePatchJson = json([
  "graph",
  "patch",
  "--json",
  "--out",
  graphInlinePatchedPath,
  graphDumpPath,
  "--expect-graph-hash",
  graphDumpJson.graphHash,
  "--op",
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="hello inline\\n"`,
]).body;
assert.equal(graphInlinePatchJson.ok, true);
assert.equal(graphInlinePatchJson.patch, "<inline>");
assert.equal(graphInlinePatchJson.originalGraphHash, graphDumpJson.graphHash);
assert.equal(graphInlinePatchJson.operationCount, 1);
assert.equal(graphInlinePatchJson.operations[0].node, graphHelloLiteralNode.id);
assert.equal(graphInlinePatchJson.operations[0].value, "hello inline\n");
assert.equal(graphInlinePatchJson.saved.path, graphInlinePatchedPath);
assert.match(zero(["graph", "view", graphInlinePatchedPath]).stdout, /check world\.out\.write "hello inline\\n"/);
writeFileSync(graphPatchInsertPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insert node="#patch_check" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="1" path="examples/hello.0" line="3" column="3"`,
  `insert node="#patch_call" kind="MethodCall" parent="#patch_check" edge="expr" order="0" name="write" type="Void" path="examples/hello.0" line="3" column="25"`,
  `insert node="#patch_write" kind="FieldAccess" parent="#patch_call" edge="left" order="0" name="write" path="examples/hello.0" line="3" column="18"`,
  `insert node="#patch_out" kind="FieldAccess" parent="#patch_write" edge="left" order="0" name="out" path="examples/hello.0" line="3" column="14"`,
  `insert node="#patch_world" kind="Identifier" parent="#patch_out" edge="left" order="0" name="world" path="examples/hello.0" line="3" column="9"`,
  `insert node="#patch_lit" kind="Literal" parent="#patch_call" edge="arg" order="0" type="String" value="second line\\n" path="examples/hello.0" line="3" column="25"`,
  "",
].join("\n"));
const graphInsertPatchJson = json(["graph", "patch", "--json", "--out", graphInsertedPath, graphDumpPath, graphPatchInsertPath]).body;
assert.equal(graphInsertPatchJson.ok, true);
assert.equal(graphInsertPatchJson.operationCount, 6);
assert.equal(graphInsertPatchJson.operations[0].op, "insert");
assert.equal(graphInsertPatchJson.operations[0].parent, graphMainBodyNode.id);
assert.equal(graphInsertPatchJson.operations[0].edge, "statement");
assert.equal(graphInsertPatchJson.operations[0].order, 1);
assert.equal(graphInsertPatchJson.operations[5].kind, "Literal");
assert.equal(graphInsertPatchJson.operations[5].type, "String");
assert.equal(graphInsertPatchJson.operations[5].value, "second line\n");
assert.equal(graphInsertPatchJson.saved.path, graphInsertedPath);
const graphInsertedView = zero(["graph", "view", graphInsertedPath]).stdout;
assert.match(graphInsertedView, /check world\.out\.write "hello from zero\\n"\n  check world\.out\.write "second line\\n"/);
assert.equal(zero(["graph", "check", graphInsertedPath]).stdout, "program graph check ok\n");
assert.equal(zero(["graph", "roundtrip", graphInsertedPath]).stdout, "program graph roundtrip ok\n");
const graphInsertedText = readFileSync(graphInsertedPath, "utf8");
const graphInsertedCheckLine = graphInsertedText.split("\n").find((line) => line.startsWith("node #patch_check ")) ?? "";
const graphInsertedCheckHash = graphQuotedField(graphInsertedCheckLine, "nodeHash") || graphNodeHash(graphInsertedCheckLine);
assert.match(graphInsertedCheckHash, /^nodehash:[0-9a-f]{16}$/);
writeFileSync(graphPatchDeletePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphInsertPatchJson.patchedGraphHash}"`,
  `delete node="#patch_check" expect="${graphInsertedCheckHash}"`,
  "",
].join("\n"));
const graphDeletePatchJson = json(["graph", "patch", "--json", "--out", graphDeletedPath, graphInsertedPath, graphPatchDeletePath]).body;
assert.equal(graphDeletePatchJson.ok, true);
assert.equal(graphDeletePatchJson.operations[0].op, "delete");
assert.equal(graphDeletePatchJson.operations[0].actual, graphInsertedCheckHash);
assert.equal(readFileSync(graphDeletedPath, "utf8"), graphDump);
const graphOriginalCheckNode = graphDumpJson.nodes.find((node) => node.kind === "Check");
assert(graphOriginalCheckNode);
writeFileSync(graphPatchDeleteThenInsertPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphInsertPatchJson.patchedGraphHash}"`,
  `delete node="${graphOriginalCheckNode.id}" expect="${graphOriginalCheckNode.nodeHash}"`,
  `insert node="#patch_replacement_check" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="0" path="examples/hello.0" line="2" column="3"`,
  `insert node="#patch_replacement_call" kind="MethodCall" parent="#patch_replacement_check" edge="expr" order="0" name="write" type="Void" path="examples/hello.0" line="2" column="25"`,
  `insert node="#patch_replacement_write" kind="FieldAccess" parent="#patch_replacement_call" edge="left" order="0" name="write" path="examples/hello.0" line="2" column="18"`,
  `insert node="#patch_replacement_out" kind="FieldAccess" parent="#patch_replacement_write" edge="left" order="0" name="out" path="examples/hello.0" line="2" column="14"`,
  `insert node="#patch_replacement_world" kind="Identifier" parent="#patch_replacement_out" edge="left" order="0" name="world" path="examples/hello.0" line="2" column="9"`,
  `insert node="#patch_replacement_lit" kind="Literal" parent="#patch_replacement_call" edge="arg" order="0" type="String" value="replacement line\\n" path="examples/hello.0" line="2" column="25"`,
  "",
].join("\n"));
const graphDeleteThenInsertPatchJson = json(["graph", "patch", "--json", "--out", graphDeleteThenInsertedPath, graphInsertedPath, graphPatchDeleteThenInsertPath]).body;
assert.equal(graphDeleteThenInsertPatchJson.ok, true);
assert.equal(graphDeleteThenInsertPatchJson.operations[0].op, "delete");
assert.equal(graphDeleteThenInsertPatchJson.operations[1].op, "insert");
const graphDeleteThenInsertedView = zero(["graph", "view", graphDeleteThenInsertedPath]).stdout;
assert.match(graphDeleteThenInsertedView, /check world\.out\.write "replacement line\\n"\n  check world\.out\.write "second line\\n"/);
assert.equal(zero(["graph", "check", graphDeleteThenInsertedPath]).stdout, "program graph check ok\n");
assert.equal(zero(["graph", "roundtrip", graphDeleteThenInsertedPath]).stdout, "program graph roundtrip ok\n");
writeFileSync(graphPatchDeleteNodeFactPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insert node="#patch_node_fact" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="1"`,
  `insertEdge from="#patch_node_fact" to="${graphModuleNode.id}" edge="backlink" target="node" order="0"`,
  `delete node="#patch_node_fact"`,
  "",
].join("\n"));
const graphDeleteNodeFactPatchJson = json(["graph", "patch", "--json", "--out", graphDeletedNodeFactPath, graphDumpPath, graphPatchDeleteNodeFactPath]).body;
assert.equal(graphDeleteNodeFactPatchJson.ok, true);
assert.equal(graphDeleteNodeFactPatchJson.operationCount, 3);
assert.equal(graphDeleteNodeFactPatchJson.operations[1].op, "insertEdge");
assert.equal(graphDeleteNodeFactPatchJson.operations[1].target, "node");
assert.equal(graphDeleteNodeFactPatchJson.operations[2].op, "delete");
assert.equal(readFileSync(graphDeletedNodeFactPath, "utf8"), graphDump);
writeFileSync(graphPatchDeleteExternalRootRefPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insert node="#patch_external_root_ref" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="1"`,
  `insertEdge from="${graphModuleNode.id}" to="#patch_external_root_ref" edge="backlink" target="node" order="0"`,
  `delete node="#patch_external_root_ref"`,
  "",
].join("\n"));
const graphDeleteExternalRootRefPatchJson = json(["graph", "patch", "--json", graphDumpPath, graphPatchDeleteExternalRootRefPath], { allowFailure: true });
assert.notEqual(graphDeleteExternalRootRefPatchJson.code, 0);
assert.equal(graphDeleteExternalRootRefPatchJson.body.ok, false);
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[0].ok, true);
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[1].ok, true);
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[2].ok, false);
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[2].code, "GPH005");
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[2].actual, "#patch_external_root_ref");
assert.equal(graphDeleteExternalRootRefPatchJson.body.saved, null);
writeFileSync(graphPatchDeleteExtraOwnerPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insert node="#patch_extra_owner" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="1"`,
  `insertEdge from="${graphModuleNode.id}" to="#patch_extra_owner" edge="function" target="node" order="1"`,
  `delete node="#patch_extra_owner"`,
  "",
].join("\n"));
const graphDeleteExtraOwnerPatchJson = json(["graph", "patch", "--json", graphDumpPath, graphPatchDeleteExtraOwnerPath], { allowFailure: true });
assert.notEqual(graphDeleteExtraOwnerPatchJson.code, 0);
assert.equal(graphDeleteExtraOwnerPatchJson.body.ok, false);
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[0].ok, true);
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[1].ok, true);
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[2].ok, false);
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[2].code, "GPH005");
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[2].actual, "#patch_extra_owner");
assert.equal(graphDeleteExtraOwnerPatchJson.body.saved, null);
const graphLiteralNode = graphDumpJson.nodes.find((node) => node.kind === "Literal" && node.type === "String");
assert(graphLiteralNode);
writeFileSync(graphPatchReplacePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `replace node="${graphLiteralNode.id}" expect="${graphLiteralNode.nodeHash}" kind="Literal" type="String" value="hello replaced structurally\\n" public="true"`,
  "",
].join("\n"));
const graphReplacePatchJson = json(["graph", "patch", "--json", "--out", graphReplacedPath, graphDumpPath, graphPatchReplacePath]).body;
assert.equal(graphReplacePatchJson.ok, true);
assert.equal(graphReplacePatchJson.operations[0].op, "replace");
assert.equal(graphReplacePatchJson.operations[0].actual, graphLiteralNode.nodeHash);
assert.equal(graphReplacePatchJson.operations[0].value, "hello replaced structurally\n");
assert.equal(graphReplacePatchJson.operations[0].public, true);
assert.match(zero(["graph", "view", graphReplacedPath]).stdout, /check world\.out\.write "hello replaced structurally\\n"/);
writeFileSync(graphPatchStaleReplacePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `replace node="${graphLiteralNode.id}" expect="${graphLiteralNode.nodeHash}" kind="Literal" type="String" value="first structural value\\n"`,
  `replace node="${graphLiteralNode.id}" expect="${graphLiteralNode.nodeHash}" kind="Literal" type="String" value="second structural value\\n"`,
  "",
].join("\n"));
const graphStaleReplacePatchJson = json(["graph", "patch", "--json", graphDumpPath, graphPatchStaleReplacePath], { allowFailure: true });
assert.notEqual(graphStaleReplacePatchJson.code, 0);
assert.equal(graphStaleReplacePatchJson.body.ok, false);
assert.equal(graphStaleReplacePatchJson.body.operations[0].ok, true);
assert.equal(graphStaleReplacePatchJson.body.operations[1].ok, false);
assert.equal(graphStaleReplacePatchJson.body.operations[1].code, "GPH005");
assert.notEqual(graphStaleReplacePatchJson.body.operations[1].actual, graphLiteralNode.nodeHash);
writeFileSync(graphPatchInsertEdgePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insertEdge from="${graphLiteralNode.id}" to="${graphLiteralNode.typeId}" edge="resolvedType" target="type" order="0"`,
  "",
].join("\n"));
const graphInsertEdgePatchJson = json(["graph", "patch", "--json", "--out", graphInsertedEdgePath, graphDumpPath, graphPatchInsertEdgePath]).body;
assert.equal(graphInsertEdgePatchJson.ok, true);
assert.equal(graphInsertEdgePatchJson.operations[0].op, "insertEdge");
assert.equal(graphInsertEdgePatchJson.operations[0].from, graphLiteralNode.id);
assert.equal(graphInsertEdgePatchJson.operations[0].to, graphLiteralNode.typeId);
assert.equal(graphInsertEdgePatchJson.operations[0].target, "type");
assert.equal(zero(["graph", "validate", graphInsertedEdgePath]).stdout, "program graph ok\n");
writeFileSync(graphPatchEmptyTypeEdgePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insertEdge from="${graphModuleNode.id}" to="" edge="resolvedType" target="type" order="0"`,
  "",
].join("\n"));
const graphEmptyTypeEdgePatchJson = json(["graph", "patch", "--json", graphDumpPath, graphPatchEmptyTypeEdgePath], { allowFailure: true });
assert.notEqual(graphEmptyTypeEdgePatchJson.code, 0);
assert.equal(graphEmptyTypeEdgePatchJson.body.ok, false);
assert.equal(graphEmptyTypeEdgePatchJson.body.diagnostic.code, "GPH004");
assert.equal(graphEmptyTypeEdgePatchJson.body.operations[0].ok, false);
assert.equal(graphEmptyTypeEdgePatchJson.body.operations[0].code, "GPH004");
assert.equal(graphEmptyTypeEdgePatchJson.body.operations[0].to, "");
assert.equal(graphEmptyTypeEdgePatchJson.body.saved, null);
writeFileSync(graphPatchRenamePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `rename node="${graphMainFunctionNode.id}" expect="main" value="start"`,
  "",
].join("\n"));
const graphRenamePatchJson = json(["graph", "patch", "--json", "--out", graphRenamedPath, graphDumpPath, graphPatchRenamePath]).body;
assert.equal(graphRenamePatchJson.ok, true);
assert.equal(graphRenamePatchJson.operations[0].op, "rename");
assert.equal(graphRenamePatchJson.operations[0].actual, "main");
assert.match(zero(["graph", "view", graphRenamedPath]).stdout, /pub fn start Void world World !/);
const graphRenamedCheck = json(["graph", "check", "--json", graphRenamedPath], { allowFailure: true });
assert.notEqual(graphRenamedCheck.code, 0);
assert.equal(graphRenamedCheck.body.diagnostics[0].message, "missing main function");
writeFileSync(graphPatchInvalidRenamePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `rename node="${graphMainFunctionNode.id}" expect="main" value="bad\\nname"`,
  "",
].join("\n"));
const graphInvalidRenamePatchJson = json(["graph", "patch", "--json", graphDumpPath, graphPatchInvalidRenamePath], { allowFailure: true });
assert.notEqual(graphInvalidRenamePatchJson.code, 0);
assert.equal(graphInvalidRenamePatchJson.body.ok, false);
assert.equal(graphInvalidRenamePatchJson.body.diagnostic.code, "GPH003");
assert.equal(graphInvalidRenamePatchJson.body.operations[0].actual, "bad\nname");
assert.equal(graphInvalidRenamePatchJson.body.saved, null);
writeFileSync(graphUncheckedPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphMainFunctionNode.id}" field="type" expect="Void" value="u32"`,
  "",
].join("\n"));
assert.equal(zero(["graph", "patch", "--out", graphUncheckedPath, graphDumpPath, graphUncheckedPatchPath]).stdout, "program graph patch ok\n");
const graphUncheckedInline = json(["graph", "check", "--json", graphUncheckedPath], { allowFailure: true });
assert.notEqual(graphUncheckedInline.code, 0);
assert.equal(graphUncheckedInline.body.ok, false);
assert.equal(graphUncheckedInline.body.canonicalSource, false);
assert.equal(graphUncheckedInline.body.check.ok, false);
assert.equal(graphUncheckedInline.body.check.phase, "typecheck");
assert.equal(graphUncheckedInline.body.check.lowering, "direct-program-graph");
assert.equal(graphUncheckedInline.body.check.sourcePath, null);
assert.equal(graphUncheckedInline.body.targetReadiness, null);
assert.equal(graphUncheckedInline.body.saved, null);
assert.equal(graphUncheckedInline.body.diagnostics[0].path, graphUncheckedPath);
assert.equal(graphUncheckedInline.body.view, null);
assert.doesNotMatch(JSON.stringify(graphUncheckedInline.body), /<generated-graph-view>|zero-graph-check/);
assert.equal(zero(["graph", "dump", "--out", graphBorrowDumpPath, "conformance/native/pass/borrow-field-independent-assignment.0"]).stdout, "");
const graphBorrowDumpJson = json(["graph", "dump", "--json", "conformance/native/pass/borrow-field-independent-assignment.0"]).body;
assert.equal(graphBorrowDumpJson.graphHash, "graph:ee4cf27e0511a4cb");
const graphBorrowRightFieldAccess = graphBorrowDumpJson.nodes.filter((node) => node.kind === "FieldAccess" && node.name === "right").at(-1);
assert(graphBorrowRightFieldAccess);
writeFileSync(graphBorrowConflictPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphBorrowDumpJson.graphHash}"`,
  `set node="${graphBorrowRightFieldAccess.id}" field="name" expect="right" value="left"`,
  "",
].join("\n"));
assert.equal(zero(["graph", "patch", "--out", graphBorrowConflictPath, graphBorrowDumpPath, graphBorrowConflictPatchPath]).stdout, "program graph patch ok\n");
const graphBorrowConflictInline = json(["graph", "check", "--json", graphBorrowConflictPath], { allowFailure: true });
assert.notEqual(graphBorrowConflictInline.code, 0);
assert.equal(graphBorrowConflictInline.body.diagnostics[0].code, "BOR001");
assert.equal(graphBorrowConflictInline.body.diagnostics[0].path, graphBorrowConflictPath);
assert.equal(graphBorrowConflictInline.body.diagnostics[0].borrowTrace.activeBorrows[0].bindingDecl.path, graphBorrowConflictPath);
assert.equal(graphBorrowConflictInline.body.view, null);
assert.doesNotMatch(JSON.stringify(graphBorrowConflictInline.body), /<generated-graph-view>|zero-graph-check/);
writeFileSync(graphPatchEmptyPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value=""`,
  "",
].join("\n"));
const graphPatchEmpty = json(["graph", "patch", "--json", graphDumpPath, graphPatchEmptyPath]).body;
assert.equal(graphPatchEmpty.ok, true);
assert.equal(graphPatchEmpty.operations[0].value, "");
writeFileSync(graphPatchControlPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="\\u0001"`,
  "",
].join("\n"));
const graphPatchControl = json(["graph", "patch", "--json", graphDumpPath, graphPatchControlPath]).body;
assert.equal(graphPatchControl.ok, true);
assert.equal(graphPatchControl.operations[0].value, "\u0001");
writeFileSync(graphPatchHighBytePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="\\u0080"`,
  "",
].join("\n"));
const graphPatchHighByteStdout = execFileSync("bin/zero", ["graph", "patch", "--json", graphDumpPath, graphPatchHighBytePath], { stdio: ["ignore", "pipe", "pipe"] });
assert.equal(graphPatchHighByteStdout.includes(Buffer.from([0x80])), false);
const graphPatchHighByteText = graphPatchHighByteStdout.toString("utf8");
assert.match(graphPatchHighByteText, /"value": "\\u0080"/);
const graphPatchHighByte = JSON.parse(graphPatchHighByteText);
assert.equal(graphPatchHighByte.ok, true);
assert.equal(graphPatchHighByte.operations[0].value.charCodeAt(0), 0x80);
writeFileSync(graphPatchBadEscapePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="\\q"`,
  "",
].join("\n"));
const graphPatchBadEscape = json(["graph", "patch", "--json", graphDumpPath, graphPatchBadEscapePath], { allowFailure: true });
assert.notEqual(graphPatchBadEscape.code, 0);
assert.equal(graphPatchBadEscape.body.ok, false);
assert.equal(graphPatchBadEscape.body.diagnostic.code, "GPH001");
writeFileSync(graphPatchNullEscapePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="\\u0000x"`,
  "",
].join("\n"));
const graphPatchNullEscape = json(["graph", "patch", "--json", graphDumpPath, graphPatchNullEscapePath], { allowFailure: true });
assert.notEqual(graphPatchNullEscape.code, 0);
assert.equal(graphPatchNullEscape.body.ok, false);
assert.equal(graphPatchNullEscape.body.diagnostic.code, "GPH001");
writeFileSync(graphPatchRawNullPath, Buffer.concat([
  Buffer.from([
    "zero-program-graph-patch v1",
    `expect graphHash "${graphDumpJson.graphHash}"`,
    `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="raw nul prefix"`,
    "",
  ].join("\n")),
  Buffer.from([0]),
  Buffer.from(`set node="${graphHelloLiteralNode.id}" field="value" value="ignored"\n`),
]));
const graphPatchRawNull = json(["graph", "patch", "--json", graphDumpPath, graphPatchRawNullPath], { allowFailure: true });
assert.notEqual(graphPatchRawNull.code, 0);
assert.equal(graphPatchRawNull.body.ok, false);
assert.equal(graphPatchRawNull.body.diagnostic.code, "GPH001");
assert.equal(graphPatchRawNull.body.diagnostic.message, "program graph patch contains NUL byte");
writeFileSync(graphPatchInvalidNamePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphMainFunctionNode.id}" field="name" expect="main" value="main\\npub fn injected Void"`,
  "",
].join("\n"));
const graphPatchInvalidName = json(["graph", "patch", "--json", graphDumpPath, graphPatchInvalidNamePath], { allowFailure: true });
assert.notEqual(graphPatchInvalidName.code, 0);
assert.equal(graphPatchInvalidName.body.ok, false);
assert.equal(graphPatchInvalidName.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidName.body.operations[0].field, "name");
assert.equal(graphPatchInvalidName.body.operations[0].value, "main\npub fn injected Void");
writeFileSync(graphPatchInvalidTypePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphMainFunctionNode.id}" field="type" expect="Void" value="Void\\npub fn injected Void"`,
  "",
].join("\n"));
const graphPatchInvalidType = json(["graph", "patch", "--json", graphDumpPath, graphPatchInvalidTypePath], { allowFailure: true });
assert.notEqual(graphPatchInvalidType.code, 0);
assert.equal(graphPatchInvalidType.body.ok, false);
assert.equal(graphPatchInvalidType.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidType.body.operations[0].field, "type");
assert.equal(graphPatchInvalidType.body.operations[0].value, "Void\npub fn injected Void");
assert.equal(zero(["graph", "dump", "--out", graphPayloadDumpPath, "conformance/check/pass/payload-match.0"]).stdout, "");
const graphPayloadDumpJson = json(["graph", "dump", "--json", "conformance/check/pass/payload-match.0"]).body;
const graphMatchNode = graphPayloadDumpJson.nodes.find((node) => node.kind === "Match");
const graphMatchArmNode = graphPayloadDumpJson.nodes.find((node) => node.kind === "MatchArm" && node.name === "ok");
assert(graphMatchNode);
assert(graphMatchArmNode);
writeFileSync(graphPatchInvalidMatchReplacePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPayloadDumpJson.graphHash}"`,
  `replace node="${graphMatchArmNode.id}" expect="${graphMatchArmNode.nodeHash}" kind="MatchArm" value="payload\\nname"`,
  "",
].join("\n"));
const graphPatchInvalidMatchReplace = json(["graph", "patch", "--json", graphPayloadDumpPath, graphPatchInvalidMatchReplacePath], { allowFailure: true });
assert.notEqual(graphPatchInvalidMatchReplace.code, 0);
assert.equal(graphPatchInvalidMatchReplace.body.ok, false);
assert.equal(graphPatchInvalidMatchReplace.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidMatchReplace.body.diagnostic.message, "patch match payload value must be a Zero identifier path or operator");
assert.equal(graphPatchInvalidMatchReplace.body.operations[0].op, "replace");
assert.equal(graphPatchInvalidMatchReplace.body.operations[0].value, "payload\nname");
assert.equal(graphPatchInvalidMatchReplace.body.saved, null);
writeFileSync(graphPatchInvalidMatchInsertPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPayloadDumpJson.graphHash}"`,
  `insert node="#patch_bad_match_arm" kind="MatchArm" parent="${graphMatchNode.id}" edge="arm" order="2" name="ok" value="payload\\nname"`,
  "",
].join("\n"));
const graphPatchInvalidMatchInsert = json(["graph", "patch", "--json", graphPayloadDumpPath, graphPatchInvalidMatchInsertPath], { allowFailure: true });
assert.notEqual(graphPatchInvalidMatchInsert.code, 0);
assert.equal(graphPatchInvalidMatchInsert.body.ok, false);
assert.equal(graphPatchInvalidMatchInsert.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidMatchInsert.body.diagnostic.message, "patch match payload value must be a Zero identifier path or operator");
assert.equal(graphPatchInvalidMatchInsert.body.operations[0].op, "insert");
assert.equal(graphPatchInvalidMatchInsert.body.operations[0].value, "payload\nname");
assert.equal(graphPatchInvalidMatchInsert.body.saved, null);
const graphWorldParamNode = graphDumpJson.nodes.find((node) => node.kind === "Param" && node.name === "world");
assert(graphWorldParamNode);
writeFileSync(graphPatchReservedParamPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphWorldParamNode.id}" field="name" expect="world" value="pub"`,
  "",
].join("\n"));
assert.equal(zero(["graph", "patch", "--out", graphReservedParamPath, graphDumpPath, graphPatchReservedParamPath]).stdout, "program graph patch ok\n");
const graphReservedParam = json(["graph", "check", "--json", graphReservedParamPath], { allowFailure: true });
assert.notEqual(graphReservedParam.code, 0);
assert.equal(graphReservedParam.body.ok, false);
assert.equal(graphReservedParam.body.check.phase, "lower");
assert.equal(graphReservedParam.body.check.lowering, "direct-program-graph");
assert.equal(graphReservedParam.body.diagnostics[0].message, "program graph parameter name is not valid Zero identifier syntax");
writeFileSync(graphPatchInternalFunctionPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphMainFunctionNode.id}" field="name" expect="main" value="__zero_bad"`,
  "",
].join("\n"));
assert.equal(zero(["graph", "patch", "--out", graphInternalFunctionPath, graphDumpPath, graphPatchInternalFunctionPath]).stdout, "program graph patch ok\n");
const graphInternalFunction = json(["graph", "check", "--json", graphInternalFunctionPath], { allowFailure: true });
assert.notEqual(graphInternalFunction.code, 0);
assert.equal(graphInternalFunction.body.ok, false);
assert.equal(graphInternalFunction.body.check.phase, "lower");
assert.equal(graphInternalFunction.body.check.lowering, "direct-program-graph");
assert.equal(graphInternalFunction.body.diagnostics[0].message, "program graph declaration uses a reserved compiler-internal symbol name");
assert.equal(zero(["graph", "dump", "--out", graphPackageDumpPath, "examples/systems-package"]).stdout, "");
const graphPackageDumpJson = json(["graph", "dump", "--json", "examples/systems-package"]).body;
const graphStatusFunctionNode = graphPackageDumpJson.nodes.find((node) => node.kind === "Function" && node.name === "status");
assert(graphStatusFunctionNode);
writeFileSync(graphPackagePathMismatchPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPackageDumpJson.graphHash}"`,
  `replace node="${graphStatusFunctionNode.id}" expect="${graphStatusFunctionNode.nodeHash}" path="examples/systems-package/src/main.0" public="true"`,
  "",
].join("\n"));
assert.equal(zero(["graph", "patch", "--out", graphPackagePathMismatchPath, graphPackageDumpPath, graphPackagePathMismatchPatchPath]).stdout, "program graph patch ok\n");
assert.equal(zero(["graph", "check", graphPackagePathMismatchPath]).stdout, "program graph check ok\n");
const graphPackagePathMismatchSize = json(["graph", "size", "--json", "--target", "linux-musl-x64", graphPackagePathMismatchPath]).body;
const graphHelpersInterface = graphPackagePathMismatchSize.incrementalInvalidation.interfaceFingerprints.modules.find((item) => item.name === "helpers");
assert(graphHelpersInterface);
assert(graphHelpersInterface.publicSymbols.some((item) => item.name === "status" && item.kind === "function"));
const graphMainInterface = graphPackagePathMismatchSize.incrementalInvalidation.interfaceFingerprints.modules.find((item) => item.name === "main");
assert(graphMainInterface);
assert(!graphMainInterface.publicSymbols.some((item) => item.name === "status"));
const graphImportNode = graphPackageDumpJson.nodes.find((node) => node.kind === "Import");
assert(graphImportNode);
writeFileSync(graphPatchInvalidImportAliasPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPackageDumpJson.graphHash}"`,
  `set node="${graphImportNode.id}" field="value" value="alias\\npub fn injected Void\\n"`,
  "",
].join("\n"));
const graphPatchInvalidImportAlias = json(["graph", "patch", "--json", graphPackageDumpPath, graphPatchInvalidImportAliasPath], { allowFailure: true });
assert.notEqual(graphPatchInvalidImportAlias.code, 0);
assert.equal(graphPatchInvalidImportAlias.body.ok, false);
assert.equal(graphPatchInvalidImportAlias.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidImportAlias.body.diagnostic.message, "patch import alias value must be a Zero identifier");
assert.equal(graphPatchInvalidImportAlias.body.operations[0].field, "value");
assert.equal(graphPatchInvalidImportAlias.body.operations[0].value, "alias\npub fn injected Void\n");
assert.equal(graphPatchInvalidImportAlias.body.saved, null);
writeFileSync(graphPatchInvalidImportNamePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPackageDumpJson.graphHash}"`,
  `set node="${graphImportNode.id}" field="name" value="+"`,
  "",
].join("\n"));
assert.equal(zero(["graph", "patch", "--out", graphInvalidImportNamePath, graphPackageDumpPath, graphPatchInvalidImportNamePath]).stdout, "program graph patch ok\n");
const graphInvalidImportName = json(["graph", "check", "--json", graphInvalidImportNamePath], { allowFailure: true });
assert.notEqual(graphInvalidImportName.code, 0);
assert.equal(graphInvalidImportName.body.ok, false);
assert.equal(graphInvalidImportName.body.check.phase, "lower");
assert.equal(graphInvalidImportName.body.check.lowering, "direct-program-graph");
assert.equal(graphInvalidImportName.body.diagnostics[0].message, "program graph import module is not valid Zero import syntax");
writeFileSync(graphPatchMissingImportPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPackageDumpJson.graphHash}"`,
  `set node="${graphImportNode.id}" field="name" value="missing"`,
  "",
].join("\n"));
assert.equal(zero(["graph", "patch", "--out", graphMissingImportPath, graphPackageDumpPath, graphPatchMissingImportPath]).stdout, "program graph patch ok\n");
const graphMissingImport = json(["graph", "check", "--json", graphMissingImportPath], { allowFailure: true });
assert.notEqual(graphMissingImport.code, 0);
assert.equal(graphMissingImport.body.ok, false);
assert.equal(graphMissingImport.body.check.phase, "lower");
assert.equal(graphMissingImport.body.check.lowering, "direct-program-graph");
assert.equal(graphMissingImport.body.diagnostics[0].message, "program graph import target module is missing");
writeFileSync(graphPatchBadHashPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "graph:0000000000000000"`,
  `set node="${graphHelloLiteralNode.id}" field="value" value="unreachable\\n"`,
  "",
].join("\n"));
const graphPatchBadHash = json(["graph", "patch", "--json", graphDumpPath, graphPatchBadHashPath], { allowFailure: true });
assert.notEqual(graphPatchBadHash.code, 0);
assert.equal(graphPatchBadHash.body.ok, false);
assert.equal(graphPatchBadHash.body.diagnostic.code, "GPH002");
assert.equal(graphPatchBadHash.body.patchedGraphHash, null);
assert.equal(graphPatchBadHash.body.saved, null);
writeFileSync(graphPatchMismatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="not this" value="unreachable\\n"`,
  "",
].join("\n"));
const graphPatchMismatch = json(["graph", "patch", "--json", graphDumpPath, graphPatchMismatchPath], { allowFailure: true });
assert.notEqual(graphPatchMismatch.code, 0);
assert.equal(graphPatchMismatch.body.ok, false);
assert.equal(graphPatchMismatch.body.diagnostic.code, "GPH005");
assert.equal(graphPatchMismatch.body.operations[0].ok, false);
assert.equal(graphPatchMismatch.body.operations[0].actual, "hello from zero\n");
assert.equal(graphPatchMismatch.body.saved, null);
assert.equal(zero(["graph", "roundtrip", "examples/hello.0"]).stdout, "program graph roundtrip ok\n");
const graphRoundtripJson = json(["graph", "roundtrip", "--json", "examples/hello.0"]).body;
assert.equal(graphRoundtripJson.ok, true);
assert.equal(graphRoundtripJson.canonicalSource, false);
assert.equal(graphRoundtripJson.semanticStable, true);
assert.equal(graphRoundtripJson.lowering, "direct-program-graph");
assert.equal(graphRoundtripJson.moduleIdentity, "module:hello");
assert.equal(graphRoundtripJson.roundtripModuleIdentity, "module:hello");
assert.equal(graphRoundtripJson.originalGraphHash, graphDumpJson.graphHash);
assert.equal(graphRoundtripJson.roundtripGraphHash, graphDumpJson.graphHash);
assert.deepEqual(graphRoundtripJson.counts.original, { nodes: 13, edges: 12 });
assert.deepEqual(graphRoundtripJson.counts.roundtrip, { nodes: 13, edges: 12 });
assert.deepEqual(graphRoundtripJson.semanticCounts.original, { nodes: 13, edges: 12 });
assert.deepEqual(graphRoundtripJson.semanticCounts.roundtrip, { nodes: 13, edges: 12 });
assert.equal(graphRoundtripJson.comparison.ok, true);
assert.equal(graphRoundtripJson.saved, null);
assert.equal(graphRoundtripJson.view, graphView);
assert.equal(zero(["graph", "roundtrip", graphDumpPath]).stdout, "program graph roundtrip ok\n");
const graphArtifactRoundtripJson = json(["graph", "roundtrip", "--json", graphDumpPath]).body;
assert.equal(graphArtifactRoundtripJson.ok, true);
assert.equal(graphArtifactRoundtripJson.artifact, graphDumpPath);
assert.equal(graphArtifactRoundtripJson.canonicalSource, false);
assert.equal(graphArtifactRoundtripJson.semanticStable, true);
assert.equal(graphArtifactRoundtripJson.lowering, "direct-program-graph");
assert.equal(graphArtifactRoundtripJson.moduleIdentity, "module:hello");
assert.equal(graphArtifactRoundtripJson.roundtripModuleIdentity, "module:hello");
assert.equal(graphArtifactRoundtripJson.originalGraphHash, graphDumpJson.graphHash);
assert.equal(graphArtifactRoundtripJson.roundtripGraphHash, graphDumpJson.graphHash);
assert.deepEqual(graphArtifactRoundtripJson.semanticCounts.original, graphArtifactRoundtripJson.semanticCounts.roundtrip);
assert.equal(graphArtifactRoundtripJson.comparison.ok, true);
assert.equal(graphArtifactRoundtripJson.view, null);
assert.equal(zero(["graph", "roundtrip", "--out", graphArtifactRoundtripPath, graphDumpPath]).stdout, "program graph roundtrip ok\n");
assert.equal(readFileSync(graphArtifactRoundtripPath, "utf8"), graphDump);
const graphArtifactRoundtripOutJson = json(["graph", "roundtrip", "--json", "--out", graphArtifactRoundtripPath, graphDumpPath]).body;
assert.equal(graphArtifactRoundtripOutJson.ok, true);
assert.equal(graphArtifactRoundtripOutJson.saved.path, graphArtifactRoundtripPath);
assert.equal(graphArtifactRoundtripOutJson.saved.kind, "program-graph");
assert.equal(graphArtifactRoundtripOutJson.view, null);
assert.equal(readFileSync(graphArtifactRoundtripPath, "utf8"), graphDump);
const graphArtifactRoundtripSourceTextJson = json(["graph", "roundtrip", "--json", "--out", graphArtifactRoundtripSourceTextPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphArtifactRoundtripSourceTextJson.code, 0);
assert.equal(graphArtifactRoundtripSourceTextJson.body.diagnostics[0].message, "program graph output must not use source text extension");
assert.equal(graphArtifactRoundtripSourceTextJson.body.diagnostics[0].expected, "zero graph roundtrip --out <program-graph-artifact> <program-graph-artifact>");
assert.equal(existsSync(graphArtifactRoundtripSourceTextPath), false);
assert.equal(zero(["graph", "roundtrip", "--out", graphSourceRoundtripPath, "examples/hello.0"]).stdout, "program graph roundtrip ok\n");
assert.equal(readFileSync(graphSourceRoundtripPath, "utf8"), graphDump);
const graphRoundtripOutJson = json(["graph", "roundtrip", "--json", "--out", graphSourceRoundtripPath, "examples/hello.0"]).body;
assert.equal(graphRoundtripOutJson.ok, true);
assert.equal(graphRoundtripOutJson.saved.path, graphSourceRoundtripPath);
assert.equal(graphRoundtripOutJson.saved.kind, "program-graph");
assert.equal(graphRoundtripOutJson.view, null);
assert.equal(readFileSync(graphSourceRoundtripPath, "utf8"), graphDump);
const graphRoundtripSourceTextOutJson = json(["graph", "roundtrip", "--json", "--out", graphSourceRoundtripSourceTextPath, "examples/hello.0"], { allowFailure: true });
assert.notEqual(graphRoundtripSourceTextOutJson.code, 0);
assert.equal(graphRoundtripSourceTextOutJson.body.diagnostics[0].message, "program graph output must not use source text extension");
assert.equal(graphRoundtripSourceTextOutJson.body.diagnostics[0].expected, "zero graph roundtrip --out <program-graph-artifact> <input>");
assert.equal(existsSync(graphSourceRoundtripSourceTextPath), false);
const graphPackageRoundtripJson = json(["graph", "roundtrip", "--json", "examples/systems-package"]).body;
assert.equal(graphPackageRoundtripJson.ok, true);
assert.equal(graphPackageRoundtripJson.semanticStable, true);
assert.equal(graphPackageRoundtripJson.moduleIdentity, "package:systems-package@0.1.0");
assert.equal(graphPackageRoundtripJson.roundtripModuleIdentity, graphPackageRoundtripJson.moduleIdentity);
assert.equal(graphPackageRoundtripJson.comparison.ok, true);
assert.deepEqual(graphPackageRoundtripJson.semanticCounts.original, graphPackageRoundtripJson.semanticCounts.roundtrip);
const graphTestBlockRoundtripJson = json(["graph", "roundtrip", "--json", "conformance/native/pass/test-blocks.0"]).body;
assert.equal(graphTestBlockRoundtripJson.ok, true);
assert.equal(graphTestBlockRoundtripJson.semanticStable, true);
assert.equal(graphTestBlockRoundtripJson.roundtripModuleIdentity, graphTestBlockRoundtripJson.moduleIdentity);
assert.equal(graphTestBlockRoundtripJson.comparison.ok, true);
let sparseOrderGraph = graphDump.replace(`edge ${graphModuleNode.id} function ${graphMainFunctionNode.id} order:0`, `edge ${graphModuleNode.id} function ${graphMainFunctionNode.id} order:1000000000000`);
sparseOrderGraph = sparseOrderGraph.replace(/hash "graph:[0-9a-f]{16}"/, `hash "${recomputeGraphHash(sparseOrderGraph)}"`);
writeFileSync(graphSparseOrderPath, sparseOrderGraph);
assert.equal(zero(["graph", "validate", graphSparseOrderPath]).stdout, "program graph ok\n");
const sparseOrderView = execFileSync("bin/zero", ["graph", "view", graphSparseOrderPath], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], timeout: 1000 });
assert.match(sparseOrderView, /pub fn main Void world World !/);
let sparseArgGraph = graphDump.replace(/edge (#[^ ]+) arg (#[^ ]+) order:0/, "edge $1 arg $2 order:1000000000000");
sparseArgGraph = sparseArgGraph.replace(/hash "graph:[0-9a-f]{16}"/, `hash "${recomputeGraphHash(sparseArgGraph)}"`);
writeFileSync(graphSparseArgPath, sparseArgGraph);
assert.equal(zero(["graph", "validate", graphSparseArgPath]).stdout, "program graph ok\n");
const sparseArgView = zero(["graph", "view", graphSparseArgPath]).stdout;
assert.match(sparseArgView, /check world\.out\.write "hello from zero\\n"/);
const graphWrongSchemaPath = join(outDir, "wrong-schema.program-graph");
writeFileSync(graphWrongSchemaPath, "zero-graph v2\n");
const graphWrongSchema = json(["graph", "validate", "--json", graphWrongSchemaPath], { allowFailure: true });
assert(graphWrongSchema.code);
assert.equal(graphWrongSchema.body.diagnostics[0].message, "unknown program graph schema version");
const graphFailedArtifactPath = join(outDir, "failed-validation.program-graph");
writeFileSync(graphFailedArtifactPath, [
  "zero-graph v1",
  "origin source-text",
  "module \"main\"",
  "hash \"\"",
  "validation \"decoded\" failed",
  "diagnostic code:\"GRF001\" message:\"program graph construction failed\"",
  "",
].join("\n"));
const graphFailedArtifact = json(["graph", "validate", "--json", graphFailedArtifactPath], { allowFailure: true });
assert(graphFailedArtifact.code);
assert.equal(graphFailedArtifact.body.diagnostics[0].message, "program graph input reports failed validation");
const graphTrailingArtifactPath = join(outDir, "trailing-content.program-graph");
writeFileSync(graphTrailingArtifactPath, `${graphDump}\nextra\n`);
const graphTrailingArtifact = json(["graph", "validate", "--json", graphTrailingArtifactPath], { allowFailure: true });
assert(graphTrailingArtifact.code);
assert.equal(graphTrailingArtifact.body.diagnostics[0].message, "unexpected content after graph header");

const skillsList = json(["skills", "list", "--json"]).body;
assert.equal(skillsList.success, true);
assert(skillsList.data.some((skill) => skill.name === "zero" && /Zero/.test(skill.description)));
const skillNames = new Set(skillsList.data.map((skill) => skill.name));
for (const name of [
  "zero",
  "agent",
  "builds",
  "diagnostics",
  "graph",
  "language",
  "packages",
  "stdlib",
  "testing",
]) {
  assert(skillNames.has(name), `missing bundled skill ${name}`);
}

const zeroSkill = json(["skills", "get", "zero", "--full", "--json"]).body;
assert.equal(zeroSkill.success, true);
assert.match(zeroSkill.data[0].content, /# Zero/);
assert.match(zeroSkill.data[0].content, /zero skills get zero --full/);
assert.equal(zeroSkill.data[0].files, undefined);

const languageSkill = json(["skills", "get", "language", "--json"]).body;
assert.equal(languageSkill.success, true);
assert.match(languageSkill.data[0].content, /# zerolang Language/);
assert.match(languageSkill.data[0].content, /pub fn main/);

const graphSkill = json(["skills", "get", "graph", "--json"]).body;
assert.equal(graphSkill.success, true);
assert.match(graphSkill.data[0].content, /# Zero Graph Authoring/);
assert.match(graphSkill.data[0].content, /primary agent authoring surface/);

const stdlibSkill = json(["skills", "get", "stdlib", "--json"]).body;
assert.equal(stdlibSkill.success, true);
assert.match(stdlibSkill.data[0].content, /std\.str/);
assert.match(stdlibSkill.data[0].content, /non-overlapping reverse/);

const diagnosticSkill = json(["skills", "get", "diagnostics", "--json"]).body;
assert.equal(diagnosticSkill.success, true);
assert.match(diagnosticSkill.data[0].content, /fixSafety/);

const missingSkill = zero(["skills", "get", "missing", "--json"], { allowFailure: true });
assert.notEqual(missingSkill.code, 0);
assert.equal(JSON.parse(missingSkill.stdout).success, false);

const removedSkillsPath = zero(["skills", "path", "zero", "--json"], { allowFailure: true });
assert.notEqual(removedSkillsPath.code, 0);
assert.match(JSON.parse(removedSkillsPath.stdout).error, /Unknown skills subcommand: path/);

const badSkillsFlag = zero(["skills", "-x"], { allowFailure: true });
assert.notEqual(badSkillsFlag.code, 0);
assert.match(badSkillsFlag.stderr, /Unknown skills flag: -x/);

const badSkillsListFlag = zero(["skills", "list", "--unknown", "--json"], { allowFailure: true });
assert.notEqual(badSkillsListFlag.code, 0);
assert.match(JSON.parse(badSkillsListFlag.stdout).error, /Unknown skills flag: --unknown/);

const badSkillsGetFlag = zero(["skills", "get", "language", "--unknown", "--json"], { allowFailure: true });
assert.notEqual(badSkillsGetFlag.code, 0);
assert.match(JSON.parse(badSkillsGetFlag.stdout).error, /Unknown skills flag: --unknown/);

const lexerTokens = json(["tokens", "--json", "conformance/lexer/compiler-smoke.0"]).body;
assert.equal(lexerTokens.schemaVersion, 1);
assert.match(lexerTokens.sourceFile, /compiler-smoke\.0$/);
assert.deepEqual(lexerTokens.tokens.slice(0, 4).map((token) => `${token.kind}:${token.text}`), [
  "word:use",
  "word:std",
  "symbol:.",
  "word:mem",
]);
assert.deepEqual(lexerTokens.tokens.filter((token) => token.kind === "number").map((token) => token.text), ["123", "0xff", "0b101", "42_u8"]);
assert.deepEqual(lexerTokens.tokens.filter((token) => token.kind === "string" || token.kind === "char").map((token) => `${token.kind}:${token.text}`), [
  "string:hi",
  "char:120",
]);
assert.equal(lexerTokens.tokens[0].line, 1);
assert.equal(lexerTokens.tokens[0].column, 1);
assert.equal(lexerTokens.tokens[0].offset, 0);
assert.equal(lexerTokens.tokens[0].length, 3);
assert.equal(lexerTokens.tokens[5].offset, 12);
assert.equal(lexerTokens.tokens[5].length, 3);
assert.equal(lexerTokens.tokens[7].kind, "word");
assert.equal(lexerTokens.tokens[7].text, "main");
assert.equal(lexerTokens.tokens[7].line, 2);
assert.equal(lexerTokens.tokens[7].column, 8);
assert.equal(lexerTokens.tokens.at(-1).kind, "eof");
assert.equal(lexerTokens.tokens.at(-1).length, 0);

const parseTree = json(["parse", "--json", "conformance/parse/compiler-smoke.0"]).body;
assert.equal(parseTree.schemaVersion, 1);
assert.equal(parseTree.root.kind, "module");
assert.equal(parseTree.root.shapeCount, 1);
assert.equal(parseTree.root.enumCount, 1);
assert.equal(parseTree.root.choiceCount, 1);
assert.equal(parseTree.root.functionCount, 1);
assert.equal(parseTree.shapes[0].name, "Point");
assert.equal(parseTree.enums[0].caseCount, 2);
assert.equal(parseTree.choices[0].caseCount, 2);
assert.equal(parseTree.functions[0].name, "main");
assert.equal(parseTree.functions[0].paramCount, 1);
assert.deepEqual(parseTree.functions[0].bodyKinds, ["if", "while", "check", "return"]);

const rowCommandFixture = join(outDir, "row_command.row");
const rowCommandSource =
  "# command fixture\n" +
  "fn inc i32 value i32\n" +
  "  ret + value 1\n" +
  "\n" +
  "pub fn main Void\n" +
  "  let total i32 inc 41\n";
writeFileSync(rowCommandFixture, rowCommandSource);
assert.equal(zero(["fmt", rowCommandFixture]).stdout, rowCommandSource);
assert.match(zero(["fmt", "--check", rowCommandFixture]).stdout, /fmt ok/);
const rowTokens = json(["tokens", "--json", rowCommandFixture]).body;
assert.equal(rowTokens.schemaVersion, 1);
assert.equal(rowTokens.syntax, "row");
assert.deepEqual(rowTokens.tokens.slice(0, 4).map((token) => `${token.kind}:${token.text}`), [
  "comment:# command fixture",
  "newline:",
  "word:fn",
  "word:inc",
]);
const rowParseTree = json(["parse", "--json", rowCommandFixture]).body;
assert.equal(rowParseTree.schemaVersion, 1);
assert.equal(rowParseTree.root.functionCount, 2);
assert.deepEqual(rowParseTree.functions.map((fun) => fun.name), ["inc", "main"]);
assert.deepEqual(rowParseTree.functions.map((fun) => fun.bodyKinds), [["return"], ["let"]]);
const rowCheck = json(["check", "--json", rowCommandFixture]).body;
assert.equal(rowCheck.ok, true);
assert.equal(rowCheck.sourceFile, rowCommandFixture);
assert(rowCheck.interfaceFingerprints.modules.some((module) => module.name === "row_command" && module.publicSymbols.some((symbol) => symbol.name === "main")));
const rowGraph = json(["graph", "--json", rowCommandFixture]).body;
assert.equal(rowGraph.schemaVersion, 1);
assert.equal(rowGraph.sourceFile, rowCommandFixture);
assert(rowGraph.sourceFiles.includes(rowCommandFixture));
assert(rowGraph.symbols.some((symbol) => symbol.name === "inc" && symbol.kind === "function"));
assert(rowGraph.functions.some((fun) => fun.name === "main"));

const rowTestFixture = join(outDir, "row_test_command.row");
writeFileSync(
  rowTestFixture,
  "fn add i32 left i32 right i32\n" +
    "  ret + left right\n" +
    "\n" +
    "test \"row addition\"\n" +
    "  expect == (add 20 22) 42\n" +
    "\n" +
    "test \"xfail: row pending\"\n" +
    "  expect false\n" +
    "\n" +
    "pub fn main Void\n",
);
const rowTestJson = json(["test", "--json", rowTestFixture]).body;
assert.equal(rowTestJson.ok, true);
assert.equal(rowTestJson.testBackend, "direct-frontend");
assert.equal(rowTestJson.discoveredTests, 2);
assert.equal(rowTestJson.expectedFailures, 1);
assert(rowTestJson.results.some((result) => result.name === "row addition" && result.status === "passed"));
assert(rowTestJson.results.some((result) => result.name === "xfail: row pending" && result.status === "expected-fail"));
const rowTestSizePath = join(outDir, "row-test-sized");
rmSync(rowTestSizePath, { force: true });
const rowTestSize = json(["size", "--json", "--target", "linux-musl-x64", rowTestFixture, "--out", rowTestSizePath]).body;
assert.equal(rowTestSize.sourceFile, rowTestFixture);
assert.equal(rowTestSize.generatedCBytes, 0);
assert(rowTestSize.retentionReasons.some((item) => item.name === "row addition" && item.reason === "zero test discovery"));
const rowTestDev = json(["dev", "--json", "--trace", rowTestFixture]).body;
assert.equal(rowTestDev.trace.requested, true);
assert(rowTestDev.watch.files.includes(rowTestFixture));
assert.equal(rowTestDev.affected.tests, 2);

const rowHelperFixture = join(outDir, "row_helper.row");
writeFileSync(
  rowHelperFixture,
  "pub fn double i32 value i32\n" +
    "  ret + value value\n",
);
const rowImportFixture = join(outDir, "row_import_main.row");
writeFileSync(
  rowImportFixture,
  "use row_helper\n" +
    "pub fn main Void\n" +
    "  let total i32 double 21\n",
);
const rowImportCheck = json(["check", "--json", rowImportFixture]).body;
assert.equal(rowImportCheck.ok, true);
assert(rowImportCheck.incrementalInvalidation.changedInputs.sourceFiles.includes(rowHelperFixture));
const rowImportGraph = json(["graph", "--json", rowImportFixture]).body;
assert(rowImportGraph.sourceFiles.includes(rowImportFixture));
assert(rowImportGraph.sourceFiles.includes(rowHelperFixture));
assert(rowImportGraph.imports.includes("row_helper"));
assert(rowImportGraph.importEdges.some((edge) => edge.from === "row_import_main" && edge.to === "row_helper" && edge.path === rowHelperFixture));
assert(rowImportGraph.functions.some((fun) => fun.name === "double"));

const rowPackage = join(outDir, "row-package");
mkdirSync(join(rowPackage, "src"), { recursive: true });
writeFileSync(
  join(rowPackage, "zero.json"),
  JSON.stringify(
    {
      package: { name: "row-package", version: "0.1.0" },
      targets: { cli: { kind: "exe", main: "src/main.row" } },
    },
    null,
    2,
  ) + "\n",
);
const rowPackageMain = join(rowPackage, "src", "main.row");
const rowPackageHelper = join(rowPackage, "src", "helper.row");
writeFileSync(
  rowPackageHelper,
  "pub fn double i32 value i32\n" +
    "  ret + value value\n",
);
writeFileSync(
  rowPackageMain,
  "use helper\n" +
    "test \"package helper\"\n" +
    "  expect == (double 21) 42\n" +
    "pub fn main Void\n" +
    "  let total i32 double 21\n",
);
assert.match(zero(["fmt", "--check", rowPackage]).stdout, /fmt ok/);
const rowPackageTokens = json(["tokens", "--json", rowPackage]).body;
assert.equal(rowPackageTokens.syntax, "row");
assert.equal(rowPackageTokens.sourceFile, rowPackageMain);
const rowPackageParse = json(["parse", "--json", rowPackage]).body;
assert.equal(rowPackageParse.root.functionCount, 3);
assert(rowPackageParse.functions.some((fun) => fun.name === "double"));
const rowPackageCheck = json(["check", "--json", rowPackage]).body;
assert.equal(rowPackageCheck.ok, true);
assert.equal(rowPackageCheck.package.name, "row-package");
assert(rowPackageCheck.incrementalInvalidation.changedInputs.sourceFiles.includes(rowPackageHelper));
const rowPackageGraph = json(["graph", "--json", rowPackage]).body;
assert(rowPackageGraph.sourceFiles.includes(rowPackageMain));
assert(rowPackageGraph.sourceFiles.includes(rowPackageHelper));
assert(rowPackageGraph.importEdges.some((edge) => edge.from === "main" && edge.to === "helper" && edge.path === rowPackageHelper));
assert(rowPackageGraph.functions.some((fun) => fun.name === "double"));
const rowPackageTest = json(["test", "--json", rowPackage]).body;
assert.equal(rowPackageTest.ok, true);
assert.equal(rowPackageTest.testDiscovery.mode, "package");
assert.equal(rowPackageTest.discoveredTests, 1);
assert(rowPackageTest.fixtures.sourceFiles.includes(rowPackageHelper));
assert.equal(rowPackageTest.results[0].status, "passed");

const rowArtifactDir = join(outDir, "row-artifact");
const currentArtifactDir = join(outDir, "current-artifact");
rmSync(rowArtifactDir, { recursive: true, force: true });
rmSync(currentArtifactDir, { recursive: true, force: true });
mkdirSync(rowArtifactDir, { recursive: true });
mkdirSync(currentArtifactDir, { recursive: true });
const rowArtifactMain = join(rowArtifactDir, "main.row");
const rowArtifactHelper = join(rowArtifactDir, "helper.row");
const currentArtifactMain = join(currentArtifactDir, "main.0");
const currentArtifactHelper = join(currentArtifactDir, "helper.0");
writeFileSync(
  rowArtifactHelper,
  "pub fn plusOne i32 value i32\n" +
    "  ret + value 1\n",
);
writeFileSync(
  rowArtifactMain,
  "use helper\n" +
    "export c fn main i32\n" +
    "  ret plusOne 41\n",
);
writeFileSync(
  currentArtifactHelper,
  "pub fn plusOne i32 value i32\n" +
    "  ret + value 1\n",
);
writeFileSync(
  currentArtifactMain,
  "use helper\n" +
    "export c fn main i32\n" +
    "  ret plusOne 41\n",
);
const rowArtifactGraph = json(["graph", "--json", rowArtifactMain]).body;
const currentArtifactGraph = json(["graph", "--json", currentArtifactMain]).body;
const functionSummary = (fun) => ({
  name: fun.name,
  public: fun.public,
  params: fun.params,
  returnType: fun.returnType,
  raises: fun.raises,
});
const publicSymbolSummary = (symbol) => ({
  name: symbol.name,
  kind: symbol.kind,
  public: symbol.public,
});
assert.deepEqual(rowArtifactGraph.functions.map(functionSummary), currentArtifactGraph.functions.map(functionSummary));
assert.deepEqual(
  rowArtifactGraph.symbols.filter((symbol) => symbol.public).map(publicSymbolSummary),
  currentArtifactGraph.symbols.filter((symbol) => symbol.public).map(publicSymbolSummary),
);
assert.deepEqual(rowArtifactGraph.imports, ["helper"]);
assert.deepEqual(currentArtifactGraph.imports, ["helper"]);
assert(rowArtifactGraph.sourceFiles.includes(rowArtifactMain));
assert(rowArtifactGraph.sourceFiles.includes(rowArtifactHelper));
assert.equal(rowArtifactGraph.sourceFiles.length, currentArtifactGraph.sourceFiles.length);

const rowArtifactBuildOut = join(outDir, "row-artifact-build");
rmSync(rowArtifactBuildOut, { force: true });
const rowArtifactBuild = json(["build", "--json", "--target", "linux-musl-x64", rowArtifactMain, "--out", rowArtifactBuildOut]).body;
assert.equal(rowArtifactBuild.sourceFile, rowArtifactMain);
assert.equal(rowArtifactBuild.generatedCBytes, 0);
assert.equal(rowArtifactBuild.cBridgeFallback ?? false, false);
assert.equal(rowArtifactBuild.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(rowArtifactBuild.objectBackend.linking.externalToolchain, "none");
assert.equal(rowArtifactBuild.objectBackend.directFacts.functionCount, 2);
assert(rowArtifactBuild.incrementalInvalidation.changedInputs.sourceFiles.includes(rowArtifactHelper));
assert(rowArtifactBuild.incrementalInvalidation.changedInputs.sourceFiles.includes(rowArtifactMain));
assertReleaseTargetContract(rowArtifactBuild, {
  target: "linux-musl-x64",
  emit: "exe",
  objectFormat: "elf",
  artifactKind: "native-executable",
  linkerFlavor: "elf64",
  targetLibcMode: "bundled-libc",
});
repeatBuildHash(
  ["build", "--json", "--target", "linux-musl-x64", rowArtifactMain, "--out", rowArtifactBuildOut],
  rowArtifactBuildOut,
  `${rowArtifactBuildOut}.repeat`,
);

const rowArtifactObjOut = join(outDir, "row-artifact.o");
rmSync(rowArtifactObjOut, { force: true });
const rowArtifactObj = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", rowArtifactMain, "--out", rowArtifactObjOut]).body;
const rowArtifactObjBytes = readFileSync(rowArtifactObjOut);
assert.equal(rowArtifactObj.sourceFile, rowArtifactMain);
assert.equal(rowArtifactObj.emit, "obj");
assert.equal(rowArtifactObj.generatedCBytes, 0);
assert.equal(rowArtifactObj.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(rowArtifactObj.objectBackend.linking.externalToolchain, "none");
assert.equal(rowArtifactObjBytes[0], 0x7f);
assert.equal(rowArtifactObjBytes[1], 0x45);
assert.equal(rowArtifactObjBytes[2], 0x4c);
assert.equal(rowArtifactObjBytes[3], 0x46);

const rowRunArtifactOut = join(outDir, "row-run-artifact");
rmSync(rowRunArtifactOut, { force: true });
rmSync(`${rowRunArtifactOut}.exe`, { force: true });
rmSync(`${rowRunArtifactOut}.c`, { force: true });
const rowRunResult = zero(["run", "--out", rowRunArtifactOut, rowArtifactMain], { allowFailure: true });
assert.equal(rowRunResult.code, 42);
assert.equal(rowRunResult.stdout, "");
assert(existsSync(version.host.startsWith("win32") ? `${rowRunArtifactOut}.exe` : rowRunArtifactOut));
assert.equal(existsSync(`${rowRunArtifactOut}.c`), false);

const rowShipOut = join(outDir, "row-ship-artifact");
const firstRowShip = json(["ship", "--json", "--target", "linux-musl-x64", rowArtifactMain, "--out", rowShipOut]).body;
assert.equal(firstRowShip.sourceFile, rowArtifactMain);
assertShipReport(firstRowShip, rowShipOut);
const secondRowShip = json(["ship", "--json", "--target", "linux-musl-x64", rowArtifactMain, "--out", rowShipOut]).body;
assertShipReport(secondRowShip, rowShipOut);
assert.equal(secondRowShip.checksum.value, firstRowShip.checksum.value);
assert.equal(secondRowShip.artifactBytes, firstRowShip.artifactBytes);

const rowArtifactMem = json(["mem", "--json", rowArtifactMain]).body;
assert.equal(rowArtifactMem.sourceFile, rowArtifactMain);
assert.equal(rowArtifactMem.generatedCBytes, 0);
assert.equal(rowArtifactMem.cBridgeFallback, false);
assert.equal(rowArtifactMem.directFacts.functionCount, 2);
assert.equal(rowArtifactMem.memory.hiddenHeapAllocation, false);
assert(rowArtifactMem.incrementalInvalidation.changedInputs.sourceFiles.includes(rowArtifactHelper));
const rowArtifactTime = json(["time", "--json", rowArtifactMain]).body;
assert.equal(rowArtifactTime.sourceFile, rowArtifactMain);
assert.equal(rowArtifactTime.generatedCBytes, 0);
assert.equal(rowArtifactTime.cBridgeFallback, false);
assert(rowArtifactTime.compilerPhases.some((phase) => phase.name === "parse"));
assert(rowArtifactTime.compilerPhases.some((phase) => phase.name === "link"));
assert(rowArtifactTime.incrementalInvalidation.changedInputs.sourceFiles.includes(rowArtifactHelper));
const rowArtifactAbiDump = json(["abi", "dump", "--json", rowArtifactMain]).body;
assert.equal(rowArtifactAbiDump.sourceFile, rowArtifactMain);
assert(rowArtifactAbiDump.cExports.some((item) => item.name === "main" && item.cReturnType === "int32_t"));
assert.match(rowArtifactAbiDump.generatedHeader.text, /int32_t main\(void\);/);
const rowArtifactAbiCheck = json(["abi", "check", "--json", rowArtifactMain]).body;
assert.equal(rowArtifactAbiCheck.ok, true);
assert.equal(rowArtifactAbiCheck.sourceFile, rowArtifactMain);
assert.deepEqual(rowArtifactAbiCheck.diagnostics, []);

const rowMissingMainPackage = join(outDir, "row-missing-main-package");
mkdirSync(join(rowMissingMainPackage, "src"), { recursive: true });
writeFileSync(
  join(rowMissingMainPackage, "zero.json"),
  JSON.stringify(
    {
      package: { name: "row-missing-main-package", version: "0.1.0" },
      targets: { cli: { kind: "exe", main: "src/missing.row" } },
    },
    null,
    2,
  ) + "\n",
);
const rowMissingMainCheck = json(["check", "--json", rowMissingMainPackage], { allowFailure: true });
assert.notEqual(rowMissingMainCheck.code, 0);
assert.equal(rowMissingMainCheck.body.diagnostics[0].code, "BLD002");
assert.match(rowMissingMainCheck.body.diagnostics[0].message, /target main source does not exist/);

const rowDependencyLib = join(outDir, "row-dependency-lib");
mkdirSync(join(rowDependencyLib, "src"), { recursive: true });
writeFileSync(
  join(rowDependencyLib, "zero.json"),
  JSON.stringify(
    {
      package: { name: "row-dependency-lib", version: "0.1.0" },
      targets: { cli: { kind: "exe", main: "src/main.row" } },
    },
    null,
    2,
  ) + "\n",
);
writeFileSync(join(rowDependencyLib, "src", "main.row"), "pub fn main Void\n");

const rowDependencyPackage = join(outDir, "row-dependency-package");
mkdirSync(join(rowDependencyPackage, "src"), { recursive: true });
writeFileSync(
  join(rowDependencyPackage, "zero.json"),
  JSON.stringify(
    {
      package: { name: "row-dependency-package", version: "0.1.0" },
      targets: { cli: { kind: "exe", main: "src/main.row" } },
      dependencies: { helper: { path: "../row-dependency-lib", version: "0.1.0" } },
    },
    null,
    2,
  ) + "\n",
);
writeFileSync(join(rowDependencyPackage, "src", "main.row"), "pub fn main Void\n");
const rowDependencyCheck = json(["check", "--json", rowDependencyPackage]).body;
assert.equal(rowDependencyCheck.ok, true);
assert(rowDependencyCheck.package.dependencies.some((dep) => dep.name === "helper" && dep.resolvedName === "row-dependency-lib" && dep.status === "path-resolved"));
assert(rowDependencyCheck.package.lockfile.generated);

const rowMalformedImportFixture = join(outDir, "row_malformed_import.row");
writeFileSync(
  rowMalformedImportFixture,
  "use row helper\n" +
    "pub fn main Void\n",
);
const rowMalformedImportCheck = json(["check", "--json", rowMalformedImportFixture], { allowFailure: true });
assert.notEqual(rowMalformedImportCheck.code, 0);
assert.equal(rowMalformedImportCheck.body.diagnostics[0].code, "PAR100");
assert.match(rowMalformedImportCheck.body.diagnostics[0].message, /expected '\.' between import module segments/);

const rowTypeMismatchFixture = join(outDir, "row_type_mismatch.row");
writeFileSync(
  rowTypeMismatchFixture,
  "pub fn main Void\n" +
    "  let value i32 \"nope\"\n",
);
const rowFixPlan = json(["fix", "--plan", "--json", rowTypeMismatchFixture]).body;
assert.equal(rowFixPlan.ok, false);
assert.equal(rowFixPlan.mode, "plan");
assert.equal(rowFixPlan.diagnostics[0].code, "TYP002");
assert.equal(rowFixPlan.diagnostics[0].path, rowTypeMismatchFixture);
assert.equal(rowFixPlan.diagnostics[0].line, 2);
assert.equal(rowFixPlan.fixes[0].diagnosticCode, "TYP002");

const rowCycleAFixture = join(outDir, "row_cycle_a.row");
const rowCycleBFixture = join(outDir, "row_cycle_b.row");
writeFileSync(
  rowCycleAFixture,
  "use row_cycle_b\n" +
    "pub fn a Void\n",
);
writeFileSync(
  rowCycleBFixture,
  "use row_cycle_a\n" +
    "pub fn b Void\n",
);
const rowCycleCheck = json(["check", "--json", rowCycleAFixture], { allowFailure: true });
assert.notEqual(rowCycleCheck.code, 0);
assert.equal(rowCycleCheck.body.diagnostics[0].code, "IMP002");
assert.equal(rowCycleCheck.body.diagnostics[0].path, rowCycleBFixture);
assert.equal(rowCycleCheck.body.diagnostics[0].actual, "row_cycle_a -> row_cycle_b -> row_cycle_a");

const rowDupMainFixture = join(outDir, "row_dup_main.row");
const rowDupAFixture = join(outDir, "row_dup_a.row");
const rowDupBFixture = join(outDir, "row_dup_b.row");
writeFileSync(
  rowDupMainFixture,
  "use row_dup_a\n" +
    "use row_dup_b\n" +
    "pub fn main Void\n",
);
writeFileSync(rowDupAFixture, "pub fn shared Void\n");
writeFileSync(rowDupBFixture, "pub fn shared Void\n");
const rowDuplicatePublicCheck = json(["check", "--json", rowDupMainFixture], { allowFailure: true });
assert.notEqual(rowDuplicatePublicCheck.code, 0);
assert.equal(rowDuplicatePublicCheck.body.diagnostics[0].code, "IMP003");
assert.match(rowDuplicatePublicCheck.body.diagnostics[0].message, /duplicate public symbol 'shared'/);
assert.match(rowDuplicatePublicCheck.body.diagnostics[0].actual, /shared also exported by row_dup_a/);

const rowMetadataFixture = join(outDir, "row_metadata.row");
const rowMetadataSource =
  "pub enum Mode\n" +
  "  auto\n" +
  "  manual\n" +
  "pub choice Result\n" +
  "  ok i32\n" +
  "  err String\n" +
  "test \"metadata\"\n" +
  "  let total i32 + 1 1\n" +
  "pub fn main Void\n";
writeFileSync(rowMetadataFixture, rowMetadataSource);
const rowMetadataGraph = json(["graph", "--json", rowMetadataFixture]).body;
assert(rowMetadataGraph.symbols.some((symbol) => symbol.name === "Mode" && symbol.kind === "enum" && symbol.public === true));
assert(rowMetadataGraph.symbols.some((symbol) => symbol.name === "Result" && symbol.kind === "choice" && symbol.public === true));
assert(!rowMetadataGraph.symbols.some((symbol) => symbol.name.startsWith("__zero_test_")));
const rowMetadataDoc = json(["doc", "--json", rowMetadataFixture]).body;
assert(rowMetadataDoc.symbols.some((symbol) => symbol.name === "Mode" && symbol.kind === "enum"));
assert(rowMetadataDoc.symbols.some((symbol) => symbol.name === "Result" && symbol.kind === "choice"));

const testJson = json(["test", "--json", "--filter", "addition", "conformance/native/pass/test-blocks.0"]).body;
assert.equal(testJson.schemaVersion, 1);
assert.equal(testJson.ok, true);
assert.match(testJson.stdout, /1 test\(s\) ok/);
assert.equal(testJson.testBackend, "direct-frontend");
assert.equal(testJson.testDiscovery.filter, "addition");
assert.equal(testJson.fixtures.snapshotKey, "zero-test-direct-frontend-v1");
assert.equal(testJson.targetFacts.capabilitySupport.status, "supported");
assert.equal(testJson.results[0].status, "passed");

const packageTestJson = json(["test", "--json", "conformance/packages/test-app"]).body;
assert.equal(packageTestJson.ok, true);
assert.equal(packageTestJson.testDiscovery.mode, "package");
assert.equal(packageTestJson.discoveredTests, 3);
assert.equal(packageTestJson.expectedFailures, 1);
assert(packageTestJson.fixtures.sourceFiles.some((path) => path.endsWith("helper.0")));

const expectedFailTestJson = json(["test", "--json", "conformance/native/pass/test-expected-fail.0"]).body;
assert.equal(expectedFailTestJson.ok, true);
assert.equal(expectedFailTestJson.expectedFailures, 1);
assert.equal(expectedFailTestJson.results[0].status, "expected-fail");

assert.match(zero(["fmt", "--check", "conformance/native/pass/test-blocks.0"]).stdout, /fmt ok/);

const unknownFlag = zero(["check", "--jsoon", "examples/hello.0"], { allowFailure: true });
assert.notEqual(unknownFlag.code, 0);
assert.match(unknownFlag.stderr, /unknown flag: --jsoon/);
assert.match(unknownFlag.stderr, /--json/);
assert.equal(hasAnsiControlBytes(unknownFlag.stderr), false);

const cleanProbe = join(".zero", "out", "contract-clean", "tmp.txt");
mkdirSync(join(".zero", "out", "contract-clean"), { recursive: true });
writeFileSync(cleanProbe, "tmp");
assert(existsSync(cleanProbe));
const clean = zero(["clean"]).stdout;
assert.match(clean, /removed:/);
assert(!existsSync(cleanProbe));

for (const kind of ["cli", "lib", "package"]) {
  const project = join(outDir, `new-${kind}`);
  rmSync(project, { recursive: true, force: true });
  const created = zero(["new", kind, project]).stdout;
  assert.match(created, new RegExp(`created ${kind} project`));
  const manifest = JSON.parse(readFileSync(join(project, "zero.json"), "utf8"));
  const readme = readFileSync(join(project, "README.md"), "utf8");
  readFileSync(join(project, ".gitignore"), "utf8");
  assertTemplateManifest(kind, manifest, readme);
  zero(["check", project]);
  zero(["test", project]);
  if (kind !== "lib") {
    const templateRun = zero(["run", "--out", join(project, "run-app"), project]).stdout;
    assert.match(templateRun, kind === "cli" ? /hello from zero\n/ : /package ok\n/);
  }
  const devReport = json(["dev", "--json", "--target", "linux-musl-x64", project]).body;
  assertDevReport(devReport, kind);
  if (kind === "lib") {
    const docReport = json(["doc", "--json", project]).body;
    assert.equal(docReport.schemaVersion, 1);
    assert.equal(docReport.generatedCBytes, 0);
  }
  if (kind === "lib") {
    removeInlineTests(join(project, "src", "lib.0"));
    zero(["parse", "--json", join(project, "src", "lib.0")]);
  } else {
    const buildOut = join(project, "app");
    const templateBuild = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", project, "--out", buildOut]).body;
    assert.equal(templateBuild.generatedCBytes, 0);
    assert.equal(templateBuild.objectBackend.objectEmission.path, "direct-elf64-exe");
    const shipOut = join(project, "ship-app");
    const firstShip = json(["ship", "--json", "--target", "linux-musl-x64", project, "--out", shipOut]).body;
    assertShipReport(firstShip, shipOut);
    const secondShip = json(["ship", "--json", "--target", "linux-musl-x64", project, "--out", shipOut]).body;
    assertShipReport(secondShip, shipOut);
    assert.equal(secondShip.checksum.value, firstShip.checksum.value);
    assert.equal(secondShip.artifactBytes, firstShip.artifactBytes);
  }
}

const tinyHello = join(outDir, "tiny-hello");
rmSync(tinyHello, { force: true });
zero(["build", "--release", "tiny", "--target", "linux-musl-x64", "examples/hello.0", "--out", tinyHello]);
assert(statSync(tinyHello).size < 10 * 1024);
const buildReport = json(["build", "--json", "--target", "linux-musl-x64", "examples/hello.0", "--out", join(outDir, "hello-linux-report")]).body;
assert.equal(buildReport.schemaVersion, 1);
assert.equal(buildReport.emit, "exe");
assert.equal(buildReport.hostTarget, version.host);
assert.equal(buildReport.target, "linux-musl-x64");
assert.equal(buildReport.compiler, "zero-elf64");
assert(buildReport.artifactBytes > 0);
assert.equal(buildReport.generatedCBytes, 0);
assert(buildReport.loweredIrBytes > 0);
assert.equal(buildReport.targetSupport.fsAvailable, true);
assert.equal(buildReport.profileSemantics.profileKey, "small");
assert.equal(buildReport.profileSemantics.profileBudget.generatedCBytes, 0);
assert.equal(buildReport.profileBudget.cBridgeFallback, false);
assert.equal(buildReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(buildReport.objectBackend.linking.externalToolchain, "none");
assertReleaseTargetContract(buildReport, {
  target: "linux-musl-x64",
  emit: "exe",
  objectFormat: "elf",
  artifactKind: "native-executable",
  linkerFlavor: "elf64",
  targetLibcMode: "bundled-libc",
});
repeatBuildHash(["build", "--json", "--target", "linux-musl-x64", "examples/hello.0", "--out", join(outDir, "hello-linux-report")], join(outDir, "hello-linux-report"), join(outDir, "hello-linux-report.repeat"));

const runArtifact = join(outDir, "run-add");
rmSync(runArtifact, { force: true });
rmSync(`${runArtifact}.exe`, { force: true });
rmSync(`${runArtifact}.c`, { force: true });
const runResult = zero(["run", "--out", runArtifact, "examples/add.0"]);
assert.match(runResult.stdout, /math works\n/);
assert(existsSync(version.host.startsWith("win32") ? `${runArtifact}.exe` : runArtifact));
assert.equal(existsSync(`${runArtifact}.c`), false);

for (const [requestedProfile, canonicalProfile, profileKey] of [
  ["debug", "debug", "debug"],
  ["fast", "release-fast", "fast"],
  ["small", "release-small", "small"],
  ["tiny", "tiny", "tiny"],
]) {
  const profileOut = join(outDir, `profile-${requestedProfile}-hello`);
  const profileReport = json(["build", "--json", "--profile", requestedProfile, "--target", "linux-musl-x64", "examples/hello.0", "--out", profileOut]).body;
  assert.equal(profileReport.generatedCBytes, 0);
  assert.equal(profileReport.profileSemantics.canonical, canonicalProfile);
  assert.equal(profileReport.profileSemantics.profileKey, profileKey);
  assert.equal(profileReport.profileSemantics.profileBudget.generatedCBytes, 0);
  assert.equal(profileReport.profileBudget.helperBudgetPolicy, profileReport.profileSemantics.profileBudget.helperBudgetPolicy);
  repeatBuildHash(["build", "--json", "--profile", requestedProfile, "--target", "linux-musl-x64", "examples/hello.0", "--out", profileOut], profileOut, `${profileOut}.repeat`);
}

const profileSizeReport = json(["size", "--json", "--profile", "debug", "--target", "linux-musl-x64", "examples/memory-primitives.0"]).body;
assert.equal(profileSizeReport.profileSemantics.profileKey, "debug");
assert.equal(profileSizeReport.sizeBreakdown.profileKey, "debug");
assert(profileSizeReport.sizeBreakdown.functions.some((item) => item.name === "main" && item.retainedBy === "entry point"));
assert(profileSizeReport.sizeBreakdown.sections.some((item) => item.name === "debug-metadata"));
assert(Array.isArray(profileSizeReport.sizeBreakdown.stdlibHelpers));
assert(Array.isArray(profileSizeReport.sizeBreakdown.imports));
assert(Array.isArray(profileSizeReport.sizeBreakdown.runtimeShims));
assert(profileSizeReport.sizeBreakdown.debugMetadata.bytes > 0);
assert(profileSizeReport.retentionReasons.some((item) => item.kind === "function"));
assert(profileSizeReport.optimizationHints.some((item) => item.id === "profile-debug-metadata"));
assert.equal(profileSizeReport.profileBudget.debugMetadataAllowed, true);

const directObjPath = join(outDir, "direct-obj-add.o");
rmSync(directObjPath, { force: true });
const directObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-obj-add.0", "--out", directObjPath]).body;
const directObjBytes = readFileSync(directObjPath);
assert.equal(directObjReport.emit, "obj");
assert.equal(directObjReport.compiler, "zero-elf64");
assert.equal(directObjReport.generatedCBytes, 0);
assert(directObjReport.loweredIrBytes > 0);
assert.equal(directObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directObjReport.objectBackend.linking.externalToolchain, "none");
assert.equal(directObjBytes[0], 0x7f);
assert.equal(directObjBytes[1], 0x45);
assert.equal(directObjBytes[2], 0x4c);
assert.equal(directObjBytes[3], 0x46);
assert.equal(directObjBytes.readUInt16LE(16), 1);
assert.equal(directObjBytes.readUInt16LE(18), 62);
const directI64ObjPath = join(outDir, "direct-i64-return.o");
rmSync(directI64ObjPath, { force: true });
const directI64ObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-i64-return.0", "--out", directI64ObjPath]).body;
const directI64ObjBytes = readFileSync(directI64ObjPath);
assert.equal(directI64ObjReport.emit, "obj");
assert.equal(directI64ObjReport.compiler, "zero-elf64");
assert.equal(directI64ObjReport.generatedCBytes, 0);
assert.equal(directI64ObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert(directI64ObjBytes.includes(Buffer.from([0x48, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f])));
assert(directI64ObjBytes.includes(Buffer.from([0x48, 0x01, 0xc8])));
const directShapeObjPath = join(outDir, "direct-token-shape.o");
rmSync(directShapeObjPath, { force: true });
const directShapeObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-token-shape.0", "--out", directShapeObjPath]).body;
const directShapeObjBytes = readFileSync(directShapeObjPath);
assert.equal(directShapeObjReport.emit, "obj");
assert.equal(directShapeObjReport.compiler, "zero-elf64");
assert.equal(directShapeObjReport.generatedCBytes, 0);
assert.equal(directShapeObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directShapeObjBytes[0], 0x7f);
assert.equal(directShapeObjBytes[1], 0x45);
const directSpanReadObjPath = join(outDir, "direct-span-read.o");
rmSync(directSpanReadObjPath, { force: true });
const directSpanReadObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-span-read.0", "--out", directSpanReadObjPath]).body;
const directSpanReadObjBytes = readFileSync(directSpanReadObjPath);
assert.equal(directSpanReadObjReport.emit, "obj");
assert.equal(directSpanReadObjReport.compiler, "zero-elf64");
assert.equal(directSpanReadObjReport.generatedCBytes, 0);
assert.equal(directSpanReadObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directSpanReadObjReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directSpanReadObjReport.objectBackend.directFacts.readonlyDataBytes, 6);
assert.equal(directSpanReadObjBytes[0], 0x7f);
assert.equal(directSpanReadObjBytes[1], 0x45);
const directByteViewLocalsObjPath = join(outDir, "direct-byte-view-reloc.o");
rmSync(directByteViewLocalsObjPath, { force: true });
const directByteViewLocalsObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-byte-view-reloc.0", "--out", directByteViewLocalsObjPath]).body;
const directByteViewLocalsObjBytes = readFileSync(directByteViewLocalsObjPath);
assert.equal(directByteViewLocalsObjReport.emit, "obj");
assert.equal(directByteViewLocalsObjReport.compiler, "zero-elf64");
assert.equal(directByteViewLocalsObjReport.generatedCBytes, 0);
assert.equal(directByteViewLocalsObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directByteViewLocalsObjReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directByteViewLocalsObjReport.objectBackend.directFacts.readonlyDataBytes, 6);
assert(directByteViewLocalsObjBytes.includes(Buffer.from(".rodata\0")));
assert(directByteViewLocalsObjBytes.includes(Buffer.from(".rela.text\0")));
const directRescueObjPath = join(outDir, "direct-rescue-basic.o");
rmSync(directRescueObjPath, { force: true });
const directRescueObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-rescue-basic.0", "--out", directRescueObjPath]).body;
const directRescueObjBytes = readFileSync(directRescueObjPath);
assert.equal(directRescueObjReport.emit, "obj");
assert.equal(directRescueObjReport.compiler, "zero-elf64");
assert.equal(directRescueObjReport.generatedCBytes, 0);
assert.equal(directRescueObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directRescueObjBytes[0], 0x7f);
assert.equal(directRescueObjBytes[1], 0x45);
const directExePath = join(outDir, "direct-exe-return");
rmSync(directExePath, { force: true });
const directExeReport = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", "examples/direct-exe-return.0", "--out", directExePath]).body;
const directExeBytes = readFileSync(directExePath);
assert.equal(directExeReport.emit, "exe");
assert.equal(directExeReport.compiler, "zero-elf64");
assert.equal(directExeReport.generatedCBytes, 0);
assert(directExeReport.loweredIrBytes > 0);
assert(directExeReport.artifactBytes < 512);
assert.equal(directExeReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(directExeReport.objectBackend.objectEmission.dataSections, false);
assert.equal(directExeReport.objectBackend.linking.externalToolchain, "none");
assert.equal(directExeBytes[0], 0x7f);
assert.equal(directExeBytes[1], 0x45);
assert.equal(directExeBytes[2], 0x4c);
assert.equal(directExeBytes[3], 0x46);
assert.equal(directExeBytes.readUInt16LE(16), 2);
assert.equal(directExeBytes.readUInt16LE(18), 62);
assert.equal(directExeBytes.readUInt16LE(54), 56);
assert.equal(directExeBytes.readUInt16LE(56), 1);
const removedEmitC = json(["build", "--json", "--emit", "c", "--target", "linux-musl-x64", "examples/direct-exe-return.0", "--out", join(outDir, "removed-c-backend.c")], { allowFailure: true });
assert.notEqual(removedEmitC.code, 0);
assert.equal(removedEmitC.body.diagnostics[0].code, "BLD003");
assert.equal(removedEmitC.body.diagnostics[0].repair.id, "use-direct-emitter");
const removedLegacyFlag = json(["build", "--json", "--legacy-backend", "--target", "linux-musl-x64", "examples/direct-exe-return.0", "--out", join(outDir, "removed-legacy-flag")], { allowFailure: true });
assert.notEqual(removedLegacyFlag.code, 0);
assert.equal(removedLegacyFlag.body.diagnostics[0].code, "BLD003");
assert.equal(removedLegacyFlag.body.diagnostics[0].repair.id, "use-direct-emitter");
const directMachOExePath = join(outDir, "direct-macho-exe-return");
rmSync(directMachOExePath, { force: true });
const directMachOExeReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-macho64", "--target", "darwin-arm64", "examples/direct-exe-return.0", "--out", directMachOExePath]).body;
const directMachOExeBytes = readFileSync(directMachOExePath);
assert.equal(directMachOExeReport.emit, "exe");
assert.equal(directMachOExeReport.compiler, "zero-macho64");
assert.equal(directMachOExeReport.generatedCBytes, 0);
assert.equal(directMachOExeReport.objectBackend.objectEmission.path, "direct-macho64-exe");
assert.equal(directMachOExeReport.objectBackend.targetFacts.status, "native-exe");
assertReleaseTargetContract(directMachOExeReport, {
  target: "darwin-arm64",
  emit: "exe",
  objectFormat: "macho",
  artifactKind: "native-executable",
  linkerFlavor: "macho64",
  targetLibcMode: "host-default",
});
repeatBuildHash(["build", "--json", "--emit", "exe", "--backend", "zero-macho64", "--target", "darwin-arm64", "examples/direct-exe-return.0", "--out", directMachOExePath], directMachOExePath, `${directMachOExePath}.repeat`);
assert.equal(directMachOExeBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOExeBytes.readUInt32LE(12), 2);
const directMachOExeUuid = assertMachOLoadCommand(directMachOExeBytes, 0x1b, 24);
assert(!directMachOExeUuid.subarray(8, 24).every((byte) => byte === 0));
assert(directMachOExeBytes.includes(Buffer.from("/usr/lib/dyld")));
assert(directMachOExeBytes.includes(Buffer.from("zero-direct")));
const directMachOX64ExePath = join(outDir, "direct-macho-x64-hello");
rmSync(directMachOX64ExePath, { force: true });
const directMachOX64ExeReport = json(["build", "--json", "--emit", "exe", "--target", "darwin-x64", "examples/hello.0", "--out", directMachOX64ExePath]).body;
const directMachOX64ExeBytes = readFileSync(directMachOX64ExePath);
assert.equal(directMachOX64ExeReport.emit, "exe");
assert.equal(directMachOX64ExeReport.compiler, "zero-macho-x64");
assert.equal(directMachOX64ExeReport.generatedCBytes, 0);
assert.equal(directMachOX64ExeReport.objectBackend.objectEmission.path, "direct-macho-x64-exe");
assert.equal(directMachOX64ExeReport.objectBackend.targetFacts.status, "native-exe");
assertReleaseTargetContract(directMachOX64ExeReport, {
  target: "darwin-x64",
  emit: "exe",
  objectFormat: "macho",
  artifactKind: "native-executable",
  linkerFlavor: "macho64",
  targetLibcMode: "sysroot",
});
repeatBuildHash(["build", "--json", "--emit", "exe", "--target", "darwin-x64", "examples/hello.0", "--out", directMachOX64ExePath], directMachOX64ExePath, `${directMachOX64ExePath}.repeat`);
assert.equal(directMachOX64ExeBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOX64ExeBytes.readUInt32LE(4), 0x01000007);
assert.equal(directMachOX64ExeBytes.readUInt32LE(8), 3);
assert.equal(directMachOX64ExeBytes.readUInt32LE(12), 2);
const directMachOX64ExeUuid = assertMachOLoadCommand(directMachOX64ExeBytes, 0x1b, 24);
assert(!directMachOX64ExeUuid.subarray(8, 24).every((byte) => byte === 0));
assert(directMachOX64ExeBytes.includes(Buffer.from("/usr/lib/dyld")));
assert(directMachOX64ExeBytes.includes(Buffer.from("zero-direct-x64")));
assert(directMachOX64ExeBytes.includes(Buffer.from("hello from zero")));
const directMachOX64UnhandledPath = join(outDir, "direct-macho-x64-unhandled-error");
rmSync(directMachOX64UnhandledPath, { force: true });
const directMachOX64UnhandledReport = json(["build", "--json", "--emit", "exe", "--target", "darwin-x64", "examples/direct-unhandled-error-exit.0", "--out", directMachOX64UnhandledPath]).body;
const directMachOX64UnhandledBytes = readFileSync(directMachOX64UnhandledPath);
assert.equal(directMachOX64UnhandledReport.emit, "exe");
assert.equal(directMachOX64UnhandledReport.compiler, "zero-macho-x64");
assert.equal(directMachOX64UnhandledReport.generatedCBytes, 0);
assert.equal(directMachOX64UnhandledReport.objectBackend.objectEmission.path, "direct-macho-x64-exe");
assert.equal(directMachOX64UnhandledReport.objectBackend.directFacts.runtimeHelperCount, 0);
assertReleaseTargetContract(directMachOX64UnhandledReport, {
  target: "darwin-x64",
  emit: "exe",
  objectFormat: "macho",
  artifactKind: "native-executable",
  linkerFlavor: "macho64",
  targetLibcMode: "sysroot",
});
assert.equal(directMachOX64UnhandledBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOX64UnhandledBytes.readUInt32LE(4), 0x01000007);
assert.equal(directMachOX64UnhandledBytes.readUInt32LE(8), 3);
assert.equal(directMachOX64UnhandledBytes.readUInt32LE(12), 2);
assert(directMachOX64UnhandledBytes.includes(Buffer.from("/usr/lib/dyld")));
assert(directMachOX64UnhandledBytes.includes(Buffer.from("zero-direct-x64")));
const directMachOU8ExePath = join(outDir, "direct-macho-u8-return");
rmSync(directMachOU8ExePath, { force: true });
const directMachOU8ExeReport = json(["build", "--json", "--emit", "exe", "--target", "darwin-arm64", "examples/direct-string-literal.0", "--out", directMachOU8ExePath]).body;
assert.equal(directMachOU8ExeReport.emit, "exe");
assert.equal(directMachOU8ExeReport.compiler, "zero-macho64");
assert.equal(directMachOU8ExeReport.generatedCBytes, 0);
assert.equal(directMachOU8ExeReport.objectBackend.objectEmission.path, "direct-macho64-exe");
const directCoffExePath = join(outDir, "direct-coff-exe-return");
rmSync(`${directCoffExePath}.exe`, { force: true });
const directCoffExeReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-coff-x64", "--target", "win32-x64.exe", "examples/direct-exe-return.0", "--out", directCoffExePath]).body;
const directCoffExeBytes = readFileSync(`${directCoffExePath}.exe`);
const directCoffPeOffset = directCoffExeBytes.readUInt32LE(0x3c);
assert.equal(directCoffExeReport.emit, "exe");
assert.equal(directCoffExeReport.compiler, "zero-coff-x64");
assert.equal(directCoffExeReport.generatedCBytes, 0);
assert.equal(directCoffExeReport.objectBackend.objectEmission.path, "direct-coff-x64-exe");
assert.equal(directCoffExeReport.objectBackend.targetFacts.status, "native-exe");
assertReleaseTargetContract(directCoffExeReport, {
  target: "win32-x64.exe",
  emit: "exe",
  objectFormat: "coff",
  artifactKind: "native-executable",
  linkerFlavor: "coff",
  targetLibcMode: "sysroot",
});
assert.equal(directCoffExeReport.releaseTargetContract.sysroot.requiredByTarget, true);
assert.equal(directCoffExeReport.releaseTargetContract.sysroot.status, "not-used-by-direct-artifact");
repeatBuildHash(["build", "--json", "--emit", "exe", "--backend", "zero-coff-x64", "--target", "win32-x64.exe", "examples/direct-exe-return.0", "--out", directCoffExePath], `${directCoffExePath}.exe`, `${directCoffExePath}.repeat`, `${directCoffExePath}.repeat.exe`);
assert.equal(directCoffExeBytes.toString("ascii", directCoffPeOffset, directCoffPeOffset + 4), "PE\u0000\u0000");
assert.equal(directCoffExeBytes.readUInt16LE(directCoffPeOffset + 4), 0x8664);
assert(directCoffExeBytes.includes(Buffer.from("KERNEL32.dll")));
const directCoffArm64ExePath = join(outDir, "direct-coff-arm64-exe-return");
rmSync(`${directCoffArm64ExePath}.exe`, { force: true });
const directCoffArm64ExeReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-coff-aarch64", "--target", "win32-arm64.exe", "examples/direct-exe-return.0", "--out", directCoffArm64ExePath]).body;
const directCoffArm64ExeBytes = readFileSync(`${directCoffArm64ExePath}.exe`);
const directCoffArm64PeOffset = directCoffArm64ExeBytes.readUInt32LE(0x3c);
assert.equal(directCoffArm64ExeReport.emit, "exe");
assert.equal(directCoffArm64ExeReport.compiler, "zero-coff-aarch64");
assert.equal(directCoffArm64ExeReport.generatedCBytes, 0);
assert.equal(directCoffArm64ExeReport.objectBackend.objectEmission.path, "direct-coff-aarch64-exe");
assert.equal(directCoffArm64ExeReport.objectBackend.targetFacts.status, "native-exe");
assertReleaseTargetContract(directCoffArm64ExeReport, {
  target: "win32-arm64.exe",
  emit: "exe",
  objectFormat: "coff",
  artifactKind: "native-executable",
  linkerFlavor: "coff",
  targetLibcMode: "sysroot",
});
assert.equal(directCoffArm64ExeReport.releaseTargetContract.sysroot.requiredByTarget, true);
assert.equal(directCoffArm64ExeReport.releaseTargetContract.sysroot.status, "not-used-by-direct-artifact");
assert.equal(directCoffArm64ExeBytes.toString("ascii", directCoffArm64PeOffset, directCoffArm64PeOffset + 4), "PE\u0000\u0000");
assert.equal(directCoffArm64ExeBytes.readUInt16LE(directCoffArm64PeOffset + 4), 0xaa64);
assert(directCoffArm64ExeBytes.includes(Buffer.from("KERNEL32.dll")));
const directCoffArm64HelloPath = join(outDir, "direct-coff-arm64-hello");
rmSync(`${directCoffArm64HelloPath}.exe`, { force: true });
const directCoffArm64HelloReport = json(["build", "--json", "--emit", "exe", "--target", "win32-arm64.exe", "examples/hello.0", "--out", directCoffArm64HelloPath]).body;
const directCoffArm64HelloBytes = readFileSync(`${directCoffArm64HelloPath}.exe`);
assert.equal(directCoffArm64HelloReport.compiler, "zero-coff-aarch64");
assert.equal(directCoffArm64HelloReport.generatedCBytes, 0);
assert.equal(directCoffArm64HelloReport.objectBackend.objectEmission.path, "direct-coff-aarch64-exe");
assert.equal(directCoffArm64HelloReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert(directCoffArm64HelloBytes.includes(Buffer.from("hello from zero")));
assert(hasAarch64Instruction(directCoffArm64HelloBytes, 0xf90017fe));
assert(hasAarch64Instruction(directCoffArm64HelloBytes, 0xf94017fe));
const directCoffU8ExePath = join(outDir, "direct-coff-u8-return");
rmSync(`${directCoffU8ExePath}.exe`, { force: true });
const directCoffU8ExeReport = json(["build", "--json", "--emit", "exe", "--target", "win32-x64.exe", "examples/direct-string-literal.0", "--out", directCoffU8ExePath]).body;
assert.equal(directCoffU8ExeReport.emit, "exe");
assert.equal(directCoffU8ExeReport.compiler, "zero-coff-x64");
assert.equal(directCoffU8ExeReport.generatedCBytes, 0);
assert.equal(directCoffU8ExeReport.objectBackend.objectEmission.path, "direct-coff-x64-exe");
const directAarch64ExePath = join(outDir, "direct-aarch64-exe-return");
rmSync(directAarch64ExePath, { force: true });
const directAarch64ExeReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf-aarch64", "--target", "linux-musl-arm64", "examples/direct-exe-return.0", "--out", directAarch64ExePath]).body;
const directAarch64ExeBytes = readFileSync(directAarch64ExePath);
assert.equal(directAarch64ExeReport.emit, "exe");
assert.equal(directAarch64ExeReport.compiler, "zero-elf-aarch64");
assert.equal(directAarch64ExeReport.generatedCBytes, 0);
assert(directAarch64ExeReport.artifactBytes < 512);
assert.equal(directAarch64ExeReport.objectBackend.objectEmission.path, "direct-elf-aarch64-exe");
const directAarch64HelloPath = join(outDir, "direct-aarch64-hello");
rmSync(directAarch64HelloPath, { force: true });
const directAarch64HelloReport = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-arm64", "examples/hello.0", "--out", directAarch64HelloPath]).body;
const directAarch64HelloBytes = readFileSync(directAarch64HelloPath);
assert.equal(directAarch64HelloReport.compiler, "zero-elf-aarch64");
assert.equal(directAarch64HelloReport.generatedCBytes, 0);
assert.equal(directAarch64HelloReport.objectBackend.objectEmission.path, "direct-elf-aarch64-exe");
assert.equal(directAarch64HelloReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert(directAarch64HelloBytes.includes(Buffer.from("hello from zero")));
assert(hasAarch64VoidReturnEpilogue(directAarch64HelloBytes));
const directAarch64VoidImplicitSource = join(outDir, "direct-aarch64-void-implicit.0");
const directAarch64VoidImplicitPath = join(outDir, "direct-aarch64-void-implicit");
writeFileSync(directAarch64VoidImplicitSource, `export c fn main Void
  let ok Bool true
`);
rmSync(directAarch64VoidImplicitPath, { force: true });
const directAarch64VoidImplicitReport = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-arm64", directAarch64VoidImplicitSource, "--out", directAarch64VoidImplicitPath]).body;
const directAarch64VoidImplicitBytes = readFileSync(directAarch64VoidImplicitPath);
assert.equal(directAarch64VoidImplicitReport.compiler, "zero-elf-aarch64");
assert.equal(directAarch64VoidImplicitReport.generatedCBytes, 0);
assert(hasAarch64VoidReturnEpilogue(directAarch64VoidImplicitBytes));
assert.equal(directAarch64ExeReport.objectBackend.targetFacts.status, "native-exe");
assert.equal(directAarch64ExeBytes.readUInt16LE(16), 2);
assert.equal(directAarch64ExeBytes.readUInt16LE(18), 183);
assert(directAarch64ExeBytes.includes(Buffer.from([0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6])));
assert(directAarch64ExeBytes.includes(Buffer.from([0xa8, 0x0b, 0x80, 0xd2, 0x01, 0x00, 0x00, 0xd4])));
const directCallObjPath = join(outDir, "direct-call-add.o");
rmSync(directCallObjPath, { force: true });
const directCallObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-call-add.0", "--out", directCallObjPath]).body;
const directCallObjBytes = readFileSync(directCallObjPath);
assert.equal(directCallObjReport.emit, "obj");
assert.equal(directCallObjReport.generatedCBytes, 0);
assert.equal(directCallObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directCallObjBytes.readUInt16LE(16), 1);
assert.equal(directCallObjBytes.readUInt16LE(18), 62);
const directArrayObjPath = join(outDir, "direct-array-fill.o");
rmSync(directArrayObjPath, { force: true });
const directArrayObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-array-fill.0", "--out", directArrayObjPath]).body;
const directArrayObjBytes = readFileSync(directArrayObjPath);
assert.equal(directArrayObjReport.emit, "obj");
assert.equal(directArrayObjReport.generatedCBytes, 0);
assert(directArrayObjReport.objectBackend.directFacts.maxFrameBytes > 0);
assert.equal(directArrayObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directArrayObjBytes.readUInt16LE(16), 1);
assert.equal(directArrayObjBytes.readUInt16LE(18), 62);
const directArm64ObjPath = join(outDir, "direct-arm64-return.o");
rmSync(directArm64ObjPath, { force: true });
const directArm64ObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-arm64", "examples/direct-exe-return.0", "--out", directArm64ObjPath]).body;
const directArm64ObjBytes = readFileSync(directArm64ObjPath);
assert.equal(directArm64ObjReport.emit, "obj");
assert.equal(directArm64ObjReport.compiler, "zero-elf-aarch64");
assert.equal(directArm64ObjReport.generatedCBytes, 0);
assert.equal(directArm64ObjReport.objectBackend.objectEmission.path, "direct-elf-aarch64-object");
assert.equal(directArm64ObjReport.objectBackend.targetFacts.status, "native-exe");
assert.equal(directArm64ObjBytes.readUInt16LE(16), 1);
assert.equal(directArm64ObjBytes.readUInt16LE(18), 183);
assert(directArm64ObjBytes.includes(Buffer.from([0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6])));
const directArm64IndexStoreSource = join(outDir, "direct-arm64-index-store-scratch.0");
const directArm64IndexStoreObjPath = join(outDir, "direct-arm64-index-store-scratch.o");
writeFileSync(directArm64IndexStoreSource, `export c fn main u32
  mut values [4]u32 [0, 0, 0, 0]
  set values[% 5_u32 4_u32] 7_u32
  ret values[1]
`);
rmSync(directArm64IndexStoreObjPath, { force: true });
const directArm64IndexStoreObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-arm64", directArm64IndexStoreSource, "--out", directArm64IndexStoreObjPath]).body;
const directArm64IndexStoreObjBytes = readFileSync(directArm64IndexStoreObjPath);
assert.equal(directArm64IndexStoreObjReport.compiler, "zero-elf-aarch64");
assert.equal(directArm64IndexStoreObjReport.generatedCBytes, 0);
assert(hasAarch64Instruction(directArm64IndexStoreObjBytes, 0xb90003ea));
assert(hasAarch64Instruction(directArm64IndexStoreObjBytes, 0xb94003ea));
const directWhilePath = join(outDir, "direct-while-sum");
rmSync(directWhilePath, { force: true });
const directWhileReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-while-sum.0", "--out", directWhilePath]).body;
const directWhileBytes = readFileSync(directWhilePath);
assert.equal(directWhileReport.emit, "exe");
assert.equal(directWhileReport.compiler, "zero-elf64");
assert.equal(directWhileReport.generatedCBytes, 0);
assert.equal(directWhileReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(directWhileBytes.readUInt16LE(16), 2);
assert.equal(directWhileBytes.readUInt16LE(18), 62);
const directCallLoopPath = join(outDir, "direct-call-loop");
rmSync(directCallLoopPath, { force: true });
const directCallLoopReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-call-loop.0", "--out", directCallLoopPath]).body;
const directCallLoopBytes = readFileSync(directCallLoopPath);
assert.equal(directCallLoopReport.emit, "exe");
assert.equal(directCallLoopReport.compiler, "zero-elf64");
assert.equal(directCallLoopReport.generatedCBytes, 0);
assert.equal(directCallLoopReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(directCallLoopBytes.readUInt16LE(16), 2);
assert.equal(directCallLoopBytes.readUInt16LE(18), 62);
const directPackagePath = join(outDir, "direct-package-arrays");
rmSync(directPackagePath, { force: true });
const directPackageReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-package-arrays", "--out", directPackagePath]).body;
const directPackageBytes = readFileSync(directPackagePath);
assert.equal(directPackageReport.emit, "exe");
assert.equal(directPackageReport.compiler, "zero-elf64");
assert.equal(directPackageReport.generatedCBytes, 0);
assert.equal(directPackageReport.objectBackend.directFacts.moduleCount, 2);
assert.equal(directPackageReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(directPackageBytes.readUInt16LE(16), 2);
assert.equal(directPackageBytes.readUInt16LE(18), 62);
const directLinuxGnuObjPath = join(outDir, "direct-linux-gnu.o");
rmSync(directLinuxGnuObjPath, { force: true });
const directLinuxGnuObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-x64", "examples/direct-call-add.0", "--out", directLinuxGnuObjPath]).body;
const directLinuxGnuObjBytes = readFileSync(directLinuxGnuObjPath);
assert.equal(directLinuxGnuObjReport.target, "linux-x64");
assert.equal(directLinuxGnuObjReport.compiler, "zero-elf64");
assert.equal(directLinuxGnuObjReport.generatedCBytes, 0);
assert.equal(directLinuxGnuObjReport.objectBackend.targetFacts.selectedEmitter, "zero-elf64");
assert.equal(directLinuxGnuObjBytes[0], 0x7f);
assert.equal(directLinuxGnuObjBytes[1], 0x45);
assert.equal(directLinuxGnuObjBytes.readUInt16LE(16), 1);
assert.equal(directLinuxGnuObjBytes.readUInt16LE(18), 62);
const directStringEqlTargets: Array<{ target: string; outName: string; compiler: string; emissionPath: string; magic: Buffer }> = [
  { target: "linux-musl-x64", outName: "direct-string-eql-linux-musl.o", compiler: "zero-elf64", emissionPath: "direct-elf64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-x64", outName: "direct-string-eql-linux-gnu.o", compiler: "zero-elf64", emissionPath: "direct-elf64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-arm64", outName: "direct-string-eql-linux-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-musl-arm64", outName: "direct-string-eql-linux-musl-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "darwin-arm64", outName: "direct-string-eql-darwin-arm64.o", compiler: "zero-macho64", emissionPath: "direct-macho64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "darwin-x64", outName: "direct-string-eql-darwin-x64.o", compiler: "zero-macho-x64", emissionPath: "direct-macho-x64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "win32-x64.exe", outName: "direct-string-eql-win-x64.obj", compiler: "zero-coff-x64", emissionPath: "direct-coff-x64-object", magic: Buffer.from([0x64, 0x86]) },
  { target: "win32-arm64.exe", outName: "direct-string-eql-win-arm64.obj", compiler: "zero-coff-aarch64", emissionPath: "direct-coff-aarch64-object", magic: Buffer.from([0x64, 0xaa]) },
];
for (const { target, outName, compiler, emissionPath, magic } of directStringEqlTargets) {
  const directStringEqlPath = join(outDir, outName);
  rmSync(directStringEqlPath, { force: true });
  const directStringEqlReport = json(["build", "--json", "--emit", "obj", "--target", target, "examples/direct-string-eql.0", "--out", directStringEqlPath]).body;
  const directStringEqlBytes = readFileSync(directStringEqlPath);
  assert.equal(directStringEqlReport.compiler, compiler);
  assert.equal(directStringEqlReport.generatedCBytes, 0);
  assert.equal(directStringEqlReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStringEqlBytes.subarray(0, magic.length).equals(magic));
  assert(directStringEqlBytes.includes(Buffer.from("token")));
}
const directArm64DynamicStringEqlSource = join(outDir, "direct-arm64-dynamic-string-eql.0");
writeFileSync(directArm64DynamicStringEqlSource, `export c fn main u8
  let text String "token"
  let start usize 0
  let end usize 5
  if std.mem.eqlBytes text[(% start 1_usize)..end] "token"[(% start 1_usize)..end]
    ret 1_u8
  ret 0_u8
`);
for (const { target, outName, compiler, emissionPath, magic } of [
  { target: "linux-arm64", outName: "direct-dynamic-string-eql-linux-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "darwin-arm64", outName: "direct-dynamic-string-eql-darwin-arm64.o", compiler: "zero-macho64", emissionPath: "direct-macho64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "win32-arm64.exe", outName: "direct-dynamic-string-eql-win-arm64.obj", compiler: "zero-coff-aarch64", emissionPath: "direct-coff-aarch64-object", magic: Buffer.from([0x64, 0xaa]) },
]) {
  const directArm64DynamicStringEqlPath = join(outDir, outName);
  rmSync(directArm64DynamicStringEqlPath, { force: true });
  const directArm64DynamicStringEqlReport = json(["build", "--json", "--emit", "obj", "--target", target, directArm64DynamicStringEqlSource, "--out", directArm64DynamicStringEqlPath]).body;
  const directArm64DynamicStringEqlBytes = readFileSync(directArm64DynamicStringEqlPath);
  assert.equal(directArm64DynamicStringEqlReport.compiler, compiler);
  assert.equal(directArm64DynamicStringEqlReport.generatedCBytes, 0);
  assert.equal(directArm64DynamicStringEqlReport.objectBackend.objectEmission.path, emissionPath);
  assert(directArm64DynamicStringEqlBytes.subarray(0, magic.length).equals(magic));
  assert(hasAarch64Instruction(directArm64DynamicStringEqlBytes, 0xb94003ea));
}
const directByteCopyFillTargets: Array<{ target: string; outName: string; compiler: string; emissionPath: string; magic: Buffer }> = [
  { target: "linux-musl-x64", outName: "direct-byte-copy-fill-linux-musl.o", compiler: "zero-elf64", emissionPath: "direct-elf64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-x64", outName: "direct-byte-copy-fill-linux-gnu.o", compiler: "zero-elf64", emissionPath: "direct-elf64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-arm64", outName: "direct-byte-copy-fill-linux-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-musl-arm64", outName: "direct-byte-copy-fill-linux-musl-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "darwin-arm64", outName: "direct-byte-copy-fill-darwin-arm64.o", compiler: "zero-macho64", emissionPath: "direct-macho64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "darwin-x64", outName: "direct-byte-copy-fill-darwin-x64.o", compiler: "zero-macho-x64", emissionPath: "direct-macho-x64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "win32-x64.exe", outName: "direct-byte-copy-fill-win-x64.obj", compiler: "zero-coff-x64", emissionPath: "direct-coff-x64-object", magic: Buffer.from([0x64, 0x86]) },
  { target: "win32-arm64.exe", outName: "direct-byte-copy-fill-win-arm64.obj", compiler: "zero-coff-aarch64", emissionPath: "direct-coff-aarch64-object", magic: Buffer.from([0x64, 0xaa]) },
];
for (const { target, outName, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directByteCopyFillPath = join(outDir, outName);
  rmSync(directByteCopyFillPath, { force: true });
  const directByteCopyFillReport = json(["build", "--json", "--emit", "obj", "--target", target, "examples/direct-byte-copy-fill.0", "--out", directByteCopyFillPath]).body;
  const directByteCopyFillBytes = readFileSync(directByteCopyFillPath);
  assert.equal(directByteCopyFillReport.compiler, compiler);
  assert.equal(directByteCopyFillReport.generatedCBytes, 0);
  assert.equal(directByteCopyFillReport.objectBackend.objectEmission.path, emissionPath);
  assert(directByteCopyFillBytes.subarray(0, magic.length).equals(magic));
  assert(directByteCopyFillBytes.includes(Buffer.from("token")));
}
const directStdPathSource = join(outDir, "direct-std-path-matrix.0");
writeFileSync(directStdPathSource, `export c fn main u8
  mut norm [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let normalized std.path.normalize norm "src//./main.0/"
  mut parent [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let parent_normalized std.path.normalize parent "src/a/../main.0"
  mut joined_buf [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let joined std.path.join joined_buf "src/" "/main.0"
  let root_expected Span<u8> "/x"[..1]
  mut empty_abs_join_buf [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let empty_abs_joined std.path.join empty_abs_join_buf "" "/main.0"
  mut rel [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let relative std.path.relative rel "src" "src/main.0"
  mut fallback_buf [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let fallback std.path.relative fallback_buf "other" "src/main.0"
  mut small [3]u8 [0, 0, 0]
  let overflow std.path.normalize small "abcd"
  mut root [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let root_normalized std.path.normalize root "/src//./main.0/"
  mut root_parent_buf [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let root_parent std.path.normalize root_parent_buf "/../src"
  mut leading_parent_buf [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let leading_parent std.path.normalize leading_parent_buf "../src"
  mut nested_parent_buf [32]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let nested_parent std.path.normalize nested_parent_buf "a/../../b"
  mut trim_fit_buf [1]u8 [0]
  let trim_fit std.path.normalize trim_fit_buf "a/"
  mut ok Bool true
  if == normalized.has false
    set ok false
  if normalized.has
    if == (std.mem.eql normalized.value "src/main.0") false
      set ok false
  if == parent_normalized.has false
    set ok false
  if parent_normalized.has
    if == (std.mem.eql parent_normalized.value "src/main.0") false
      set ok false
  if == joined.has false
    set ok false
  if joined.has
    if == (std.mem.eql joined.value "src/main.0") false
      set ok false
  if == empty_abs_joined.has false
    set ok false
  if empty_abs_joined.has
    if == (std.mem.eql empty_abs_joined.value "/main.0") false
      set ok false
  if == relative.has false
    set ok false
  if relative.has
    if == (std.mem.eql relative.value "main.0") false
      set ok false
  if == fallback.has false
    set ok false
  if fallback.has
    if == (std.mem.eql fallback.value "src/main.0") false
      set ok false
  if overflow.has
    set ok false
  if == root_normalized.has false
    set ok false
  if root_normalized.has
    if == (std.mem.eql root_normalized.value "/src/main.0") false
      set ok false
  if == root_parent.has false
    set ok false
  if root_parent.has
    if == (std.mem.eql root_parent.value "/src") false
      set ok false
  if == leading_parent.has false
    set ok false
  if leading_parent.has
    if == (std.mem.eql leading_parent.value "../src") false
      set ok false
  if == nested_parent.has false
    set ok false
  if nested_parent.has
    if == (std.mem.eql nested_parent.value "../b") false
      set ok false
  if == trim_fit.has false
    set ok false
  if trim_fit.has
    if == (std.mem.eql trim_fit.value "a") false
      set ok false
  if == (std.mem.eql (std.path.basename normalized.value) "main.0") false
    set ok false
  if == (std.mem.eql (std.path.dirname normalized.value) "src") false
    set ok false
  if == (std.mem.eql (std.path.dirname "/main.0") root_expected) false
    set ok false
  if == (std.mem.eql (std.path.dirname root_expected) root_expected) false
    set ok false
  if == (std.mem.eql (std.path.extension normalized.value) "0") false
    set ok false
  if ok
    ret 1_u8
  ret 0_u8
`);
for (const { target, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directStdPathPath = join(outDir, `direct-std-path-${target.replace(/[^a-z0-9]+/gi, "-")}.o`);
  rmSync(directStdPathPath, { force: true });
  const directStdPathReport = json(["build", "--json", "--emit", "obj", "--target", target, directStdPathSource, "--out", directStdPathPath]).body;
  const directStdPathBytes = readFileSync(directStdPathPath);
  assert.equal(directStdPathReport.compiler, compiler);
  assert.equal(directStdPathReport.generatedCBytes, 0);
  assert.equal(directStdPathReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStdPathBytes.subarray(0, magic.length).equals(magic));
  assert(directStdPathBytes.includes(Buffer.from("src/main.0")));
}
const directStdStrSource = join(outDir, "direct-std-str-matrix.0");
writeFileSync(directStdStrSource, `export c fn main u8
  mut reversed_buf [6]u8 [0_u8;6]
  let reversed std.str.reverse reversed_buf "drawer"
  let trimmed std.str.trimAscii "  zero row  "
  mut small [3]u8 [0_u8;3]
  let overflow std.str.reverse small "drawer"
  mut ok Bool true
  if == reversed.has false
    set ok false
  if reversed.has
    if == (std.mem.eql reversed.value "reward") false
      set ok false
  if overflow.has
    set ok false
  if != (std.str.countByte "banana" 97_u8) 3
    set ok false
  if == (std.str.startsWith "zero row syntax" "zero") false
    set ok false
  if == (std.str.endsWith "zero row syntax" "syntax") false
    set ok false
  if == (std.str.contains "zero row syntax" "row") false
    set ok false
  if std.str.contains "zero row syntax" "column"
    set ok false
  if == (std.str.contains "zero" "") false
    set ok false
  if == (std.mem.eql trimmed "zero row") false
    set ok false
  if != (std.str.wordCountAscii "zero row syntax") 3
    set ok false
  if ok
    ret 1_u8
  ret 0_u8
`);
for (const { target, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directStdStrPath = join(outDir, `direct-std-str-${target.replace(/[^a-z0-9]+/gi, "-")}.o`);
  rmSync(directStdStrPath, { force: true });
  const directStdStrReport = json(["build", "--json", "--emit", "obj", "--target", target, directStdStrSource, "--out", directStdStrPath]).body;
  const directStdStrBytes = readFileSync(directStdStrPath);
  assert.equal(directStdStrReport.compiler, compiler);
  assert.equal(directStdStrReport.generatedCBytes, 0);
  assert.equal(directStdStrReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStdStrBytes.subarray(0, magic.length).equals(magic));
  assert(directStdStrBytes.includes(Buffer.from("zero row syntax")));
}
const directStdMathSource = join(outDir, "direct-std-math-matrix.0");
writeFileSync(directStdMathSource, `export c fn main u8
  mut ok Bool true
  if != (std.math.minU32 8 3) 3
    set ok false
  if != (std.math.minU32 4000000000_u32 3) 3
    set ok false
  if != (std.math.maxU32 8 3) 8
    set ok false
  if != (std.math.maxU32 4000000000_u32 3) 4000000000_u32
    set ok false
  if != (std.math.clampU32 10 2 7) 7
    set ok false
  if != (std.math.clampU32 4000000000_u32 2 7) 7
    set ok false
  if != (std.math.clampU32 1 7 2) 2
    set ok false
  if != (std.math.gcdU32 84 30) 6
    set ok false
  if != (std.math.lcmU32 21 6) 42
    set ok false
  if != (std.math.powU32 3 4) 81
    set ok false
  if != (std.math.modPowU32 4 13 497) 445
    set ok false
  if != (std.math.modPowU32 9 0 1) 0
    set ok false
  if == (std.math.isPrimeU32 31) false
    set ok false
  if std.math.isPrimeU32 33
    set ok false
  if != (std.math.divisorCountU32 28) 6
    set ok false
  if != (std.math.properDivisorSumU32 28) 28
    set ok false
  if ok
    ret 1_u8
  ret 0_u8
`);
for (const { target, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directStdMathPath = join(outDir, `direct-std-math-${target.replace(/[^a-z0-9]+/gi, "-")}.o`);
  rmSync(directStdMathPath, { force: true });
  const directStdMathReport = json(["build", "--json", "--emit", "obj", "--target", target, directStdMathSource, "--out", directStdMathPath]).body;
  const directStdMathBytes = readFileSync(directStdMathPath);
  assert.equal(directStdMathReport.compiler, compiler);
  assert.equal(directStdMathReport.generatedCBytes, 0);
  assert.equal(directStdMathReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStdMathBytes.subarray(0, magic.length).equals(magic));
  if (compiler === "zero-coff-x64") {
    assert(directStdMathBytes.includes(Buffer.from([0x0f, 0x92, 0xc0])));
    assert(directStdMathBytes.includes(Buffer.from([0x0f, 0x97, 0xc0])));
    assert.equal(directStdMathBytes.includes(Buffer.from([0x0f, 0x9c, 0xc0])), false);
    assert.equal(directStdMathBytes.includes(Buffer.from([0x0f, 0x9f, 0xc0])), false);
  }
}
const directMachOPath = join(outDir, "direct-darwin-arm64.o");
rmSync(directMachOPath, { force: true });
const directMachOReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/direct-call-add.0", "--out", directMachOPath]).body;
const directMachOBytes = readFileSync(directMachOPath);
assert.equal(directMachOReport.compiler, "zero-macho64");
assert.equal(directMachOReport.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(directMachOReport.objectBackend.linking.objectFormat, "macho");
assert.equal(directMachOReport.objectBackend.directFacts.stackBytes, 544);
assert.equal(directMachOReport.objectBackend.directFacts.maxFrameBytes, 272);
assert.equal(directMachOBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOBytes.readUInt32LE(4), 0x0100000c);
assert.equal(directMachOBytes.readUInt32LE(12), 1);
assert(directMachOBytes.includes(Buffer.concat([Buffer.from("_main"), Buffer.from([0])])));
assert(directMachOBytes.includes(Buffer.concat([Buffer.from("_add"), Buffer.from([0])])));
assert(directMachOBytes.includes(Buffer.from([0x00, 0x01, 0x09, 0x0b])));
const directMachOSection = 32 + 72;
const directMachORelocOffset = directMachOBytes.readUInt32LE(directMachOSection + 56);
assert(directMachORelocOffset > 0);
assert(directMachOBytes.readUInt32LE(directMachOSection + 60) > 0);
assert.equal((directMachOBytes.readUInt32LE(directMachORelocOffset + 4) >>> 28) & 15, 2);
const directMachODataPath = join(outDir, "direct-darwin-arm64-data.o");
rmSync(directMachODataPath, { force: true });
const directMachODataReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/direct-byte-view-reloc.0", "--out", directMachODataPath]).body;
const directMachODataBytes = readFileSync(directMachODataPath);
assert.equal(directMachODataReport.compiler, "zero-macho64");
assert.equal(directMachODataReport.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(directMachODataReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directMachODataReport.objectBackend.directFacts.readonlyDataBytes, 6);
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
const directMachOWorldPath = join(outDir, "direct-darwin-arm64-world.o");
rmSync(directMachOWorldPath, { force: true });
const directMachOWorldReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/hello.0", "--out", directMachOWorldPath]).body;
const directMachOWorldBytes = readFileSync(directMachOWorldPath);
assert.equal(directMachOWorldReport.compiler, "zero-macho64");
assert.equal(directMachOWorldReport.generatedCBytes, 0);
assert.equal(directMachOWorldReport.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(directMachOWorldReport.objectBackend.objectEmission.symbolCount, 3);
assert.equal(directMachOWorldReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert.equal(directMachOWorldBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOWorldBytes.readUInt32LE(4), 0x0100000c);
assert.equal(directMachOWorldBytes.readUInt32LE(12), 1);
assert(directMachOWorldBytes.includes(Buffer.from("hello from zero")));
assert(directMachOWorldBytes.includes(Buffer.from("_zero_world_write")));
const directMachOWorldSection = 32 + 72;
const directMachOWorldRelocOffset = directMachOWorldBytes.readUInt32LE(directMachOWorldSection + 56);
const directMachOWorldRelocCount = directMachOWorldBytes.readUInt32LE(directMachOWorldSection + 60);
assert(directMachOWorldRelocOffset > 0);
assert(directMachOWorldRelocCount >= 2);
let sawMachOWorldBranchReloc = false;
for (let i = 0; i < directMachOWorldRelocCount; i++) {
  const info = directMachOWorldBytes.readUInt32LE(directMachOWorldRelocOffset + i * 8 + 4);
  sawMachOWorldBranchReloc ||= ((info >>> 28) & 15) === 2;
}
assert.equal(sawMachOWorldBranchReloc, true);
const directMachOX64WorldPath = join(outDir, "direct-darwin-x64-world.o");
rmSync(directMachOX64WorldPath, { force: true });
const directMachOX64WorldReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-x64", "examples/hello.0", "--out", directMachOX64WorldPath]).body;
const directMachOX64WorldBytes = readFileSync(directMachOX64WorldPath);
assert.equal(directMachOX64WorldReport.compiler, "zero-macho-x64");
assert.equal(directMachOX64WorldReport.generatedCBytes, 0);
assert.equal(directMachOX64WorldReport.objectBackend.objectEmission.path, "direct-macho-x64-object");
assert.equal(directMachOX64WorldReport.objectBackend.objectEmission.symbolCount, 3);
assert.equal(directMachOX64WorldReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert.equal(directMachOX64WorldBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOX64WorldBytes.readUInt32LE(4), 0x01000007);
assert.equal(directMachOX64WorldBytes.readUInt32LE(8), 3);
assert.equal(directMachOX64WorldBytes.readUInt32LE(12), 1);
assert(directMachOX64WorldBytes.includes(Buffer.from("hello from zero")));
assert(directMachOX64WorldBytes.includes(Buffer.from("_zero_world_write")));
const directMachOX64WorldSection = 32 + 72;
const directMachOX64WorldRelocOffset = directMachOX64WorldBytes.readUInt32LE(directMachOX64WorldSection + 56);
const directMachOX64WorldRelocCount = directMachOX64WorldBytes.readUInt32LE(directMachOX64WorldSection + 60);
assert(directMachOX64WorldRelocOffset > 0);
assert(directMachOX64WorldRelocCount >= 2);
let sawMachOX64SignedReloc = false;
let sawMachOX64BranchReloc = false;
for (let i = 0; i < directMachOX64WorldRelocCount; i++) {
  const info = directMachOX64WorldBytes.readUInt32LE(directMachOX64WorldRelocOffset + i * 8 + 4);
  sawMachOX64SignedReloc ||= ((info >>> 28) & 15) === 1;
  sawMachOX64BranchReloc ||= ((info >>> 28) & 15) === 2;
}
assert.equal(sawMachOX64SignedReloc, true);
assert.equal(sawMachOX64BranchReloc, true);
const directCoffPath = join(outDir, "direct-win-x64.obj");
rmSync(directCoffPath, { force: true });
const directCoffReport = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", "examples/direct-call-add.0", "--out", directCoffPath]).body;
const directCoffBytes = readFileSync(directCoffPath);
assert.equal(directCoffReport.compiler, "zero-coff-x64");
assert.equal(directCoffReport.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(directCoffReport.objectBackend.linking.objectFormat, "coff");
assert.equal(directCoffBytes.readUInt16LE(0), 0x8664);
assert.equal(directCoffBytes.readUInt16LE(2), 1);
assert(directCoffBytes.includes(Buffer.concat([Buffer.from("main"), Buffer.from([0])])));
assert(directCoffBytes.includes(Buffer.concat([Buffer.from("add"), Buffer.from([0])])));
assert(directCoffBytes.includes(Buffer.from([0xe8])));
const directCoffRelocOffset = directCoffBytes.readUInt32LE(20 + 24);
const directCoffRelocCount = directCoffBytes.readUInt16LE(20 + 32);
assert(directCoffRelocOffset > 0);
assert(directCoffRelocCount > 0);
assert.equal(directCoffBytes.readUInt16LE(directCoffRelocOffset + 8), 4);
const directCoffDataPath = join(outDir, "direct-win-x64-data.obj");
rmSync(directCoffDataPath, { force: true });
const directCoffDataReport = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", "examples/direct-byte-view-reloc.0", "--out", directCoffDataPath]).body;
const directCoffDataBytes = readFileSync(directCoffDataPath);
assert.equal(directCoffDataReport.compiler, "zero-coff-x64");
assert.equal(directCoffDataReport.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(directCoffDataReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directCoffDataReport.objectBackend.directFacts.readonlyDataBytes, 6);
assert.equal(directCoffDataBytes.readUInt16LE(0), 0x8664);
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
const directCoffArm64DataPath = join(outDir, "direct-win-arm64-data.obj");
rmSync(directCoffArm64DataPath, { force: true });
const directCoffArm64DataReport = json(["build", "--json", "--emit", "obj", "--target", "win32-arm64.exe", "examples/direct-byte-view-reloc.0", "--out", directCoffArm64DataPath]).body;
const directCoffArm64DataBytes = readFileSync(directCoffArm64DataPath);
assert.equal(directCoffArm64DataReport.compiler, "zero-coff-aarch64");
assert.equal(directCoffArm64DataReport.objectBackend.objectEmission.path, "direct-coff-aarch64-object");
assert.equal(directCoffArm64DataReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directCoffArm64DataReport.objectBackend.directFacts.readonlyDataBytes, 6);
assert.equal(directCoffArm64DataBytes.readUInt16LE(0), 0xaa64);
assert.equal(directCoffArm64DataBytes.readUInt16LE(2), 2);
assert(directCoffArm64DataBytes.includes(Buffer.from(".rdata\0")));
assert(directCoffArm64DataBytes.includes(Buffer.from("token")));
const directCoffArm64DataRelocOffset = directCoffArm64DataBytes.readUInt32LE(20 + 24);
const directCoffArm64DataRelocCount = directCoffArm64DataBytes.readUInt16LE(20 + 32);
assert(directCoffArm64DataRelocOffset > 0);
assert(directCoffArm64DataRelocCount > 0);
let sawCoffArm64Addr64Reloc = false;
for (let i = 0; i < directCoffArm64DataRelocCount; i++) {
  sawCoffArm64Addr64Reloc ||= directCoffArm64DataBytes.readUInt16LE(directCoffArm64DataRelocOffset + i * 10 + 8) === 0x000e;
}
assert.equal(sawCoffArm64Addr64Reloc, true);
const directCoffWorldPath = join(outDir, "direct-win-x64-world.obj");
rmSync(directCoffWorldPath, { force: true });
const directCoffWorldReport = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", "examples/hello.0", "--out", directCoffWorldPath]).body;
const directCoffWorldBytes = readFileSync(directCoffWorldPath);
assert.equal(directCoffWorldReport.compiler, "zero-coff-x64");
assert.equal(directCoffWorldReport.generatedCBytes, 0);
assert.equal(directCoffWorldReport.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(directCoffWorldReport.objectBackend.objectEmission.symbolCount, 4);
assert.equal(directCoffWorldReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert.equal(directCoffWorldBytes.readUInt16LE(0), 0x8664);
assert.equal(directCoffWorldBytes.readUInt16LE(2), 2);
assert(directCoffWorldBytes.includes(Buffer.from("hello from zero")));
assert(directCoffWorldBytes.includes(Buffer.from("zero_world_write")));
const directCoffWorldRelocOffset = directCoffWorldBytes.readUInt32LE(20 + 24);
const directCoffWorldRelocCount = directCoffWorldBytes.readUInt16LE(20 + 32);
assert(directCoffWorldRelocOffset > 0);
assert(directCoffWorldRelocCount >= 2);
let sawCoffWorldRel32Reloc = false;
for (let i = 0; i < directCoffWorldRelocCount; i++) {
  sawCoffWorldRel32Reloc ||= directCoffWorldBytes.readUInt16LE(directCoffWorldRelocOffset + i * 10 + 8) === 4;
}
assert.equal(sawCoffWorldRel32Reloc, true);
const directElfFsFallibleResourcesPath = join(outDir, "direct-std-fs-fallible-resources");
rmSync(directElfFsFallibleResourcesPath, { force: true });
const directElfFsFallibleResourcesReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "conformance/native/pass/std-fs-fallible-resources.0", "--out", directElfFsFallibleResourcesPath]).body;
const directElfFsFallibleResourcesBytes = readFileSync(directElfFsFallibleResourcesPath);
assert.equal(directElfFsFallibleResourcesReport.generatedCBytes, 0);
assert.equal(directElfFsFallibleResourcesReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert(directElfFsFallibleResourcesBytes.includes(elfPackedErrorBytes(2)));
assert(directElfFsFallibleResourcesBytes.includes(elfPackedErrorBytes(4)));
const directElfFsFalliblePath = join(outDir, "direct-std-fs-fallible");
rmSync(directElfFsFalliblePath, { force: true });
const directElfFsFallibleReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "conformance/native/pass/std-fs-fallible.0", "--out", directElfFsFalliblePath]).body;
const directElfFsFallibleBytes = readFileSync(directElfFsFalliblePath);
assert.equal(directElfFsFallibleReport.generatedCBytes, 0);
assert.equal(directElfFsFallibleReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert(directElfFsFallibleBytes.includes(elfPackedErrorBytes(2)));
assert(directElfFsFallibleBytes.includes(elfPackedErrorBytes(3)));
assert(directElfFsFallibleBytes.includes(elfPackedErrorBytes(4)));
const directArm64ElfPath = join(outDir, "direct-arm64.o");
rmSync(directArm64ElfPath, { force: true });
const directArm64ElfReport = json(["build", "--json", "--emit", "obj", "--target", "linux-arm64", "examples/direct-call-add.0", "--out", directArm64ElfPath]).body;
const directArm64ElfBytes = readFileSync(directArm64ElfPath);
assert.equal(directArm64ElfReport.compiler, "zero-elf-aarch64");
assert.equal(directArm64ElfReport.generatedCBytes, 0);
assert.equal(directArm64ElfReport.objectBackend.objectEmission.path, "direct-elf-aarch64-object");
assert(directArm64ElfReport.objectBackend.directFacts.functionCount >= 2);
assert(directArm64ElfBytes.subarray(0, 4).equals(Buffer.from([0x7f, 0x45, 0x4c, 0x46])));
assert(directArm64ElfBytes.includes(Buffer.from("main")));
const hostLeakGraph = json(["graph", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package"]).body;
assert.equal(hostLeakGraph.cLibraries[0].targetValidation.status, "blocked");
assert.equal(hostLeakGraph.cLibraries[0].linkPlan.hostDiscovery, "blocked");
const hostLeakReadiness = json(["check", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package"]).body;
assert.equal(hostLeakReadiness.ok, true);
assert.equal(hostLeakReadiness.diagnostics.length, 0);
assert.equal(hostLeakReadiness.targetReadiness.ok, false);
assert.equal(hostLeakReadiness.targetReadiness.buildable, false);
assert.equal(hostLeakReadiness.targetReadiness.diagnostics[0].code, "CIMP003");
assert.match(hostLeakReadiness.targetReadiness.diagnostics[0].help, /target sysroot|vendored/);
const hostLeakBuild = json(["build", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package", "--out", join(outDir, "host-leak-package")], { allowFailure: true });
assert.notEqual(hostLeakBuild.code, 0);
assert.equal(hostLeakBuild.body.diagnostics[0].code, "CIMP003");
const depGraph = json(["graph", "--json", "--target", "linux-musl-x64", "conformance/packages/dep-app"]).body;
assert.equal(depGraph.package.name, "dep-app");
assert.equal(depGraph.package.resolver.deterministic, true);
assert(depGraph.package.dependencies.some((item) => item.name === "dep-lib" && item.status === "path-resolved"));
assert(depGraph.package.dependencies.some((item) => item.name === "remote-tools" && item.status === "registry-reference"));
assert.match(depGraph.package.lockfile.path, /\.zero\/package-locks\/[0-9a-f]+\.lock\.json/);
assert.match(depGraph.packageCache.cacheKeyInputs.dependencyGraphHash, /^[0-9a-f]{16}$/);
const depDoc = json(["doc", "--json", "conformance/packages/dep-app"]).body;
assert.equal(depDoc.package.name, "dep-app");
assert.equal(depDoc.publicationGate.requiresExamplesForPublicApi, true);
const depBuild = json(["build", "--json", "--target", "linux-musl-x64", "conformance/packages/dep-app", "--out", join(outDir, "dep-app")]).body;
assert.equal(depBuild.packageCache.invalidationReasons.includes("dependency graph changed"), true);
assert.equal(depBuild.compilerCaches.every((item) => item.compilerVersion === "0.1.4" && item.packageVersion === "0.1.0"), true);
const depDevTrace = json(["dev", "--json", "--trace", "--target", "linux-musl-x64", "conformance/packages/dep-app"]).body;
assert.equal(depDevTrace.trace.requested, true);
assert.equal(depDevTrace.partialDiagnostics.stable, true);
assert(depDevTrace.watch.files.some((item) => item.endsWith("src/main.0")));
assert.match(depDevTrace.watch.packageLocks[0], /\.zero\/package-locks\/[0-9a-f]+\.lock\.json/);
assert.equal(depDevTrace.interfaceFingerprints.algorithm, "fnv1a64-zero-interface-v1");
assert(depDevTrace.interfaceFingerprints.modules.some((item) => item.name === "main" && /^[0-9a-f]{16}$/.test(item.publicInterfaceHash)));
assert.equal(depDevTrace.incrementalInvalidation.partialDiagnosticsStable, true);
assert.equal(depDevTrace.incrementalInvalidation.interfaceFingerprints.importedPackageMetadataHash, depDevTrace.interfaceFingerprints.importedPackageMetadataHash);
const depTime = json(["time", "--json", "--target", "linux-musl-x64", "conformance/packages/dep-app"]).body;
assert.equal(depTime.interfaceFingerprints.algorithm, "fnv1a64-zero-interface-v1");
assert.match(depTime.interfaceFingerprints.targetFactsHash, /^[0-9a-f]{16}$/);
assert(depTime.cacheSummary.entries >= 5);
assert(depTime.incrementalInvalidation.changedInputs.sourceFiles.some((item) => item.endsWith("src/main.0")));
const targetGraph = json(["graph", "--json", "--target", "linux-musl-x64", "conformance/packages/target-incompatible-app"]).body;
assert.equal(targetGraph.package.dependencies[0].targetCompatible, false);
for (const [fixture, code] of [
  ["conformance/packages/missing-dep-app", "PKG001"],
  ["conformance/packages/cycle-a", "PKG002"],
  ["conformance/packages/conflict-app", "PKG003"],
]) {
  const result = json(["check", "--json", fixture], { allowFailure: true });
  assert.notEqual(result.code, 0);
  assert.equal(result.body.diagnostics[0].code, code);
}
const targetIncompatible = json(["check", "--json", "--target", "linux-musl-x64", "conformance/packages/target-incompatible-app"], { allowFailure: true });
assert.notEqual(targetIncompatible.code, 0);
assert.equal(targetIncompatible.body.diagnostics[0].code, "PKG004");

const zeroHashSize = json(["size", "--json", "--target", "linux-musl-x64", "examples/zero-hash", "--out", join(outDir, "zero-hash-sized")]).body;
assert.equal(zeroHashSize.generatedCBytes, 0);
assert(zeroHashSize.usedStdlibHelpers.some((helper) => helper.name === "std.codec.crc32Bytes"));
assert(zeroHashSize.artifactBytes < 100 * 1024);
const invalidCheckEmit = json(["check", "--json", "--emit", "bogus", "examples/hello.0"], { allowFailure: true });
assert.equal(invalidCheckEmit.code, 1);
assert.equal(invalidCheckEmit.body.ok, false);
assert.equal(invalidCheckEmit.body.diagnostics[0].code, "BLD002");
assert.equal(invalidCheckEmit.body.diagnostics[0].actual, "--emit bogus");
assert.equal(invalidCheckEmit.body.targetReadiness, undefined);
const backendBlockedReadiness = json(["check", "--json", "--emit", "obj", "--target", "linux-musl-x64", "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.0"]).body;
assert.equal(backendBlockedReadiness.ok, true);
assert.equal(backendBlockedReadiness.diagnostics.length, 0);
assert.equal(backendBlockedReadiness.targetReadiness.ok, false);
assert.equal(backendBlockedReadiness.targetReadiness.buildable, false);
assert.equal(backendBlockedReadiness.targetReadiness.languageOk, true);
assert.equal(backendBlockedReadiness.targetReadiness.emit, "obj");
assert.equal(backendBlockedReadiness.targetReadiness.target, "linux-musl-x64");
assert.equal(backendBlockedReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.deepEqual(backendBlockedReadiness.targetReadiness.diagnostics[0].backendBlocker, {
  target: "linux-musl-x64",
  objectFormat: "elf",
  backend: "zero-elf64",
  stage: "lower",
  unsupportedFeature: "owned<Tracked>",
});
const directExeBlockedReadiness = json(["check", "--json", "--emit", "exe", "--target", "linux-musl-x64", "examples/direct-call-add.0"]).body;
assert.equal(directExeBlockedReadiness.ok, true);
assert.equal(directExeBlockedReadiness.diagnostics.length, 0);
assert.equal(directExeBlockedReadiness.targetReadiness.ok, false);
assert.equal(directExeBlockedReadiness.targetReadiness.buildable, false);
assert.equal(directExeBlockedReadiness.targetReadiness.languageOk, true);
assert.equal(directExeBlockedReadiness.targetReadiness.emit, "exe");
assert.equal(directExeBlockedReadiness.targetReadiness.target, "linux-musl-x64");
assert.equal(directExeBlockedReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(directExeBlockedReadiness.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
assert.match(directExeBlockedReadiness.targetReadiness.diagnostics[0].message, /main must not take parameters/);
const directExeBlockedBuild = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", "examples/direct-call-add.0", "--out", join(outDir, "direct-call-add-blocked")], { allowFailure: true });
assert.notEqual(directExeBlockedBuild.code, 0);
for (const key of ["code", "path", "line", "column", "length", "expected", "actual", "help"]) {
  assert.equal(directExeBlockedBuild.body.diagnostics[0][key], directExeBlockedReadiness.targetReadiness.diagnostics[0][key]);
}
assert.equal(directExeBlockedBuild.body.diagnostics[0].backendBlocker.stage, "buildability");
const directExeBlockedGraph = json(["graph", "--json", "--emit", "exe", "--target", "linux-musl-x64", "examples/direct-call-add.0"]).body;
assert.equal(directExeBlockedGraph.targetReadiness.ok, false);
assert.equal(directExeBlockedGraph.targetReadiness.diagnostics[0].code, "BLD004");
for (const key of ["code", "path", "line", "column", "length", "expected", "actual", "help"]) {
  assert.equal(directExeBlockedGraph.targetReadiness.diagnostics[0][key], directExeBlockedReadiness.targetReadiness.diagnostics[0][key]);
}
assert.equal(directExeBlockedGraph.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
const coffMaybeByteViewFixture = "conformance/native/pass/coff-maybe-byte-view-buildable.0";
const coffMaybeByteViewReadiness = json(["check", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffMaybeByteViewFixture]).body;
assert.equal(coffMaybeByteViewReadiness.ok, true);
assert.equal(coffMaybeByteViewReadiness.targetReadiness.ok, true);
assert.equal(coffMaybeByteViewReadiness.targetReadiness.buildable, true);
assert.equal(coffMaybeByteViewReadiness.targetReadiness.backend, "zero-coff-x64");
const coffMaybeByteViewBuild = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffMaybeByteViewFixture, "--out", join(outDir, "coff-maybe-byte-view.obj")]).body;
assert.equal(coffMaybeByteViewBuild.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(coffMaybeByteViewBuild.generatedCBytes, 0);
const coffDynamicSliceFixture = "conformance/native/pass/coff-dynamic-byte-slice.0";
const coffDynamicSliceReadiness = json(["check", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffDynamicSliceFixture]).body;
assert.equal(coffDynamicSliceReadiness.ok, true);
assert.equal(coffDynamicSliceReadiness.diagnostics.length, 0);
assert.equal(coffDynamicSliceReadiness.targetReadiness.ok, true);
assert.equal(coffDynamicSliceReadiness.targetReadiness.buildable, true);
assert.equal(coffDynamicSliceReadiness.targetReadiness.languageOk, true);
assert.equal(coffDynamicSliceReadiness.targetReadiness.emit, "obj");
assert.equal(coffDynamicSliceReadiness.targetReadiness.target, "win32-x64.exe");
assert.equal(coffDynamicSliceReadiness.targetReadiness.backend, "zero-coff-x64");
const coffDynamicSlicePath = join(outDir, "coff-dynamic-byte-slice.obj");
const coffDynamicSliceBuild = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffDynamicSliceFixture, "--out", coffDynamicSlicePath]).body;
assert.equal(coffDynamicSliceBuild.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(coffDynamicSliceBuild.generatedCBytes, 0);
const coffDynamicSliceBytes = readFileSync(coffDynamicSlicePath);
assert.equal(coffDynamicSliceBytes.readUInt16LE(0), 0x8664);
function assertMachOObjectBuildabilityBlocked(fixture: string, outName: string, expectedMessage: RegExp) {
  const readiness = json(["check", "--json", "--emit", "obj", "--target", "darwin-arm64", fixture]).body;
  assert.equal(readiness.ok, true);
  assert.equal(readiness.diagnostics.length, 0);
  assert.equal(readiness.targetReadiness.ok, false);
  assert.equal(readiness.targetReadiness.buildable, false);
  assert.equal(readiness.targetReadiness.languageOk, true);
  assert.equal(readiness.targetReadiness.emit, "obj");
  assert.equal(readiness.targetReadiness.target, "darwin-arm64");
  assert.equal(readiness.targetReadiness.diagnostics[0].code, "BLD004");
  assert.equal(readiness.targetReadiness.diagnostics[0].backendBlocker.backend, "zero-macho64");
  assert.equal(readiness.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
  assert.match(readiness.targetReadiness.diagnostics[0].message, expectedMessage);
  const build = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", fixture, "--out", join(outDir, outName)], { allowFailure: true });
  assert.notEqual(build.code, 0);
  for (const key of ["code", "path", "line", "column", "length", "expected", "actual", "help"]) {
    assert.equal(build.body.diagnostics[0][key], readiness.targetReadiness.diagnostics[0][key]);
  }
  assert.equal(build.body.diagnostics[0].backendBlocker.backend, "zero-macho64");
  assert.equal(build.body.diagnostics[0].backendBlocker.stage, "buildability");
}
assertMachOObjectBuildabilityBlocked(
  "conformance/native/pass/macho-large-byte-slice-blocked.0",
  "macho-large-byte-slice.o",
  /constant start/,
);
assertMachOObjectBuildabilityBlocked(
  "conformance/native/pass/macho-nested-call-scratch-blocked.0",
  "macho-nested-call-scratch.o",
  /scratch spill capacity/,
);
assertMachOObjectBuildabilityBlocked(
  "conformance/native/pass/macho-open-byte-slice-blocked.0",
  "macho-open-byte-slice.o",
  /byte-view length/,
);
const machOMemoryPackageReadiness = json(["check", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/memory-package"]).body;
assert.equal(machOMemoryPackageReadiness.ok, true);
assert.equal(machOMemoryPackageReadiness.diagnostics.length, 0);
assert.equal(machOMemoryPackageReadiness.targetReadiness.ok, true);
assert.equal(machOMemoryPackageReadiness.targetReadiness.buildable, true);
assert.equal(machOMemoryPackageReadiness.targetReadiness.languageOk, true);
assert.equal(machOMemoryPackageReadiness.targetReadiness.emit, "obj");
assert.equal(machOMemoryPackageReadiness.targetReadiness.target, "darwin-arm64");
assert.equal(machOMemoryPackageReadiness.targetReadiness.backend, "zero-macho64");
const machOMemoryPackagePath = join(outDir, "direct-darwin-arm64-memory-package.o");
rmSync(machOMemoryPackagePath, { force: true });
const machOMemoryPackageReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/memory-package", "--out", machOMemoryPackagePath]).body;
const machOMemoryPackageBytes = readFileSync(machOMemoryPackagePath);
assert.equal(machOMemoryPackageReport.compiler, "zero-macho64");
assert.equal(machOMemoryPackageReport.generatedCBytes, 0);
assert.equal(machOMemoryPackageReport.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(machOMemoryPackageReport.objectBackend.directFacts.moduleCount, 3);
assert.equal(machOMemoryPackageReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert.equal(machOMemoryPackageBytes.readUInt32LE(0), 0xfeedfacf);
assert(machOMemoryPackageBytes.includes(Buffer.from("memory package ok")));

const diagnostics = [
  ["PAR100", ["check", "--json", "conformance/check/fail/parse-missing-brace.0"]],
  ["NAM003", ["check", "--json", "conformance/check/fail/unknown-name.0"]],
  ["IMP001", ["check", "--json", "conformance/check/fail/missing-import"]],
  ["NAM004", ["check", "--json", "conformance/native/fail/wrong-arity.0"]],
  ["TYP002", ["check", "--json", "conformance/check/fail/shape-default-type-mismatch.0"]],
  ["STC003", ["check", "--json", "conformance/check/fail/static-value-mismatch.0"]],
  ["TYP025", ["check", "--json", "conformance/check/fail/generic-cannot-infer.0"]],
  ["FLD002", ["check", "--json", "conformance/check/fail/shape-default-missing-required.0"]],
  ["RCV001", ["check", "--json", "conformance/check/fail/receiver-method-unknown.0"]],
  ["BOR001", ["check", "--json", "conformance/native/fail/borrow-conflict.0"]],
  ["OWN001", ["check", "--json", "conformance/native/fail/owned-use-after-move.0"]],
  ["ERR001", ["check", "--json", "conformance/native/fail/raise-without-raises.0"]],
  ["BLD002", ["check", "--json", "conformance/check/fail/bad-manifest-kind"]],
  ["ABI001", ["check", "--json", "conformance/native/fail/bad-c-export.0"]],
  ["PUB001", ["check", "--json", "conformance/check/fail/public-const-missing-type.0"]],
  ["TYP009", ["check", "--json", "conformance/native/fail/mem-copy-immutable-dst.0"]],
  ["ERR002", ["check", "--json", "conformance/native/fail/std-fs-create-error-set-mismatch.0"]],
  ["ERR003", ["check", "--json", "conformance/native/fail/std-fs-unchecked-resource-fallible.0"]],
  ["STD003", ["check", "--json", "conformance/native/fail/fs-readall-invalid-alloc.0"]],
  ["IFC002", ["check", "--json", "conformance/check/fail/interface-missing-method.0"]],
  ["STC002", ["check", "--json", "conformance/check/fail/static-value-non-constant.0"]],
  ["SHM001", ["check", "--json", "conformance/check/fail/generic-shape-method-cannot-infer.0"]],
  ["RCV001", ["check", "--json", "conformance/check/fail/receiver-method-unknown.0"]],
  ["RCV002", ["check", "--json", "conformance/check/fail/receiver-method-immutable.0"]],
].map(([code, args]) => {
  const body = json(args, { allowFailure: true }).body;
  const diagnostic = body.diagnostics[0];
  assert.equal(diagnostic.code, code);
  assert.equal(typeof diagnostic.fixSafety, "string");
  assert.equal(typeof diagnostic.repair.id, "string");
  assert.equal(typeof diagnostic.expected, "string");
  assert.equal(typeof diagnostic.actual, "string");
  assert.equal(Array.isArray(diagnostic.related), true);
  return {
    code,
    fixSafety: diagnostic.fixSafety,
    repair: diagnostic.repair,
    expected: diagnostic.expected,
    actual: diagnostic.actual,
    help: diagnostic.help,
    relatedCount: diagnostic.related.length,
  };
});

const graph = json(["graph", "--json", "--target", "linux-musl-x64", "examples/memory-package"]).body;
assert.equal(graph.schemaVersion, 1);
assert.equal(graph.targetSupport.target, "linux-musl-x64");
assert.equal(typeof graph.targetSupport.hostTarget, "string");
assert.equal(graph.targetSupport.fsAvailable, true);
assert(graph.modules.some((module) => module.name === "main"));
assert(graph.imports.includes("buffer"));
assert(graph.importEdges.some((edge) => edge.from === "main" && edge.to === "buffer"));
assert(graph.symbols.some((symbol) => symbol.name === "main" && symbol.kind === "function"));
assert(graph.functions.some((fun) => fun.name === "prepare" && fun.requiresCapabilities.includes("memory")));
assert(graph.stdlibHelpers.some((helper) => helper.name === "std.mem.copy" && helper.targetSupport === "target-neutral"));
assert(graph.stdlibHelpers.some((helper) => helper.name === "std.mem.byteBuf" && /explicit allocator/.test(helper.allocationBehavior)));
assert(graph.stdlibHelpers.some((helper) => helper.name === "std.fs.createOrRaise" && helper.targetSupport === "host"));
const graphMemCopyHelper = graph.stdlibHelpers.find((helper) => helper.name === "std.mem.copy");
assert.equal(graphMemCopyHelper.module, "std.mem");
assert(graphMemCopyHelper.effects.includes("memory"));
assert.equal(graphMemCopyHelper.errorBehavior, "infallible");
assert.match(graphMemCopyHelper.ownershipNotes, /caller-owned storage/);
assert.equal(graphMemCopyHelper.example, "examples/memory-primitives.0");
assert.equal(graphMemCopyHelper.apiStability, "bootstrap-stable");

const httpErrorsGraph = json(["graph", "--json", "conformance/native/pass/std-http-errors.0"]).body;
assert.deepEqual(httpErrorsGraph.requiresCapabilities, []);
const httpTimeoutHelper = httpErrorsGraph.stdlibHelpers.find((helper) => helper.name === "std.http.errorTimeout");
assert.equal(httpTimeoutHelper.returnType, "HttpError");
assert.equal(httpTimeoutHelper.capability, "none");
assert.deepEqual(httpTimeoutHelper.effects, []);
assert.equal(httpTimeoutHelper.allocationBehavior, "no allocation");
assert.equal(httpTimeoutHelper.ownershipNotes, "no ownership transfer");

const coreGraph = json(["graph", "--json", "examples/static-method.0"]).body;
const counterShape = coreGraph.shapes.find((shape) => shape.name === "Counter");
assert(counterShape);
assert(counterShape.fields.some((field) => field.name === "value" && field.hasDefault === false));
assert(counterShape.methods.some((method) => method.name === "add" && method.staticDispatch === true));

const interfaceGraph = json(["graph", "--json", "examples/static-interface.0"]).body;
assert(interfaceGraph.interfaces.some((item) => item.name === "Readable" && item.staticOnly === true));
assert(interfaceGraph.functions.some((item) => item.name === "readValue" && item.constraints.some((constraint) => constraint.interface === "Readable<T>")));

const staticValueGraph = json(["graph", "--json", "examples/static-value-params.0"]).body;
const fixedVecShape = staticValueGraph.shapes.find((shape) => shape.name === "FixedVec");
assert(fixedVecShape);
assert(fixedVecShape.staticParams.some((param) => param.name === "N" && param.type === "usize"));
assert(staticValueGraph.functions.some((fun) => fun.name === "first" && fun.staticParams.some((param) => param.name === "N")));

const fixedVecMethodsGraph = json(["graph", "--json", "examples/fixed-vec.0"]).body;
const fixedVecMethodsShape = fixedVecMethodsGraph.shapes.find((shape) => shape.name === "FixedVec");
assert(fixedVecMethodsShape);
assert(fixedVecMethodsShape.methods.some((method) => method.name === "push" && method.inheritedShapeParams === true && method.shapeStaticParams.some((param) => param.name === "N")));
assert(fixedVecMethodsShape.methods.some((method) => method.name === "push" && typeof method.doc === "string"));
assert(fixedVecMethodsShape.fields.some((field) => field.name === "len" && field.hasDefault === true));

const aliasGraph = json(["graph", "--json", "examples/type-alias.0"]).body;
assert(aliasGraph.aliases.some((alias) => alias.name === "BytePair" && alias.target === "Pair<u8,u8>"));
assert(aliasGraph.symbols.some((symbol) => symbol.name === "BytePair" && symbol.kind === "type-alias"));

const constGraph = json(["graph", "--json", "examples/const-arithmetic.0"]).body;
assert(constGraph.consts.some((item) => item.name === "answer" && item.type === "i32"));

const errorGraph = json(["graph", "--json", "conformance/native/pass/fallibility-error-sets.0"]).body;
const maybeFail = errorGraph.functions.find((item) => item.name === "maybe_fail");
assert(maybeFail);
assert.equal(maybeFail.errorSetKind, "explicit");
assert(maybeFail.errorNames.includes("BadInput"));
const inferredGraph = json(["graph", "--json", "conformance/native/pass/check-maybe-fallibility.0"]).body;
const inferredPrivate = inferredGraph.functions.find((item) => item.name === "first_or_none");
assert(inferredPrivate);
assert.equal(inferredPrivate.errorSetKind, "inferred");

const size = json(["size", "--json", "--target", "linux-musl-x64", "examples/memory-package"]).body;
assert.equal(size.schemaVersion, 1);
assert(size.stdlibHelpers.some((helper) => helper.name === "std.mem.copy"));
assert(size.usedStdlibHelpers.some((helper) => helper.name === "std.mem.copy"));
assert(size.usedStdlibHelpers.every((helper) => helper.module && helper.effects?.length && helper.errorBehavior && helper.ownershipNotes && helper.example && helper.apiStability));
assert(size.requiresCapabilities.includes("memory"));
assert.equal(size.generatedCBytes, 0);
assert.equal(size.cBridgeFallback, false);
assert(size.loweredIrBytes > 0);
assert(size.sections.some((section) => section.name === "lowered-ir" && section.bytes === size.loweredIrBytes));
assert(size.sections.some((section) => section.name === "direct-size-metadata" && section.kind === "metadata"));
assert(size.topLargestEmittedHelpers.some((helper) => helper.name === "std.mem.copy" && helper.estimatedDirectBytes > 0));
assert.equal(size.objectBackend.objectEmission.path, "direct-elf64-object");
assert(size.compilerRuntimeHelpers.every((helper) => helper.payAsUsed === true && helper.emitted === false));
const sizedArtifact = join(outDir, "sized-memory-package");
rmSync(sizedArtifact, { force: true });
rmSync(`${sizedArtifact}.c`, { force: true });
const sizeWithArtifact = json(["size", "--json", "--out", sizedArtifact, "examples/memory-package"]).body;
assert.equal(sizeWithArtifact.artifactPath, sizedArtifact);
assert(sizeWithArtifact.artifactBytes > 0);
assert(existsSync(sizedArtifact));
const sizeBlockedParent = join(outDir, "size-output-not-dir");
const sizeBlockedOut = join(sizeBlockedParent, "report.json");
rmSync(sizeBlockedParent, { force: true, recursive: true });
writeFileSync(sizeBlockedParent, "not a directory\n");
const sizeBlocked = json(["size", "--json", "--out", sizeBlockedOut, "examples/hello.0"], { allowFailure: true });
assert.notEqual(sizeBlocked.code, 0);
assert.equal(sizeBlocked.body.diagnostics[0].path, sizeBlockedOut);
assert.equal(diagnostics.find((item) => item.code === "ERR001").repair.id, "add-fallible-marker-or-rescue");
assert.equal(diagnostics.find((item) => item.code === "ERR003").repair.id, "check-or-rescue-fallible-call");
assert.equal(diagnostics.find((item) => item.code === "TYP025").repair.id, "add-explicit-generic-type-arguments");
assert.equal(diagnostics.find((item) => item.code === "TYP009").repair.id, "make-binding-mutable");
assert.equal(diagnostics.find((item) => item.code === "FLD002").repair.id, "initialize-missing-field");
assert.equal(diagnostics.find((item) => item.code === "PUB001").repair.id, "add-public-api-type");
assert.equal(diagnostics.find((item) => item.code === "IMP001").repair.id, "fix-import-path");
const generatedCBytesAfterReadOnlyCommands = json(["size", "--json", "examples/memory-package"]).body.generatedCBytes;
assert.equal(generatedCBytesAfterReadOnlyCommands, generatedCBytesBeforeReadOnlyCommands);

const targets = json(["targets"]).body;
assert.equal(targets.schemaVersion, 1);
assert.equal(typeof targets.host, "string");
const publicTargetNames = ["darwin-arm64", "darwin-x64", "linux-arm64", "linux-musl-arm64", "linux-musl-x64", "linux-x64", "win32-arm64.exe", "win32-x64.exe"];
assert.deepEqual([...targets.targets.map((target) => target.name)].sort(), [...publicTargetNames].sort());
for (const targetName of publicTargetNames) {
  assert(targets.targets.some((target) => target.name === targetName), `${targetName} should be listed`);
}
assert(targets.targets.some((target) => target.hosted === true && target.capabilities.includes("fs")));
assert(targets.targets.some((target) => target.hosted === false && !target.capabilities.includes("fs")));
const linuxGnuTarget = targets.targets.find((target) => target.name === "linux-x64");
const linuxMuslTarget = targets.targets.find((target) => target.name === "linux-musl-x64");
const darwinArm64Target = targets.targets.find((target) => target.name === "darwin-arm64");
const darwinX64Target = targets.targets.find((target) => target.name === "darwin-x64");
const winX64Target = targets.targets.find((target) => target.name === "win32-x64.exe");
const winArm64Target = targets.targets.find((target) => target.name === "win32-arm64.exe");
const linuxArm64Target = targets.targets.find((target) => target.name === "linux-arm64");
assert.equal(linuxMuslTarget.directBackend.exeSupported, true);
assert.equal(linuxMuslTarget.directBackend.exeEmitter, "zero-elf64-exe");
assert.equal(linuxGnuTarget.directBackend.objectEmitter, "zero-elf64");
assert.equal(linuxGnuTarget.directBackend.exeSupported, true);
assert.equal(linuxGnuTarget.directBackend.exeEmitter, "zero-elf64-exe");
assert.equal(darwinArm64Target.directBackend.objectEmitter, "zero-macho64");
assert.equal(darwinArm64Target.directBackend.exeSupported, true);
assert.equal(darwinArm64Target.directBackend.exeEmitter, "zero-macho64-exe");
assert.equal(darwinX64Target.directBackend.objectEmitter, "zero-macho-x64");
assert.equal(darwinX64Target.directBackend.exeSupported, true);
assert.equal(darwinX64Target.directBackend.exeEmitter, "zero-macho-x64-exe");
assert.equal(winX64Target.directBackend.objectEmitter, "zero-coff-x64");
assert.equal(winX64Target.directBackend.objectSupported, true);
assert.equal(winX64Target.directBackend.exeSupported, true);
assert.equal(winX64Target.directBackend.exeEmitter, "zero-coff-x64-exe");
assert.equal(winArm64Target.directBackend.status, "native-exe");
assert.equal(winArm64Target.directBackend.objectEmitter, "zero-coff-aarch64");
assert.equal(winArm64Target.directBackend.exeEmitter, "zero-coff-aarch64-exe");
assert.match(winArm64Target.directBackend.reason, /direct object and executable backend available/);
assert.equal(linuxArm64Target.directBackend.status, "native-exe");
assert.equal(linuxArm64Target.directBackend.objectEmitter, "zero-elf-aarch64");
assert.equal(linuxArm64Target.directBackend.exeEmitter, "zero-elf-aarch64-exe");
assert.match(linuxArm64Target.directBackend.reason, /direct object and executable backend available/);

const cAbiExport = zero(["check", "conformance/native/pass/c-abi-export.0"]);
assert.match(cAbiExport.stdout, /ok/);
const cAbiDump = json(["abi", "dump", "--json", "conformance/native/pass/c-abi-export.0"]).body;
assert(cAbiDump.cExports.some((item) => item.name === "zero_add" && item.cReturnType === "int32_t"));
assert.match(cAbiDump.generatedHeader.text, /int32_t zero_add\(int32_t a, int32_t b\);/);
const badCAbi = json(["check", "--json", "conformance/native/fail/bad-c-export.0"], { allowFailure: true }).body;
assert.equal(badCAbi.diagnostics[0].code, "ABI001");

const report = {
  generatedAt: new Date().toISOString(),
  productShell: {
    version: version.version,
    host: version.host,
    backend: version.backend,
    doctorStatus: doctor.status,
    cleanRemovedOut: !existsSync(cleanProbe),
  },
  diagnostics,
  graph: {
    target: graph.targetSupport.target,
    requiresCapabilities: graph.requiresCapabilities,
    stdlibHelperCount: graph.stdlibHelpers.length,
    coreMethodCount: counterShape.methods.length,
    interfaceCount: interfaceGraph.interfaces.length,
    aliasCount: aliasGraph.aliases.length,
    constCount: constGraph.consts.length,
    errorFunction: maybeFail.errorNames,
  },
  size: {
    generatedCBytes: size.generatedCBytes,
    stdlibHelperCount: size.stdlibHelpers.length,
    usedStdlibHelperCount: size.usedStdlibHelpers.length,
    artifactBytes: sizeWithArtifact.artifactBytes,
    unchangedByReadOnlyCommands: generatedCBytesAfterReadOnlyCommands === generatedCBytesBeforeReadOnlyCommands,
  },
  noCDefaultRouteSentinels: {
    defaultNoC: [
      {
        id: "direct-linux-exe",
        generatedCBytes: directExeReport.generatedCBytes,
        cBridgeFallback: directExeReport.selfHostRouting?.cBridge?.required ?? false,
        objectEmissionPath: directExeReport.objectBackend.objectEmission.path,
      },
    ],
    knownDefaultCGap: {
      id: "hello-linux-default",
      generatedCBytes: buildReport.generatedCBytes,
      cBridgeFallback: buildReport.selfHostRouting?.cBridge?.required ?? false,
      objectEmissionPath: buildReport.objectBackend.objectEmission.path,
    },
    removedGeneratedC: {
      emitCDiagnostic: removedEmitC.body.diagnostics[0].code,
      legacyFlagDiagnostic: removedLegacyFlag.body.diagnostics[0].code,
      repair: removedEmitC.body.diagnostics[0].repair.id,
    },
    directLinkedExecutables: {
      darwin: {
        compiler: directMachOExeReport.compiler,
        generatedCBytes: directMachOExeReport.generatedCBytes,
        objectEmissionPath: directMachOExeReport.objectBackend.objectEmission.path,
      },
      windows: {
        compiler: directCoffExeReport.compiler,
        generatedCBytes: directCoffExeReport.generatedCBytes,
        objectEmissionPath: directCoffExeReport.objectBackend.objectEmission.path,
      },
    },
  },
  targets: {
    host: targets.host,
    count: targets.targets.length,
  },
  generatedCAudit: null,
};

writeFileSync(join(outDir, "summary.json"), `${JSON.stringify(report, null, 2)}\n`);
console.log("command contract snapshots ok");
