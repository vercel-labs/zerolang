import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { access, mkdtemp, mkdir, readdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, relative, sep } from "node:path";
import { describe, it } from "node:test";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const root = process.cwd();
const zero = join(root, "bin", "zero");
const nativeZero = join(root, ".zero", "bin", "zero");
const supportedTargets = [
  "darwin-arm64",
  "darwin-x64",
  "linux-musl-x64",
  "linux-musl-arm64",
  "linux-x64",
  "linux-arm64",
  "win32-x64.exe",
  "win32-arm64.exe",
];
const artifactSummaryPattern = /\((?:\d+ B|\d+\.\d KiB|\d+\.\d MiB|\d+\.\d GiB), (?:\d+ ms|\d+\.\d s)\)/;
const runnableDirectTarget =
  process.platform === "darwin" && process.arch === "arm64" ? "darwin-arm64" :
  process.platform === "linux" && process.arch === "x64" ? "linux-musl-x64" :
  null;

function runnableBuildArgs(input: string, out: string) {
  if (runnableDirectTarget) return ["build", "--text", "--emit", "exe", "--target", runnableDirectTarget, input, "--out", out];
  throw new Error("no runnable direct target for this host");
}

function runZero(args: string[], options: { cwd?: string; env?: NodeJS.ProcessEnv } = {}) {
  return execFileAsync(zero, args, { cwd: options.cwd ?? root, env: options.env ?? process.env });
}

function runNativeZero(args: string[], options: { cwd?: string; env?: NodeJS.ProcessEnv } = {}) {
  return execFileAsync(nativeZero, args, { cwd: options.cwd ?? root, env: options.env ?? process.env });
}

async function collectSkillMdFiles(dir: string): Promise<string[]> {
  const ignoredDirs = new Set([".git", ".next", ".zero", "node_modules"]);
  let entries;
  try {
    entries = await readdir(dir, { withFileTypes: true });
  } catch {
    return [];
  }

  const files: string[] = [];
  for (const entry of entries) {
    if (entry.isDirectory()) {
      if (ignoredDirs.has(entry.name)) continue;
      files.push(...await collectSkillMdFiles(join(dir, entry.name)));
    } else if (entry.isFile() && entry.name === "SKILL.md") {
      files.push(relative(root, join(dir, entry.name)).split(sep).join("/"));
    }
  }
  return files.sort();
}

