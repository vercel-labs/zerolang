#!/usr/bin/env node
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { join } from "node:path";

if (process.env.ZERO_NATIVE_TEST_SANDBOX !== "1" && process.env.ZERO_NATIVE_TEST_ALLOW_LOCAL !== "1") {
  console.error("command contract snapshots emit native test artifacts; run `npm run command-contracts` for Vercel Sandbox execution or set ZERO_NATIVE_TEST_ALLOW_LOCAL=1 to opt into local artifacts.");
  process.exit(1);
}

const outDir = ".zero/command-contracts";
const keysDir = join(outDir, "keys");
const zeroBin = process.env.ZERO_BIN ?? "bin/zero";
const trustedKeysPath = join(process.cwd(), "tests", "fixtures", "trusted-keys.json");
const trustedKeysSigPath = join(process.cwd(), "tests", "fixtures", "trusted-keys.sig");
const seedDefault = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
rmSync(keysDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });

function zero(args, options = {}) {
  const env = {
    ...process.env,
    ZERO_KEYS_DIR: keysDir,
    ZERO_KEY_EMAIL: "command-contracts@example.com",
    ZERO_KEY_NAME: "Command Contracts",
    ZERO_TRUSTED_KEYS: trustedKeysPath,
    ZERO_TRUSTED_KEYS_SIG: trustedKeysSigPath,
    ZERO_KEY_SEED: seedDefault,
  };
  try {
    const stdout = execFileSync(zeroBin, args, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], env });
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
  rmSync(`${repeatOut}.wasm`, { force: true });
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

function wasmI64ConstBytes(value) {
  let n = BigInt(value);
  const bytes = [0x42];
  while (true) {
    let byte = Number(n & 0x7fn);
    n >>= 7n;
    const signSet = (byte & 0x40) !== 0;
    const done = (n === 0n && !signSet) || (n === -1n && signSet);
    if (!done) byte |= 0x80;
    bytes.push(byte);
    if (done) return Buffer.from(bytes);
  }
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
  assert.equal(report.checksum.algorithm, "sha256");
  assert.match(report.checksum.value, /^[0-9a-f]{64}$/);
  assert(report.signature);
  assert.match(report.signature.keyId, /^[0-9a-f]{64}$/);
  assert(existsSync(report.signature.path), `signature should exist at ${report.signature.path}`);
  const artifactKinds = new Set(report.artifacts.map((artifact) => artifact.kind));
  for (const kind of ["binary", "stripped-binary", "checksum", "checksum-signature", "archive", "debug-symbol-metadata", "size-report", "sbom-placeholder"]) {
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

const version = json(["--version", "--json"]).body;
assert.equal(version.schemaVersion, 1);
assert.equal(version.version, "0.1.2");
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
assert(doctor.checks.some((check) => check.name === "wasi-runner" && /WASI|wasmtime|wasmer|wasmedge/.test(check.message)));
assert(doctor.checks.some((check) => check.name === "docs-examples"));
assert(Array.isArray(doctor.wasiRunners));
assert(doctor.wasiRunners.some((runner) => runner.name === "wasmtime"));

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
]) {
  assert.match(zero(command).stdout, expected);
}

const skillsList = json(["skills", "list", "--json"]).body;
assert.equal(skillsList.success, true);
assert(skillsList.data.some((skill) => skill.name === "zero" && /Zero/.test(skill.description)));
const skillNames = new Set(skillsList.data.map((skill) => skill.name));
for (const name of [
  "zero",
  "zero-agent",
  "zero-builds",
  "zero-diagnostics",
  "zero-language",
  "zero-packages",
  "zero-stdlib",
  "zero-testing",
]) {
  assert(skillNames.has(name), `missing bundled skill ${name}`);
}

