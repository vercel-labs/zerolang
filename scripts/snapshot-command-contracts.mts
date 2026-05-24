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

assert.equal(zero(["--version"]).stdout, "zero 0.1.3\n");

const version = json(["--version", "--json"]).body;
assert.equal(version.schemaVersion, 1);
assert.equal(version.version, "0.1.3");
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

const skillsList = json(["skills", "list", "--json"]).body;
assert.equal(skillsList.success, true);
assert(skillsList.data.some((skill) => skill.name === "zero" && /Zero/.test(skill.description)));
const skillNames = new Set(skillsList.data.map((skill) => skill.name));
for (const name of [
  "zero",
  "agent",
  "builds",
  "diagnostics",
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
const directArm64ElfBlocked = json(["build", "--json", "--emit", "obj", "--target", "linux-arm64", "examples/direct-call-add.0", "--out", directArm64ElfPath], { allowFailure: true });
assert.notEqual(directArm64ElfBlocked.code, 0);
assert.equal(directArm64ElfBlocked.body.diagnostics[0].code, "BLD004");
assert.equal(directArm64ElfBlocked.body.diagnostics[0].backendBlocker.backend, "zero-elf-aarch64");
assert.equal(directArm64ElfBlocked.body.diagnostics[0].backendBlocker.stage, "buildability");
assert.match(directArm64ElfBlocked.body.diagnostics[0].message, /without parameters|small integer literal/);
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
assert.equal(depBuild.compilerCaches.every((item) => item.compilerVersion === "0.1.3" && item.packageVersion === "0.1.0"), true);
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
const coffDynamicSliceFixture = "conformance/native/pass/coff-dynamic-byte-slice-blocked.0";
const coffDynamicSliceReadiness = json(["check", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffDynamicSliceFixture]).body;
assert.equal(coffDynamicSliceReadiness.ok, true);
assert.equal(coffDynamicSliceReadiness.diagnostics.length, 0);
assert.equal(coffDynamicSliceReadiness.targetReadiness.ok, false);
assert.equal(coffDynamicSliceReadiness.targetReadiness.buildable, false);
assert.equal(coffDynamicSliceReadiness.targetReadiness.languageOk, true);
assert.equal(coffDynamicSliceReadiness.targetReadiness.emit, "obj");
assert.equal(coffDynamicSliceReadiness.targetReadiness.target, "win32-x64.exe");
assert.equal(coffDynamicSliceReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(coffDynamicSliceReadiness.targetReadiness.diagnostics[0].backendBlocker.backend, "zero-coff-x64");
assert.equal(coffDynamicSliceReadiness.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
assert.match(coffDynamicSliceReadiness.targetReadiness.diagnostics[0].message, /constant start/);
const coffDynamicSliceBuild = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffDynamicSliceFixture, "--out", join(outDir, "coff-dynamic-byte-slice.obj")], { allowFailure: true });
assert.notEqual(coffDynamicSliceBuild.code, 0);
for (const key of ["code", "path", "line", "column", "length", "expected", "actual", "help"]) {
  assert.equal(coffDynamicSliceBuild.body.diagnostics[0][key], coffDynamicSliceReadiness.targetReadiness.diagnostics[0][key]);
}
assert.equal(coffDynamicSliceBuild.body.diagnostics[0].backendBlocker.backend, "zero-coff-x64");
assert.equal(coffDynamicSliceBuild.body.diagnostics[0].backendBlocker.stage, "buildability");
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
const machOObjectBlockedReadiness = json(["check", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/memory-package"]).body;
assert.equal(machOObjectBlockedReadiness.ok, true);
assert.equal(machOObjectBlockedReadiness.diagnostics.length, 0);
assert.equal(machOObjectBlockedReadiness.targetReadiness.ok, false);
assert.equal(machOObjectBlockedReadiness.targetReadiness.buildable, false);
assert.equal(machOObjectBlockedReadiness.targetReadiness.languageOk, true);
assert.equal(machOObjectBlockedReadiness.targetReadiness.emit, "obj");
assert.equal(machOObjectBlockedReadiness.targetReadiness.target, "darwin-arm64");
assert.equal(machOObjectBlockedReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(machOObjectBlockedReadiness.targetReadiness.diagnostics[0].backendBlocker.backend, "zero-macho64");
assert.equal(machOObjectBlockedReadiness.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");

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
for (const targetName of ["darwin-arm64", "darwin-x64", "linux-arm64", "linux-x64", "linux-musl-arm64", "linux-musl-x64", "win32-arm64.exe", "win32-x64.exe"]) {
  assert(targets.targets.some((target) => target.name === targetName), `${targetName} should be listed`);
}
assert(targets.targets.some((target) => target.hosted === true && target.capabilities.includes("fs")));
assert(targets.targets.some((target) => target.hosted === false && !target.capabilities.includes("fs")));
const linuxGnuTarget = targets.targets.find((target) => target.name === "linux-x64");
const linuxMuslTarget = targets.targets.find((target) => target.name === "linux-musl-x64");
const darwinArm64Target = targets.targets.find((target) => target.name === "darwin-arm64");
const winX64Target = targets.targets.find((target) => target.name === "win32-x64.exe");
const linuxArm64Target = targets.targets.find((target) => target.name === "linux-arm64");
assert.equal(linuxMuslTarget.directBackend.exeSupported, true);
assert.equal(linuxMuslTarget.directBackend.exeEmitter, "zero-elf64-exe");
assert.equal(linuxGnuTarget.directBackend.objectEmitter, "zero-elf64");
assert.equal(linuxGnuTarget.directBackend.exeSupported, true);
assert.equal(linuxGnuTarget.directBackend.exeEmitter, "zero-elf64-exe");
assert.equal(darwinArm64Target.directBackend.objectEmitter, "zero-macho64");
assert.equal(darwinArm64Target.directBackend.exeSupported, true);
assert.equal(darwinArm64Target.directBackend.exeEmitter, "zero-macho64-exe");
assert.equal(winX64Target.directBackend.objectEmitter, "zero-coff-x64");
assert.equal(winX64Target.directBackend.objectSupported, true);
assert.equal(winX64Target.directBackend.exeSupported, true);
assert.equal(winX64Target.directBackend.exeEmitter, "zero-coff-x64-exe");
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