describe("native zero CLI", () => {
  it("prints a terse plain version", async () => {
    assert.equal((await runZero(["--version", "--text"])).stdout, "zero 0.1.4\n");
    assert.equal((await runNativeZero(["--version", "--text"])).stdout, "zero 0.1.4\n");
  });

  it("checks directly and rejects removed legacy build flags", async () => {
    const check = await runZero(["check", "examples/hello.0"]);
    assert.match(check.stdout, /ok/);

    const removedEmit = await runZero(["build", "--json", "--emit", "c", "examples/hello.0"]).catch((error) => error);
    assert.notEqual(removedEmit.code, 0);
    assert.equal(JSON.parse(removedEmit.stdout).diagnostics[0].code, "BLD003");

    const removedFlag = await runZero(["build", "--json", "--legacy-backend", "examples/hello.0"]).catch((error) => error);
    assert.notEqual(removedFlag.code, 0);
    assert.equal(JSON.parse(removedFlag.stdout).diagnostics[0].code, "BLD003");
  });

  it("builds and runs an executable", async () => {
    const cwd = await mkdtemp(join(tmpdir(), "zero-cli-"));
    const exe = join(cwd, "add");

    try {
      const build = await runZero(runnableBuildArgs("examples/add.0", exe));
      assert.ok(build.stdout.includes(`${exe} (`));
      assert.match(build.stdout, artifactSummaryPattern);
      const run = await execFileAsync(exe, []);
      assert.match(run.stdout, /math works/);
      await assert.rejects(access(`${exe}.c`));

      const runExe = join(cwd, "add-run");
      const zeroRun = await runZero(["run", "--out", runExe, "examples/add.0"]);
      assert.match(zeroRun.stdout, /math works/);
      assert.equal(zeroRun.stderr, "");
      await assert.rejects(access(`${runExe}.c`));

      const zeroRunWithArgs = await runZero(["run", "--out", join(cwd, "add-run-args"), "examples/add.0", "--", "--emit", "c"]);
      assert.match(zeroRunWithArgs.stdout, /math works/);

      const exitCodeRun = await runZero(["run", "--out", join(cwd, "return42"), "examples/direct-exe-return.0"]).catch((error) => error);
      assert.equal(exitCodeRun.code, 42);
    } finally {
      await rm(cwd, { force: true, recursive: true });
    }
  });

  it("checks and builds a manifest project", async () => {
    const cwd = await mkdtemp(join(tmpdir(), "zero-project-"));
    const project = join(cwd, "demo");
    const src = join(project, "src");
    const exe = join(cwd, "demo-exe");

    try {
      await mkdir(src, { recursive: true });
      await writeFile(
        join(project, "zero.json"),
        `{
  "package": { "name": "demo", "version": "0.1.0" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } }
}
`,
      );
      await writeFile(
        join(src, "main.0"),
        `pub fn main Void world World !
  check world.out.write "hello from demo\\n"
`,
      );

      assert.match((await runZero(["check", project])).stdout, /ok/);
      await runZero(runnableBuildArgs(project, exe));
      assert.match((await execFileAsync(exe, [])).stdout, /hello from demo/);
    } finally {
      await rm(cwd, { force: true, recursive: true });
    }
  });

  it("reports graph, size, objects, and targets", async () => {
    const graph = JSON.parse((await runZero(["graph", "--json", "examples/point.0"])).stdout);
    assert.equal(graph.shapes[0].name, "Point");
    assert.equal(graph.functions.some((item: { name: string }) => item.name === "main"), true);

    const size = JSON.parse((await runZero(["size", "--json", "examples/point.0"])).stdout);
    assert.equal(size.schemaVersion, 1);
    assert.equal(size.generatedCBytes, 0);

    const objOut = join(tmpdir(), `zero-target-${Date.now()}.o`);
    try {
      const objBuild = await runZero(["build", "--text", "--emit", "obj", "--target", "linux-musl-arm64", "examples/direct-exe-return.0", "--out", objOut]);
      assert.ok(objBuild.stdout.includes(`${objOut} (`));
      assert.match(objBuild.stdout, artifactSummaryPattern);
      const bytes = await readFile(objOut);
      assert.equal(bytes[0], 0x7f);
      assert.equal(bytes[1], 0x45);
    } finally {
      await rm(objOut, { force: true });
    }

    const targets = JSON.parse((await runZero(["targets", "--json"])).stdout);
    assert.deepEqual(
      targets.targets.map((target: { name: string }) => target.name),
      supportedTargets,
    );
    assert.equal(targets.targets.find((target: { name: string }) => target.name === "linux-musl-x64")?.compilerTarget, "x86_64-linux-musl");
    assert.equal(targets.targets.find((target: { name: string }) => target.name === "win32-x64.exe")?.exeSuffix, ".exe");
  });

  it("lists and retrieves bundled skills", async () => {
    assert.deepEqual(await collectSkillMdFiles(root), ["skills/zero/SKILL.md"]);

    const list = JSON.parse((await runZero(["skills", "list", "--json"])).stdout);
    assert.equal(list.success, true);
    const skillNames = new Set(list.data.map((skill: { name: string }) => skill.name));
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
      assert.equal(skillNames.has(name), true);
    }

    const zeroSkill = JSON.parse((await runZero(["skills", "get", "zero", "--full", "--json"])).stdout);
    assert.equal(zeroSkill.success, true);
    assert.match(zeroSkill.data[0].content, /# Zero/);
    assert.match(zeroSkill.data[0].content, /zero skills get zero --full/);
    assert.equal(zeroSkill.data[0].files, undefined);

    const languageSkill = JSON.parse((await runZero(["skills", "get", "language", "--json"])).stdout);
    assert.equal(languageSkill.success, true);
    assert.match(languageSkill.data[0].content, /# zerolang Language/);
    assert.match(languageSkill.data[0].content, /pub fn main/);

    const diagnosticSkill = JSON.parse((await runZero(["skills", "get", "diagnostics", "--json"])).stdout);
    assert.equal(diagnosticSkill.success, true);
    assert.match(diagnosticSkill.data[0].content, /fixSafety/);

    const missing = await runZero(["skills", "get", "missing", "--json"]).catch((error) => error);
    assert.notEqual(missing.code, 0);
    assert.equal(JSON.parse(missing.stdout).success, false);

    const removedPath = await runZero(["skills", "path", "zero", "--json"]).catch((error) => error);
    assert.notEqual(removedPath.code, 0);
    assert.match(JSON.parse(removedPath.stdout).error, /Unknown skills subcommand: path/);

    const badSkillsFlag = await runZero(["skills", "--text", "-x"]).catch((error) => error);
    assert.notEqual(badSkillsFlag.code, 0);
    assert.match(badSkillsFlag.stderr, /Unknown skills flag: -x/);

    const badListFlag = await runZero(["skills", "list", "--unknown", "--json"]).catch((error) => error);
    assert.notEqual(badListFlag.code, 0);
    assert.match(JSON.parse(badListFlag.stdout).error, /Unknown skills flag: --unknown/);

    const badGetFlag = await runZero(["skills", "get", "language", "--unknown", "--json"]).catch((error) => error);
    assert.notEqual(badGetFlag.code, 0);
    assert.match(JSON.parse(badGetFlag.stdout).error, /Unknown skills flag: --unknown/);

    const nativeList = JSON.parse((await runNativeZero(["skills", "list", "--json"])).stdout);
    assert.equal(nativeList.success, true);
    assert.equal(nativeList.data.some((skill: { name: string }) => skill.name === "language"), true);

    const nativeLanguageSkill = JSON.parse((await runNativeZero(["skills", "get", "language", "--json"])).stdout);
    assert.equal(nativeLanguageSkill.success, true);
    assert.match(nativeLanguageSkill.data[0].content, /# zerolang Language/);
  });

  it("handles target-specific executable names", async () => {
    const cwd = await mkdtemp(join(tmpdir(), "zero-target-"));
    const out = join(cwd, "hello-windows");

    try {
      const result = await runZero(["build", "--emit", "exe", "--target", "win32-x64.exe", "examples/hello.0", "--out", out], {
        env: { ...process.env, ZERO_CC: "/usr/bin/false" },
      });
      assert.match(result.stdout, /hello-windows\.exe/);
      const bytes = await readFile(`${out}.exe`);
      assert.equal(bytes[0], 0x4d);
      assert.equal(bytes[1], 0x5a);
      await assert.rejects(access(`${out}.exe.c`));
    } finally {
      await rm(cwd, { force: true, recursive: true });
    }
  });

  it("rejects unsupported target triples", async () => {
    const result = await runZero(["check", "--text", "--target", "mips-unknown-none", "examples/hello.0"]).catch((error) => error);
    assert.notEqual(result.code, 0);
    assert.match(result.stderr, /unknown target 'mips-unknown-none'/);
  });

  it("builds hosted Linux hello through the direct executable backend", async () => {
    const cwd = await mkdtemp(join(tmpdir(), "zero-cross-"));
    const out = join(cwd, "hello-linux-musl");

    try {
      const build = await runZero(["build", "--emit", "exe", "--target", "linux-musl-x64", "examples/hello.0", "--out", out]);
      assert.match(build.stdout, /hello-linux-musl/);
      assert.ok((await readFile(out)).includes(Buffer.from("hello from zero\n")));
    } finally {
      await rm(cwd, { force: true, recursive: true });
    }
  });

  it("emits native diagnostic codes", async () => {
    const result = await runZero(["check", "--text", "conformance/native/fail/unknown-field.0"]).catch((error) => error);
    assert.notEqual(result.code, 0);
    assert.match(result.stderr, /FLD001/);
    assert.match(result.stderr, /explain: zero explain FLD001/);
  });

  it("produces full-data ZDN fields for check output", async () => {
  // Verify that --zdn check includes all JSON-equivalent compiler metadata fields
  const check = await runZero(["check", "--zdn", "examples/point.0"]);
  const out = check.stdout;

  // Core fields
  assert.ok(out.includes("CheckResult"), `missing CheckResult record`);

  // Compiler internals (previously JSON-only)
  assert.ok(out.includes("compileTime"), `missing compileTime in ZDN output`);
  assert.ok(out.includes("deterministic"), `missing deterministic in compileTime`);
  assert.ok(out.includes("metaCache"), `missing metaCache in ZDN output`);
  assert.ok(out.includes("hits"), `missing metaCache.hits`);
  assert.ok(out.includes("targetReadiness"), `missing targetReadiness`);
  assert.ok(out.includes("buildable"), `missing targetReadiness.buildable`);
  assert.ok(out.includes("compilerPhases"), `missing compilerPhases`);
  assert.ok(out.includes("compilerCaches"), `missing compilerCaches`);
  assert.ok(out.includes("interfaceFingerprints"), `missing interfaceFingerprints`);
  assert.ok(out.includes("incrementalInvalidation"), `missing incrementalInvalidation`);
  assert.ok(out.includes("selfHostRouting"), `missing selfHostRouting`);
  assert.ok(out.includes("subsetCompatible"), `missing selfHostRouting.subsetCompatible`);
  assert.ok(out.includes("package"), `missing package in ZDN output`);
  assert.ok(out.includes("packageCache"), `missing packageCache in ZDN output`);

  // Include "Phase" type tag for compiler phases
  assert.ok(out.includes("Phase\n"), `missing Phase type tag in compilerPhases`);
  assert.ok(out.includes('name "resolve"'), `missing resolve phase`);

  // Verify ok field at the correct indent level (agent-accessible in first few lines)
  const okLine = out.split("\n").find((line) => line.includes("ok true"));
  assert.ok(okLine, `missing ok true line`);
  assert.ok(okLine.startsWith("  ok"), `ok field should be at indent level 1`);
});

it("produces full-data ZDN fields for build output", async () => {
  // Build to /dev/null to avoid writing temp files.
  // Use direct-exe-return.0 (primitive-only) rather than point.0 (shape types).
  const result = await runZero(["build", "--zdn", "--emit", "exe", "examples/direct-exe-return.0", "--out", "/dev/null"]);
  const out = result.stdout;

  assert.ok(out.includes("BuildResult"), `missing BuildResult record`);
  assert.ok(out.includes("toolchain"), `missing toolchain in build ZDN`);
  assert.ok(out.includes("driverKind"), `missing driverKind`);
  assert.ok(out.includes("targetTriple"), `missing targetTriple`);
  assert.ok(out.includes("targetSupport"), `missing targetSupport`);
  assert.ok(out.includes("directStatus"), `missing directStatus`);
  assert.ok(out.includes("legacy"), `missing legacy field`);
  assert.ok(out.includes("legacyBackend"), `missing legacyBackend field`);

  // Check the artifact output
  assert.ok(out.includes('artifactPath "/dev/null"'), `missing artifact path`);
});

it("produces full-data ZDN fields for version output", async () => {
  const ver = await runZero(["--version", "--zdn"]);
  const out = ver.stdout;

  assert.ok(out.includes("VersionResult"), `missing VersionResult record`);
  assert.ok(out.includes("schemaVersion"), `missing schemaVersion`);
  assert.ok(out.includes('version "0.1.4"'), `missing version`);
  assert.ok(out.includes("commit"), `missing commit field`);
  assert.ok(out.includes("backend"), `missing backend field`);
  assert.ok(out.includes('backend "zero-c"'), `missing zero-c backend`);
  assert.ok(out.includes("targets"), `missing targets field`);
  assert.ok(out.includes("targetCompiler"), `missing targetCompiler`);
  assert.ok(out.includes("available"), `missing targetCompiler.available`);
  assert.ok(out.includes("crossCompilation"), `missing crossCompilation`);
  assert.ok(out.includes("ready"), `missing crossCompilation.ready`);
});

it("produces full-data ZDN fields for doctor output", async () => {
  const doctor = await runZero(["doctor", "--zdn"]);
  const out = doctor.stdout;

  assert.ok(out.includes("DoctorResult"), `missing DoctorResult record`);
  assert.ok(out.includes("status"), `missing status`);
  assert.ok(out.includes("nativeCCompiler"), `missing nativeCCompiler`);
  assert.ok(out.includes("targetCCompiler"), `missing targetCCompiler`);
  assert.ok(out.includes("checks"), `missing checks array`);
  assert.ok(out.includes("targetToolchains"), `missing targetToolchains array`);

  // Check individual doctor checks
  assert.ok(out.includes("native-c-compiler"), `missing native-c-compiler check`);
  assert.ok(out.includes("target-c-compiler"), `missing target-c-compiler check`);
  assert.ok(out.includes("host-target"), `missing host-target check`);
});

it("verifies JSON and ZDN output field parity for check command", async () => {
  // Run both --json and --zdn on the same file and verify the same fields are present
  const [jsonResult, zdnResult] = await Promise.all([
    runZero(["check", "--json", "examples/direct-exe-return.0"]).then((r) => JSON.parse(r.stdout)),
    runZero(["check", "--zdn", "examples/direct-exe-return.0"]),
  ]);
  const zdn = zdnResult.stdout;

  // JSON has these top-level keys; verify they appear in ZDN
  const jsonTopKeys = ["schemaVersion", "ok", "sourceFile", "package", "packageCache",
    "metaCache", "compileTime", "targetReadiness", "compilerPhases",
    "compilerCaches", "interfaceFingerprints", "incrementalInvalidation",
    "selfHostRouting"];
  for (const key of jsonTopKeys) {
    assert.ok(zdn.includes(key), `ZDN output missing field "${key}" present in JSON`);
  }

  // Verify JSON values match ZDN values for core fields
  assert.ok(zdn.includes('ok true'), `ZDN should report ok true`);
  assert.ok(zdn.includes(`sourceFile "examples/direct-exe-return.0"`), `ZDN should include source file path`);

  // Verify metaCache details
  assert.ok(zdn.includes('hits') && zdn.includes('misses'), `ZDN should include metaCache hit/miss`);

  // Verify targetReadiness
  assert.ok(zdn.includes('buildable'), `ZDN should include buildable in targetReadiness`);
  assert.ok(zdn.includes('backend'), `ZDN should include backend`);

  // Verify compilerPhases have structured items
  assert.ok(zdn.includes('Phase'), `ZDN compilerPhases should have Phase type tags`);
  assert.ok(zdn.includes('name "parse"'), `ZDN should include parse phase`);

  // Verify selfHostRouting
  assert.ok(zdn.includes('subsetCompatible'), `ZDN should include subsetCompatible`);
  assert.ok(zdn.includes('native-bootstrap'), `ZDN should include native-bootstrap mode`);
});

it("verifies ZDN ship output contains release preview fields", async () => {
  const cwd = await mkdtemp(join(tmpdir(), "zero-ship-zdn-"));
  const out = join(cwd, "ship-test");

  try {
    const result = await runZero(["ship", "--zdn", "--target", "linux-musl-x64", "examples/direct-exe-return.0", "--out", out], {
      env: { ...process.env, ZERO_CC: "/usr/bin/false" },
    });
    const zdn = result.stdout;
    assert.ok(zdn.includes("ShipResult"), `missing ShipResult`);

    // Release preview fields
    assert.ok(zdn.includes("releasePreview"), `missing releasePreview`);
    assert.ok(zdn.includes("releaseTargetContract"), `missing releaseTargetContract`);
    assert.ok(zdn.includes("artifacts"), `missing artifacts array`);
    assert.ok(zdn.includes("stripped-binary"), `missing stripped-binary artifact`);
    assert.ok(zdn.includes("checksum"), `missing checksum`);
    assert.ok(zdn.includes("fnv1a64"), `missing checksum algorithm`);

    // Compiler metadata
    assert.ok(zdn.includes("compilerPhases"), `missing compilerPhases in ship ZDN`);
    assert.ok(zdn.includes("targetFacts"), `missing targetFacts`);
    assert.ok(zdn.includes("objectBackend"), `missing objectBackend`);

  } finally {
    await rm(cwd, { force: true, recursive: true });
  }
});

it("produces safetyLevels in fix plan ZDN output", async () => {
  const fix = await runZero(["fix", "--plan", "--zdn", "examples/does-not-exist.0"]).catch((error) => error);
  const out = fix.stdout;

  assert.ok(out.includes("FixPlanResult"), `missing FixPlanResult record`);
  assert.ok(out.includes("safetyLevels"), `missing safetyLevels array`);
  assert.ok(out.includes("format-only"), `missing format-only safety level`);
  assert.ok(out.includes("behavior-preserving"), `missing behavior-preserving safety level`);
  assert.ok(out.includes("api-changing"), `missing api-changing safety level`);
  assert.ok(out.includes("target-changing"), `missing target-changing safety level`);
  assert.ok(out.includes("requires-human-review"), `missing requires-human-review safety level`);
});

it("produces ZDN output for check, test, tokens, parse, and explain", async () => {
    // check --zdn on a valid program
    const check = await runZero(["check", "--zdn", "examples/add.0"]);
    assert.ok(check.stdout.includes("CheckResult"), `expected CheckResult in:\n${check.stdout}`);
    assert.ok(check.stdout.includes("ok true"), `expected ok true in:\n${check.stdout}`);

    // check --zdn on an invalid program
    const checkFail = await runZero(["check", "--zdn", "conformance/native/fail/unknown-field.0"]).catch((error) => error);
    assert.notEqual(checkFail.code, 0);
    assert.ok(checkFail.stdout.includes("CheckResult"), `expected CheckResult in:\n${checkFail.stdout}`);
    assert.ok(checkFail.stdout.includes("ok false"), `expected ok false in:\n${checkFail.stdout}`);

    // tokens --zdn
    const tokens = await runZero(["tokens", "--zdn", "examples/add.0"]);
    assert.ok(tokens.stdout.includes("TokensResult"), `expected TokensResult in:\n${tokens.stdout}`);

    // parse --zdn
    const parse = await runZero(["parse", "--zdn", "examples/add.0"]);
    assert.ok(parse.stdout.includes("ParseResult"), `expected ParseResult in:\n${parse.stdout}`);

    // doc --zdn
    const doc = await runZero(["doc", "--zdn", "examples/add.0"]);
    assert.ok(doc.stdout.includes("DocResult"), `expected DocResult in:\n${doc.stdout}`);

    // test --zdn (pass)
    const testPass = await runZero(["test", "--zdn", "conformance/native/pass/test-blocks.0"]);
    assert.ok(testPass.stdout.includes("TestResult"), `expected TestResult in:\n${testPass.stdout}`);
    assert.ok(testPass.stdout.includes("ok true"), `expected ok true in:\n${testPass.stdout}`);

    // fix --plan --zdn (success)
    const fixOk = await runZero(["fix", "--plan", "--zdn", "examples/add.0"]);
    assert.ok(fixOk.stdout.includes("FixPlanResult"), `expected FixPlanResult in:\n${fixOk.stdout}`);
    assert.ok(fixOk.stdout.includes("ok true"), `expected ok true in:\n${fixOk.stdout}`);

    // fix --plan --zdn (with diagnostic)
    const fixDiag = await runZero(["fix", "--plan", "--zdn", "examples/does-not-exist.0"]).catch((error) => error);
    assert.ok(fixDiag.stdout.includes("FixPlanResult"), `expected FixPlanResult in:\n${fixDiag.stdout}`);
    assert.ok(fixDiag.stdout.includes("ok false"), `expected ok false in:\n${fixDiag.stdout}`);

    // version --zdn
    const ver = await runZero(["--version", "--zdn"]);
    assert.ok(ver.stdout.includes("VersionResult"), `expected VersionResult in:\n${ver.stdout}`);

    // doctor --zdn
    const doctorResult = await runZero(["doctor", "--zdn"]);
    assert.ok(doctorResult.stdout.includes("DoctorResult"), `expected DoctorResult in:\n${doctorResult.stdout}`);

    // skills list --zdn
    const skills = await runZero(["skills", "list", "--zdn"]);
    assert.ok(skills.stdout.includes("SkillsList"), `expected SkillsList in:\n${skills.stdout}`);

    // targets --zdn
    const targets = await runZero(["targets", "--zdn"]);
    assert.ok(targets.stdout.includes("TargetsResult"), `expected TargetsResult in:\n${targets.stdout}`);

    // abi dump --zdn
    const abiDump = await runZero(["abi", "dump", "--zdn", "examples/point.0"]);
    assert.ok(abiDump.stdout.includes("AbiDump"), `expected AbiDump in:\n${abiDump.stdout}`);

    // abi check --zdn
    const abiCheck = await runZero(["abi", "check", "--zdn", "examples/point.0"]);
    assert.ok(abiCheck.stdout.includes("AbiCheck"), `expected AbiCheck in:\n${abiCheck.stdout}`);

    // explain --zdn
    const explain = await runZero(["explain", "--zdn", "TAR001"]);
    assert.ok(explain.stdout.includes("ExplainResult"), `expected ExplainResult in:\n${explain.stdout}`);
    assert.ok(explain.stdout.includes('code "TAR001"'), `expected code field in:\n${explain.stdout}`);
  });
});