const zeroSkill = json(["skills", "get", "zero", "--full", "--json"]).body;
assert.equal(zeroSkill.success, true);
assert.match(zeroSkill.data[0].content, /# Zero/);
assert.match(zeroSkill.data[0].content, /zero skills get zero --full/);
assert.equal(zeroSkill.data[0].files, undefined);

const languageSkill = json(["skills", "get", "zero-language", "--json"]).body;
assert.equal(languageSkill.success, true);
assert.match(languageSkill.data[0].content, /# Zero Language/);
assert.match(languageSkill.data[0].content, /pub fun main/);

const diagnosticSkill = json(["skills", "get", "zero-diagnostics", "--json"]).body;
assert.equal(diagnosticSkill.success, true);
assert.match(diagnosticSkill.data[0].content, /fixSafety/);

const skillsPath = json(["skills", "path", "zero", "--json"]).body;
assert.equal(skillsPath.success, true);
assert.match(skillsPath.data.path, /skills\/zero$/);

const languagePath = json(["skills", "path", "zero-language", "--json"]).body;
assert.equal(languagePath.success, true);
assert.match(languagePath.data.path, /skill-data\/zero-language\.md$/);

const missingSkill = zero(["skills", "get", "missing", "--json"], { allowFailure: true });
assert.notEqual(missingSkill.code, 0);
assert.equal(JSON.parse(missingSkill.stdout).success, false);

const lexerTokens = json(["tokens", "--json", "conformance/lexer/compiler-smoke.0"]).body;
assert.equal(lexerTokens.schemaVersion, 1);
assert.match(lexerTokens.sourceFile, /compiler-smoke\.0$/);
assert.deepEqual(lexerTokens.tokens.slice(0, 4).map((token) => `${token.kind}:${token.text}`), [
  "keyword:use",
  "keyword:pub",
  "keyword:fun",
  "ident:main",
]);
assert.deepEqual(lexerTokens.tokens.slice(4, 8).map((token) => token.text), ["123", "0xff", "0b101", "42_u8"]);
assert.deepEqual(lexerTokens.tokens.slice(8, 12).map((token) => `${token.kind}:${token.text}`), [
  "string:hi",
  "char:120",
  "symbol:(",
  "symbol:)",
]);
assert.equal(lexerTokens.tokens[0].line, 1);
assert.equal(lexerTokens.tokens[0].column, 1);
assert.equal(lexerTokens.tokens[0].offset, 0);
assert.equal(lexerTokens.tokens[0].length, 3);
assert.equal(lexerTokens.tokens[5].offset, 21);
assert.equal(lexerTokens.tokens[5].length, 4);
assert.equal(lexerTokens.tokens[12].kind, "ident");
assert.equal(lexerTokens.tokens[12].text, "main");
assert.equal(lexerTokens.tokens[12].line, 2);
assert.equal(lexerTokens.tokens[12].column, 1);
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
  assert.match(created, /Here's your package:/);
  assert.match(created, /pubkey:/);
  assert.match(created, /next: zero check/);
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
const directIfWasmPath = join(outDir, "direct-if-return.wasm");
rmSync(directIfWasmPath, { force: true });
const directIfWasmReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "examples/direct-if-return.0", "--out", directIfWasmPath]).body;
assert.equal(directIfWasmReport.emit, "wasm");
assert.equal(directIfWasmReport.generatedCBytes, 0);
assert.equal(directIfWasmReport.objectBackend.objectEmission.path, "direct-wasm");
assertReleaseTargetContract(directIfWasmReport, {
  target: "wasm32-wasi",
  emit: "wasm",
  objectFormat: "wasm",
  artifactKind: "wasm-module",
  linkerFlavor: "wasm",
  targetLibcMode: "bundled-libc",
});
repeatBuildHash(["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "examples/direct-if-return.0", "--out", directIfWasmPath], directIfWasmPath, join(outDir, "direct-if-return.repeat.wasm"));
const directCallWasmPath = join(outDir, "direct-call-branch.wasm");
rmSync(directCallWasmPath, { force: true });
const directCallWasmReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "examples/direct-call-branch.0", "--out", directCallWasmPath]).body;
assert.equal(directCallWasmReport.emit, "wasm");
assert.equal(directCallWasmReport.generatedCBytes, 0);
assert.equal(directCallWasmReport.objectBackend.objectEmission.path, "direct-wasm");
const directArrayWasmPath = join(outDir, "direct-array-sum.wasm");
rmSync(directArrayWasmPath, { force: true });
const directArrayWasmReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "examples/direct-array-sum.0", "--out", directArrayWasmPath]).body;
assert.equal(directArrayWasmReport.emit, "wasm");
assert.equal(directArrayWasmReport.generatedCBytes, 0);
assert(directArrayWasmReport.objectBackend.directFacts.stackBytes > 0);
assert.equal(directArrayWasmReport.objectBackend.objectEmission.path, "direct-wasm");
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
const directWebWasmPath = join(outDir, "direct-web-wasm");
rmSync(`${directWebWasmPath}.wasm`, { force: true });
const directWebWasmReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-wasm-add.0", "--out", directWebWasmPath]).body;
const directWebWasmBytes = readFileSync(`${directWebWasmPath}.wasm`);
assert.equal(directWebWasmReport.target, "wasm32-web");
assert.equal(directWebWasmReport.compiler, "zero-wasm");
assert.equal(directWebWasmReport.generatedCBytes, 0);
assert.equal(directWebWasmReport.objectBackend.targetFacts.selectedEmitter, "zero-wasm");
assert.equal(directWebWasmBytes[0], 0);
assert.equal(directWebWasmBytes[1], 0x61);
assert.equal(directWebWasmBytes[2], 0x73);
assert.equal(directWebWasmBytes[3], 0x6d);
const directU8HelperPath = join(outDir, "direct-u8-helper-call");
rmSync(`${directU8HelperPath}.wasm`, { force: true });
const directU8HelperReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-u8-helper-call.0", "--out", directU8HelperPath]).body;
const directU8HelperBytes = readFileSync(`${directU8HelperPath}.wasm`);
assert.equal(directU8HelperReport.generatedCBytes, 0);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directU8HelperBytes)).exports.main(), 1);
const directArrayBoundsTrapPath = join(outDir, "direct-array-bounds-trap");
rmSync(`${directArrayBoundsTrapPath}.wasm`, { force: true });
const directArrayBoundsTrapReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-array-bounds-trap.0", "--out", directArrayBoundsTrapPath]).body;
const directArrayBoundsTrapBytes = readFileSync(`${directArrayBoundsTrapPath}.wasm`);
assert.equal(directArrayBoundsTrapReport.generatedCBytes, 0);
assert.equal(directArrayBoundsTrapReport.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(directArrayBoundsTrapReport.objectBackend.directFacts.runtime.boundsTraps, "pay-as-used");
assert.throws(() => new WebAssembly.Instance(new WebAssembly.Module(directArrayBoundsTrapBytes)).exports.main(), WebAssembly.RuntimeError);
const directUnhandledErrorWasmPath = join(outDir, "direct-unhandled-error-exit");
rmSync(`${directUnhandledErrorWasmPath}.wasm`, { force: true });
const directUnhandledErrorWasmReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-unhandled-error-exit.0", "--out", directUnhandledErrorWasmPath]).body;
const directUnhandledErrorWasmBytes = readFileSync(`${directUnhandledErrorWasmPath}.wasm`);
assert.equal(directUnhandledErrorWasmReport.generatedCBytes, 0);
assert.equal(directUnhandledErrorWasmReport.objectBackend.objectEmission.path, "direct-wasm");
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directUnhandledErrorWasmBytes)).exports.main(), 1);
const directFsFallibleResourcesWasiPath = join(outDir, "direct-std-fs-fallible-resources-wasi");
rmSync(`${directFsFallibleResourcesWasiPath}.wasm`, { force: true });
const directFsFallibleResourcesWasiReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "conformance/native/pass/std-fs-fallible-resources.0", "--out", directFsFallibleResourcesWasiPath]).body;
const directFsFallibleResourcesWasiBytes = readFileSync(`${directFsFallibleResourcesWasiPath}.wasm`);
assert.equal(directFsFallibleResourcesWasiReport.generatedCBytes, 0);
assert.equal(directFsFallibleResourcesWasiReport.objectBackend.objectEmission.path, "direct-wasm");
assert.equal(directFsFallibleResourcesWasiReport.targetSupport.fsAvailable, true);
assert(directFsFallibleResourcesWasiBytes.includes(wasmI64ConstBytes(2n << 32n)));
assert(directFsFallibleResourcesWasiBytes.includes(wasmI64ConstBytes(4n << 32n)));
const directFsFallibleWasiPath = join(outDir, "direct-std-fs-fallible-wasi");
rmSync(`${directFsFallibleWasiPath}.wasm`, { force: true });
const directFsFallibleWasiReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "conformance/native/pass/std-fs-fallible.0", "--out", directFsFallibleWasiPath]).body;
const directFsFallibleWasiBytes = readFileSync(`${directFsFallibleWasiPath}.wasm`);
assert.equal(directFsFallibleWasiReport.generatedCBytes, 0);
assert.equal(directFsFallibleWasiReport.objectBackend.objectEmission.path, "direct-wasm");
assert.equal(directFsFallibleWasiReport.targetSupport.fsAvailable, true);
assert(directFsFallibleWasiBytes.includes(wasmI64ConstBytes(2n << 32n)));
assert(directFsFallibleWasiBytes.includes(wasmI64ConstBytes(3n << 32n)));
assert(directFsFallibleWasiBytes.includes(wasmI64ConstBytes(4n << 32n)));
const directStringLenPath = join(outDir, "direct-string-len");
rmSync(`${directStringLenPath}.wasm`, { force: true });
const directStringLenReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-string-len.0", "--out", directStringLenPath]).body;
const directStringLenBytes = readFileSync(`${directStringLenPath}.wasm`);
assert.equal(directStringLenReport.generatedCBytes, 0);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directStringLenBytes)).exports.main(), 5);
const directStringLiteralPath = join(outDir, "direct-string-literal");
rmSync(`${directStringLiteralPath}.wasm`, { force: true });
const directStringLiteralReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-string-literal.0", "--out", directStringLiteralPath]).body;
const directStringLiteralBytes = readFileSync(`${directStringLiteralPath}.wasm`);
assert.equal(directStringLiteralReport.generatedCBytes, 0);
assert.equal(directStringLiteralReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directStringLiteralReport.objectBackend.directFacts.runtime.readonlyDataBytes, 6);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directStringLiteralBytes)).exports.main(), 116);
const directSpanReadPath = join(outDir, "direct-span-read");
rmSync(`${directSpanReadPath}.wasm`, { force: true });
const directSpanReadReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-span-read.0", "--out", directSpanReadPath]).body;
const directSpanReadBytes = readFileSync(`${directSpanReadPath}.wasm`);
assert.equal(directSpanReadReport.generatedCBytes, 0);
assert.equal(directSpanReadReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directSpanReadReport.objectBackend.directFacts.runtime.readonlyDataBytes, 6);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directSpanReadBytes)).exports.main(), 107);
const directCrc32BytesPath = join(outDir, "direct-crc32-bytes");
rmSync(`${directCrc32BytesPath}.wasm`, { force: true });
const directCrc32BytesReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-crc32-bytes.0", "--out", directCrc32BytesPath]).body;
const directCrc32BytesBytes = readFileSync(`${directCrc32BytesPath}.wasm`);
assert.equal(directCrc32BytesReport.generatedCBytes, 0);
assert.equal(directCrc32BytesReport.objectBackend.objectEmission.path, "direct-wasm");
assert.equal(directCrc32BytesReport.objectBackend.directFacts.runtime.readonlyDataBytes, 19);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directCrc32BytesBytes)).exports.main(), 1120241454);
const directStringEqlPath = join(outDir, "direct-string-eql");
rmSync(`${directStringEqlPath}.wasm`, { force: true });
const directStringEqlReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-string-eql.0", "--out", directStringEqlPath]).body;
const directStringEqlBytes = readFileSync(`${directStringEqlPath}.wasm`);
assert.equal(directStringEqlReport.generatedCBytes, 0);
assert.equal(directStringEqlReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directStringEqlReport.objectBackend.directFacts.runtime.readonlyDataBytes, 12);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directStringEqlBytes)).exports.main(), 1);
const directByteViewLocalsPath = join(outDir, "direct-byte-view-locals");
rmSync(`${directByteViewLocalsPath}.wasm`, { force: true });
const directByteViewLocalsReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-byte-view-locals.0", "--out", directByteViewLocalsPath]).body;
const directByteViewLocalsBytes = readFileSync(`${directByteViewLocalsPath}.wasm`);
assert.equal(directByteViewLocalsReport.generatedCBytes, 0);
assert.equal(directByteViewLocalsReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directByteViewLocalsReport.objectBackend.directFacts.runtime.readonlyDataBytes, 10);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directByteViewLocalsBytes)).exports.main(), 107);
const directMutSpanLenPath = join(outDir, "direct-mutspan-len");
rmSync(`${directMutSpanLenPath}.wasm`, { force: true });
const directMutSpanLenReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-mutspan-len.0", "--out", directMutSpanLenPath]).body;
const directMutSpanLenBytes = readFileSync(`${directMutSpanLenPath}.wasm`);
assert.equal(directMutSpanLenReport.generatedCBytes, 0);
assert.equal(directMutSpanLenReport.objectBackend.directFacts.runtime.linearMemory, true);
const directMutSpanLenInstance = new WebAssembly.Instance(new WebAssembly.Module(directMutSpanLenBytes));
assert.equal(directMutSpanLenInstance.exports.main(), 5);
assert(directMutSpanLenInstance.exports.memory instanceof WebAssembly.Memory);
const directMemoryPeekPath = join(outDir, "direct-memory-peek");
rmSync(`${directMemoryPeekPath}.wasm`, { force: true });
const directMemoryPeekReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-memory-peek.0", "--out", directMemoryPeekPath]).body;
const directMemoryPeekBytes = readFileSync(`${directMemoryPeekPath}.wasm`);
assert.equal(directMemoryPeekReport.generatedCBytes, 0);
assert.equal(directMemoryPeekReport.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(directMemoryPeekReport.objectBackend.directFacts.exportCount, 3);
const directMemoryPeekInstance = new WebAssembly.Instance(new WebAssembly.Module(directMemoryPeekBytes));
assert(directMemoryPeekInstance.exports.memory instanceof WebAssembly.Memory);
new Uint8Array(directMemoryPeekInstance.exports.memory.buffer)[1024] = 90;
assert.equal(directMemoryPeekInstance.exports.read_at(1024), 90);
const directByteCopyFillPath = join(outDir, "direct-byte-copy-fill");
rmSync(`${directByteCopyFillPath}.wasm`, { force: true });
const directByteCopyFillReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-byte-copy-fill.0", "--out", directByteCopyFillPath]).body;
const directByteCopyFillBytes = readFileSync(`${directByteCopyFillPath}.wasm`);
assert.equal(directByteCopyFillReport.generatedCBytes, 0);
assert.equal(directByteCopyFillReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directByteCopyFillReport.objectBackend.directFacts.runtime.readonlyDataBytes, 6);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directByteCopyFillBytes)).exports.main(), 111);
const directAllocBumpPath = join(outDir, "direct-alloc-bump");
rmSync(`${directAllocBumpPath}.wasm`, { force: true });
const directAllocBumpReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-alloc-bump.0", "--out", directAllocBumpPath]).body;
const directAllocBumpBytes = readFileSync(`${directAllocBumpPath}.wasm`);
assert.equal(directAllocBumpReport.generatedCBytes, 0);
assert.equal(directAllocBumpReport.objectBackend.directFacts.allocatorHelperCount, 2);
assert.equal(directAllocBumpReport.objectBackend.directFacts.runtime.heapPolicy, "explicit-fixed-buffer-bump");
assert.equal(directAllocBumpReport.objectBackend.directFacts.runtime.allocator, "FixedBufAlloc");
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directAllocBumpBytes)).exports.main(), 114);
const directAllocOverflowPath = join(outDir, "direct-alloc-overflow");
rmSync(`${directAllocOverflowPath}.wasm`, { force: true });
const directAllocOverflowReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-alloc-overflow.0", "--out", directAllocOverflowPath]).body;
const directAllocOverflowBytes = readFileSync(`${directAllocOverflowPath}.wasm`);
assert.equal(directAllocOverflowReport.generatedCBytes, 0);
assert.equal(directAllocOverflowReport.objectBackend.directFacts.allocatorHelperCount, 2);
assert.equal(directAllocOverflowReport.objectBackend.directFacts.runtime.heapPolicy, "explicit-fixed-buffer-bump");
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directAllocOverflowBytes)).exports.main(), 1);
const directTokenShapePath = join(outDir, "direct-token-shape");
rmSync(`${directTokenShapePath}.wasm`, { force: true });
const directTokenShapeReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-token-shape.0", "--out", directTokenShapePath]).body;
const directTokenShapeBytes = readFileSync(`${directTokenShapePath}.wasm`);
assert.equal(directTokenShapeReport.generatedCBytes, 0);
assert.equal(directTokenShapeReport.objectBackend.directFacts.runtime.linearMemory, true);
assert(directTokenShapeReport.objectBackend.directFacts.maxFrameBytes > 0);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directTokenShapeBytes)).exports.main(), 65);
const directEnumMatchPath = join(outDir, "direct-enum-match");
rmSync(`${directEnumMatchPath}.wasm`, { force: true });
const directEnumMatchReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-enum-match.0", "--out", directEnumMatchPath]).body;
const directEnumMatchBytes = readFileSync(`${directEnumMatchPath}.wasm`);
assert.equal(directEnumMatchReport.generatedCBytes, 0);
assert.equal(directEnumMatchReport.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directEnumMatchBytes)).exports.main(), 2);
const directRaisesBasicPath = join(outDir, "direct-raises-basic");
rmSync(`${directRaisesBasicPath}.wasm`, { force: true });
const directRaisesBasicReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-raises-basic.0", "--out", directRaisesBasicPath]).body;
const directRaisesBasicBytes = readFileSync(`${directRaisesBasicPath}.wasm`);
assert.equal(directRaisesBasicReport.generatedCBytes, 0);
assert.equal(directRaisesBasicReport.objectBackend.targetFacts.selectedEmitter, "zero-wasm");
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directRaisesBasicBytes)).exports.main(), 7);
const directRescueBasicPath = join(outDir, "direct-rescue-basic");
rmSync(`${directRescueBasicPath}.wasm`, { force: true });
const directRescueBasicReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-rescue-basic.0", "--out", directRescueBasicPath]).body;
const directRescueBasicBytes = readFileSync(`${directRescueBasicPath}.wasm`);
assert.equal(directRescueBasicReport.generatedCBytes, 0);
assert.equal(directRescueBasicReport.objectBackend.targetFacts.selectedEmitter, "zero-wasm");
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directRescueBasicBytes)).exports.main(), 9);
const directByteBufPath = join(outDir, "direct-byte-buf");
rmSync(`${directByteBufPath}.wasm`, { force: true });
const directByteBufReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-byte-buf.0", "--out", directByteBufPath]).body;
const directByteBufBytes = readFileSync(`${directByteBufPath}.wasm`);
assert.equal(directByteBufReport.generatedCBytes, 0);
assert.equal(directByteBufReport.objectBackend.directFacts.bufferHelperCount, 3);
assert.equal(directByteBufReport.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(directByteBufReport.objectBackend.directFacts.runtime.buffer, "Vec<u8>");
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directByteBufBytes)).exports.main(), 66);
const directGenericIdentityPath = join(outDir, "direct-generic-identity");
rmSync(`${directGenericIdentityPath}.wasm`, { force: true });
const directGenericIdentityReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-generic-identity.0", "--out", directGenericIdentityPath]).body;
const directGenericIdentityBytes = readFileSync(`${directGenericIdentityPath}.wasm`);
assert.equal(directGenericIdentityReport.generatedCBytes, 0);
assert.equal(directGenericIdentityReport.objectBackend.directFacts.functionCount, 2);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directGenericIdentityBytes)).exports.main(), 42);
const directGenericFixedBufPath = join(outDir, "direct-generic-fixedbuf");
rmSync(`${directGenericFixedBufPath}.wasm`, { force: true });
const directGenericFixedBufReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-generic-fixedbuf.0", "--out", directGenericFixedBufPath]).body;
const directGenericFixedBufBytes = readFileSync(`${directGenericFixedBufPath}.wasm`);
assert.equal(directGenericFixedBufReport.generatedCBytes, 0);
assert.equal(directGenericFixedBufReport.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directGenericFixedBufBytes)).exports.main(), 55);
const directGenericVecPath = join(outDir, "direct-generic-vec");
rmSync(`${directGenericVecPath}.wasm`, { force: true });
const directGenericVecReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-generic-vec.0", "--out", directGenericVecPath]).body;
const directGenericVecBytes = readFileSync(`${directGenericVecPath}.wasm`);
assert.equal(directGenericVecReport.generatedCBytes, 0);
assert.equal(directGenericVecReport.objectBackend.directFacts.runtime.linearMemory, true);
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directGenericVecBytes)).exports.main(), 61);
const directStdIoMem = json(["mem", "--json", "conformance/native/pass/std-io-direct.0"]).body;
assert.equal(directStdIoMem.generatedCBytes, 0);
assert.equal(directStdIoMem.cBridgeFallback, false);
assert.equal(directStdIoMem.memory.hiddenHeapAllocation, false);
assert.equal(directStdIoMem.memory.stackBytes > 0, true);
assert.equal(directStdIoMem.memory.maxFrameBytes > 0, true);
assert.equal(directStdIoMem.directFacts.runtimeHelperCount, 1);
assert(directStdIoMem.usedStdlibHelpers.some((helper) => helper.name === "std.io.bufferedReader"));
assert(directStdIoMem.usedStdlibHelpers.some((helper) => helper.name === "std.io.bufferedWriter"));
assert(directStdIoMem.usedStdlibHelpers.some((helper) => helper.name === "std.io.copy"));
assert.equal(directStdIoMem.capabilityFacts.worldStdio, "available");
const directPackageCallOrderPath = join(outDir, "direct-package-call-order");
rmSync(`${directPackageCallOrderPath}.wasm`, { force: true });
const directPackageCallOrderReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/direct-package-call-order", "--out", directPackageCallOrderPath]).body;
const directPackageCallOrderBytes = readFileSync(`${directPackageCallOrderPath}.wasm`);
assert.equal(directPackageCallOrderReport.generatedCBytes, 0);
assert.equal(directPackageCallOrderReport.objectBackend.directFacts.moduleCount, 3);
assert.equal(directPackageCallOrderReport.objectBackend.internalIr.functionIdentity, "module-qualified-stable-sorted");
assert.equal(new WebAssembly.Instance(new WebAssembly.Module(directPackageCallOrderBytes)).exports.main(), 27);
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
let sawMachOUnsignedReloc = false;
for (let i = 0; i < directMachODataRelocCount; i++) {
  const info = directMachODataBytes.readUInt32LE(directMachODataRelocOffset + i * 8 + 4);
  sawMachOUnsignedReloc ||= ((info >>> 28) & 15) === 0;
}
assert.equal(sawMachOUnsignedReloc, true);
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
const directArm64ElfReport = json(["build", "--json", "--emit", "obj", "--target", "linux-arm64", "examples/direct-call-add.0", "--out", directArm64ElfPath]).body;
const directArm64ElfBytes = readFileSync(directArm64ElfPath);
assert.equal(directArm64ElfReport.compiler, "zero-elf-aarch64");
assert.equal(directArm64ElfReport.objectBackend.objectEmission.path, "direct-elf-aarch64-object");
assert.equal(directArm64ElfReport.objectBackend.linking.objectFormat, "elf");
assert.equal(directArm64ElfReport.generatedCBytes, 0);
assert.equal(directArm64ElfBytes[0], 0x7f);
assert.equal(directArm64ElfBytes.toString("ascii", 1, 4), "ELF");
assert.equal(directArm64ElfBytes.readUInt16LE(18), 183);
assert(directArm64ElfBytes.includes(Buffer.concat([Buffer.from("main"), Buffer.from([0])])));
assert(directArm64ElfBytes.includes(Buffer.from([0x00, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6])));
const compilerZeroStage0Path = join(outDir, "compiler-zero-stage0");
rmSync(`${compilerZeroStage0Path}.wasm`, { force: true });
const compilerZeroStage0Report = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "compiler-zero", "--out", compilerZeroStage0Path]).body;
const compilerZeroStage0Bytes = readFileSync(`${compilerZeroStage0Path}.wasm`);
assert.equal(compilerZeroStage0Report.emit, "wasm");
assert.equal(compilerZeroStage0Report.target, "wasm32-web");
assert.equal(compilerZeroStage0Report.generatedCBytes, 0);
assert.equal(compilerZeroStage0Report.productCliHandoff, true);
assert.equal(compilerZeroStage0Report.objectBackend.directFacts.moduleCount, 10);
assert.equal(compilerZeroStage0Report.objectBackend.directFacts.bufferHelperCount, 3);
assert.equal(compilerZeroStage0Report.selfHostRouting.subsetCompatible, true);
assert.equal(compilerZeroStage0Report.selfHostRouting.mode, "self-hosted-cli");
assert.equal(compilerZeroStage0Report.selfHostRouting.phases.parse, "compiler-zero-wasm");
assert.equal(compilerZeroStage0Report.selfHostRouting.phases.emit, "compiler-zero-wasm");
assert.equal(compilerZeroStage0Report.selfHostRouting.phases.selfHostCompilerRole, "product-cli");
assert.equal(compilerZeroStage0Report.selfHostRouting.frontend.selected, true);
assert.equal(compilerZeroStage0Report.selfHostRouting.wasmEmitter.selected, true);
assert.equal(compilerZeroStage0Report.selfHostRouting.cBridge.required, false);
assert.equal(compilerZeroStage0Bytes[0], 0);
assert.equal(compilerZeroStage0Bytes[1], 0x61);
const compilerZeroStage0Instance = new WebAssembly.Instance(new WebAssembly.Module(compilerZeroStage0Bytes));
assert.equal(compilerZeroStage0Instance.exports.main(), 48);
assert(compilerZeroStage0Instance.exports.memory instanceof WebAssembly.Memory);
assert.equal(compilerZeroStage0Instance.exports.compiler_runtime_arena_plan(64, 16), 54);
assert.equal(compilerZeroStage0Instance.exports.compiler_source_input_mode(1, 1, 0), 61);
assert.equal(compilerZeroStage0Instance.exports.compiler_source_input_mode(2, 0, 1), 62);
const compilerZeroSourceBytes = new TextEncoder().encode("pub fun main() -> Void {}\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroSourceBytes, 1024);
assert.equal(compilerZeroStage0Instance.exports.compiler_accept_source_buffer(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 63);
assert.equal(compilerZeroStage0Instance.exports.compiler_token_summary(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 1025);
assert.equal(compilerZeroStage0Instance.exports.compiler_source_hash(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength) >>> 0, 3863097883);
assert.equal(compilerZeroStage0Instance.exports.compiler_source_location_at(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 18), 1019);
assert.equal(compilerZeroStage0Instance.exports.compiler_source_location_at(1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 26), 2001);
assert.equal(compilerZeroStage0Instance.exports.compiler_source_order_key(1, 2, 3863097883, 3863097884), 64);
assert.equal(compilerZeroStage0Instance.exports.compiler_source_order_key(2, 1, 3863097883, 3863097884), 65);
assert.equal(compilerZeroStage0Instance.exports.compiler_source_order_key(1, 1, 3863097883, 3863097884), 66);
const compilerZeroManifestBytes = readFileSync("compiler-zero/zero.json");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroManifestBytes, 4096);
assert.equal(compilerZeroStage0Instance.exports.compiler_manifest_summary(4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 52);
assert.equal(compilerZeroStage0Instance.exports.compiler_hosted_package_source_plan(4096, compilerZeroManifestBytes.length, 1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1), 68);
assert.equal(compilerZeroStage0Instance.exports.compiler_hosted_package_source_plan(4096, compilerZeroManifestBytes.length, 1024, compilerZeroSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 0), 0);
const compilerZeroRuntimeAbiBytes = new TextEncoder().encode('export c fun add(a: i32, b: i32) -> i32 { return a + b }\npub fun main(world: World, args: Args, env: Env, alloc: Alloc) -> Void raises { Bad } { let bytes: [4]u8 = [1, 2, 3, 4] check world.out.write("ok") check world.err.write("bad") raise Bad }\n');
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroRuntimeAbiBytes, 34816);
assert.equal(compilerZeroStage0Instance.exports.compiler_runtime_abi_summary(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1), 1023);
assert.equal(compilerZeroStage0Instance.exports.compiler_runtime_memory_layout(64, 4, 1), 6400041);
assert.equal(compilerZeroStage0Instance.exports.compiler_runtime_shim_cost(1, 1, 1, 1, 1), 11610);
assert.equal(compilerZeroStage0Instance.exports.compiler_runtime_helper_summary(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1), 249);
assert.equal(compilerZeroStage0Instance.exports.compiler_runtime_helper_summary(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 2), 250);
assert.equal(compilerZeroStage0Instance.exports.compiler_runtime_helper_summary(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 3), 252);
assert.equal(compilerZeroStage0Instance.exports.compiler_browser_compile_request(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1, 2, 3), 1020363);
assert.equal(compilerZeroStage0Instance.exports.compiler_browser_compile_response(511, 255, 65535, 141), 800141);
assert.equal(compilerZeroStage0Instance.exports.compiler_browser_artifact_plan(34816, compilerZeroRuntimeAbiBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1, 3), 1311);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_source_graph_plan(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1), 52680001);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_compile_request(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1, 2, 3), 1821673);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_module_graph_packet(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1), 13171111);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_source_diag_code(1024, compilerZeroSourceBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1), 0);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_write_minimal_wasm(8192, 8, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 8);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_minimal_wasm_hash(8192, 8, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 1836278016);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_write_return42_wasm(14336, 64, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 38);
const compilerZeroReturn42Bytes = new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).slice(14336, 14336 + 38);
const compilerZeroReturn42Instance = new WebAssembly.Instance(new WebAssembly.Module(compilerZeroReturn42Bytes));
assert.equal(compilerZeroReturn42Instance.exports.main(), 42);
const compilerZeroStage0PlaygroundHelloBytes = new TextEncoder().encode('pub fun main(world: World) -> Void raises { check world.out.write("hello from zero\\n") }\n');
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroStage0PlaygroundHelloBytes, 20480);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_write_playground_wasm(20480, compilerZeroStage0PlaygroundHelloBytes.length, 24576, 512, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 157);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_command_response(1, 52680001, 1821673, 1836278016), 4010001);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_command_response(2, 52680001, 1821673, 1836278016), 4020008);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_command_response(3, 52680001, 1821673, 1836278016), 4030001);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_command_response(4, 52680001, 1821673, 1836278016), 4040008);
const compilerDriverCheck = json(["check", "--json", "--target", "wasm32-web", "examples/hello.0"]).body;
assert.equal(compilerDriverCheck.productCliHandoff, true);
assert.equal(compilerDriverCheck.selfHostRouting.mode, "self-hosted-driver");
assert.equal(compilerDriverCheck.selfHostRouting.phases.check, "compiler-zero-wasm");
assert.equal(compilerDriverCheck.selfHostRouting.nativeFallback.used, false);
assert.equal(compilerDriverCheck.selfHostDriver.owner, "compiler-zero");
assert.equal(compilerDriverCheck.selfHostDriver.input, "examples/hello.0");
assert.equal(compilerDriverCheck.selfHostDriver.commandResponse, 4010001);
const compilerDriverBuildPath = join(outDir, "compiler-driver-hello");
rmSync(`${compilerDriverBuildPath}.wasm`, { force: true });
const compilerDriverBuild = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-web", "examples/hello.0", "--out", compilerDriverBuildPath]).body;
const compilerDriverBuildBytes = readFileSync(`${compilerDriverBuildPath}.wasm`);
assert.equal(compilerDriverBuild.selfHostRouting.mode, "self-hosted-driver");
assert.equal(compilerDriverBuild.selfHostRouting.phases.emit, "compiler-zero-wasm");
assert.equal(compilerDriverBuild.selfHostRouting.nativeFallback.used, false);
assert.equal(compilerDriverBuild.selfHostDriver.commandResponse, 4020008);
assert.equal(compilerDriverBuild.generatedCBytes, 0);
assert.equal(compilerDriverBuild.cBridgeFallback, false);
assert.equal(compilerDriverBuild.artifactBytes, 157);
assert.equal(compilerDriverBuildBytes[0], 0);
assert.equal(compilerDriverBuildBytes[1], 0x61);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_fixed_point_packet(2, 4020008, 7009121, 1836278016), 2203300);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_fixed_point_packet(3, 4020008, 7009121, 1836278016), 3303300);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_native_artifact_plan(1, 1, 0, 0), 801101);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_native_artifact_plan(2, 2, 0, 0), 802202);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_native_artifact_plan(2, 3, 0, 0), 802303);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_native_artifact_plan(2, 2, 0, 1), 0);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_target_capability_diag(2, 1, 1, 3), 0);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_target_capability_diag(5, 1, 0, 2), 6002);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_required_capabilities(), 8191);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_supported_capabilities(), 8191);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_gap_mask(), 0);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_gap_diag_code(512), 7003);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_gap_diag_code(1024), 7004);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_gap_diag_code(2048), 7005);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_gap_diag_code(4096), 7006);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_stage_plan(3, 8191, 8191, 0, 0), 3300);
const compilerZeroUnsupportedSelfHostBytes = new TextEncoder().encode("pub extern c fun main() -> i32\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroUnsupportedSelfHostBytes, 12288);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_source_diag_code(12288, compilerZeroUnsupportedSelfHostBytes.length, 4096, compilerZeroManifestBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1), 7009);
assert.equal(compilerZeroStage0Instance.exports.compiler_self_host_diagnostic_packet(7009, 1, 4, 2), 7009121);
const compilerZeroModuleSourceBytes = new TextEncoder().encode("use lib\npub fun main() -> Void {}\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroModuleSourceBytes, 8192);
assert.equal(compilerZeroStage0Instance.exports.compiler_module_import_summary(8192, compilerZeroModuleSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 2), 73);
assert.equal(compilerZeroStage0Instance.exports.compiler_module_import_summary(8192, compilerZeroModuleSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1), 0);
assert.equal(compilerZeroStage0Instance.exports.compiler_module_import_diag_code(8192, compilerZeroModuleSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1), 3003);
assert.equal(compilerZeroStage0Instance.exports.compiler_module_graph_reject_code(1, 1, 0, 0, 0), 3003);
assert.equal(compilerZeroStage0Instance.exports.compiler_module_graph_reject_code(2, 1, 1, 0, 0), 3008);
assert.equal(compilerZeroStage0Instance.exports.compiler_module_graph_reject_code(2, 1, 0, 1, 0), 3009);
assert.equal(compilerZeroStage0Instance.exports.compiler_module_graph_reject_code(2, 1, 0, 0, 1), 3010);
const compilerZeroScopeBytes = new TextEncoder().encode("pub shape Point { x: i32 }\npub fun main() -> Void {\n let x = 1\n let y = x\n}\nfun helper() -> Void {}\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroScopeBytes, 32768);
assert.equal(compilerZeroStage0Instance.exports.compiler_scope_summary(32768, compilerZeroScopeBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 40202);
assert.equal(compilerZeroStage0Instance.exports.compiler_name_resolution_packet(32768, compilerZeroScopeBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 0, 0), 0);
assert.equal(compilerZeroStage0Instance.exports.compiler_name_resolution_packet(32768, compilerZeroScopeBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1, 0), 3003);
assert.equal(compilerZeroStage0Instance.exports.compiler_name_resolution_packet(32768, compilerZeroScopeBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 0, 1), 3009);
const compilerZeroMemberNameBytes = new TextEncoder().encode("type BytePair = Pair<u8, u8>\nshape Pair<T, static N: usize> { left: T }\npub fun main() -> Void { pair.left }\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroMemberNameBytes, 40960);
assert.equal(compilerZeroStage0Instance.exports.compiler_member_name_summary(40960, compilerZeroMemberNameBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 1010201);
const compilerZeroTypeSurfaceBytes = new TextEncoder().encode("shape Box { items: [4]u8, name: String, next: ref<Box> } enum Color { red } choice Event { quit } pub fun main(flag: Bool) -> Void { let n: usize = 1 }\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroTypeSurfaceBytes, 53248);
assert.equal(compilerZeroStage0Instance.exports.compiler_type_surface_summary(53248, compilerZeroTypeSurfaceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 63);
const compilerZeroExpressionSurfaceBytes = new TextEncoder().encode("let mut pair: Pair = Pair { left: 1, right: 2 } pair.left = pair.items[0]");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroExpressionSurfaceBytes, 57344);
assert.equal(compilerZeroStage0Instance.exports.compiler_expression_surface_summary(57344, compilerZeroExpressionSurfaceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 63);
const compilerZeroControlFlowBytes = new TextEncoder().encode("if ok { check call() } while ok { return } rescue err { return }");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroControlFlowBytes, 61440);
assert.equal(compilerZeroStage0Instance.exports.compiler_control_flow_summary(61440, compilerZeroControlFlowBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 15);
const compilerZeroGenericStaticBytes = new TextEncoder().encode("interface Readable<T> { fun read(self: ref<T>) -> i32 } shape FixedVec<T, static N: usize> { items: [N]T fun push(self: mutref<Self>, value: T) -> Void {} } fun first<T, static N: usize>(vec: ref<FixedVec<T,N>>) -> T { return vec.items[0] } pub fun main() -> Void { vec.push(1) }\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroGenericStaticBytes, 45056);
assert.equal(compilerZeroStage0Instance.exports.compiler_generic_static_summary(45056, compilerZeroGenericStaticBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 255);
const compilerZeroFallibilityBytes = new TextEncoder().encode("fun fail() -> Void raises { BadInput } { raise BadInput } pub fun main() -> Void raises { check fail() rescue err { return } }\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroFallibilityBytes, 47104);
assert.equal(compilerZeroStage0Instance.exports.compiler_fallibility_summary(47104, compilerZeroFallibilityBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 255);
assert.equal(compilerZeroStage0Instance.exports.compiler_fallibility_diag_code(0, 0, 0, 0), 0);
assert.equal(compilerZeroStage0Instance.exports.compiler_fallibility_diag_code(1, 0, 0, 0), 1003);
assert.equal(compilerZeroStage0Instance.exports.compiler_fallibility_diag_code(0, 1, 0, 0), 1002);
assert.equal(compilerZeroStage0Instance.exports.compiler_fallibility_diag_code(0, 0, 0, 1), 1001);
const compilerZeroCapabilityBytes = new TextEncoder().encode('pub fun main(world: World, net: Net) -> Void raises { let fs = std.fs.host() let env = std.env.get("X") let proc = std.proc.spawn("noop") check world.out.write("target web") }\n');
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroCapabilityBytes, 59392);
assert.equal(compilerZeroStage0Instance.exports.compiler_capability_summary(59392, compilerZeroCapabilityBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 255);
assert.equal(compilerZeroStage0Instance.exports.compiler_capability_diag_code(0, 0), 0);
assert.equal(compilerZeroStage0Instance.exports.compiler_capability_diag_code(1, 0), 6002);
assert.equal(compilerZeroStage0Instance.exports.compiler_capability_diag_code(1, 1), 0);
const compilerZeroOwnershipBytes = new TextEncoder().encode("shape Receiver { value: i32 fun add(self: mutref<Self>) -> Void { self.value = self.value + 1 } } pub fun main() -> Void { let mut bytes: [2]u8 = [0, 0] let span: MutSpan<u8> = bytes bytes[0] = 1 let owned: owned<Receiver> = Receiver { value: 1 } consume(owned) Receiver.add(&mut owned) }\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroOwnershipBytes, 6144);
assert.equal(compilerZeroStage0Instance.exports.compiler_ownership_summary(6144, compilerZeroOwnershipBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 255);
assert.equal(compilerZeroStage0Instance.exports.compiler_ownership_diag_code(1, 0, 0, 0), 1009);
assert.equal(compilerZeroStage0Instance.exports.compiler_ownership_diag_code(0, 1, 0, 0), 3013);
assert.equal(compilerZeroStage0Instance.exports.compiler_ownership_diag_code(0, 0, 1, 0), 3048);
assert.equal(compilerZeroStage0Instance.exports.compiler_ownership_diag_code(0, 0, 0, 1), 3109);
const compilerZeroMirBytes = new TextEncoder().encode('pub const LIMIT: usize = 4\nshape Pair { left: i32, right: i32 }\nenum Color { red }\nchoice Result { ok: i32, bad }\npub fun main(world: World) -> i32 raises { Bad } { let local: [4]u8 = [1, 2, 3, 4] let msg: String = "ok" let span: Span<u8> = local if local[0] == 1 { return helper(span.len) } raise Bad }\n');
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroMirBytes, 32768);
assert.equal(compilerZeroStage0Instance.exports.compiler_mir_lowering_summary(32768, compilerZeroMirBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 65535);
const compilerZeroMirHashA = compilerZeroStage0Instance.exports.compiler_mir_hash(32768, compilerZeroMirBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 1);
const compilerZeroMirHashB = compilerZeroStage0Instance.exports.compiler_mir_hash(32768, compilerZeroMirBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 2);
assert(compilerZeroMirHashA > 0);
assert(compilerZeroMirHashB > 0);
assert.equal(compilerZeroStage0Instance.exports.compiler_mir_module_order_hash(1, compilerZeroMirHashA, 2, compilerZeroMirHashB), compilerZeroStage0Instance.exports.compiler_mir_module_order_hash(2, compilerZeroMirHashB, 1, compilerZeroMirHashA));
assert.equal(compilerZeroStage0Instance.exports.compiler_mir_json_contract(65535, 1, 2), 167535);
const compilerZeroParseItemBytes = new TextEncoder().encode('import math\nuse lib\nconst answer: i32 = 42\nshape Point { x: i32 }\nenum Color { red }\nchoice Event { quit }\nextern c "x.h" as x\npub fun main() -> Void {}\ntest "ok" {}\n');
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroParseItemBytes, 16384);
assert.equal(compilerZeroStage0Instance.exports.compiler_parse_item_summary(16384, compilerZeroParseItemBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 511);
const compilerZeroParseStmtBytes = new TextEncoder().encode('pub fun main() -> Void raises {\n let x = 1\n if x { check world.out.write("x") }\n while x { return }\n match x { ._ { raise Bad } }\n let y = call() rescue err { return }\n}\n');
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroParseStmtBytes, 20480);
assert.equal(compilerZeroStage0Instance.exports.compiler_parse_stmt_expr_summary(20480, compilerZeroParseStmtBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 255);
const compilerZeroParseRootBytes = readFileSync("conformance/parse/compiler-smoke.0");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroParseRootBytes, 24576);
assert.equal(compilerZeroStage0Instance.exports.compiler_parse_root_summary(24576, compilerZeroParseRootBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 1010101);
const compilerZeroParsePublicBytes = new TextEncoder().encode('pub const answer: i32 = 42\npub shape Point { x: i32 }\npub enum Color { red }\npub choice Event { quit }\npub fun main() -> Void {}\nfun helper() -> Void {}\n');
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroParsePublicBytes, 28672);
assert.equal(compilerZeroStage0Instance.exports.compiler_parse_public_api_summary(28672, compilerZeroParsePublicBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 11111);
const compilerZeroParseAstBytes = new TextEncoder().encode("pub const answer: i32 = 42\nshape Point { x: i32 }\npub fun main() -> Void { let x = 1 return }\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroParseAstBytes, 49152);
assert.equal(compilerZeroStage0Instance.exports.compiler_parse_ast_summary(49152, compilerZeroParseAstBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength), 1010081);
assert.equal(compilerZeroStage0Instance.exports.compiler_byte_buffer_plan(8, 2), 71);
const compilerZeroDiagSourceBytes = new TextEncoder().encode("pub fun main() -> Void {}\nmissing\n");
new Uint8Array(compilerZeroStage0Instance.exports.memory.buffer).set(compilerZeroDiagSourceBytes, 36864);
assert.equal(compilerZeroStage0Instance.exports.compiler_diagnostic_packet_build(36864, compilerZeroDiagSourceBytes.length, compilerZeroStage0Instance.exports.memory.buffer.byteLength, 26, 3003, 1, 2), 2014203);
assert.equal(compilerZeroStage0Instance.exports.compiler_diagnostic_detail_summary(1, 2, 3, 4, 5, 6), 123456);
assert.equal(compilerZeroStage0Instance.exports.compiler_related_span_summary(2, 3, 7, 11, 4), 2037114);
const compilerZeroGraph = json(["graph", "--json", "--target", "wasm32-web", "compiler-zero"]).body;
assert.equal(compilerZeroGraph.selfHostSubset.compatible, true);
assert.equal(compilerZeroGraph.selfHostSubset.target, "wasm32-web");
assert.equal(compilerZeroGraph.selfHostSubset.backend, "direct-wasm");
assert.equal(compilerZeroGraph.selfHostRouting.mode, "seed");
assert.equal(compilerZeroGraph.selfHostRouting.phases.parse, "zero-c");
assert.equal(compilerZeroGraph.selfHostRouting.frontend.selected, true);
assert.equal(compilerZeroGraph.selfHostRouting.metadata.graphJson, true);
assert.equal(compilerZeroGraph.selfHostRouting.cBridge.required, false);
assert(compilerZeroGraph.releaseMatrixTargetSupport.some((target) => target.target === "linux-musl-x64" && target.directExeEmitter === "zero-elf64-exe"));
assert(compilerZeroGraph.releaseMatrixTargetSupport.some((target) => target.target === "darwin-arm64" && target.directObjectEmitter === "zero-macho64"));
assert(compilerZeroGraph.releaseMatrixTargetSupport.every((target) => target.fallbackPolicy === "explicit-direct-never-c-bridge"));
assert.equal(compilerZeroGraph.selfHostSubset.blockedReasons.length, 0);
assert.equal(compilerZeroGraph.selfHostSubset.featureFacts.strings.dataSegments, true);
assert.equal(compilerZeroGraph.selfHostSubset.featureFacts.strings.representation, "readonly-data-ptr-len");
assert.equal(compilerZeroGraph.selfHostSubset.featureFacts.spans.mutableRepresentation, "ptr-len-mutspan-u8");
assert(compilerZeroGraph.selfHostSubset.featureFacts.spans.helpers.includes("std.mem.eqlBytes"));
assert.equal(compilerZeroGraph.selfHostSubset.featureFacts.aggregates.directWasmLowering, false);
assert.equal(compilerZeroGraph.selfHostSubset.featureFacts.generics.runtimeMetadata, false);
assert.equal(compilerZeroGraph.selfHostSubset.cInterop.generatedCRequired, false);
assert(compilerZeroGraph.selfHostSubset.targetLimitations.includes("external-c-calls-require-target-library-audit"));
assert(compilerZeroGraph.sourceMaps.some((item) => item.path === "compiler-zero/src/main.0" && item.lineCount > 0 && item.columnUnit === "utf8-byte"));
const hostedGraph = json(["graph", "--json", "--target", "wasm32-web", "conformance/native/fail/std-fs-target-unsupported.0"]).body;
assert.equal(hostedGraph.selfHostSubset.compatible, false);
assert.equal(hostedGraph.selfHostRouting.subsetCompatible, false);
assert(hostedGraph.selfHostSubset.blockedReasons.includes("fs"));
assert(!hostedGraph.selfHostSubset.blockedReasons.includes("world"));
const cImportGraph = json(["graph", "--json", "--target", "wasm32-web", "conformance/check/pass/c-header-import.0"]).body;
assert.equal(cImportGraph.selfHostSubset.compatible, true);
assert.equal(cImportGraph.selfHostSubset.cInterop.headerImports, true);
assert.equal(cImportGraph.selfHostSubset.cInterop.typedBindings, true);
assert.equal(cImportGraph.selfHostSubset.cInterop.generatedCRequired, false);
assert.equal(cImportGraph.selfHostRouting.cBridge.required, false);
assert.equal(cImportGraph.selfHostSubset.blockedReasons.length, 0);
const cImport = cImportGraph.cImports.find((item) => item.header === "conformance/c/simple.h");
assert(cImport.typedModel.functions.some((item) => item.name === "zero_c_add"));
assert(cImport.typedModel.constants.some((item) => item.name === "ZERO_C_ANSWER"));
assert(cImport.typedModel.structs.some((item) => item.name === "zero_c_point"));
assert(cImport.typedModel.enums.some((item) => item.name === "zero_c_color"));
assert(cImport.typedModel.typedefs.some((item) => item.name === "zero_c_int"));
assert.equal(cImport.cache.target, "wasm32-web");
const hostLeakGraph = json(["graph", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package"]).body;
assert.equal(hostLeakGraph.cLibraries[0].targetValidation.status, "blocked");
assert.equal(hostLeakGraph.cLibraries[0].linkPlan.hostDiscovery, "blocked");
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
assert.equal(depBuild.compilerCaches.every((item) => item.compilerVersion === "0.1.2" && item.packageVersion === "0.1.0"), true);
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
const compilerZeroCheck = json(["check", "--json", "--target", "wasm32-web", "compiler-zero"]).body;
assert.equal(compilerZeroCheck.selfHostRouting.mode, "seed");
assert.equal(compilerZeroCheck.selfHostRouting.phases.check, "zero-c");
assert.equal(compilerZeroCheck.selfHostRouting.frontend.selected, true);
assert.equal(compilerZeroCheck.selfHostRouting.cBridge.required, false);

const zeroHashSize = json(["size", "--json", "--target", "linux-musl-x64", "examples/zero-hash", "--out", join(outDir, "zero-hash-sized")]).body;
assert.equal(zeroHashSize.generatedCBytes, 0);
assert(zeroHashSize.usedStdlibHelpers.some((helper) => helper.name === "std.codec.crc32Bytes"));
assert(zeroHashSize.artifactBytes < 100 * 1024);
const zeroHashWasiSize = json(["size", "--json", "--target", "wasm32-wasi", "examples/zero-hash"]).body;
assert.equal(zeroHashWasiSize.runtimeImportAudit.module, "wasi_snapshot_preview1");
assert(zeroHashWasiSize.runtimeImportAudit.functions.includes("fd_write"));
assert(zeroHashWasiSize.runtimeImportAudit.functions.includes("path_open"));
assert.equal(zeroHashWasiSize.runtimeImportAudit.memoryFloor.floorBytes, 65536);
assert.equal(zeroHashWasiSize.portableRuntime.runtimeKind, "wasi");
assert.equal(zeroHashWasiSize.portableRuntime.providerSpecificDeployment, false);
assert.equal(zeroHashWasiSize.portableRuntime.hostedDeployment, "out-of-scope");
assert.equal(zeroHashWasiSize.portableRuntime.localRunner.productionLikeImports, true);
assert(zeroHashWasiSize.portableRuntime.imports.functions.includes("path_open"));
assert.equal(zeroHashWasiSize.portableRuntime.memoryFloor.floorBytes, 65536);
const stdEnvWebSize = json(["size", "--json", "--target", "wasm32-web", "conformance/native/pass/std-env.0"]).body;
assert.equal(stdEnvWebSize.portableRuntime.runtimeKind, "browser-worker");
assert.equal(stdEnvWebSize.portableRuntime.providerSpecificDeployment, false);
assert.equal(stdEnvWebSize.portableRuntime.imports.adapter, "browser-worker-import-shim");
assert(stdEnvWebSize.portableRuntime.imports.functions.includes("environ_get"));
assert.equal(stdEnvWebSize.portableRuntime.capabilityRestrictions.filesystem, "denied");
const zeroHashWasiPath = join(outDir, "zero-hash-wasi");
rmSync(`${zeroHashWasiPath}.wasm`, { force: true });
const zeroHashWasiReport = json(["build", "--json", "--emit", "wasm", "--target", "wasm32-wasi", "examples/zero-hash", "--out", zeroHashWasiPath]).body;
assert.equal(zeroHashWasiReport.generatedCBytes, 0);
assert.equal(zeroHashWasiReport.targetSupport.fsAvailable, true);
assert.equal(zeroHashWasiReport.objectBackend.objectEmission.path, "direct-wasm");
assert.equal(zeroHashWasiReport.objectBackend.directFacts.runtime.allocator, "FixedBufAlloc");
assert.equal(zeroHashWasiReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert(!existsSync(`${zeroHashWasiPath}.wasm.c`));
const zeroHashMem = json(["mem", "--json", "--target", "wasm32-wasi", "examples/zero-hash"]).body;
assert.equal(zeroHashMem.generatedCBytes, 0);
assert.equal(zeroHashMem.cBridgeFallback, false);
assert.deepEqual(zeroHashMem.capabilityFacts.requiredCapabilities, ["args", "fs", "memory", "alloc", "codec", "world"]);

const explainText = zero(["explain", "TAR002"]).stdout;
assert.match(explainText, /Hosted filesystem unavailable/);

const explainJson = json(["explain", "--json", "TAR002"]).body;
assert.equal(explainJson.schemaVersion, 1);
assert.equal(explainJson.code, "TAR002");
assert.equal(explainJson.repair.id, "remove-hosted-fs-or-use-host-target");

const fixPlan = json(["fix", "--plan", "--json", "--target", "wasm32-web", "conformance/native/fail/std-fs-target-unsupported.0"]).body;
assert.equal(fixPlan.schemaVersion, 1);
assert.equal(fixPlan.mode, "plan");
assert.equal(fixPlan.appliesEdits, false);
assert.equal(fixPlan.fixes[0].id, "remove-hosted-fs-or-use-host-target");
assert.equal(fixPlan.selfHostRepairPolicy.unsupportedFeatureSafety, "requires-human-review");
assert.equal(fixPlan.selfHostRepairPolicy.directFallback, "never-c-bridge");
const targetDeniedCapability = json(["check", "--json", "--target", "wasm32-web", "conformance/native/fail/std-fs-target-unsupported.0"], { allowFailure: true }).body;
assert.equal(targetDeniedCapability.diagnostics[0].code, "TAR002");
assert.equal(targetDeniedCapability.diagnostics[0].repair.id, "remove-hosted-fs-or-use-host-target");
assert.equal(targetDeniedCapability.generatedCBytes ?? 0, 0);

const routes = json(["routes", "--json", "examples/web/hello"]).body;
assert.equal(routes.schemaVersion, 1);
assert.equal(routes.runtime, "wasm32-web");
assert(routes.routes.some((route) => route.path === "/" && route.method === "GET"));
assert.equal(routes.routeCount, 1);
assert(routes.requiresCapabilities.includes("web"));
assert.equal(routes.capabilityFacts.filesystem.browserWasm, "unavailable");
assert.equal(routes.capabilityFacts.filesystem.wasi, "capability-gated");
assert.equal(routes.artifact.target, "wasm32-web");
assert.equal(routes.artifact.available, true);
assert.equal(routes.artifact.kind, "route-manifest");
assert.equal(routes.webBundle.available, true);
assert.equal(routes.webBundle.javascriptFrameworkTaxBytes, 0);
assert(routes.webSurfaces.request.includes("headers"));
assert.equal(routes.artifactAudit.filesystem, "denied for browser wasm");
assert.equal(routes.localRuntime.runtimeKind, "browser-worker");
assert.equal(routes.localRuntime.providerSpecificDeployment, false);
assert.equal(routes.localRuntime.productionLikeImports, true);
assert.equal(routes.localRuntime.frameworkTaxBytes, 0);
assert.equal(routes.webBundle.deployment.providerSpecific, false);
assert.equal(routes.webBundle.deployment.vercel, "out-of-scope");
const webDevPlan = json(["dev", "--json", "--target", "wasm32-web", "examples/web/hello"]).body;
assert.equal(webDevPlan.localRuntime.runtimeKind, "browser-worker");
assert.equal(webDevPlan.localRuntime.productionLikeImports, true);
assert.equal(webDevPlan.localRuntime.providerSpecificDeployment, false);
assert.equal(webDevPlan.localRuntime.capabilityRestrictions.filesystem, "denied");

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
  ["TAR002", ["check", "--json", "--target", "wasm32-web", "conformance/native/fail/std-fs-target-unsupported.0"]],
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
assert(aliasGraph.symbols.some((symbol) => symbol.name === "BytePair" && symbol.kind === "alias"));

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
const compilerZeroSize = json(["size", "--json", "compiler-zero"]).body;
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_runtime_arena_plan" && helper.emitted === true && helper.estimatedDirectBytes > 0));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_source_input_mode" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_accept_source_buffer" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_token_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_source_hash" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_source_location_at" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_source_order_key" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_hosted_package_source_plan" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_runtime_abi_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_runtime_memory_layout" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_runtime_shim_cost" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_runtime_helper_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_manifest_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_module_import_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_module_import_diag_code" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_module_graph_reject_code" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_scope_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_name_resolution_packet" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_member_name_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_type_surface_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_expression_surface_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_control_flow_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_generic_static_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_fallibility_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_fallibility_diag_code" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_capability_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_capability_diag_code" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_ownership_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_ownership_diag_code" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_mir_lowering_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_mir_hash" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_mir_module_order_hash" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_mir_json_contract" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_browser_compile_request" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_browser_compile_response" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_browser_artifact_plan" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_source_graph_plan" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_compile_request" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_module_graph_packet" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_source_diag_code" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_diagnostic_packet" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_command_response" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_fixed_point_packet" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_native_artifact_plan" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_target_capability_diag" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_write_minimal_wasm" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_write_return42_wasm" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_write_playground_wasm" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_minimal_wasm_hash" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_required_capabilities" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_supported_capabilities" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_gap_mask" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_gap_diag_code" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_self_host_stage_plan" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_parse_item_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_parse_stmt_expr_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_parse_root_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_parse_public_api_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_parse_ast_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_byte_buffer_plan" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_diagnostic_packet_build" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_diagnostic_detail_summary" && helper.emitted === true));
assert(compilerZeroSize.compilerRuntimeHelpers.some((helper) => helper.symbol === "compiler_related_span_summary" && helper.emitted === true));
assert.equal(compilerZeroSize.selfHostRouting.mode, "seed");
assert.equal(compilerZeroSize.selfHostRouting.metadata.sizeJson, true);
assert.equal(compilerZeroSize.selfHostRouting.cBridge.required, false);
const sizedArtifact = join(outDir, "sized-memory-package");
rmSync(sizedArtifact, { force: true });
rmSync(`${sizedArtifact}.c`, { force: true });
const sizeWithArtifact = json(["size", "--json", "--out", sizedArtifact, "examples/memory-package"]).body;
assert.equal(sizeWithArtifact.artifactPath, sizedArtifact);
assert(sizeWithArtifact.artifactBytes > 0);
assert(existsSync(sizedArtifact));
assert.equal(diagnostics.find((item) => item.code === "ERR001").repair.id, "add-raises-or-rescue");
assert.equal(diagnostics.find((item) => item.code === "ERR003").repair.id, "check-or-rescue-fallible-call");
assert.equal(diagnostics.find((item) => item.code === "TYP025").repair.id, "add-explicit-generic-type-arguments");
assert.equal(diagnostics.find((item) => item.code === "TYP009").repair.id, "make-binding-mutable");
assert.equal(diagnostics.find((item) => item.code === "FLD002").repair.id, "initialize-missing-field");
assert.equal(diagnostics.find((item) => item.code === "TAR002").repair.id, "remove-hosted-fs-or-use-host-target");
assert.equal(diagnostics.find((item) => item.code === "PUB001").repair.id, "add-public-api-type");
assert.equal(diagnostics.find((item) => item.code === "IMP001").repair.id, "fix-import-path");
assert.match(diagnostics.find((item) => item.code === "TAR002").help, /host target|hosted std\.fs/);
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
const wasmWebTarget = targets.targets.find((target) => target.name === "wasm32-web");
const linuxGnuTarget = targets.targets.find((target) => target.name === "linux-x64");
const linuxMuslTarget = targets.targets.find((target) => target.name === "linux-musl-x64");
const darwinArm64Target = targets.targets.find((target) => target.name === "darwin-arm64");
const winX64Target = targets.targets.find((target) => target.name === "win32-x64.exe");
const linuxArm64Target = targets.targets.find((target) => target.name === "linux-arm64");
assert.equal(wasmWebTarget.directBackend.status, "wasm-module");
assert.equal(wasmWebTarget.directBackend.objectEmitter, "zero-wasm");
assert.equal(wasmWebTarget.directBackend.explicitDirectFallback, "never-c-bridge");
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
  explain: {
    textIncludesTitle: explainText.includes("Hosted filesystem unavailable"),
    jsonCode: explainJson.code,
  },
  fixPlan: {
    mode: fixPlan.mode,
    appliesEdits: fixPlan.appliesEdits,
    fixCount: fixPlan.fixes.length,
  },
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
      {
        id: "direct-web-wasm",
        generatedCBytes: directWebWasmReport.generatedCBytes,
        cBridgeFallback: directWebWasmReport.selfHostRouting?.cBridge?.required ?? false,
        objectEmissionPath: directWebWasmReport.objectBackend.objectEmission.path,
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
    targetDeniedCapability: {
      code: targetDeniedCapability.diagnostics[0].code,
      repair: targetDeniedCapability.diagnostics[0].repair.id,
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
