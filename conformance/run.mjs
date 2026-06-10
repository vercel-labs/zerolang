#!/usr/bin/env node
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { access, chmod, mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { createAggregateAssert, describeFailure, finishAggregateAssert } from "../scripts/aggregate-assert.mjs";

const assert = createAggregateAssert();

if (process.env.ZERO_NATIVE_TEST_SANDBOX !== "1" && process.env.ZERO_NATIVE_TEST_ALLOW_LOCAL !== "1") {
  console.error("conformance emits native test artifacts; run `pnpm run conformance` for Vercel Sandbox execution or set ZERO_NATIVE_TEST_ALLOW_LOCAL=1 to opt into local artifacts.");
  process.exit(1);
}

const execMaxBuffer = 16 * 1024 * 1024;
const zero = resolve(process.env.ZERO_BIN || (existsSync(".zero/bin/zero") ? ".zero/bin/zero" : "bin/zero"));
const outDir = process.env.ZERO_CONFORMANCE_OUT_DIR || ".zero/conformance";
const canRunLinuxMuslX64 = process.platform === "linux" && process.arch === "x64";
const runnableDirectTarget =
  process.platform === "darwin" && process.arch === "arm64" ? "darwin-arm64" :
  process.platform === "linux" && process.arch === "x64" ? "linux-musl-x64" :
  null;
const checkTimeoutMs = Number(process.env.ZERO_CHECK_TIMEOUT_MS ?? 2000);

function parsePositiveInt(value, fallback) {
  const parsed = Number.parseInt(value ?? "", 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

const defaultCheckJobs = 1;
const checkJobs = parsePositiveInt(process.env.ZERO_CONFORMANCE_CHECK_JOBS, defaultCheckJobs);

function runnableExeArgs(input, out) {
  if (!runnableDirectTarget) return null;
  return ["build", "--emit", "exe", "--target", runnableDirectTarget, compilerInputPath(input), "--out", out];
}

await mkdir(outDir, { recursive: true });
await mkdir(`${outDir}/check-cache`, { recursive: true });

function execFileAsync(file, args = [], options = {}) {
  return new Promise((resolve, reject) => {
    const normalizedArgs = file === zero ? normalizeZeroCompilerArgs(args) : args;
    execFile(file, normalizedArgs, { maxBuffer: execMaxBuffer, ...options }, (error, stdout, stderr) => {
      if (error) {
        error.stdout = stdout;
        error.stderr = stderr;
        reject(error);
        return;
      }
      resolve({ stdout, stderr });
    });
  });
}

function tomlQuote(value) {
  return JSON.stringify(String(value));
}

function tomlArray(values) {
  return `[${values.map(tomlQuote).join(", ")}]`;
}

function appendManifestFields(lines, object, order) {
  for (const key of order) {
    if (!(key in object)) continue;
    const value = object[key];
    if (Array.isArray(value)) lines.push(`${key} = ${tomlArray(value)}`);
    else if (typeof value === "boolean") lines.push(`${key} = ${value ? "true" : "false"}`);
    else lines.push(`${key} = ${tomlQuote(value)}`);
  }
}

function manifestToml(manifest) {
  const lines = ["[package]"];
  appendManifestFields(lines, manifest.package ?? {}, ["name", "version", "license"]);
  for (const [targetName, target] of Object.entries(manifest.targets ?? {})) {
    lines.push("", `[targets.${targetName}]`);
    appendManifestFields(lines, target, ["kind", "main", "graph", "defaultTarget", "devTarget", "releaseProfile"]);
  }
  if (manifest.repositoryGraph) {
    lines.push("", "[repositoryGraph]");
    appendManifestFields(lines, manifest.repositoryGraph, ["compilerInput"]);
  }
  for (const sectionName of ["deps", "dependencies"]) {
    if (!manifest[sectionName]) continue;
    lines.push("", `[${sectionName}]`);
    for (const [name, value] of Object.entries(manifest[sectionName])) {
      if (typeof value === "string") lines.push(`${name} = ${tomlQuote(value)}`);
    }
    for (const [name, value] of Object.entries(manifest[sectionName])) {
      if (typeof value === "string") continue;
      lines.push("", `[${sectionName}.${name}]`);
      appendManifestFields(lines, value, ["path", "version", "targets", "target"]);
    }
  }
  for (const [name, lib] of Object.entries(manifest.c?.libs ?? {})) {
    lines.push("", `[c.libs.${name}]`);
    appendManifestFields(lines, lib, ["headers", "include", "lib", "link", "mode", "pkg_config", "pkgConfig"]);
  }
  return `${lines.join("\n").replace(/\n{3,}/g, "\n\n")}\n`;
}

async function writeZeroToml(root, manifest) {
  await writeFile(`${root}/zero.toml`, manifestToml(manifest));
}

function graphSidecarPath(sourcePath) {
  if (!sourcePath.endsWith(".0")) throw new Error(`${sourcePath}: expected a .0 projection path`);
  return `${sourcePath.slice(0, -2)}.graph`;
}

const compilerInputCommands = new Set(["check", "build", "run", "test", "size", "mem", "doc", "dev", "time", "fix"]);
const compilerInputValueFlags = new Set(["--backend", "--emit", "--filter", "--out", "--profile", "--release", "--target"]);
const abiInputSubcommands = new Set(["check", "dump"]);

function compilerInputPath(inputPath) {
  if (typeof inputPath !== "string" || !inputPath.endsWith(".0")) return inputPath;
  const graphPath = graphSidecarPath(inputPath);
  if (!existsSync(graphPath)) {
    throw new Error(`${inputPath}: compiler command requires graph input; missing graph sidecar ${graphPath}`);
  }
  return graphPath;
}

function normalizeZeroCompilerArgs(args) {
  if (!Array.isArray(args)) return args;
  const isCompilerInputCommand = compilerInputCommands.has(args[0]);
  const isAbiInputCommand = args[0] === "abi" && abiInputSubcommands.has(args[1]);
  if (!isCompilerInputCommand && !isAbiInputCommand) return args;
  let afterProgramArgs = false;
  let skipOptionValue = false;
  return args.map((arg) => {
    if (afterProgramArgs) return arg;
    if (arg === "--") afterProgramArgs = true;
    if (skipOptionValue) {
      skipOptionValue = false;
      return arg;
    }
    if (compilerInputValueFlags.has(arg)) {
      skipOptionValue = true;
      return arg;
    }
    return afterProgramArgs ? arg : compilerInputPath(arg);
  });
}

async function writeGraphFixture(sourcePath, source) {
  await writeFile(sourcePath, source);
  const graphPath = graphSidecarPath(sourcePath);
  await execFileAsync(zero, ["import", "--format", "binary", "--out", graphPath, sourcePath]);
  return graphPath;
}

async function importGraphFixtureFailure(sourcePath) {
  const result = await execFileAsync(zero, ["import", "--json", "--format", "binary", "--out", graphSidecarPath(sourcePath), sourcePath]).catch((error) => error);
  assert.notEqual(result.code, 0);
  return JSON.parse(result.stdout);
}

async function writeImportFailureFixture(sourcePath, source) {
  await writeFile(sourcePath, source);
  return importGraphFixtureFailure(sourcePath);
}

async function importPackageGraph(root) {
  await execFileAsync(zero, ["import", root]);
}

function isolatedCacheEnv(workerIndex) {
  return {
    ...process.env,
    ZERO_CACHE_DIR: `${outDir}/check-cache/worker-${workerIndex}`,
  };
}

async function mapLimit(items, limit, callback) {
  const results = new Array(items.length);
  let next = 0;
  const workerCount = Math.min(Math.max(1, limit), Math.max(1, items.length));
  async function worker(workerIndex) {
    await mkdir(`${outDir}/check-cache/worker-${workerIndex}`, { recursive: true });
    for (;;) {
      const index = next++;
      if (index >= items.length) return;
      try {
        results[index] = await callback(items[index], index, workerIndex);
      } catch (error) {
        const item = Array.isArray(items[index]) ? items[index][0] : items[index];
        results[index] = { error };
        assert.fail(`parallel conformance item failed: ${item}\n${describeFailure(error)}`);
      }
    }
  }
  await Promise.all(Array.from({ length: workerCount }, (_, index) => worker(index)));
  return results;
}

async function checkFixtureParallel(fixture, workerIndex) {
  const options = checkJobs > 1 ? { env: isolatedCacheEnv(workerIndex) } : {};
  const result = await execFileAsync(zero, ["check", fixture], options).catch((error) => error);
  assert.equal(result.code ?? 0, 0, `${fixture} should check cleanly\n${result.stderr ?? ""}`);
}

async function checkFailureFixtureParallel(fixture, code, workerIndex) {
  const options = checkJobs > 1 ? { env: isolatedCacheEnv(workerIndex) } : {};
  const result = await execFileAsync(zero, ["check", fixture], options).catch((error) => error);
  assert.notEqual(result.code, 0);
  assert.match(result.stderr, code);
}

async function fileExists(path) {
  try {
    await access(path);
    return true;
  } catch {
    return false;
  }
}

function assertRepositoryGraphNativeCheck(body, sourceProjectionState = "clean", options = {}) {
  const graphHirToMirUsed = options.graphHirToMirUsed === false ? false : true;
  const compilerInputReady = body.targetReadiness?.ok === true && graphHirToMirUsed;
  assert.equal(body.graphCompiler.input, "repository-graph-store");
  assert.equal(body.graphCompiler.graphStoreLoaded, true);
  assert.equal(body.graphCompiler.sourceProjectionRequiredForCompilerInput, false);
  assert.equal(body.graphCompiler.sourceProjectionState, sourceProjectionState);
  assert.equal(body.graphCompiler.graphNativeCheckerUsed, true);
  assert.equal(body.graphCompiler.graphHirToMirUsed, graphHirToMirUsed);
  assert.equal(body.graphCompiler.unsupportedGraphFacts.count, 0);
  assert.equal(body.graphCompiler.resolution.ok, true);
  assert.equal(body.graphCompiler.resolution.state, "resolved-graph-facts");
  assert.equal(body.graphCompiler.checking.ok, true);
  assert.equal(body.graphCompiler.checking.state, "checked-graph-readiness-facts");
  assert.equal(body.graphCompiler.checking.scope, "resolution-package-target-and-graph-mir-readiness");
  assert.equal(body.graphCompiler.checking.semanticDiagnosticsEnforced, false);
  assert.equal(body.graphCompiler.checking.semanticDiagnosticsAuthority, "stored-typed-graph-facts");
  assert.equal(body.graphCompiler.checking.authority, "ProgramGraphStore");
  assert.equal(body.graphCompiler.checking.sourceTextAuthority, false);
  assert.equal(body.graphCompiler.semanticFacts.state, "typed-facts");
  assert.equal(body.graphCompiler.semanticFacts.ok, true);
  const targetReady = body.targetReadiness?.ok === true;
  assert.equal(body.graphCompiler.defaultReadiness.compilerInputReady, compilerInputReady);
  assert.equal(body.graphCompiler.defaultReadiness.claim, compilerInputReady ? "ready-for-repository-graph-input" : "blocked");
  assert.equal(body.graphCompiler.defaultReadiness.sourceFreeCompile, compilerInputReady);
  assert.equal(body.graphCompiler.defaultReadiness.sourceProjectionRequired, false);
  assert.equal(body.graphCompiler.defaultReadiness.sourceProjectionState, sourceProjectionState);
  assert.equal(body.graphCompiler.defaultReadiness.graphMir.used, graphHirToMirUsed);
  assert.equal(Object.hasOwn(body.graphCompiler.defaultReadiness, "fallback"), false);
  assert.equal(body.graphCompiler.defaultReadiness.performance.validationInLoad, true);
  assert.equal(body.graphCompiler.defaultReadiness.cacheInvalidation.parserArtifactsInKey, false);
  assert(body.graphCompiler.defaultReadiness.cacheInvalidation.keyedBy.includes("nodeHashes"));
  assert(body.graphCompiler.defaultReadiness.cacheInvalidation.keyedBy.includes("symbolFacts"));
  assert(body.graphCompiler.defaultReadiness.cacheInvalidation.keyedBy.includes("modulePaths"));
  assert(body.graphCompiler.defaultReadiness.cacheInvalidation.keyedBy.includes("importPaths"));
  assert.equal(body.graphCompiler.defaultReadiness.targetReadinessOk, targetReady);
  assert.equal(body.compileTime.deterministic, true);
  assert.equal(body.targetReadiness.languageOk, true);
  assert.equal(body.safetyFacts.schemaVersion, 1);
}

function assertSourceGraph(body, artifact, moduleIdentity, lowering = "typed-program-graph-mir", canonicalSource = false, sourceProjectionState = undefined) {
  assert.equal(body.graph.artifact, artifact);
  assert.equal(body.graph.canonicalSource, canonicalSource);
  assert.equal(body.graph.moduleIdentity, moduleIdentity);
  assert.match(body.graph.graphHash, /^graph:[0-9a-f]{16}$/);
  assert.equal(body.graph.lowering, lowering);
  if (sourceProjectionState !== undefined) assert.equal(body.graph.sourceProjectionState, sourceProjectionState);
}

const programGraphParseTreeKeys = new Map();

function assertProgramGraphCompilerInput(body, artifact) {
  assert(body.compilerCaches.every((cache) => cache.sourceKind === "program-graph" && cache.graphHash === body.graph.graphHash));
  assert(body.compilerCaches.every((cache) => cache.parserArtifactsInKey === false));
  const caches = new Map(body.compilerCaches.map((cache) => [cache.name, cache]));
  const assertCacheInputs = (name, includes, excludes = []) => {
    const cache = caches.get(name);
    assert(cache, `missing compiler cache ${name}`);
    for (const key of includes) assert(cache.graphKeyInputs.includes(key), `${name} cache key inputs should include ${key}`);
    for (const key of excludes) assert(!cache.graphKeyInputs.includes(key), `${name} cache key inputs should not include ${key}`);
  };
  assertCacheInputs("parseTree", ["graphHash", "nodeHashes", "importPaths", "compilerVersion", "packageDependencies"], ["sourceFiles", "targetFacts", "profile"]);
  assertCacheInputs("interface", ["graphHash", "modulePaths", "symbolFacts", "importGraph"], ["targetFacts", "profile", "compilerVersion", "packageDependencies"]);
  assertCacheInputs("checkedBody", ["graphHash", "importPaths", "targetFacts", "compilerVersion", "packageDependencies"], ["sourceFiles", "profile"]);
  assertCacheInputs("specialization", ["graphHash", "importPaths", "targetFacts", "profile", "compilerVersion", "packageDependencies"], ["sourceFiles"]);
  if (body.graph.lowering === "mapped-final-mir") {
    assertCacheInputs("mappedFinalMir", ["graphHash", "importPaths", "targetFacts", "compilerVersion", "packageDependencies", "emitKind", "backend"], ["sourceFiles", "profile"]);
    const mappedMirCache = caches.get("mappedFinalMir");
    assert.match(mappedMirCache.path, /\.zero\/cache\/native\/mir-[0-9a-f]+\.zmir$/);
    assert.equal(mappedMirCache.memoryMapped, true);
    assert.equal(mappedMirCache.borrowedStorage, true);
    assert.equal(mappedMirCache.byteLength > 0, true);
    assert.equal(body.incrementalInvalidation.graphInput.mappedFinalMir.path, mappedMirCache.path);
    assert.equal(body.incrementalInvalidation.graphInput.mappedFinalMir.memoryMapped, true);
    assert.equal(body.incrementalInvalidation.graphInput.mappedFinalMir.borrowedStorage, true);
  }
  assertCacheInputs("emittedObject", ["graphHash", "importPaths", "targetFacts", "profile", "compilerVersion", "packageDependencies"], ["sourceFiles"]);
  const parseTreeCache = caches.get("parseTree");
  assert.equal(parseTreeCache.invalidatesOn, "ProgramGraph input");
  if (programGraphParseTreeKeys.has(artifact)) assert.equal(parseTreeCache.key, programGraphParseTreeKeys.get(artifact));
  else programGraphParseTreeKeys.set(artifact, parseTreeCache.key);
  assert.equal(body.incrementalInvalidation.sourceKind, "program-graph");
  assert.equal(body.incrementalInvalidation.graphInput.artifact, artifact);
  assert.equal(body.incrementalInvalidation.graphInput.graphHash, body.graph.graphHash);
  assert.equal(body.incrementalInvalidation.graphInput.parserArtifactsInKey, false);
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("graphHash"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("nodeHashes"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("typeFacts"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("symbolFacts"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("modulePaths"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("importPaths"));
  assert.equal(body.incrementalInvalidation.changedInputs.graphArtifact, artifact);
  assert.equal(body.incrementalInvalidation.interfaceFingerprints.sourceKind, "program-graph");
  assert.equal(body.incrementalInvalidation.interfaceFingerprints.graphHash, body.graph.graphHash);
}

function assertLlvmPhiPredecessors(ir) {
  const predecessors = new Map();
  const phiEdges = [];
  let functionIndex = -1;
  let current = "-1:entry";
  const scoped = (label) => `${functionIndex}:${label}`;
  for (const line of ir.split("\n")) {
    if (line.startsWith("define ")) {
      functionIndex++;
      current = scoped("entry");
      continue;
    }
    const label = line.match(/^(L[0-9]+):$/);
    if (label) {
      current = scoped(label[1]);
      continue;
    }
    const branch = line.match(/^  br (?:label %(L[0-9]+)|i1 [^,]+, label %(L[0-9]+), label %(L[0-9]+))$/);
    if (branch) {
      for (const target of branch.slice(1).filter(Boolean)) {
        const key = scoped(target);
        if (!predecessors.has(key)) predecessors.set(key, new Set());
        predecessors.get(key).add(current);
      }
      continue;
    }
    if (!line.includes(" = phi ")) continue;
    for (const match of line.matchAll(/\[[^\]]+, %(L[0-9]+)\]/g)) {
      phiEdges.push({ block: current, predecessor: scoped(match[1]) });
    }
  }
  for (const edge of phiEdges) {
    assert(
      predecessors.get(edge.block)?.has(edge.predecessor),
      `LLVM phi predecessor ${edge.predecessor} must branch to ${edge.block}`,
    );
  }
}

async function buildLlvmIrFixture(fixture, name) {
  const outPath = `${outDir}/${name}.ll`;
  const build = await execFileAsync(zero, [
    "build",
    "--json",
    "--emit",
    "llvm-ir",
    "--backend",
    "llvm",
    fixture,
    "--out",
    outPath,
  ]);
  const body = JSON.parse(build.stdout);
  assert.equal(body.emit, "llvm-ir");
  assert.equal(body.compiler, "zero-c-llvm-ir");
  assert.equal(body.generatedCBytes, 0);
  return readFile(outPath, "utf8");
}

async function assertLlvmHostExitCode(fixture, name, expectedCode) {
  const readiness = await execFileAsync(zero, ["check", "--json", "--backend", "llvm", fixture]).catch((error) => error);
  if (readiness.code) return;
  const readinessBody = JSON.parse(readiness.stdout);
  if (!readinessBody.targetReadiness?.ok) return;

  const out = `${outDir}/${name}`;
  const build = await execFileAsync(zero, ["build", "--json", "--backend", "llvm", fixture, "--out", out]);
  const body = JSON.parse(build.stdout);
  assert.equal(body.emit, "exe");
  assert.equal(body.objectBackend.backendFamily, "llvm");
  const run = await execFileAsync(out, []).catch((error) => error);
  assert.equal(run.code ?? 0, expectedCode);
  assert.equal(run.signal ?? null, null);
}

async function assertBoundsTrap(fixture, name) {
  const out = `${outDir}/${name}`;
  const build = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", fixture, "--out", out]).catch((error) => error);
  if (build.code) {
    const body = JSON.parse(build.stdout);
    assert.equal(body.diagnostics?.[0]?.code, "BLD004");
    return;
  }
  const body = JSON.parse(build.stdout);
  assert.equal(body.generatedCBytes, 0);
  if (!canRunLinuxMuslX64) return;
  const failedRun = await execFileAsync(out, []).catch((error) => error);
  assert.notEqual(failedRun.code ?? (failedRun.signal ? 1 : 0), 0);
  if (failedRun.stderr) assert.match(failedRun.stderr, /zero bounds check failed/);
}

async function assertDirectRuntimeOrUnsupported(fixture, name, expected) {
  const out = `${outDir}/${name}`;
  const build = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", fixture, "--out", out]).catch((error) => error);
  if (build.code) {
    const body = JSON.parse(build.stdout);
    assert.equal(body.diagnostics?.[0]?.code, "BLD004");
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

async function assertDirectRuntimeRequired(fixture, name, expected) {
  const target = runnableDirectTarget ?? "linux-musl-x64";
  const out = `${outDir}/${name}`;
  const build = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", target, fixture, "--out", out]);
  const body = JSON.parse(build.stdout);
  assert.equal(body.generatedCBytes, 0);
  assert.equal(body.legacy, false);
  if (!runnableDirectTarget) return;
  const run = await execFileAsync(out, expected.args ?? [], expected.env ? { env: { ...process.env, ...expected.env } } : {});
  if (expected.stdout instanceof RegExp) assert.match(run.stdout, expected.stdout);
  else assert.equal(run.stdout, expected.stdout);
  if (expected.stderr !== undefined) assert.equal(run.stderr, expected.stderr);
}

async function assertCommonRuntimeOrUnsupported(fixture, name, expected) {
  const target = runnableDirectTarget ?? "linux-musl-x64";
  const out = `${outDir}/${name}`;
  const build = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", target, fixture, "--out", out]).catch((error) => error);
  if (build.code) {
    const body = JSON.parse(build.stdout);
    assert.equal(body.diagnostics?.[0]?.code, "BLD004");
    return;
  }

  const body = JSON.parse(build.stdout);
  assert.equal(body.generatedCBytes, 0);
  assert.equal(body.legacy, false);
  if (!runnableDirectTarget) return;
  const run = await execFileAsync(out, expected.args ?? [], expected.env ? { env: { ...process.env, ...expected.env } } : {}).catch((error) => error);
  assert.equal(run.code ?? 0, 0);
  assert.equal(run.signal ?? null, null);
  if (expected.stdout instanceof RegExp) assert.match(run.stdout, expected.stdout);
  else assert.equal(run.stdout, expected.stdout);
  if (expected.stderr !== undefined) assert.equal(run.stderr, expected.stderr);
  if (expected.file) assert.equal(await readFile(`${outDir}/${expected.file.name}`, "utf8"), expected.file.text);
}

async function assertCheckTimeoutOrDiagnostic(fixture, expectedCodes) {
  const result = await execFileAsync(zero, ["check", "--json", fixture], { timeout: checkTimeoutMs }).catch((error) => error);
  if (result.killed || result.signal) {
    assert.equal(result.killed, true);
    return { timedOut: true };
  }
  assert.notEqual(result.code, 0);
  const body = JSON.parse(result.stdout);
  const code = body.diagnostics?.[0]?.code;
  assert(expectedCodes.includes(code), `expected one of ${expectedCodes.join(", ")}, got ${code}`);
  return { timedOut: false, code };
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
  assert(bytes.includes(Buffer.concat([Buffer.from(exportedName), Buffer.from([0])])));
  return bytes;
}

function hasAarch64Instruction(bytes, expected) {
  for (let offset = 0; offset + 4 <= bytes.length; offset++) {
    if (bytes.readUInt32LE(offset) === expected) return true;
  }
  return false;
}

function hasAarch64CondBranch(bytes, cond) {
  for (let offset = 0; offset + 4 <= bytes.length; offset++) {
    const instruction = bytes.readUInt32LE(offset);
    if ((instruction & 0xff000010) === 0x54000000 && (instruction & 0xf) === cond) return true;
  }
  return false;
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

async function assertMachOX64Object(path, exportedName) {
  const bytes = await readFile(path);
  assert.equal(bytes.readUInt32LE(0), 0xfeedfacf);
  assert.equal(bytes.readUInt32LE(4), 0x01000007);
  assert.equal(bytes.readUInt32LE(12), 1);
  assert(bytes.includes(Buffer.concat([Buffer.from(`_${exportedName}`), Buffer.from([0])])));
  return bytes;
}

function assertX64SliceBoundsTrapBytes(bytes) {
  assert(bytes.includes(Buffer.from([0x0f, 0x86])));
  assert(bytes.includes(Buffer.from([0x0f, 0x0b])));
}

function assertX64U16RecordFieldBytes(bytes) {
  assert(bytes.includes(Buffer.from([0x0f, 0xb7])));
  assert(bytes.includes(Buffer.from([0x66, 0x89])));
}

function isRexW(byte) {
  return (byte & 0xf8) === 0x48;
}

function hasX64MovMemoryToR64(bytes) {
  for (let i = 0; i + 2 < bytes.length; i++) {
    if (!isRexW(bytes[i]) || bytes[i + 1] !== 0x8b) continue;
    const mod = bytes[i + 2] >> 6;
    if (mod !== 3) return true;
  }
  return false;
}

function hasX64MovR64ToMemory(bytes) {
  for (let i = 0; i + 2 < bytes.length; i++) {
    if (!isRexW(bytes[i]) || bytes[i + 1] !== 0x89) continue;
    const mod = bytes[i + 2] >> 6;
    if (mod !== 3) return true;
  }
  return false;
}

function hasX64CmpR64(bytes) {
  for (let i = 0; i + 2 < bytes.length; i++) {
    if (!isRexW(bytes[i]) || bytes[i + 1] !== 0x39) continue;
    const mod = bytes[i + 2] >> 6;
    if (mod === 3) return true;
  }
  return false;
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

const passCheckFixtures = [
  "conformance/run/pass/hello.0",
  "conformance/native/pass/params.0",
  "conformance/native/pass/shape.0",
  "conformance/native/pass/mutref-shape-param.0",
  "conformance/native/pass/mutref-shape-param-nested.0",
  "conformance/native/pass/primitive-stdlib.0",
  "conformance/native/pass/variants-defer-stdlib.0",
  "conformance/native/pass/defer-return-raise-nested.0",
  "conformance/native/pass/payload-match.0",
  "conformance/native/pass/break-continue.0",
  "conformance/native/pass/nested-break-continue.0",
  "conformance/native/pass/untyped-literal-adoption.0",
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
  "conformance/native/pass/std-hosted-cli.0",
  "conformance/native/pass/std-fs.0",
  "conformance/native/pass/std-fs-bytes.0",
  "conformance/native/pass/frame-large-locals.0",
  "conformance/native/pass/frame-limit-boundary.0",
  "conformance/native/pass/std-fs-resource.0",
  "conformance/native/pass/std-fs-readall.0",
  "conformance/native/pass/std-fs-polish.0",
  "conformance/native/pass/std-fs-breadth.0",
  "conformance/native/pass/std-fs-file-helpers.0",
  "conformance/native/pass/std-math-breadth.0",
  "conformance/native/pass/std-numeric-random-time.0",
  "conformance/native/pass/std-io-lines.0",
  "conformance/native/pass/std-path-io-breadth.0",
  "conformance/native/pass/std-str-breadth.0",
  "conformance/native/pass/std-testing-log.0",
  "conformance/native/pass/std-testing-helpers-test.0",
  "conformance/native/pass/std-path-helper-name-collision.0",
  "conformance/native/pass/std-net-http-breadth.0",
  "conformance/native/pass/std-http-metadata-neutral.0",
  "conformance/native/pass/std-http-fetch.0",
  "conformance/native/pass/std-http-errors.0",
  "conformance/native/pass/std-http-response-helpers.0",
  "conformance/native/pass/std-http-text-html-response-helpers.0",
  "conformance/native/pass/std-http-redirect-response-helpers.0",
  "conformance/native/pass/std-http-api-helpers.0",
  "conformance/native/pass/std-http-cors-helpers.0",
  "conformance/native/pass/std-http-auth-helpers.0",
  "conformance/native/pass/std-data-formats.0",
  "conformance/native/pass/std-codec-json-url.0",
  "conformance/native/pass/std-json-bytes.0",
  "conformance/native/pass/std-json-inline-bytes.0",
  "conformance/native/pass/std-json-duplicate-keys.0",
  "conformance/native/pass/std-json-allocator-capacity.0",
  "conformance/native/pass/std-platform-basics.0",
  "conformance/native/pass/std-mem-arrays.0",
  "conformance/native/pass/std-mem-generic-items.0",
  "conformance/native/pass/std-mem-field-items.0",
  "conformance/native/pass/std-mem-byte-field-copy.0",
  "conformance/native/pass/std-mem-bool-copy-items.0",
  "conformance/native/pass/std-mem-u64-contains.0",
  "conformance/native/pass/std-mem-u64-copy-items.0",
  "conformance/native/pass/std-mem-local-u64-copy-items.0",
  "conformance/native/pass/array-repeat-literal.0",
  "conformance/native/pass/array-repeat-record-field.0",
  "conformance/native/pass/integer-widths.0",
  "conformance/native/pass/std-codec-widths.0",
  "conformance/native/pass/std-crypto-hmac32.0",
  "conformance/native/pass/parse-integers.0",
  "conformance/native/pass/explicit-casts.0",
  "conformance/native/pass/float-char-casts.0",
  "conformance/native/pass/radix-suffix-literals.0",
  "conformance/native/pass/char-literals.0",
  "conformance/native/pass/float-primitives.0",
  "conformance/native/pass/wrapping-saturating-arithmetic.0",
  "conformance/native/pass/maybe-error-flow.0",
  "conformance/native/pass/maybe-guard-branch-restore.0",
  "conformance/native/pass/maybe-guard-negated-conjunction.0",
  "conformance/native/pass/maybe-guard-scalar-match.0",
  "conformance/native/pass/maybe-guard-short-circuit-match.0",
  "conformance/native/pass/maybe-guard-static-index-literals.0",
  "conformance/native/pass/maybe-guard-variant-match-after.0",
  "conformance/native/pass/maybe-guard-variant-guard-side-effect.0",
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
  "conformance/native/pass/byte-view-call-single-eval.0",
  "conformance/native/pass/generic-spans.0",
  "conformance/native/pass/open-ended-slices.0",
  "conformance/native/pass/string-slices.0",
  "conformance/native/pass/string-param-span-slice.0",
  "conformance/native/pass/coff-dynamic-byte-slice.0",
  "conformance/native/pass/macho-large-byte-slice-blocked.0",
  "conformance/native/pass/macho-nested-call-scratch-blocked.0",
  "conformance/native/pass/macho-open-byte-slice.0",
  "conformance/native/pass/string-byte-ergonomics.0",
  "conformance/native/pass/indexed-mutation.0",
  "conformance/native/pass/dynamic-indexed-store-scratch.0",
  "conformance/native/pass/nested-lvalues.0",
  "conformance/native/pass/mutable-spans.0",
  "conformance/native/pass/mutref-indexed-lvalues.0",
  "conformance/native/pass/generic-mem.0",
  "conformance/native/pass/generic-function-basic.0",
  "conformance/native/pass/generic-nested-calls.0",
  "conformance/native/pass/generic-specialization-reuse.0",
  "conformance/native/pass/generic-multi-specialization.0",
  "conformance/native/pass/generic-inferred-specialized-call.0",
  "conformance/native/pass/generic-nested-local-specialization.0",
  "conformance/native/pass/generic-static-array-specialization.0",
  "conformance/native/pass/generic-static-forwarded-array-specialization.0",
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
  "conformance/native/pass/c-import-type-shadowing.0",
  "conformance/native/pass/c-import-alias-later-local.0",
  "conformance/native/pass/match-fallback.0",
  "conformance/native/pass/memory-types.0",
  "conformance/native/pass/owned-transfer.0",
  "conformance/native/pass/owned-field-move-return-branch.0",
  "conformance/native/pass/owned-field-reassignment-after-move.0",
  "conformance/native/pass/owned-array-static-index-reassignment.0",
  "conformance/native/pass/owned-mutspan-alias-reassignment-after-move.0",
  "conformance/native/pass/maybe-owned-null-repeat.0",
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
  "conformance/native/pass/std-collections-algorithms.0",
  "conformance/native/pass/std-collections-u8.0",
  "conformance/native/pass/std-collections-mutspan-memory.0",
  "conformance/native/pass/std-collections-usize-memory.0",
  "conformance/native/pass/std-collections-query-memory.0",
  "conformance/native/pass/std-search-sort-widths.0",
  "conformance/native/pass/owned-byte-buffer.0",
  "conformance/check/pass/generic-function-basic.0",
  "conformance/check/pass/generic-array-inference.0",
  "conformance/check/pass/generic-static-explicit-shadowing.0",
  "conformance/check/pass/generic-static-forwarding.0",
  "conformance/check/pass/generic-static-method-forwarding.0",
  "conformance/check/pass/generic-static-return-substitution.0",
  "conformance/check/pass/generic-static-wrapper-type-collision.0",
  "conformance/check/pass/static-interface-return-substitution.0",
  "conformance/check/pass/generic-untyped-static-const-inference.0",
  "conformance/check/pass/generic-const-shadowing.0",
  "conformance/check/pass/generic-const-type-name-collision.0",
  "conformance/check/pass/generic-mixed-const-type-name-collision.0",
  "conformance/check/pass/generic-method-outer-param-inference.0",
  "conformance/native/pass/generic-nested-calls.0",
  "conformance/native/pass/generic-specialization-reuse.0",
  "conformance/native/pass/generic-multi-specialization.0",
  "conformance/native/pass/generic-inferred-specialized-call.0",
  "conformance/native/pass/generic-nested-local-specialization.0",
  "conformance/native/pass/generic-static-array-specialization.0",
  "conformance/native/pass/generic-static-forwarded-array-specialization.0",
  "conformance/check/pass/generic-shape-basic.0",
  "conformance/check/pass/generic-shape-multi.0",
  "conformance/check/pass/generic-shape-methods.0",
  "conformance/native/pass/generic-constructor-expected.0",
  "conformance/native/pass/generic-literals-arrays.0",
  "conformance/check/pass/receiver-method-calls.0",
  "conformance/check/pass/shape-field-defaults.0",
  "conformance/check/pass/static-value-params.0",
  "conformance/check/pass/static-interface-basic.0",
  "conformance/check/pass/call-resolution-inspection.0",
  "conformance/check/pass/call-resolution-edge-cases.0",
  "conformance/native/pass/static-interface-mutref.0",
  "conformance/native/pass/static-interface-static-param.0",
  "conformance/check/pass/top-level-const.0",
  "conformance/check/pass/const-arithmetic.0",
  "conformance/check/pass/type-alias-basic.0",
  "conformance/check/pass/static-method-namespace.0",
  "conformance/check/pass/fmt-core-usability.0",
  "conformance/check/pass/c-header-import.0",
  "conformance/check/pass/c-import-local-shadowing.0",
  "conformance/check/pass/match-fallback.0",
  "conformance/native/pass/match-choice-fallback.0",
  "conformance/check/pass/memory-types.0",
  "conformance/check/pass/std-mem-field-items.0",
  "conformance/check/pass/std-mem-field-slice.0",
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
];
await mapLimit(passCheckFixtures, checkJobs, (fixture, _index, workerIndex) => checkFixtureParallel(fixture, workerIndex));

const checkJsonSuccess = await execFileAsync(zero, ["check", "--json", "conformance/native/pass/explicit-casts.0"]);
const checkJsonSuccessBody = JSON.parse(checkJsonSuccess.stdout);
assert.equal(checkJsonSuccessBody.ok, true);
assert.equal(checkJsonSuccessBody.diagnostics.length, 0);
assert.equal(checkJsonSuccessBody.artifact, "conformance/native/pass/explicit-casts.graph");
assert.equal(checkJsonSuccessBody.canonicalSource, false);
assert.equal(checkJsonSuccessBody.moduleIdentity, "module:explicit-casts");
assert.match(checkJsonSuccessBody.graphHash, /^graph:[0-9a-f]{16}$/);
assert.equal(checkJsonSuccessBody.check.phase, "typecheck");
assert.equal(checkJsonSuccessBody.check.lowering, "graph-native-check");
assert.equal(checkJsonSuccessBody.graphCompiler.input, "program-graph-artifact");
assert.equal(checkJsonSuccessBody.graphCompiler.graphNativeCheckerUsed, true);
assert.equal(checkJsonSuccessBody.graphCompiler.graphHirToMirUsed, true);

const agentSurfaceClassification = JSON.parse(await readFile("conformance/agent-surface/classification.json", "utf8"));
assert.equal(agentSurfaceClassification.schema, 1);
assert.deepEqual(agentSurfaceClassification.fixtures.map((item) => item.id), [
  "interface-method-generic-binding",
  "direct-generic-recursion",
  "direct-generic-specialization-name-collision",
  "stdlib-signature-parity",
  "owned-drop-direct-backend-unsupported",
]);

const agentSurfaceInterfaceMethodGeneric = await execFileAsync(zero, ["check", "--json", "conformance/agent-surface/fixtures/interface-method-generic-binding.0"]);
const agentSurfaceInterfaceMethodGenericBody = JSON.parse(agentSurfaceInterfaceMethodGeneric.stdout);
assert.equal(agentSurfaceInterfaceMethodGenericBody.ok, true);
assert.equal(agentSurfaceInterfaceMethodGenericBody.diagnostics.length, 0);

const agentSurfaceDirectGenericCheck = await execFileAsync(zero, ["check", "--json", "conformance/agent-surface/fixtures/direct-generic-recursion.0"]);
const agentSurfaceDirectGenericCheckBody = JSON.parse(agentSurfaceDirectGenericCheck.stdout);
assert.equal(agentSurfaceDirectGenericCheckBody.ok, true);
assert.equal(agentSurfaceDirectGenericCheckBody.diagnostics.length, 0);
const agentSurfaceDirectGenericReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "linux-musl-x64",
  "conformance/agent-surface/fixtures/direct-generic-recursion.0",
]);
const agentSurfaceDirectGenericReadinessBody = JSON.parse(agentSurfaceDirectGenericReadiness.stdout);
assert.equal(agentSurfaceDirectGenericReadinessBody.ok, true);
assert.equal(agentSurfaceDirectGenericReadinessBody.targetReadiness.ok, true);
assert.equal(agentSurfaceDirectGenericReadinessBody.targetReadiness.buildable, true);
assert.equal(agentSurfaceDirectGenericReadinessBody.targetReadiness.diagnostics.length, 0);

const agentSurfaceDirectGenericCollisionReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "linux-musl-x64",
  "conformance/agent-surface/fixtures/direct-generic-specialization-name-collision.0",
]);
const agentSurfaceDirectGenericCollisionReadinessBody = JSON.parse(agentSurfaceDirectGenericCollisionReadiness.stdout);
assert.equal(agentSurfaceDirectGenericCollisionReadinessBody.ok, true);
assert.equal(agentSurfaceDirectGenericCollisionReadinessBody.targetReadiness.ok, false);
assert.equal(agentSurfaceDirectGenericCollisionReadinessBody.targetReadiness.buildable, false);
assert.equal(agentSurfaceDirectGenericCollisionReadinessBody.targetReadiness.diagnostics[0].code, "BLD004");
assert.match(agentSurfaceDirectGenericCollisionReadinessBody.targetReadiness.diagnostics[0].message, /specialization name collides/);

const compilerMetrics = await execFileAsync("node", ["--experimental-strip-types", "--disable-warning=ExperimentalWarning", "scripts/compiler-metrics.mts"]);
const compilerMetricsBody = JSON.parse(compilerMetrics.stdout);
assert.equal(compilerMetricsBody.schema, 1);
assert(compilerMetricsBody.files["native/zero-c/src/checker.c"].lines > 0);
assert.equal(compilerMetricsBody.files["native/zero-c/src/fs.c"].shellCalls, 0);
assert.equal(compilerMetricsBody.files["native/zero-c/src/main.c"].shellCalls, 0);
for (const [path, metrics] of Object.entries(compilerMetricsBody.files)) {
  assert.equal(metrics.shellCalls, 0, `${path} should not introduce shell execution calls`);
}

const relativeToolDir = `${outDir}/relative-tools`;
await mkdir(relativeToolDir, { recursive: true });
await writeFile(`${relativeToolDir}/cc`, "#!/bin/sh\nprintf 'fake relative cc\\n'\n");
await chmod(`${relativeToolDir}/cc`, 0o755);
const relativePathDoctor = await execFileAsync(zero, ["doctor", "--json"], { env: { ...process.env, PATH: relativeToolDir } }).catch((error) => error);
assert.notEqual(relativePathDoctor.code, 0);
const relativePathDoctorBody = JSON.parse(relativePathDoctor.stdout);
const relativePathNativeCompiler = relativePathDoctorBody.checks.find((check) => check.name === "native-c-compiler");
assert.equal(relativePathNativeCompiler.status, "error");
assert.match(relativePathNativeCompiler.message, /no native C compiler found/);

assert(Array.isArray(compilerMetricsBody.largeFunctions));
assert(compilerMetricsBody.stdlib.mainHelperCount > 0);
assert.equal(compilerMetricsBody.stdlib.mainHelperCount, compilerMetricsBody.stdlib.checkerReturnCount);
assert.equal(compilerMetricsBody.stdlib.mainHelperCount, compilerMetricsBody.stdlib.checkerArgCountCount);
assert.deepEqual(compilerMetricsBody.stdlib.duplicateCheckerReturnTypes, []);
assert.deepEqual(compilerMetricsBody.stdlib.duplicateCheckerArgCounts, []);
assert.deepEqual(compilerMetricsBody.stdlib.checkerReturnsMissingFromMainHelpers, []);
assert.deepEqual(compilerMetricsBody.stdlib.mainHelpersMissingFromCheckerReturns, []);
assert.deepEqual(compilerMetricsBody.stdlib.checkerArgCountsMissingFromMainHelpers, []);
assert.deepEqual(compilerMetricsBody.stdlib.mainHelpersMissingFromCheckerArgCounts, []);
assert.deepEqual(compilerMetricsBody.stdlib.argCountMismatches, []);
assert.equal(compilerMetricsBody.stdlib.sharedSignatureLookup.checkerReturnTypes, true);
assert.equal(compilerMetricsBody.stdlib.sharedSignatureLookup.checkerArgCounts, true);
assert.equal(compilerMetricsBody.backendFormats.elf.sharedWriter, true);
assert.equal(compilerMetricsBody.backendFormats.elf.x86ObjectUsesSharedWriter, true);
assert.equal(compilerMetricsBody.backendFormats.elf.x86ExecutableUsesSharedWriter, true);
assert.equal(compilerMetricsBody.backendFormats.elf.aarch64ObjectUsesSharedWriter, true);
assert.equal(compilerMetricsBody.backendFormats.elf.aarch64ExecutableUsesSharedWriter, true);
assert.deepEqual(compilerMetricsBody.backendFormats.elf.archFilesWithLocalSectionWriters, []);
assert.equal(compilerMetricsBody.backendFormats.elf.patchStateModule, true);
assert.equal(compilerMetricsBody.backendFormats.elf.x86UsesPatchStateModule, true);
assert.deepEqual(compilerMetricsBody.backendFormats.elf.archFilesWithLocalPatchState, []);
assert.equal(compilerMetricsBody.backendFormats.coff.sharedWriter, true);
assert.equal(compilerMetricsBody.backendFormats.coff.objectUsesSharedWriter, true);
assert.equal(compilerMetricsBody.backendFormats.coff.executableUsesSharedWriter, true);
assert.deepEqual(compilerMetricsBody.backendFormats.coff.archFilesWithLocalContainerWriters, []);
assert.equal(compilerMetricsBody.backendFormats.coff.patchStateModule, true);
assert.equal(compilerMetricsBody.backendFormats.coff.x64UsesPatchStateModule, true);
assert.deepEqual(compilerMetricsBody.backendFormats.coff.archFilesWithLocalPatchState, []);
assert.equal(compilerMetricsBody.backendFormats.macho.sharedWriter, true);
assert.equal(compilerMetricsBody.backendFormats.macho.objectUsesSharedWriter, true);
assert.equal(compilerMetricsBody.backendFormats.macho.executableUsesSharedWriter, true);
assert.deepEqual(compilerMetricsBody.backendFormats.macho.archFilesWithLocalContainerWriters, []);
assert.equal(compilerMetricsBody.backendFormats.macho.patchStateModule, true);
assert.equal(compilerMetricsBody.backendFormats.macho.archFileUsesPatchStateModule, true);
assert.deepEqual(compilerMetricsBody.backendFormats.macho.archFilesWithLocalPatchState, []);
assert.equal(compilerMetricsBody.backendFormats.x64.sharedEncodingPrimitives, true);
assert.equal(compilerMetricsBody.backendFormats.x64.elfUsesSharedEncodingPrimitives, true);
assert.equal(compilerMetricsBody.backendFormats.x64.coffUsesSharedEncodingPrimitives, true);
assert.deepEqual(compilerMetricsBody.backendFormats.x64.formatFilesWithLocalEncodingPrimitives, []);
assert.equal(compilerMetricsBody.backendFormats.aarch64.sharedEncodingPrimitives, true);
assert.equal(compilerMetricsBody.backendFormats.aarch64.elfUsesSharedEncodingPrimitives, true);
assert.equal(compilerMetricsBody.backendFormats.aarch64.machoUsesSharedEncodingPrimitives, true);
assert.deepEqual(compilerMetricsBody.backendFormats.aarch64.formatFilesWithLocalEncodingPrimitives, []);
assert.equal(compilerMetricsBody.budget.ok, true);
assert.deepEqual(compilerMetricsBody.budget.violations, []);
assert.equal(typeof compilerMetricsBody.budget.reportThreshold, "number");
for (const item of compilerMetricsBody.largeFunctions) {
  assert.equal(typeof item.path, "string");
  assert.equal(typeof item.signature, "string");
  assert.equal(typeof item.line, "number");
  assert.equal(typeof item.lines, "number");
  assert(item.lines >= compilerMetricsBody.budget.reportThreshold);
}

const unsafeCompilerOverrideBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--out",
  `${outDir}/unsafe-cc-override`,
  "examples/json-api-client.graph",
], { env: { ...process.env, ZERO_CC: "cc;touch" } }).catch((error) => error);
assert.notEqual(unsafeCompilerOverrideBuild.code, 0);
assert.match(unsafeCompilerOverrideBuild.stderr, /compiler override contains unsafe shell characters/);
const unsafeCompilerOverrideBody = JSON.parse(unsafeCompilerOverrideBuild.stdout);
assert.equal(unsafeCompilerOverrideBody.ok, false);
assert.equal(unsafeCompilerOverrideBody.diagnostics[0].code, "BLD003");
assert.equal(unsafeCompilerOverrideBody.diagnostics[0].actual, "compiler override contains unsafe shell characters");
assert.match(unsafeCompilerOverrideBody.diagnostics[0].help, /without flags, whitespace, or shell syntax/);

const agentSurfaceBorrowExplain = await execFileAsync(zero, ["explain", "--json", "BOR001"]);
const agentSurfaceBorrowExplainBody = JSON.parse(agentSurfaceBorrowExplain.stdout);
assert.equal(agentSurfaceBorrowExplainBody.code, "BOR001");
assert.equal(agentSurfaceBorrowExplainBody.repair.id, "end-conflicting-borrow");
assert.match(agentSurfaceBorrowExplainBody.summary, /lexical scope/);

const agentSurfaceMalformedLocalUseFixture = `${outDir}/malformed-local-use.0`;
await writeFile(agentSurfaceMalformedLocalUseFixture, 'use local.\n\npub fn main(world: World) -> Void raises {\n    check world.out.write("malformed local use parser fixture\\n")\n}\n');
const agentSurfaceMalformedLocalUse = await execFileAsync(zero, ["parse", "--json", agentSurfaceMalformedLocalUseFixture]).catch((error) => error);
assert.notEqual(agentSurfaceMalformedLocalUse.code, 0);
const agentSurfaceMalformedLocalUseBody = JSON.parse(agentSurfaceMalformedLocalUse.stdout);
assert.equal(agentSurfaceMalformedLocalUseBody.diagnostics[0].code, "PAR100");
assert.match(agentSurfaceMalformedLocalUseBody.diagnostics[0].message, /expected import module segment/);
assert.equal(agentSurfaceMalformedLocalUseBody.diagnostics[0].line, 1);
assert.equal(agentSurfaceMalformedLocalUseBody.diagnostics[0].column, 11);

const agentSurfaceSplitUseFixture = `${outDir}/split-use-path.0`;
await writeFile(agentSurfaceSplitUseFixture, 'use std.\ncodec\n\npub fn main(world: World) -> Void raises {\n    check world.out.write("split use parser fixture\\n")\n}\n');
const agentSurfaceSplitUse = await execFileAsync(zero, ["parse", "--json", agentSurfaceSplitUseFixture]).catch((error) => error);
assert.notEqual(agentSurfaceSplitUse.code, 0);
const agentSurfaceSplitUseBody = JSON.parse(agentSurfaceSplitUse.stdout);
assert.equal(agentSurfaceSplitUseBody.diagnostics[0].code, "PAR100");
assert.match(agentSurfaceSplitUseBody.diagnostics[0].message, /expected import module segment/);
assert.equal(agentSurfaceSplitUseBody.diagnostics[0].line, 1);
assert.equal(agentSurfaceSplitUseBody.diagnostics[0].column, 9);

const agentSurfaceKeywordUseFixture = `${outDir}/keyword-use.0`;
await writeFile(agentSurfaceKeywordUseFixture, 'use pub\n\npub fn main(world: World) -> Void raises {\n    check world.out.write("keyword use parser fixture\\n")\n}\n');
const agentSurfaceKeywordUse = await execFileAsync(zero, ["parse", "--json", agentSurfaceKeywordUseFixture]).catch((error) => error);
assert.notEqual(agentSurfaceKeywordUse.code, 0);
const agentSurfaceKeywordUseBody = JSON.parse(agentSurfaceKeywordUse.stdout);
assert.equal(agentSurfaceKeywordUseBody.diagnostics[0].code, "PAR100");
assert.match(agentSurfaceKeywordUseBody.diagnostics[0].message, /reserved word cannot be used as an identifier/);
assert.equal(agentSurfaceKeywordUseBody.diagnostics[0].line, 1);
assert.equal(agentSurfaceKeywordUseBody.diagnostics[0].column, 5);

const agentSurfaceOwnedDropCheck = await execFileAsync(zero, ["check", "--json", "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.0"]);
const agentSurfaceOwnedDropCheckBody = JSON.parse(agentSurfaceOwnedDropCheck.stdout);
assert.equal(agentSurfaceOwnedDropCheckBody.ok, true);

const agentSurfaceOwnedDropReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "linux-musl-x64",
  "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.0",
]);
const agentSurfaceOwnedDropReadinessBody = JSON.parse(agentSurfaceOwnedDropReadiness.stdout);
assert.equal(agentSurfaceOwnedDropReadinessBody.ok, true);
assert.equal(agentSurfaceOwnedDropReadinessBody.diagnostics.length, 0);
assert.equal(agentSurfaceOwnedDropReadinessBody.targetReadiness.ok, false);
assert.equal(agentSurfaceOwnedDropReadinessBody.targetReadiness.buildable, false);
assert.equal(agentSurfaceOwnedDropReadinessBody.targetReadiness.languageOk, true);
assert.equal(agentSurfaceOwnedDropReadinessBody.targetReadiness.diagnostics[0].code, "BLD004");
assert.deepEqual(agentSurfaceOwnedDropReadinessBody.targetReadiness.diagnostics[0].backendBlocker, {
  target: "linux-musl-x64",
  objectFormat: "elf",
  backend: "zero-elf64",
  stage: "lower",
  unsupportedFeature: "owned<Tracked>",
});

const directCallExeReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "exe",
  "--target",
  "linux-musl-x64",
  "examples/direct-call-add.0",
]);
const directCallExeReadinessBody = JSON.parse(directCallExeReadiness.stdout);
assert.equal(directCallExeReadinessBody.ok, true);
assert.equal(directCallExeReadinessBody.diagnostics.length, 0);
assert.equal(directCallExeReadinessBody.targetReadiness.ok, false);
assert.equal(directCallExeReadinessBody.targetReadiness.buildable, false);
assert.equal(directCallExeReadinessBody.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(directCallExeReadinessBody.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
assert.match(directCallExeReadinessBody.targetReadiness.diagnostics[0].message, /main must not take parameters/);
const directCallExeBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "exe",
  "--target",
  "linux-musl-x64",
  "examples/direct-call-add.0",
  "--out",
  `${outDir}/direct-call-add-blocked`,
]).catch((error) => error);
assert.notEqual(directCallExeBuild.code, 0);
const directCallExeBuildBody = JSON.parse(directCallExeBuild.stdout);
const directCallReadinessDiag = directCallExeReadinessBody.targetReadiness.diagnostics[0];
const directCallBuildDiag = directCallExeBuildBody.diagnostics[0];
for (const key of ["code", "path", "line", "column", "length", "expected", "actual", "help"]) {
  assert.equal(directCallBuildDiag[key], directCallReadinessDiag[key]);
}
assert.equal(directCallBuildDiag.backendBlocker.stage, "buildability");
const directCallExeGraph = await execFileAsync(zero, [
  "inspect",
  "--json",
  "--emit",
  "exe",
  "--target",
  "linux-musl-x64",
  "examples/direct-call-add.0",
]);
const directCallExeGraphBody = JSON.parse(directCallExeGraph.stdout);
assert.equal(directCallExeGraphBody.targetReadiness.ok, false);
assert.equal(directCallExeGraphBody.targetReadiness.diagnostics[0].code, "BLD004");
const directCallGraphDiag = directCallExeGraphBody.targetReadiness.diagnostics[0];
assert.equal(directCallGraphDiag.path, "direct-call-add.0");
assert.equal(directCallGraphDiag.expected, "direct ELF64 object subset");
assert.equal(directCallGraphDiag.actual, "unsupported construct");
assert.equal(directCallGraphDiag.backendBlocker.backend, "zero-elf64-exe");
assert.equal(directCallGraphDiag.backendBlocker.stage, "lower");

const llvmLoopIrPath = `${outDir}/llvm-direct-while-sum.ll`;
await execFileAsync(zero, [
  "build",
  "--emit",
  "llvm-ir",
  "--backend",
  "llvm",
  "examples/direct-while-sum.0",
  "--out",
  llvmLoopIrPath,
]);
const llvmLoopIr = await readFile(llvmLoopIrPath, "utf8");
assert.match(llvmLoopIr, /^; zero llvm-ir v1\n/);
assert.match(llvmLoopIr, /target triple = "/);
assert.match(llvmLoopIr, /define i32 @main\(\) \{/);
assert.match(llvmLoopIr, /br label %L0\nL0:\n/);
assert.match(llvmLoopIr, /icmp slt i32 %v[0-9]+, 5/);
assert.match(llvmLoopIr, /add i32 %v[0-9]+, %v[0-9]+/);
assert.match(llvmLoopIr, /ret i32 %v[0-9]+/);

const llvmArrayIr = await buildLlvmIrFixture("examples/direct-array-sum.0", "llvm-direct-array-sum");
assert.match(llvmArrayIr, /alloca \[4 x i32\]/);
assert.match(llvmArrayIr, /getelementptr inbounds \[4 x i32\], ptr %slot[0-9]+, i64 0, i64 0/);
assert.match(llvmArrayIr, /getelementptr inbounds \[4 x i32\], ptr %slot[0-9]+, i64 0, i64 %v[0-9]+/);
assert.match(llvmArrayIr, /store i32 4, ptr %v[0-9]+, align 4/);
assert.match(llvmArrayIr, /load i32, ptr %v[0-9]+, align 4/);
assert.match(llvmArrayIr, /call void @llvm\.trap\(\)/);
await assertLlvmHostExitCode("examples/direct-array-sum.0", "llvm-direct-array-sum", 10);

const llvmStringLenIr = await buildLlvmIrFixture("examples/direct-string-len.0", "llvm-direct-string-len");
assert.match(llvmStringLenIr, /@\.zero\.data\.0 = private unnamed_addr constant \[6 x i8\] c"token\\00", align 1/);
assert.match(llvmStringLenIr, /insertvalue \{ ptr, i64 \} poison, ptr %v[0-9]+, 0/);
assert.match(llvmStringLenIr, /insertvalue \{ ptr, i64 \} %v[0-9]+, i64 5, 1/);
assert.match(llvmStringLenIr, /ret i64 5/);
await assertLlvmHostExitCode("examples/direct-string-len.0", "llvm-direct-string-len", 5);

const llvmByteCopyFillIr = await buildLlvmIrFixture("examples/direct-byte-copy-fill.0", "llvm-direct-byte-copy-fill");
assert.match(llvmByteCopyFillIr, /declare void @llvm\.memcpy\.p0\.p0\.i64\(ptr, ptr, i64, i1\)/);
assert.match(llvmByteCopyFillIr, /declare void @llvm\.memset\.p0\.i64\(ptr, i8, i64, i1\)/);
assert.match(llvmByteCopyFillIr, /alloca \[5 x i8\]/);
assert.match(llvmByteCopyFillIr, /call void @llvm\.memset\.p0\.i64\(ptr %v[0-9]+, i8 33, i64 (?:5|%v[0-9]+), i1 false\)/);
assert.match(llvmByteCopyFillIr, /select i1 %v[0-9]+, i64 %v[0-9]+, i64 %v[0-9]+/);
assert.match(llvmByteCopyFillIr, /call void @llvm\.memcpy\.p0\.p0\.i64\(ptr %v[0-9]+, ptr %v[0-9]+, i64 %v[0-9]+, i1 false\)/);
await assertLlvmHostExitCode("examples/direct-byte-copy-fill.0", "llvm-direct-byte-copy-fill", 111);

const llvmStringEqlIr = await buildLlvmIrFixture("examples/direct-string-eql.0", "llvm-direct-string-eql");
assertLlvmPhiPredecessors(llvmStringEqlIr);
assert.match(llvmStringEqlIr, /phi i64 \[0, %L[0-9]+\], \[%v[0-9]+, %L[0-9]+\]/);
assert.match(llvmStringEqlIr, /icmp eq i8 %v[0-9]+, %v[0-9]+/);
assert.match(llvmStringEqlIr, /phi i1 \[1, %L[0-9]+\], \[0, %L[0-9]+\]/);
await assertLlvmHostExitCode("examples/direct-string-eql.0", "llvm-direct-string-eql", 1);

const llvmByteViewLocalsIr = await buildLlvmIrFixture("examples/direct-byte-view-locals.0", "llvm-direct-byte-view-locals");
assertLlvmPhiPredecessors(llvmByteViewLocalsIr);
assert.match(llvmByteViewLocalsIr, /alloca \{ ptr, i64 \}/);
assert.match(llvmByteViewLocalsIr, /store \{ ptr, i64 \} %v[0-9]+, ptr %slot[0-9]+, align 8/);
assert.match(llvmByteViewLocalsIr, /load \{ ptr, i64 \}, ptr %slot[0-9]+, align 8/);
assert.match(llvmByteViewLocalsIr, /extractvalue \{ ptr, i64 \} %v[0-9]+, 0/);
assert.match(llvmByteViewLocalsIr, /extractvalue \{ ptr, i64 \} %v[0-9]+, 1/);
await assertLlvmHostExitCode("examples/direct-byte-view-locals.0", "llvm-direct-byte-view-locals", 107);

const llvmSpanReadIr = await buildLlvmIrFixture("examples/direct-span-read.0", "llvm-direct-span-read");
assert.match(llvmSpanReadIr, /icmp ule i64 %v[0-9]+, %v[0-9]+/);
assert.match(llvmSpanReadIr, /icmp ult i64 %v[0-9]+, %v[0-9]+/);
assert.match(llvmSpanReadIr, /getelementptr inbounds i8, ptr %v[0-9]+, i64 %v[0-9]+/);
assert.match(llvmSpanReadIr, /load i8, ptr %v[0-9]+, align 1/);
await assertLlvmHostExitCode("examples/direct-span-read.0", "llvm-direct-span-read", 107);

const llvmShortCircuitSourcePath = `${outDir}/llvm-short-circuit.0`;
const llvmShortCircuitIrPath = `${outDir}/llvm-short-circuit.ll`;
await writeGraphFixture(llvmShortCircuitSourcePath, `fn rhsAnd() -> Bool {
    return true
}

fn rhsOr() -> Bool {
    return false
}

export c fn main() -> i32 {
    if false && rhsAnd() {
        return 1
    }
    if true || rhsOr() {
        return 2
    }
    if true && (false && rhsAnd()) {
        return 3
    }
    if false || (true || rhsOr()) {
        return 4
    }
    return 0
}
`);
await execFileAsync(zero, [
  "build",
  "--emit",
  "llvm-ir",
  "--backend",
  "llvm",
  llvmShortCircuitSourcePath,
  "--out",
  llvmShortCircuitIrPath,
]);
const llvmShortCircuitIr = await readFile(llvmShortCircuitIrPath, "utf8");
assertLlvmPhiPredecessors(llvmShortCircuitIr);
assert.match(llvmShortCircuitIr, /br i1 0, label %L[0-9]+, label %L[0-9]+\nL[0-9]+:\n  %v[0-9]+ = call i1 @\.zero\.fn\.[0-9]+\.rhsAnd\(\)/);
assert.match(llvmShortCircuitIr, /%v[0-9]+ = phi i1 \[%v[0-9]+, %L[0-9]+\], \[0, %L[0-9]+\]/);
assert.match(llvmShortCircuitIr, /br i1 1, label %L[0-9]+, label %L[0-9]+\nL[0-9]+:\n  %v[0-9]+ = call i1 @\.zero\.fn\.[0-9]+\.rhsOr\(\)/);
assert.match(llvmShortCircuitIr, /%v[0-9]+ = phi i1 \[%v[0-9]+, %L[0-9]+\], \[1, %L[0-9]+\]/);

const llvmAddIrPath = `${outDir}/llvm-add.ll`;
const llvmAddBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "llvm-ir",
  "--backend",
  "llvm",
  "examples/add.0",
  "--out",
  llvmAddIrPath,
]);
const llvmAddBuildBody = JSON.parse(llvmAddBuild.stdout);
assert.equal(llvmAddBuildBody.objectBackend.linking.targetLibraries, "zero-runtime");
assert.equal(llvmAddBuildBody.objectBackend.linking.toolchainSource, "textual-llvm-ir-runtime-link-plan");
assert.deepEqual(llvmAddBuildBody.objectBackend.linkerPlan.staticLibraries, ["zero_runtime.o"]);
const llvmAddIr = await readFile(llvmAddIrPath, "utf8");
assert.match(llvmAddIr, /@\.zero\.data\.0 = private unnamed_addr constant \[12 x i8\] c"math works\\0A\\00", align 1/);
assert.match(llvmAddIr, /declare i32 @zero_world_write\(i32, ptr, i32\)/);
assert.match(llvmAddIr, /declare void @llvm\.trap\(\)/);
assert.match(llvmAddIr, /define i32 @\.zero\.fn\.[0-9]+\.answer\(\) \{/);
assert.match(llvmAddIr, /call i32 @zero_world_write\(i32 1, ptr %v[0-9]+, i32 (?:11|%v[0-9]+)\)/);
assert.match(llvmAddIr, /%v[0-9]+ = call i32 @zero_world_write\(i32 1, ptr %v[0-9]+, i32 (?:11|%v[0-9]+)\)\n  %v[0-9]+ = icmp eq i32 %v[0-9]+, 0\n  br i1 %v[0-9]+, label %L[0-9]+, label %L[0-9]+\nL[0-9]+:\n  call void @llvm\.trap\(\)\n  unreachable\nL[0-9]+:/);

const llvmSymbolCollisionSourcePath = `${outDir}/llvm-symbol-collision.0`;
const llvmSymbolCollisionIrPath = `${outDir}/llvm-symbol-collision.ll`;
await writeGraphFixture(llvmSymbolCollisionSourcePath, `fn foo() -> i32 {
    return 1
}

export c fn z_fn_0_foo() -> i32 {
    return 2
}

export c fn main() -> i32 {
    return foo() + z_fn_0_foo()
}
`);
await execFileAsync(zero, [
  "build",
  "--emit",
  "llvm-ir",
  "--backend",
  "llvm",
  llvmSymbolCollisionSourcePath,
  "--out",
  llvmSymbolCollisionIrPath,
]);
const llvmSymbolCollisionIr = await readFile(llvmSymbolCollisionIrPath, "utf8");
assert.match(llvmSymbolCollisionIr, /define i32 @\.zero\.fn\.[0-9]+\.foo\(\) \{/);
assert.match(llvmSymbolCollisionIr, /define i32 @z_fn_0_foo\(\) \{/);
assert.match(llvmSymbolCollisionIr, /call i32 @\.zero\.fn\.[0-9]+\.foo\(\)/);
assert.equal((llvmSymbolCollisionIr.match(/define i32 @z_fn_0_foo\(\) \{/g) || []).length, 1);

const llvmRuntimeCollisionSourcePath = `${outDir}/llvm-runtime-symbol-collision.0`;
await writeGraphFixture(llvmRuntimeCollisionSourcePath, `export c fn zero_world_write() -> i32 {
    return 7
}

pub fn main(world: World) -> Void raises {
    check world.out.write("hello\\n")
}
`);
const llvmRuntimeCollisionReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "llvm-ir",
  "--backend",
  "llvm",
  llvmRuntimeCollisionSourcePath,
]);
const llvmRuntimeCollisionReadinessBody = JSON.parse(llvmRuntimeCollisionReadiness.stdout);
assert.equal(llvmRuntimeCollisionReadinessBody.ok, true);
assert.equal(llvmRuntimeCollisionReadinessBody.targetReadiness.ok, false);
assert.equal(llvmRuntimeCollisionReadinessBody.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(llvmRuntimeCollisionReadinessBody.targetReadiness.diagnostics[0].actual, "zero_world_write");
assert.equal(llvmRuntimeCollisionReadinessBody.targetReadiness.diagnostics[0].backendBlocker.backend, "llvm");
assert.equal(llvmRuntimeCollisionReadinessBody.targetReadiness.diagnostics[0].backendBlocker.stage, "lower");
const llvmRuntimeCollisionBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "llvm-ir",
  "--backend",
  "llvm",
  llvmRuntimeCollisionSourcePath,
  "--out",
  `${outDir}/llvm-runtime-symbol-collision.ll`,
]).catch((error) => error);
assert.notEqual(llvmRuntimeCollisionBuild.code, 0);
const llvmRuntimeCollisionBuildBody = JSON.parse(llvmRuntimeCollisionBuild.stdout);
assert.equal(llvmRuntimeCollisionBuildBody.diagnostics[0].actual, "zero_world_write");
assert.equal(llvmRuntimeCollisionBuildBody.diagnostics[0].backendBlocker.backend, "llvm");

const llvmLongExportName = `llvm_export_${"a".repeat(200)}`;
const llvmLongExportSourcePath = `${outDir}/llvm-long-export.0`;
const llvmLongExportIrPath = `${outDir}/llvm-long-export.ll`;
await writeGraphFixture(llvmLongExportSourcePath, `export c fn ${llvmLongExportName}() -> i32 {
    return 7
}

export c fn main() -> i32 {
    return ${llvmLongExportName}()
}
`);
await execFileAsync(zero, [
  "build",
  "--emit",
  "llvm-ir",
  "--backend",
  "llvm",
  llvmLongExportSourcePath,
  "--out",
  llvmLongExportIrPath,
]);
const llvmLongExportIr = await readFile(llvmLongExportIrPath, "utf8");
assert(llvmLongExportIr.includes(`define i32 @${llvmLongExportName}() {`));
assert(llvmLongExportIr.includes(`call i32 @${llvmLongExportName}()`));

const directStringMachOExe = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "exe",
  "--target",
  "darwin-arm64",
  "examples/direct-string-literal.0",
  "--out",
  `${outDir}/direct-string-literal-darwin`,
]);
const directStringMachOExeBody = JSON.parse(directStringMachOExe.stdout);
assert.equal(directStringMachOExeBody.compiler, "zero-macho64");
assert.equal(directStringMachOExeBody.generatedCBytes, 0);
const directStringCoffExe = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "exe",
  "--target",
  "win32-x64.exe",
  "examples/direct-string-literal.0",
  "--out",
  `${outDir}/direct-string-literal-win`,
]);
const directStringCoffExeBody = JSON.parse(directStringCoffExe.stdout);
assert.equal(directStringCoffExeBody.compiler, "zero-coff-x64");
assert.equal(directStringCoffExeBody.generatedCBytes, 0);

const coffDynamicSliceFixture = "conformance/native/pass/coff-dynamic-byte-slice.0";
const coffDynamicSliceReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "win32-x64.exe",
  coffDynamicSliceFixture,
]);
const coffDynamicSliceReadinessBody = JSON.parse(coffDynamicSliceReadiness.stdout);
assert.equal(coffDynamicSliceReadinessBody.ok, true);
assert.equal(coffDynamicSliceReadinessBody.diagnostics.length, 0);
assert.equal(coffDynamicSliceReadinessBody.targetReadiness.ok, true);
assert.equal(coffDynamicSliceReadinessBody.targetReadiness.buildable, true);
assert.equal(coffDynamicSliceReadinessBody.targetReadiness.backend, "zero-coff-x64");
const coffDynamicSliceBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "win32-x64.exe",
  coffDynamicSliceFixture,
  "--out",
  `${outDir}/coff-dynamic-byte-slice.obj`,
]);
const coffDynamicSliceBuildBody = JSON.parse(coffDynamicSliceBuild.stdout);
assert.equal(coffDynamicSliceBuildBody.compiler, "zero-coff-x64");
assert.equal(coffDynamicSliceBuildBody.generatedCBytes, 0);
assert.equal(coffDynamicSliceBuildBody.objectBackend.objectEmission.path, "direct-coff-x64-object");
await assertCoffX64Object(`${outDir}/coff-dynamic-byte-slice.obj`, "main");

const coffU64CopyFixture = "conformance/native/pass/std-mem-u64-copy-items.0";
const coffU64CopyReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "win32-x64.exe",
  coffU64CopyFixture,
]);
const coffU64CopyReadinessBody = JSON.parse(coffU64CopyReadiness.stdout);
assert.equal(coffU64CopyReadinessBody.ok, true);
assert.equal(coffU64CopyReadinessBody.targetReadiness.ok, true);
assert.equal(coffU64CopyReadinessBody.targetReadiness.buildable, true);
assert.equal(coffU64CopyReadinessBody.targetReadiness.backend, "zero-coff-x64");
const coffU64CopyPath = `${outDir}/coff-u64-copy-items.obj`;
const coffU64CopyBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "win32-x64.exe",
  coffU64CopyFixture,
  "--out",
  coffU64CopyPath,
]);
const coffU64CopyBuildBody = JSON.parse(coffU64CopyBuild.stdout);
assert.equal(coffU64CopyBuildBody.compiler, "zero-coff-x64");
assert.equal(coffU64CopyBuildBody.generatedCBytes, 0);
assert.equal(coffU64CopyBuildBody.objectBackend.objectEmission.path, "direct-coff-x64-object");
const coffU64CopyBytes = await assertCoffX64Object(coffU64CopyPath, "main");
assert(hasX64MovMemoryToR64(coffU64CopyBytes));
assert(hasX64MovR64ToMemory(coffU64CopyBytes));

const coffU64ContainsFixture = "conformance/native/pass/std-mem-u64-contains.0";
const coffU64ContainsReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "win32-x64.exe",
  coffU64ContainsFixture,
]);
const coffU64ContainsReadinessBody = JSON.parse(coffU64ContainsReadiness.stdout);
assert.equal(coffU64ContainsReadinessBody.ok, true);
assert.equal(coffU64ContainsReadinessBody.targetReadiness.ok, true);
assert.equal(coffU64ContainsReadinessBody.targetReadiness.buildable, true);
assert.equal(coffU64ContainsReadinessBody.targetReadiness.backend, "zero-coff-x64");
const coffU64ContainsPath = `${outDir}/coff-u64-contains.obj`;
const coffU64ContainsBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "win32-x64.exe",
  coffU64ContainsFixture,
  "--out",
  coffU64ContainsPath,
]);
const coffU64ContainsBuildBody = JSON.parse(coffU64ContainsBuild.stdout);
assert.equal(coffU64ContainsBuildBody.compiler, "zero-coff-x64");
assert.equal(coffU64ContainsBuildBody.generatedCBytes, 0);
assert.equal(coffU64ContainsBuildBody.objectBackend.objectEmission.path, "direct-coff-x64-object");
const coffU64ContainsBytes = await assertCoffX64Object(coffU64ContainsPath, "main");
assert(coffU64ContainsBytes.includes(Buffer.from([0x48, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00])));
assert(hasX64MovMemoryToR64(coffU64ContainsBytes));
assert(hasX64CmpR64(coffU64ContainsBytes));

const coffBoolCopyFixture = "conformance/native/pass/std-mem-bool-copy-items.0";
const coffBoolCopyReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "win32-x64.exe",
  coffBoolCopyFixture,
]);
const coffBoolCopyReadinessBody = JSON.parse(coffBoolCopyReadiness.stdout);
assert.equal(coffBoolCopyReadinessBody.ok, true);
assert.equal(coffBoolCopyReadinessBody.targetReadiness.ok, true);
assert.equal(coffBoolCopyReadinessBody.targetReadiness.buildable, true);
assert.equal(coffBoolCopyReadinessBody.targetReadiness.backend, "zero-coff-x64");
const coffBoolCopyPath = `${outDir}/coff-bool-copy-items.obj`;
const coffBoolCopyBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "win32-x64.exe",
  coffBoolCopyFixture,
  "--out",
  coffBoolCopyPath,
]);
const coffBoolCopyBuildBody = JSON.parse(coffBoolCopyBuild.stdout);
assert.equal(coffBoolCopyBuildBody.compiler, "zero-coff-x64");
assert.equal(coffBoolCopyBuildBody.generatedCBytes, 0);
assert.equal(coffBoolCopyBuildBody.objectBackend.objectEmission.path, "direct-coff-x64-object");
await assertCoffX64Object(coffBoolCopyPath, "main");

async function assertMachOObjectBuildabilityBlocked(fixture, outName, expectedMessage) {
  const readiness = await execFileAsync(zero, [
    "check",
    "--json",
    "--emit",
    "obj",
    "--target",
    "darwin-arm64",
    fixture,
  ]);
  const readinessBody = JSON.parse(readiness.stdout);
  assert.equal(readinessBody.ok, true);
  assert.equal(readinessBody.diagnostics.length, 0);
  assert.equal(readinessBody.targetReadiness.ok, false);
  assert.equal(readinessBody.targetReadiness.buildable, false);
  assert.equal(readinessBody.targetReadiness.languageOk, true);
  assert.equal(readinessBody.targetReadiness.emit, "obj");
  assert.equal(readinessBody.targetReadiness.target, "darwin-arm64");
  assert.equal(readinessBody.targetReadiness.diagnostics[0].code, "BLD004");
  assert.equal(readinessBody.targetReadiness.diagnostics[0].backendBlocker.backend, "zero-macho64");
  assert.equal(readinessBody.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
  assert.match(readinessBody.targetReadiness.diagnostics[0].message, expectedMessage);
  const build = await execFileAsync(zero, [
    "build",
    "--json",
    "--emit",
    "obj",
    "--target",
    "darwin-arm64",
    fixture,
    "--out",
    `${outDir}/${outName}`,
  ]).catch((error) => error);
  assert.notEqual(build.code, 0);
  const buildBody = JSON.parse(build.stdout);
  const readinessDiag = readinessBody.targetReadiness.diagnostics[0];
  const buildDiag = buildBody.diagnostics[0];
  for (const key of ["code", "path", "line", "column", "length", "expected", "actual", "help"]) {
    assert.equal(buildDiag[key], readinessDiag[key]);
  }
  assert.equal(buildDiag.backendBlocker.backend, "zero-macho64");
  assert.equal(buildDiag.backendBlocker.stage, "buildability");
}

await assertMachOObjectBuildabilityBlocked(
  "conformance/native/pass/macho-large-byte-slice-blocked.0",
  "macho-large-byte-slice.o",
  /constant start/,
);
await assertMachOObjectBuildabilityBlocked(
  "conformance/native/pass/macho-nested-call-scratch-blocked.0",
  "macho-nested-call-scratch.o",
  /scratch spill capacity/,
);

const machoOpenByteSliceFixture = "conformance/native/pass/macho-open-byte-slice.0";
const machoOpenByteSliceReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "darwin-arm64",
  machoOpenByteSliceFixture,
]);
const machoOpenByteSliceReadinessBody = JSON.parse(machoOpenByteSliceReadiness.stdout);
assert.equal(machoOpenByteSliceReadinessBody.ok, true);
assert.equal(machoOpenByteSliceReadinessBody.diagnostics.length, 0);
assert.equal(machoOpenByteSliceReadinessBody.targetReadiness.ok, true);
assert.equal(machoOpenByteSliceReadinessBody.targetReadiness.buildable, true);
assert.equal(machoOpenByteSliceReadinessBody.targetReadiness.backend, "zero-macho64");
const machoOpenByteSliceBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "darwin-arm64",
  machoOpenByteSliceFixture,
  "--out",
  `${outDir}/macho-open-byte-slice.o`,
]);
const machoOpenByteSliceBuildBody = JSON.parse(machoOpenByteSliceBuild.stdout);
assert.equal(machoOpenByteSliceBuildBody.compiler, "zero-macho64");
assert.equal(machoOpenByteSliceBuildBody.generatedCBytes, 0);
assert.equal(machoOpenByteSliceBuildBody.objectBackend.objectEmission.path, "direct-macho64-object");
await assertMachOArm64Object(`${outDir}/macho-open-byte-slice.o`, "main");

const aarch64OpenSliceBoundsFixture = `${outDir}/aarch64-open-byte-slice-bounds.0`;
await writeGraphFixture(aarch64OpenSliceBoundsFixture, `export c fn main() -> u32 {
    let words: [2]u16 = [1_u16, 2_u16]
    let suffix: Span<u16> = words[3_usize..]
    return (std.mem.len(suffix)) as u32
}
`);
await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "linux-arm64",
  aarch64OpenSliceBoundsFixture,
  "--out",
  `${outDir}/aarch64-open-byte-slice-bounds.o`,
]);
const aarch64OpenSliceBoundsBytes = await assertElfAarch64Object(`${outDir}/aarch64-open-byte-slice-bounds.o`, "main");
assert(hasAarch64CondBranch(aarch64OpenSliceBoundsBytes, 9));
assert(hasAarch64Instruction(aarch64OpenSliceBoundsBytes, 0xd4200000));
await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "darwin-arm64",
  aarch64OpenSliceBoundsFixture,
  "--out",
  `${outDir}/macho-open-byte-slice-bounds.o`,
]);
const machoOpenSliceBoundsBytes = await assertMachOArm64Object(`${outDir}/macho-open-byte-slice-bounds.o`, "main");
assert(hasAarch64CondBranch(machoOpenSliceBoundsBytes, 9));
assert(hasAarch64Instruction(machoOpenSliceBoundsBytes, 0xd4200000));

const x64OpenSliceBoundsFixture = `${outDir}/x64-open-u16-slice-bounds.0`;
await writeGraphFixture(x64OpenSliceBoundsFixture, `export c fn main() -> u32 {
    let words: [2]u16 = [1_u16, 2_u16]
    let suffix: Span<u16> = words[3_usize..]
    return (std.mem.len(suffix)) as u32
}
`);
const x64SliceLenBoundsFixture = `${outDir}/x64-len-u16-slice-bounds.0`;
await writeGraphFixture(x64SliceLenBoundsFixture, `export c fn main() -> u32 {
    let words: [2]u16 = [1_u16, 2_u16]
    return (std.mem.len(words[..3_usize])) as u32
}
`);

await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "linux-arm64",
  x64SliceLenBoundsFixture,
  "--out",
  `${outDir}/aarch64-len-u16-slice-bounds.o`,
]);
const aarch64LenSliceBoundsBytes = await assertElfAarch64Object(`${outDir}/aarch64-len-u16-slice-bounds.o`, "main");
assert(hasAarch64CondBranch(aarch64LenSliceBoundsBytes, 9));
assert(hasAarch64Instruction(aarch64LenSliceBoundsBytes, 0xd4200000));
await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "darwin-arm64",
  x64SliceLenBoundsFixture,
  "--out",
  `${outDir}/macho-len-u16-slice-bounds.o`,
]);
const machoLenSliceBoundsBytes = await assertMachOArm64Object(`${outDir}/macho-len-u16-slice-bounds.o`, "main");
assert(hasAarch64CondBranch(machoLenSliceBoundsBytes, 9));
assert(hasAarch64Instruction(machoLenSliceBoundsBytes, 0xd4200000));

async function assertX64SliceBoundsObject(fixture, name) {
  const elfPath = `${outDir}/${name}-linux.o`;
  await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", fixture, "--out", elfPath]);
  assertX64SliceBoundsTrapBytes((await assertElf64Object(elfPath, "main")).bytes);

  const machoPath = `${outDir}/${name}-macho-x64.o`;
  await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "darwin-x64", fixture, "--out", machoPath]);
  assertX64SliceBoundsTrapBytes(await assertMachOX64Object(machoPath, "main"));

  const coffPath = `${outDir}/${name}-coff.obj`;
  await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", fixture, "--out", coffPath]);
  assertX64SliceBoundsTrapBytes(await assertCoffX64Object(coffPath, "main"));
}

await assertX64SliceBoundsObject(x64OpenSliceBoundsFixture, "x64-open-u16-slice-bounds");
await assertX64SliceBoundsObject(x64SliceLenBoundsFixture, "x64-len-u16-slice-bounds");

const x64U16RecordFieldFixture = `${outDir}/x64-u16-record-field.0`;
await writeGraphFixture(x64U16RecordFieldFixture, `type Holder {
    values: [3]u16,
}

export c fn main() -> u32 {
    var holder: Holder = Holder { values: [10_u16, 20_u16, 30_u16] }
    if holder.values[0] == 10_u16 && holder.values[1] == 20_u16 && holder.values[2] == 30_u16 {
        return 1
    }
    return 0
}
`);
const x64U16RecordFieldElfPath = `${outDir}/x64-u16-record-field-linux.o`;
await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", x64U16RecordFieldFixture, "--out", x64U16RecordFieldElfPath]);
assertX64U16RecordFieldBytes((await assertElf64Object(x64U16RecordFieldElfPath, "main")).bytes);
const x64U16RecordFieldMachOPath = `${outDir}/x64-u16-record-field-macho-x64.o`;
await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "darwin-x64", x64U16RecordFieldFixture, "--out", x64U16RecordFieldMachOPath]);
assertX64U16RecordFieldBytes(await assertMachOX64Object(x64U16RecordFieldMachOPath, "main"));

async function assertAArch64ObjectBuildabilityBlocked(target, backend, outName, expectedMessage) {
  const fixture = "conformance/native/pass/macho-nested-call-scratch-blocked.0";
  const readiness = await execFileAsync(zero, [
    "check",
    "--json",
    "--emit",
    "obj",
    "--target",
    target,
    fixture,
  ]);
  const readinessBody = JSON.parse(readiness.stdout);
  assert.equal(readinessBody.ok, true);
  assert.equal(readinessBody.diagnostics.length, 0);
  assert.equal(readinessBody.targetReadiness.ok, false);
  assert.equal(readinessBody.targetReadiness.buildable, false);
  assert.equal(readinessBody.targetReadiness.languageOk, true);
  assert.equal(readinessBody.targetReadiness.emit, "obj");
  assert.equal(readinessBody.targetReadiness.target, target);
  assert.equal(readinessBody.targetReadiness.diagnostics[0].code, "BLD004");
  assert.equal(readinessBody.targetReadiness.diagnostics[0].backendBlocker.backend, backend);
  assert.equal(readinessBody.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
  assert.match(readinessBody.targetReadiness.diagnostics[0].message, expectedMessage);
  const build = await execFileAsync(zero, [
    "build",
    "--json",
    "--emit",
    "obj",
    "--target",
    target,
    fixture,
    "--out",
    `${outDir}/${outName}`,
  ]).catch((error) => error);
  assert.notEqual(build.code, 0);
  const buildBody = JSON.parse(build.stdout);
  const readinessDiag = readinessBody.targetReadiness.diagnostics[0];
  const buildDiag = buildBody.diagnostics[0];
  for (const key of ["code", "path", "line", "column", "length", "expected", "actual", "help"]) {
    assert.equal(buildDiag[key], readinessDiag[key]);
  }
  assert.equal(buildDiag.backendBlocker.backend, backend);
  assert.equal(buildDiag.backendBlocker.stage, "buildability");
}

await assertAArch64ObjectBuildabilityBlocked("linux-arm64", "zero-elf-aarch64", "elf-aarch64-nested-call-scratch.o", /scratch spill capacity/);
await assertAArch64ObjectBuildabilityBlocked("win32-arm64.exe", "zero-coff-aarch64", "coff-aarch64-nested-call-scratch.obj", /scratch spill capacity/);
const machoBoolArraysBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "darwin-arm64",
  "conformance/native/pass/bool-arrays.0",
  "--out",
  `${outDir}/macho-bool-arrays.o`,
]);
const machoBoolArraysBody = JSON.parse(machoBoolArraysBuild.stdout);
assert.equal(machoBoolArraysBody.compiler, "zero-macho64");
assert.equal(machoBoolArraysBody.generatedCBytes, 0);
assert.equal(machoBoolArraysBody.objectBackend.objectEmission.path, "direct-macho64-object");

const directCallArm64ObjReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "linux-arm64",
  "examples/direct-call-add.0",
]);
const directCallArm64ObjReadinessBody = JSON.parse(directCallArm64ObjReadiness.stdout);
assert.equal(directCallArm64ObjReadinessBody.ok, true);
assert.equal(directCallArm64ObjReadinessBody.diagnostics.length, 0);
assert.equal(directCallArm64ObjReadinessBody.targetReadiness.ok, true);
assert.equal(directCallArm64ObjReadinessBody.targetReadiness.buildable, true);
assert.equal(directCallArm64ObjReadinessBody.targetReadiness.backend, "zero-elf-aarch64");
const directCallArm64ObjBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "linux-arm64",
  "examples/direct-call-add.0",
  "--out",
  `${outDir}/direct-call-add-arm64.o`,
]);
const directCallArm64ObjBuildBody = JSON.parse(directCallArm64ObjBuild.stdout);
assert.equal(directCallArm64ObjBuildBody.compiler, "zero-elf-aarch64");
assert.equal(directCallArm64ObjBuildBody.generatedCBytes, 0);
assert.equal(directCallArm64ObjBuildBody.objectBackend.objectEmission.path, "direct-elf-aarch64-object");
await assertElfAarch64Object(`${outDir}/direct-call-add-arm64.o`, "main");

let arm64NestedIndexExpr = "values[idx]";
for (let i = 0; i < 32; i++) arm64NestedIndexExpr = `(0_u32 + ${arm64NestedIndexExpr})`;
const arm64NestedIndexFixture = `${outDir}/aarch64-nested-index-scratch-blocked.0`;
await writeGraphFixture(arm64NestedIndexFixture, `export c fn main() -> u32 {
    let values: [1]u32 = [7]
    let idx: u32 = 0_u32
    return ${arm64NestedIndexExpr}
}
`);
async function assertArm64NestedScratchBlocked(fixture, expectedMessage, outPrefix) {
  for (const blocked of [["linux-arm64", "zero-elf-aarch64", "linux.o"], ["darwin-arm64", "zero-macho64", "macho.o"], ["win32-arm64.exe", "zero-coff-aarch64", "coff.obj"]]) {
    const readiness = await execFileAsync(zero, ["check", "--json", "--emit", "obj", "--target", blocked[0], fixture]);
    const readinessBody = JSON.parse(readiness.stdout);
    assert.equal(readinessBody.ok, true);
    assert.equal(readinessBody.targetReadiness.ok, false);
    assert.equal(readinessBody.targetReadiness.diagnostics[0].code, "BLD004");
    assert.equal(readinessBody.targetReadiness.diagnostics[0].backendBlocker.backend, blocked[1]);
    assert.equal(readinessBody.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
    assert.match(readinessBody.targetReadiness.diagnostics[0].message, expectedMessage);
    const build = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", blocked[0], fixture, "--out", `${outDir}/${outPrefix}-${blocked[2]}`]).catch((error) => error);
    assert.notEqual(build.code, 0);
    assert.equal(JSON.parse(build.stdout).diagnostics[0].backendBlocker.stage, "buildability");
  }
}
await assertArm64NestedScratchBlocked(arm64NestedIndexFixture, /indexed load exceeds scratch register spill capacity/, "aarch64-nested-index");
let arm64NestedLenExpr = "((std.mem.len(text[start..end])) as u32)";
for (let i = 0; i < 32; i++) arm64NestedLenExpr = `(0_u32 + ${arm64NestedLenExpr})`;
const arm64NestedLenFixture = `${outDir}/aarch64-nested-len-scratch-blocked.0`;
await writeGraphFixture(arm64NestedLenFixture, `export c fn main() -> u32 {
    let text: String = "abcdef"
    let start: usize = 1
    let end: usize = 4
    return ${arm64NestedLenExpr}
}
`);
await assertArm64NestedScratchBlocked(arm64NestedLenFixture, /byte-view length exceeds scratch register spill capacity/, "aarch64-nested-len");
let arm64NestedDynamicStartLenExpr = "((std.mem.len(text[(start + 0_usize)..end])) as u32)";
for (let i = 0; i < 31; i++) arm64NestedDynamicStartLenExpr = `(0_u32 + ${arm64NestedDynamicStartLenExpr})`;
const arm64NestedDynamicStartLenFixture = `${outDir}/aarch64-nested-dynamic-start-len-scratch-blocked.0`;
await writeGraphFixture(arm64NestedDynamicStartLenFixture, `export c fn main() -> u32 {
    let text: String = "abcdef"
    let start: usize = 1
    let end: usize = 4
    return ${arm64NestedDynamicStartLenExpr}
}
`);
await assertArm64NestedScratchBlocked(arm64NestedDynamicStartLenFixture, /byte-view length exceeds scratch register spill capacity/, "aarch64-nested-dynamic-start-len");
let arm64NestedEndLenExpr = "((std.mem.len(text[1..(end + 0_usize)])) as u32)";
for (let i = 0; i < 32; i++) arm64NestedEndLenExpr = `(0_u32 + ${arm64NestedEndLenExpr})`;
const arm64NestedEndLenFixture = `${outDir}/aarch64-nested-end-len-scratch-blocked.0`;
await writeGraphFixture(arm64NestedEndLenFixture, `export c fn main() -> u32 {
    let text: String = "abcdef"
    let end: usize = 4
    return ${arm64NestedEndLenExpr}
}
`);
await assertArm64NestedScratchBlocked(arm64NestedEndLenFixture, /byte-view length exceeds scratch register spill capacity/, "aarch64-nested-end-len");
let arm64NestedDynamicEqlExpr = "std.mem.eqlBytes(text[(start + (0_usize + 0_usize))..end], text[(start + (0_usize + 0_usize))..end])";
for (let i = 0; i < 29; i++) arm64NestedDynamicEqlExpr = `(true == ${arm64NestedDynamicEqlExpr})`;
const arm64NestedDynamicEqlFixture = `${outDir}/aarch64-nested-dynamic-eql-scratch-blocked.0`;
await writeGraphFixture(arm64NestedDynamicEqlFixture, `export c fn main() -> Bool {
    let text: String = "abcdef"
    let start: usize = 1
    let end: usize = 4
    return ${arm64NestedDynamicEqlExpr}
}
`);
await assertArm64NestedScratchBlocked(arm64NestedDynamicEqlFixture, /byte-view equality exceeds scratch register spill capacity/, "aarch64-nested-dynamic-eql");

let arm64WorldWriteSliceStart = "(start + 0_usize)";
for (let i = 0; i < 31; i++) arm64WorldWriteSliceStart = `(0_usize + ${arm64WorldWriteSliceStart})`;
const arm64WorldWriteFixture = `${outDir}/aarch64-world-write-dynamic-slice-scratch-blocked.0`;
await writeGraphFixture(arm64WorldWriteFixture, `pub fn main(world: World) -> Void raises {
    let text: String = "abcdef"
    let start: usize = 1
    check world.out.write(text[${arm64WorldWriteSliceStart}..6])
}
`);
async function assertArm64WorldWriteScratchBlocked(fixture, expectedMessage, outPrefix) {
  for (const blocked of [["linux-musl-arm64", "zero-elf-aarch64-exe", "linux"], ["win32-arm64.exe", "zero-coff-aarch64-exe", "coff.exe"]]) {
    const readiness = await execFileAsync(zero, ["check", "--json", "--emit", "exe", "--target", blocked[0], fixture]);
    const readinessBody = JSON.parse(readiness.stdout);
    assert.equal(readinessBody.ok, true);
    assert.equal(readinessBody.targetReadiness.ok, false);
    assert.equal(readinessBody.targetReadiness.diagnostics[0].code, "BLD004");
    assert.equal(readinessBody.targetReadiness.diagnostics[0].backendBlocker.backend, blocked[1]);
    assert.equal(readinessBody.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
    assert.match(readinessBody.targetReadiness.diagnostics[0].message, expectedMessage);
    const build = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", blocked[0], fixture, "--out", `${outDir}/${outPrefix}-${blocked[2]}`]).catch((error) => error);
    assert.notEqual(build.code, 0);
    const buildDiag = JSON.parse(build.stdout).diagnostics[0];
    assert.equal(buildDiag.backendBlocker.backend, blocked[1]);
    assert.equal(buildDiag.backendBlocker.stage, "buildability");
    assert.match(buildDiag.message, expectedMessage);
  }
}
await assertArm64WorldWriteScratchBlocked(arm64WorldWriteFixture, /expression nesting exceeds scratch register spill capacity/, "aarch64-world-write-dynamic-slice");

let arm64WorldWriteSliceEnd = "(end + 0_usize)";
for (let i = 0; i < 32; i++) arm64WorldWriteSliceEnd = `(0_usize + ${arm64WorldWriteSliceEnd})`;
const arm64WorldWriteEndFixture = `${outDir}/aarch64-world-write-dynamic-end-scratch-blocked.0`;
await writeGraphFixture(arm64WorldWriteEndFixture, `pub fn main(world: World) -> Void raises {
    let text: String = "abcdef"
    let end: usize = 6
    check world.out.write(text[1..${arm64WorldWriteSliceEnd}])
}
`);
await assertArm64WorldWriteScratchBlocked(arm64WorldWriteEndFixture, /expression nesting exceeds scratch register spill capacity/, "aarch64-world-write-dynamic-end");

const arm64PrivateHelperObj = `${outDir}/aarch64-private-helper-ignored.o`;
await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "linux-arm64",
  "conformance/native/pass/aarch64-private-helper-ignored.0",
  "--out",
  arm64PrivateHelperObj,
]);
const arm64PrivateHelperBytes = await assertElfAarch64Object(arm64PrivateHelperObj, "main");
assert(arm64PrivateHelperBytes.includes(Buffer.from([0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6])));

const arm64CompareSource = `${outDir}/aarch64-typed-compare.0`;
const arm64CompareObj = `${outDir}/aarch64-typed-compare.o`;
await writeGraphFixture(arm64CompareSource, `export c fn main() -> u32 {
    let large: u32 = 4294967295
    if large > 0_u32 {
        return 7
    }
    return 3
}

export c fn wide_guard() -> u32 {
    let wide: u64 = 4294967296
    if wide != 0_u64 {
        return 9
    }
    return 4
}
`);
await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "linux-arm64",
  arm64CompareSource,
  "--out",
  arm64CompareObj,
]);
const arm64CompareBytes = await readFile(arm64CompareObj);
assert(hasAarch64CondBranch(arm64CompareBytes, 9));
assert(hasAarch64Instruction(arm64CompareBytes, 0xeb09011f));

const memoryPackageMachOReadiness = await execFileAsync(zero, [
  "check",
  "--json",
  "--emit",
  "obj",
  "--target",
  "darwin-arm64",
  "examples/memory-package",
]);
const memoryPackageMachOReadinessBody = JSON.parse(memoryPackageMachOReadiness.stdout);
assert.equal(memoryPackageMachOReadinessBody.ok, true);
assert.equal(memoryPackageMachOReadinessBody.diagnostics.length, 0);
assert.equal(memoryPackageMachOReadinessBody.targetReadiness.ok, true);
assert.equal(memoryPackageMachOReadinessBody.targetReadiness.buildable, true);
assert.equal(memoryPackageMachOReadinessBody.targetReadiness.backend, "zero-macho64");
assert.equal(memoryPackageMachOReadinessBody.targetReadiness.diagnostics.length, 0);
const memoryPackageMachOObj = `${outDir}/memory-package-macho.o`;
const memoryPackageMachOBuild = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "darwin-arm64",
  "examples/memory-package",
  "--out",
  memoryPackageMachOObj,
]);
const memoryPackageMachOBuildBody = JSON.parse(memoryPackageMachOBuild.stdout);
assert.equal(memoryPackageMachOBuildBody.compiler, "zero-macho64");
assert.equal(memoryPackageMachOBuildBody.generatedCBytes, 0);
assert.equal(memoryPackageMachOBuildBody.objectBackend.objectEmission.path, "direct-macho64-object");
await assertMachOArm64Object(memoryPackageMachOObj, "main");

async function assertAgentSurfaceOwnedDropUnsupported(target, emit, outName, expectedPattern, expectedObjectFormat, expectedBackend, options = {}) {
  const extraArgs = options.extraArgs ?? [];
  const build = await execFileAsync(zero, [
    "build",
    "--json",
    "--emit",
    emit,
    "--target",
    target,
    ...extraArgs,
    "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.0",
    "--out",
    `${outDir}/${outName}`,
  ]).catch((error) => error);
  assert.notEqual(build.code, 0);
  const body = JSON.parse(build.stdout);
  const diagnostic = body.diagnostics[0];
  assert.equal(diagnostic.code, "BLD004");
  assert.match(diagnostic.expected, expectedPattern);
  assert.equal(diagnostic.actual, "owned<Tracked>");
  assert.deepEqual(diagnostic.backendBlocker, {
    target,
    objectFormat: expectedObjectFormat,
    backend: expectedBackend,
    stage: "lower",
    unsupportedFeature: "owned<Tracked>",
  });
}

await assertAgentSurfaceOwnedDropUnsupported("linux-musl-x64", "obj", "agent-surface-owned-drop-elf.o", /typed program graph MIR subset/, "elf", "zero-elf64-exe");
await assertAgentSurfaceOwnedDropUnsupported("darwin-arm64", "obj", "agent-surface-owned-drop-macho.o", /typed program graph MIR subset/, "macho", "zero-macho64-exe");
await assertAgentSurfaceOwnedDropUnsupported("win32-x64.exe", "obj", "agent-surface-owned-drop-coff.obj", /typed program graph MIR subset/, "coff", "zero-coff-x64-exe");

const mismatchedDirectEmitter = await execFileAsync(zero, [
  "build",
  "--json",
  "--emit",
  "obj",
  "--target",
  "darwin-arm64",
  "--backend",
  "zero-elf64",
  "examples/add.0",
  "--out",
  `${outDir}/direct-emitter-mismatch.o`,
]).catch((error) => error);
assert.notEqual(mismatchedDirectEmitter.code, 0);
const mismatchedDirectEmitterBody = JSON.parse(mismatchedDirectEmitter.stdout);
assert.equal(mismatchedDirectEmitterBody.diagnostics[0].code, "BLD004");
assert.equal(mismatchedDirectEmitterBody.diagnostics[0].actual, "--backend zero-elf64");
assert.deepEqual(mismatchedDirectEmitterBody.diagnostics[0].backendBlocker, {
  target: "darwin-arm64",
  objectFormat: "macho",
  backend: "zero-elf64",
  stage: "target-selection",
  unsupportedFeature: "requested direct emitter does not match target",
});

const commonPassFixtures = [
  ["conformance/common/pass/array-sum-min-max.0", "common-array-sum-min-max", { stdout: "array sum min max ok\n" }],
  ["conformance/common/pass/bytes-reverse.0", "common-bytes-reverse", { stdout: "bytes reverse ok\n" }],
  ["conformance/common/pass/cli-args.0", "common-cli-args", { stdout: "alpha\n", args: ["alpha", "beta"] }],
  ["conformance/common/pass/count-words-lines.0", "common-count-words-lines", { stdout: "count words lines ok\n" }],
  ["conformance/common/pass/factorial.0", "common-factorial", { stdout: "factorial ok\n" }],
  ["conformance/common/pass/fib-iterative.0", "common-fib-iterative", { stdout: "fib iterative ok\n" }],
  ["conformance/common/pass/fib-recursive.0", "common-fib-recursive", { stdout: "fib recursive ok\n" }],
  ["conformance/common/pass/file-copy.0", "common-file-copy", { stdout: "file copy ok\n", file: { name: "common-file-copy-output.txt", text: "zero file copy\n" } }],
  ["conformance/common/pass/gcd.0", "common-gcd", { stdout: "gcd ok\n" }],
  ["conformance/common/pass/json-roundtrip.0", "common-json-roundtrip", { stdout: "json roundtrip ok\n" }],
  ["conformance/common/pass/palindrome.0", "common-palindrome", { stdout: "palindrome ok\n" }],
  ["conformance/common/pass/prime.0", "common-prime", { stdout: "prime ok\n" }],
  ["conformance/common/pass/sieve-small.0", "common-sieve-small", { stdout: "sieve small ok\n" }],
  ["conformance/common/pass/sort-small.0", "common-sort-small", { stdout: "sort small ok\n" }],
  ["conformance/common/pass/string-search.0", "common-string-search", { stdout: "string search ok\n" }],
  ["conformance/common/pass/word-reverse.0", "common-word-reverse", { stdout: "word reverse ok\n" }],
];

for (const [fixture, name, expected] of commonPassFixtures) {
  const check = await execFileAsync(zero, ["check", "--json", fixture]);
  assert.equal(JSON.parse(check.stdout).ok, true);
  const graph = await execFileAsync(zero, ["inspect", "--json", fixture]);
  const graphBody = JSON.parse(graph.stdout);
  assert.equal(graphBody.graph.artifact, fixture.replace(/\.0$/, ".graph"));
  assert(graphBody.sourceFiles.includes(fixture) || graphBody.sourceFiles.includes(fixture.split("/").at(-1)));
  const size = await execFileAsync(zero, ["size", "--json", fixture]);
  assert.equal(JSON.parse(size.stdout).schemaVersion, 1);
  await assertCommonRuntimeOrUnsupported(fixture, name, expected);
}

const commonUnsupportedTarget = await execFileAsync(zero, ["check", "--json", "--target", "linux-musl-x64", "conformance/common/fail/unsupported-target-feature.0"]).catch((error) => error);
assert.notEqual(commonUnsupportedTarget.code, 0);
assert.equal(JSON.parse(commonUnsupportedTarget.stdout).diagnostics[0].code, "TAR002");

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
assert.equal(compileTimeBody.safetyFacts.schemaVersion, 1);
assert.equal(compileTimeBody.safetyFacts.bounds.runtimeTraps, true);
assert.equal(compileTimeBody.safetyFacts.bounds.optimizerElision, false);
assert.equal(compileTimeBody.safetyFacts.overflow.runtimeArithmetic, "unchecked-machine-wrap");
assert.equal(compileTimeBody.safetyFacts.overflow.unchecked, true);
assert.equal(compileTimeBody.safetyFacts.initialization.locals, "initializer-required");
assert.equal(compileTimeBody.safetyFacts.initialization.maybePayloadReads, "guard-checked");
assert.equal(compileTimeBody.safetyFacts.aliasing.mutableAliases, "diagnostic");
assert.equal(compileTimeBody.safetyFacts.mir.invalidMemoryContractsBlockEmission, true);

const compileTimeGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/native/pass/compile-time-v1.0"]);
const compileTimeGraphBody = JSON.parse(compileTimeGraph.stdout);
assert.equal(compileTimeGraphBody.compileTime.deterministic, true);
assert.equal(compileTimeGraphBody.safetyFacts.profileKey, "small");
assert.equal(compileTimeGraphBody.safetyFacts.lifetimes.escapedLocalBorrow, "diagnostic");
const readGateGraph = compileTimeGraphBody.functions.find((item) => item.name === "readGate");
assert.ok(readGateGraph.staticParams.some((item) => item.name === "enabledFlag" && item.kind === "bool"));
assert.ok(readGateGraph.staticParams.some((item) => item.name === "selectedMode" && item.kind === "enum"));
const gateGraph = compileTimeGraphBody.shapes.find((item) => item.name === "Gate");
assert.ok(gateGraph.staticParams.some((item) => item.name === "enabledFlag" && item.kind === "bool"));
assert.ok(gateGraph.staticParams.some((item) => item.name === "selectedMode" && item.kind === "enum"));

const fastCheck = await execFileAsync(zero, ["check", "--json", "--profile", "fast", "examples/hello.0"]);
const fastCheckBody = JSON.parse(fastCheck.stdout);
assert.equal(fastCheckBody.artifact, "examples/hello.graph");
assert.equal(fastCheckBody.canonicalSource, false);
assert.equal(fastCheckBody.check.lowering, "graph-native-check");
assert.equal(fastCheckBody.safetyFacts.profile, "release-fast");
assert.equal(fastCheckBody.safetyFacts.profileKey, "fast");

const fastGraph = await execFileAsync(zero, ["inspect", "--json", "--profile", "fast", "examples/hello.0"]);
const fastGraphBody = JSON.parse(fastGraph.stdout);
assert.equal(fastGraphBody.packageCache.profile, "fast");
assert.equal(fastGraphBody.incrementalInvalidation.profileDependency, "fast");
assert.equal(fastGraphBody.safetyFacts.profile, "release-fast");
assert.equal(fastGraphBody.safetyFacts.profileKey, "fast");

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
assert.equal(buildJsonM6Body.profileSemantics.boundsPolicy, "checked");
assert.equal(buildJsonM6Body.profileSemantics.overflowPolicy, "literal-range-checked-runtime-unchecked");
assert.equal(buildJsonM6Body.profileSemantics.profileBudget.generatedCBytes, 0);
assert.equal(buildJsonM6Body.safetyFacts.profile, "tiny");
assert.equal(buildJsonM6Body.safetyFacts.bounds.policy, "checked");
assert.equal(buildJsonM6Body.safetyFacts.bounds.optimizerElision, false);
assert.equal(buildJsonM6Body.safetyFacts.overflow.policy, "literal-range-checked-runtime-unchecked");
assert.equal(buildJsonM6Body.safetyFacts.overflow.integerLiterals, "range-checked");
assert.equal(buildJsonM6Body.safetyFacts.ownership.useAfterMove, "diagnostic");
assert.equal(buildJsonM6Body.profileBudget.helperBudgetPolicy, "pay-as-used-minimum-runtime");
assert(buildJsonM6Body.profileCatalog.some((item) => item.canonical === "debug" && item.debugInfo === true));
assert(buildJsonM6Body.profileCatalog.some((item) => item.canonical === "release-fast" && item.boundsPolicy === "checked"));
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
  assert.equal(profileBody.profileSemantics.boundsPolicy, "checked");
  assert.equal(profileBody.profileSemantics.overflowPolicy, "literal-range-checked-runtime-unchecked");
  assert.equal(profileBody.profileSemantics.profileBudget.generatedCBytes, 0);
  assert.equal(profileBody.safetyFacts.profile, canonicalProfile);
  assert.equal(profileBody.safetyFacts.profileKey, profileKey);
  assert.equal(profileBody.safetyFacts.bounds.optimizerElision, false);
  assert.equal(profileBody.profileBudget.cBridgeFallback, false);
}

const profileSize = await execFileAsync(zero, ["size", "--json", "--profile", "debug", "--target", "linux-musl-x64", "examples/hello.0"]);
const profileSizeBody = JSON.parse(profileSize.stdout);
assert.equal(profileSizeBody.generatedCBytes, 0);
assert.equal(profileSizeBody.graph.artifact, "examples/hello.graph");
assert.equal(profileSizeBody.graph.lowering, "mapped-final-mir");
assert.equal(profileSizeBody.profileSemantics.profileKey, "debug");
assert.equal(profileSizeBody.safetyFacts.profileKey, "debug");
assert.equal(profileSizeBody.safetyFacts.uncheckedSurfaces[0].surface, "C imports");
assert.equal(profileSizeBody.sizeBreakdown.profileKey, "debug");
assert(Array.isArray(profileSizeBody.sizeBreakdown.functions));
assert(profileSizeBody.sizeBreakdown.sections.some((item) => item.name === "text" && item.retainedBy.includes("retained functions")));
assert(Array.isArray(profileSizeBody.sizeBreakdown.literals.items));
assert(Array.isArray(profileSizeBody.sizeBreakdown.stdlibHelpers));
assert(Array.isArray(profileSizeBody.sizeBreakdown.imports));
assert(Array.isArray(profileSizeBody.sizeBreakdown.runtimeShims));
assert(profileSizeBody.sizeBreakdown.debugMetadata.bytes > 0);
assert(profileSizeBody.compilerCaches.some((item) => item.name === "mappedFinalMir" && item.sourceKind === "program-graph" && item.programReconstructed === false));
assert(profileSizeBody.retentionReasons.some((item) => item.kind === "debugMetadata"));
assert(profileSizeBody.optimizationHints.some((item) => item.id === "profile-debug-metadata"));
assert.equal(profileSizeBody.profileBudget.debugMetadataAllowed, true);

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

const directWideMainSource = `${outDir}/direct-exe-wide-main.0`;
const directWideMainOut = `${outDir}/direct-exe-wide-main`;
await writeGraphFixture(directWideMainSource, `export c fn main() -> usize {
    return 8589934590
}
`);
const directWideMainJson = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", directWideMainSource, "--out", directWideMainOut]);
const directWideMainBody = JSON.parse(directWideMainJson.stdout);
const directWideMainBytes = await readFile(directWideMainOut);
assert.equal(directWideMainBody.compiler, "zero-elf64");
assert.equal(directWideMainBody.objectBackend.objectEmission.path, "direct-elf64-exe");
assert(!directWideMainBytes.includes(Buffer.from([0x48, 0xc1, 0xe9, 0x20])));
assert(directWideMainBytes.includes(Buffer.from([0x89, 0xc7, 0xb8, 0x3c, 0x00, 0x00, 0x00, 0x0f, 0x05])));

const directI64MainSource = `${outDir}/direct-exe-i64-main.0`;
const directI64MainOut = `${outDir}/direct-exe-i64-main`;
await writeGraphFixture(directI64MainSource, `export c fn main() -> i64 {
    return 8589934590_i64
}
`);
const directI64MainJson = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", directI64MainSource, "--out", directI64MainOut]);
const directI64MainBody = JSON.parse(directI64MainJson.stdout);
assert.equal(directI64MainBody.compiler, "zero-elf64");
assert.equal(directI64MainBody.objectBackend.objectEmission.path, "direct-elf64-exe");

const directMachOU64LiteralSource = `${outDir}/direct-macho-u64-literal.0`;
const directMachOU64LiteralOut = `${outDir}/direct-macho-u64-literal.o`;
await writeGraphFixture(directMachOU64LiteralSource, `export c fn main() -> u64 {
    let value: u64 = 4294967296
    return value
}
`);
const directMachOU64LiteralReadinessJson = await execFileAsync(zero, ["check", "--json", "--emit", "obj", "--target", "darwin-arm64", directMachOU64LiteralSource]);
const directMachOU64LiteralReadinessBody = JSON.parse(directMachOU64LiteralReadinessJson.stdout);
assert.equal(directMachOU64LiteralReadinessBody.targetReadiness.buildable, true);
const directMachOU64LiteralJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "darwin-arm64", directMachOU64LiteralSource, "--out", directMachOU64LiteralOut]);
const directMachOU64LiteralBody = JSON.parse(directMachOU64LiteralJson.stdout);
const directMachOU64LiteralBytes = await readFile(directMachOU64LiteralOut);
assert.equal(directMachOU64LiteralBody.compiler, "zero-macho64");
assert.equal(directMachOU64LiteralBody.objectBackend.objectEmission.path, "direct-macho64-object");
assert(directMachOU64LiteralBytes.includes(Buffer.from([0x28, 0x00, 0xc0, 0xf2])));

const directMachOU64DivSource = `${outDir}/direct-macho-u64-div.0`;
const directMachOU64DivOut = `${outDir}/direct-macho-u64-div.o`;
await writeGraphFixture(directMachOU64DivSource, `export c fn main(a: u64, b: u64) -> u64 {
    return a / b
}
`);
const directMachOU64DivJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "darwin-arm64", directMachOU64DivSource, "--out", directMachOU64DivOut]);
const directMachOU64DivBody = JSON.parse(directMachOU64DivJson.stdout);
const directMachOU64DivBytes = await readFile(directMachOU64DivOut);
assert.equal(directMachOU64DivBody.compiler, "zero-macho64");
assert.equal(directMachOU64DivBody.objectBackend.objectEmission.path, "direct-macho64-object");
assert(directMachOU64DivBytes.includes(Buffer.from([0x00, 0x09, 0xc9, 0x9a])));
assert(!directMachOU64DivBytes.includes(Buffer.from([0x00, 0x09, 0xc9, 0x1a])));

const directMachOU64ModSource = `${outDir}/direct-macho-u64-mod.0`;
const directMachOU64ModOut = `${outDir}/direct-macho-u64-mod.o`;
await writeGraphFixture(directMachOU64ModSource, `export c fn main(a: u64, b: u64) -> u64 {
    return a % b
}
`);
const directMachOU64ModJson = await execFileAsync(zero, ["build", "--json", "--emit", "obj", "--target", "darwin-arm64", directMachOU64ModSource, "--out", directMachOU64ModOut]);
const directMachOU64ModBody = JSON.parse(directMachOU64ModJson.stdout);
const directMachOU64ModBytes = await readFile(directMachOU64ModOut);
assert.equal(directMachOU64ModBody.compiler, "zero-macho64");
assert.equal(directMachOU64ModBody.objectBackend.objectEmission.path, "direct-macho64-object");
assert(directMachOU64ModBytes.includes(Buffer.from([0x0a, 0x09, 0xc9, 0x9a])));
assert(directMachOU64ModBytes.includes(Buffer.from([0x40, 0xa1, 0x09, 0x9b])));
assert(!directMachOU64ModBytes.includes(Buffer.from([0x0a, 0x09, 0xc9, 0x1a])));
assert(!directMachOU64ModBytes.includes(Buffer.from([0x40, 0xa1, 0x09, 0x1b])));

const metaJsonSuccess = await execFileAsync(zero, ["check", "--json", "conformance/native/pass/meta-typed-target-type.0"]);
const metaJsonSuccessBody = JSON.parse(metaJsonSuccess.stdout);
assert.equal(metaJsonSuccessBody.ok, true);
assert.equal(metaJsonSuccessBody.artifact, "conformance/native/pass/meta-typed-target-type.graph");
assert.equal(metaJsonSuccessBody.canonicalSource, false);
assert.equal(metaJsonSuccessBody.check.lowering, "graph-native-check");
assert.equal(metaJsonSuccessBody.compileTime.deterministic, true);

const collectionsUsizeMemory = await execFileAsync(zero, ["mem", "--json", "conformance/native/pass/std-collections-usize-memory.0"]);
const collectionsUsizeMemoryBody = JSON.parse(collectionsUsizeMemory.stdout);
assert.equal(collectionsUsizeMemoryBody.graph.artifact, "conformance/native/pass/std-collections-usize-memory.graph");
assert.equal(collectionsUsizeMemoryBody.graph.canonicalSource, false);
assert.match(collectionsUsizeMemoryBody.graph.graphHash, /^graph:[0-9a-f]{16}$/);
assert.equal(collectionsUsizeMemoryBody.compilerCaches.every((item) => item.sourceKind === "program-graph" && item.graphHash === collectionsUsizeMemoryBody.graph.graphHash), true);
assert.equal(collectionsUsizeMemoryBody.incrementalInvalidation.sourceKind, "program-graph");
assert.equal(collectionsUsizeMemoryBody.incrementalInvalidation.changedInputs.graphArtifact, "conformance/native/pass/std-collections-usize-memory.graph");
assert.equal(collectionsUsizeMemoryBody.memoryBudgets.collectionCapacityBytes, 32);
assert.equal(collectionsUsizeMemoryBody.collectionFacts.FixedStorage.capacityBytes, 32);

const collectionsMutspanMemory = await execFileAsync(zero, ["mem", "--json", "conformance/native/pass/std-collections-mutspan-memory.0"]);
const collectionsMutspanMemoryBody = JSON.parse(collectionsMutspanMemory.stdout);
assert.equal(collectionsMutspanMemoryBody.memoryBudgets.collectionCapacityBytes, 4);
assert.equal(collectionsMutspanMemoryBody.collectionFacts.FixedStorage.storageSites, 1);
assert.equal(collectionsMutspanMemoryBody.collectionFacts.FixedStorage.capacityBytes, 4);

const collectionsQueryMemory = await execFileAsync(zero, ["mem", "--json", "conformance/native/pass/std-collections-query-memory.0"]);
const collectionsQueryMemoryBody = JSON.parse(collectionsQueryMemory.stdout);
assert.equal(collectionsQueryMemoryBody.memoryBudgets.collectionCapacityBytes, 2);
assert.equal(collectionsQueryMemoryBody.collectionFacts.FixedStorage.storageSites, 1);
assert.equal(collectionsQueryMemoryBody.collectionFacts.FixedStorage.capacityBytes, 2);
assert.equal(collectionsQueryMemoryBody.collectionFacts.FixedStorage.queryCalls, 1);

const interfaceStaticUnsupportedTypeFixture = `${outDir}/interface-static-unsupported-type.0`;
const interfaceStaticUnsupportedTypeBody = await writeImportFailureFixture(interfaceStaticUnsupportedTypeFixture, `interface Bad<static N: String> {
    fn value() -> u8
}

pub fn main() -> Void {
}
`);
assert.equal(interfaceStaticUnsupportedTypeBody.diagnostics[0].code, "STC001");

const interfaceMethodStaticUnsupportedTypeFixture = `${outDir}/interface-method-static-unsupported-type.0`;
const interfaceMethodStaticUnsupportedTypeBody = await writeImportFailureFixture(interfaceMethodStaticUnsupportedTypeFixture, `interface Bad {
    fn value<static N: String>() -> u8
}

pub fn main() -> Void {
}
`);
assert.equal(interfaceMethodStaticUnsupportedTypeBody.diagnostics[0].code, "STC001");

const shapeMethodStaticUnsupportedTypeFixture = `${outDir}/shape-method-static-unsupported-type.0`;
const shapeMethodStaticUnsupportedTypeBody = await writeImportFailureFixture(shapeMethodStaticUnsupportedTypeFixture, `type Box {
    value: u8,
    fn value<static N: String>(self: ref<Self>) -> u8 {
        return self.value
    }
}

pub fn main() -> Void {
}
`);
assert.equal(shapeMethodStaticUnsupportedTypeBody.diagnostics[0].code, "STC001");

const methodUnknownConstraintFixtures = [
	  {
	    name: "interface-method-unknown-constraint",
	    source: `interface Bad {
    fn value<T: Missing>() -> u8
}

pub fn main() -> Void {
}
`,
	  },
	  {
	    name: "shape-method-unknown-constraint",
	    source: `type Box {
    value: u8,

    fn value<T: Missing>(self: ref<Self>) -> u8 {
        return self.value
    }
}

pub fn main() -> Void {
}
`,
	  },
];

for (const fixtureCase of methodUnknownConstraintFixtures) {
  const fixture = `${outDir}/${fixtureCase.name}.0`;
  const methodUnknownConstraintBody = await writeImportFailureFixture(fixture, fixtureCase.source);
  assert.equal(methodUnknownConstraintBody.diagnostics[0].code, "IFC001");
}

const duplicateStaticGenericFixtures = [
	  {
	    name: "function-duplicate-static-param",
	    message: /duplicate generic parameter/,
	    source: `fn id<static N: usize, static N: usize>() -> u8 {
    return 1
}

pub fn main() -> Void {
}
`,
	  },
	  {
	    name: "interface-duplicate-static-param",
	    message: /duplicate generic parameter/,
	    source: `interface Bad<static N: usize, static N: usize> {
    fn value() -> u8
}

pub fn main() -> Void {
}
`,
	  },
	  {
	    name: "shape-method-duplicate-static-param",
	    message: /duplicate generic parameter/,
	    source: `type Box {
    value: u8,

    fn value<static N: usize, static N: usize>(self: ref<Self>) -> u8 {
        return self.value
    }
}

pub fn main() -> Void {
}
`,
	  },
	  {
	    name: "shape-method-static-shadows-shape-static",
	    message: /shadows outer generic parameter/,
	    source: `type Box<static N: usize> {
    value: [N]u8,

    fn len<static N: usize>(self: ref<Self>) -> usize {
        return N
    }
}

pub fn main() -> Void {
}
`,
	  },
	  {
	    name: "shape-method-static-self-param",
	    message: /generic type parameter shadows Self type/,
	    source: `type Box {
    value: u8,

    fn value<static Self: usize>(self: ref<Self>) -> usize {
        return Self
    }
}

pub fn main() -> Void {
}
`,
	  },
];

for (const fixtureCase of duplicateStaticGenericFixtures) {
  const fixture = `${outDir}/${fixtureCase.name}.0`;
  const duplicateStaticBody = await writeImportFailureFixture(fixture, fixtureCase.source);
  assert.equal(duplicateStaticBody.diagnostics[0].code, "NAM004");
  assert.match(duplicateStaticBody.diagnostics[0].message, fixtureCase.message);
}

for (const value of ["4_", "M"]) {
  const fixture = `${outDir}/interface-static-constraint-${value.replace(/[^A-Za-z0-9]/g, "_")}.0`;
  const interfaceStaticConstraintBody = await writeImportFailureFixture(fixture, `interface First<T: Type, static N: usize> {
    fn first(self: ref<T>) -> u8
}

fn readFirst<T: First<T, ${value}>>(value: ref<T>) -> u8 {
    return T.first(value)
}

pub fn main() -> Void {
}
`);
  assert.equal(interfaceStaticConstraintBody.diagnostics[0].code, "STC002");
  assert.equal(interfaceStaticConstraintBody.diagnostics[0].actual, value);
}

const shapeMethodStaticParamFixture = `${outDir}/shape-method-static-param.0`;
await writeGraphFixture(shapeMethodStaticParamFixture, `type Box {
    value: u8,

    fn tag<static N: usize>(self: ref<Self>) -> usize {
        return N
    }

    fn value<static N: usize>() -> usize {
        return N
    }
}

pub fn main() -> Void {
    let box: Box = Box { value: 1 }
    let receiverTag: usize = box.tag<4>()
    let namespaceTag: usize = Box.value<8>()
}
`);
const shapeMethodStaticParamJson = await execFileAsync(zero, ["check", "--json", shapeMethodStaticParamFixture]);
const shapeMethodStaticParamBody = JSON.parse(shapeMethodStaticParamJson.stdout);
assert.equal(shapeMethodStaticParamBody.ok, true);
const shapeMethodStaticParamGraph = await execFileAsync(zero, ["inspect", "--json", shapeMethodStaticParamFixture]);
const shapeMethodStaticParamGraphBody = JSON.parse(shapeMethodStaticParamGraph.stdout);
const shapeMethodStaticBox = shapeMethodStaticParamGraphBody.shapes.find((item) => item.name === "Box");
assert(shapeMethodStaticBox);
const shapeMethodStaticTag = shapeMethodStaticBox.methods.find((item) => item.name === "tag");
assert(shapeMethodStaticTag);
assert(shapeMethodStaticTag.staticParams.some((item) => item.name === "N" && item.type === "usize" && item.staticDispatch === true));

const shapeMethodStaticCanonicalFixture = `${outDir}/shape-method-static-canonical.0`;
await writeGraphFixture(shapeMethodStaticCanonicalFixture, `type Box {
    fn take<static N: usize>(a: [N]u8, b: [N]u8) -> usize {
        return N
    }
}

pub fn main() -> Void {
    let a: [4]u8 = [1, 2, 3, 4]
    let b: [0x4]u8 = [1, 2, 3, 4]
    let inferred: usize = Box.take(a, b)
    let explicit: usize = Box.take<0x4>(a, b)
}
`);
const shapeMethodStaticCanonicalJson = await execFileAsync(zero, ["check", "--json", shapeMethodStaticCanonicalFixture]);
const shapeMethodStaticCanonicalBody = JSON.parse(shapeMethodStaticCanonicalJson.stdout);
assert.equal(shapeMethodStaticCanonicalBody.ok, true);

const interfaceMethodStaticParamFixture = `${outDir}/interface-method-static-param.0`;
await writeGraphFixture(interfaceMethodStaticParamFixture, `interface Width<T: Type> {
    fn width<static N: usize>(self: ref<T>) -> usize
}

type Bytes {
    value: u8,

    fn width<static N: usize>(self: ref<Self>) -> usize {
        return N
    }
}

fn readWidth<T: Width<T>>(value: ref<T>) -> usize {
    return T.width<4>(value)
}

pub fn main() -> Void {
    let bytes: Bytes = Bytes { value: 1 }
    let width: usize = readWidth<Bytes>(&bytes)
}
`);
const interfaceMethodStaticParamJson = await execFileAsync(zero, ["check", "--json", interfaceMethodStaticParamFixture]);
const interfaceMethodStaticParamBody = JSON.parse(interfaceMethodStaticParamJson.stdout);
assert.equal(interfaceMethodStaticParamBody.ok, true);
const interfaceMethodStaticParamGraph = await execFileAsync(zero, ["inspect", "--json", interfaceMethodStaticParamFixture]);
const interfaceMethodStaticParamGraphBody = JSON.parse(interfaceMethodStaticParamGraph.stdout);
const interfaceMethodStaticWidth = interfaceMethodStaticParamGraphBody.interfaces.find((item) => item.name === "Width");
assert(interfaceMethodStaticWidth);
const interfaceMethodStaticWidthMethod = interfaceMethodStaticWidth.methods.find((item) => item.name === "width");
assert(interfaceMethodStaticWidthMethod);
assert(interfaceMethodStaticWidthMethod.staticParams.some((item) => item.name === "N" && item.type === "usize" && item.staticDispatch === true));
const interfaceMethodStaticBytes = interfaceMethodStaticParamGraphBody.shapes.find((item) => item.name === "Bytes");
assert(interfaceMethodStaticBytes);
const interfaceMethodStaticBytesWidth = interfaceMethodStaticBytes.methods.find((item) => item.name === "width");
assert(interfaceMethodStaticBytesWidth);
assert(interfaceMethodStaticBytesWidth.staticParams.some((item) => item.name === "N" && item.type === "usize" && item.staticDispatch === true));

const interfaceMethodStaticRenamedParamFixture = `${outDir}/interface-method-static-renamed-param.0`;
await writeGraphFixture(interfaceMethodStaticRenamedParamFixture, `interface Width<T: Type> {
    fn width<static N: usize>(self: ref<T>, bytes: [N]u8) -> [N]u8
}

type Bytes {
    value: u8,

    fn width<static M: usize>(self: ref<Self>, bytes: [M]u8) -> [M]u8 {
        return bytes
    }
}

fn readWidth<T: Width<T>>(value: ref<T>, bytes: [4]u8) -> [4]u8 {
    return T.width<4>(value, bytes)
}

pub fn main() -> Void {
    let bytes: Bytes = Bytes { value: 1 }
    let output: [4]u8 = readWidth<Bytes>(&bytes, [1, 2, 3, 4])
}
`);
const interfaceMethodStaticRenamedParamJson = await execFileAsync(zero, ["check", "--json", interfaceMethodStaticRenamedParamFixture]);
const interfaceMethodStaticRenamedParamBody = JSON.parse(interfaceMethodStaticRenamedParamJson.stdout);
assert.equal(interfaceMethodStaticRenamedParamBody.ok, true);
const interfaceMethodStaticRenamedParamGraph = await execFileAsync(zero, ["inspect", "--json", interfaceMethodStaticRenamedParamFixture]);
const interfaceMethodStaticRenamedParamGraphBody = JSON.parse(interfaceMethodStaticRenamedParamGraph.stdout);
const interfaceMethodStaticRenamedWidth = interfaceMethodStaticRenamedParamGraphBody.interfaces.find((item) => item.name === "Width");
assert(interfaceMethodStaticRenamedWidth);
const interfaceMethodStaticRenamedWidthMethod = interfaceMethodStaticRenamedWidth.methods.find((item) => item.name === "width");
assert(interfaceMethodStaticRenamedWidthMethod);
assert(interfaceMethodStaticRenamedWidthMethod.staticParams.some((item) => item.name === "N" && item.type === "usize" && item.staticDispatch === true));
const interfaceMethodStaticRenamedBytes = interfaceMethodStaticRenamedParamGraphBody.shapes.find((item) => item.name === "Bytes");
assert(interfaceMethodStaticRenamedBytes);
const interfaceMethodStaticRenamedBytesWidth = interfaceMethodStaticRenamedBytes.methods.find((item) => item.name === "width");
assert(interfaceMethodStaticRenamedBytesWidth);
assert(interfaceMethodStaticRenamedBytesWidth.staticParams.some((item) => item.name === "M" && item.type === "usize" && item.staticDispatch === true));

const staticInterfaceReturnMismatchFixture = `${outDir}/static-interface-return-mismatch.0`;
const staticInterfaceReturnMismatchBody = await writeImportFailureFixture(staticInterfaceReturnMismatchFixture, `interface Sized<T: Type, static N: usize> {
    fn bytes(self: ref<T>) -> [N]u8
}

type Bytes<static N: usize> {
    items: [N]u8,

    fn bytes(self: ref<Self>) -> [N]u8 {
        return self.items
    }
}

fn read<T: Sized<T, N>, static N: usize>(value: ref<T>) -> [N]u8 {
    return T.bytes(value)
}

pub fn main() -> Void {
    let bytes: Bytes<4> = Bytes { items: [1, 2, 3, 4] }
    let out: [3]u8 = read<Bytes<4>, 3>(&bytes)
}
`);
assert.equal(staticInterfaceReturnMismatchBody.diagnostics[0].code, "IFC004");
assert.match(staticInterfaceReturnMismatchBody.diagnostics[0].expected, /\[3\]u8/);
assert.match(staticInterfaceReturnMismatchBody.diagnostics[0].actual, /\[4\]u8/);

const shapeMethodGenericConstraintFixture = `${outDir}/shape-method-generic-constraint.0`;
const shapeMethodGenericConstraintBody = await writeImportFailureFixture(shapeMethodGenericConstraintFixture, `interface NeedsMethod<T: Type> {
    fn need(self: ref<T>) -> u8
}

type Box {
    value: u8,

    fn accept<T: NeedsMethod<T>>(self: ref<Self>, item: T) -> u8 {
        return self.value
    }
}

type Plain {
    value: u8,
}

pub fn main() -> Void {
    let box: Box = Box { value: 1 }
    let plain: Plain = Plain { value: 2 }
    let out: u8 = box.accept<Plain>(plain)
}
`);
assert.equal(shapeMethodGenericConstraintBody.diagnostics[0].code, "IFC002");

const interfaceMethodGenericConstraintFixture = `${outDir}/interface-method-generic-constraint.0`;
const interfaceMethodGenericConstraintBody = await writeImportFailureFixture(interfaceMethodGenericConstraintFixture, `interface NeedsMethod<T: Type> {
    fn need(self: ref<T>) -> u8
}

interface Caller<T: Type> {
    fn accept<U: NeedsMethod<U>>(self: ref<T>, item: U) -> u8
}

type Box {
    value: u8,

    fn accept<U: NeedsMethod<U>>(self: ref<Self>, item: U) -> u8 {
        return self.value
    }
}

type Plain {
    value: u8,
}

fn read<T: Caller<T>>(value: ref<T>, plain: Plain) -> u8 {
    return T.accept<Plain>(value, plain)
}

pub fn main() -> Void {
    let box: Box = Box { value: 1 }
    let plain: Plain = Plain { value: 2 }
    let out: u8 = read<Box>(&box, plain)
}
`);
assert.equal(interfaceMethodGenericConstraintBody.diagnostics[0].code, "IFC002");

const interfaceMethodGenericMismatchFixtures = [
	  {
	    name: "interface-method-generic-constraint-mismatch",
	    code: "IFC005",
	    message: /constraint does not match/,
	    source: `interface NeedsA<T: Type> {
    fn needA(self: ref<T>) -> u8
}

interface NeedsB<T: Type> {
    fn needB(self: ref<T>) -> u8
}

interface Caller<T: Type> {
    fn accept<U: NeedsA<U>>(self: ref<T>, item: U) -> u8
}

type Box {
    value: u8,

    fn accept<U: NeedsB<U>>(self: ref<Self>, item: U) -> u8 {
        return self.value
    }
}

type A {
    value: u8,

    fn needA(self: ref<Self>) -> u8 {
        return self.value
    }
}

fn read<T: Caller<T>>(value: ref<T>, item: A) -> u8 {
    return T.accept<A>(value, item)
}

pub fn main() -> Void {
    let box: Box = Box { value: 1 }
    let a: A = A { value: 2 }
    let out: u8 = read<Box>(&box, a)
}
`,
	  },
	  {
	    name: "interface-method-missing-static-param",
	    code: "IFC003",
	    source: `interface Width<T: Type> {
    fn width<static N: usize>(self: ref<T>) -> usize
}

type Bytes {
    value: u8,

    fn width(self: ref<Self>) -> usize {
        return 1
    }
}

fn readWidth<T: Width<T>>(value: ref<T>) -> usize {
    return T.width<4>(value)
}

pub fn main() -> Void {
    let bytes: Bytes = Bytes { value: 1 }
    let width: usize = readWidth<Bytes>(&bytes)
}
`,
	  },
	  {
	    name: "interface-method-extra-static-param",
	    code: "IFC003",
	    source: `interface Width<T: Type> {
    fn width(self: ref<T>) -> usize
}

type Bytes {
    value: u8,

    fn width<static N: usize>(self: ref<Self>) -> usize {
        return N
    }
}

fn readWidth<T: Width<T>>(value: ref<T>) -> usize {
    return T.width(value)
}

pub fn main() -> Void {
    let bytes: Bytes = Bytes { value: 1 }
    let width: usize = readWidth<Bytes>(&bytes)
}
`,
	  },
	  {
	    name: "interface-method-static-param-type-mismatch",
	    code: "IFC005",
	    source: `interface Width<T: Type> {
    fn width<static N: usize>(self: ref<T>) -> usize
}

type Bytes {
    value: u8,

    fn width<static N: Bool>(self: ref<Self>) -> usize {
        return 1
    }
}

fn readWidth<T: Width<T>>(value: ref<T>) -> usize {
    return T.width<4>(value)
}

pub fn main() -> Void {
    let bytes: Bytes = Bytes { value: 1 }
    let width: usize = readWidth<Bytes>(&bytes)
}
`,
	  },
];

for (const fixtureCase of interfaceMethodGenericMismatchFixtures) {
  const fixture = `${outDir}/${fixtureCase.name}.0`;
  const interfaceMethodGenericMismatchBody = await writeImportFailureFixture(fixture, fixtureCase.source);
  assert.equal(interfaceMethodGenericMismatchBody.diagnostics[0].code, fixtureCase.code);
  if (fixtureCase.message) assert.match(interfaceMethodGenericMismatchBody.diagnostics[0].message, fixtureCase.message);
}

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

const importGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/check/pass/imports"]);
const importGraphBody = JSON.parse(importGraph.stdout);
assert.deepEqual(importGraphBody.imports, []);
assert.equal(importGraphBody.sourceFiles.length, 3);
assert.equal(importGraphBody.sourceMaps.length, 3);
assert(importGraphBody.sourceMaps.every((item) => item.columnUnit === "utf8-byte"));
assert.deepEqual(importGraphBody.targets.map((item) => item.name), ["cli"]);
assert.deepEqual(importGraphBody.modules.map((item) => item.name), ["math", "types", "main"]);
assert.deepEqual(importGraphBody.importEdges.map((item) => `${item.from}->${item.to}`), ["main->math", "main->types"]);
assert.deepEqual(importGraphBody.importEdges.map((item) => `${item.from}->${item.to}:${item.sourceRange.path}:${item.sourceRange.start.line}:${item.sourceRange.start.column}:${item.sourceRange.end.column}`), [
  "main->math:src/main.0:1:1:9",
  "main->types:src/main.0:3:1:10",
]);
assert.deepEqual(importGraphBody.useImports.map((item) => `${item.from}->${item.to}:${item.kind}:${item.line}:${item.column}`), [
  "main->math:package-local:1:1",
  "main->types:package-local:3:1",
]);
assert.deepEqual(importGraphBody.useImports.map((item) => item.resolvedPath), [
  "src/math.0",
  "src/types.0",
]);
assert(importGraphBody.useImports.every((item) => item.sourceRange.columnUnit === "utf8-byte"));
assert(importGraphBody.functions.some((item) => item.name === "add_one" && item.returnType === "i32"));
assert(importGraphBody.functions.some((item) => item.name === "main" && item.returnType === "Void" && item.effects.includes("world")));

const whitespaceUsePackage = `${outDir}/use-whitespace-package`;
await mkdir(`${whitespaceUsePackage}/src`, { recursive: true });
await writeZeroToml(whitespaceUsePackage, {
  package: { name: "use-whitespace-package", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  deps: {},
});
await mkdir(`${whitespaceUsePackage}/src/math`, { recursive: true });
await writeFile(`${whitespaceUsePackage}/src/math.0`, 'fn add_one(value: i32) -> i32 {\n    return value + 1\n}\n');
await writeFile(`${whitespaceUsePackage}/src/math/util.0`, 'fn add_two(value: i32) -> i32 {\n    return value + 2\n}\n');
await writeFile(`${whitespaceUsePackage}/src/types.0`, 'type Point {\n    value: i32,\n}\n');
await writeFile(`${whitespaceUsePackage}/src/main.0`, 'use math\nuse math . util\nuse   types   as   model\n\npub fn main(world: World) -> Void raises {\n    let point: Point = Point { value: add_two(40) }\n    if point.value == add_one(41) {\n        check world.out.write("whitespace imports pass\\n")\n    }\n}\n');
await importPackageGraph(whitespaceUsePackage);
const whitespaceUseCheck = await execFileAsync(zero, ["check", "--json", whitespaceUsePackage]);
assert.equal(JSON.parse(whitespaceUseCheck.stdout).ok, true);
const whitespaceUseGraph = await execFileAsync(zero, ["inspect", "--json", whitespaceUsePackage]);
const whitespaceUseGraphBody = JSON.parse(whitespaceUseGraph.stdout);
assert.deepEqual(whitespaceUseGraphBody.useImports.map((item) => `${item.from}->${item.to}:${item.kind}:${item.alias ?? "null"}:${item.sourceRange.end.column}`), [
  "main->math:package-local:null:9",
  "main->math.util:package-local:null:14",
  "main->types:package-local:model:10",
]);
assert.deepEqual(whitespaceUseGraphBody.importEdges.map((item) => `${item.from}->${item.to}`), ["main->math", "main->math.util", "main->types"]);

const packageUseGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/check/pass/package"]);
const packageUseGraphBody = JSON.parse(packageUseGraph.stdout);
assert.deepEqual(packageUseGraphBody.useImports.map((item) => `${item.from}->${item.to}:${item.kind}:${item.resolvedPath ?? "null"}`), [
  "main->std.codec:stdlib:null",
  "main->std.parse:stdlib:null",
  "main->std.time:stdlib:null",
  "main->types:package-local:src/types.0",
]);

const resourceGraph = await execFileAsync(zero, ["inspect", "--json", "examples/resource-cli"]);
const resourceGraphBody = JSON.parse(resourceGraph.stdout);
assert(resourceGraphBody.targets.some((item) => item.name === "cli" && item.kind === "exe"));
assert.deepEqual(resourceGraphBody.importEdges.map((item) => `${item.from}->${item.to}`), ["main->config", "main->payload"]);
assert(resourceGraphBody.requiresCapabilities.includes("fs"));
assert(resourceGraphBody.functions.some((item) => item.name === "outputDir" && item.effects.includes("env")));
const resourceMainFunction = resourceGraphBody.functions.find((item) => item.name === "main");
assert(resourceMainFunction.effects.includes("fs"));
assert.equal(resourceMainFunction.allocationBehavior, "no heap allocation");
assert.equal(resourceMainFunction.ownership.params[0].type, "World");

const memoryGraph = await execFileAsync(zero, ["inspect", "--json", "--target", "linux-musl-x64", "examples/memory-package"]);
const memoryGraphBody = JSON.parse(memoryGraph.stdout);
assert.deepEqual(memoryGraphBody.importEdges.map((item) => `${item.from}->${item.to}`), ["main->buffer", "main->checksum"]);
assert(memoryGraphBody.requiresCapabilities.includes("memory"));
assert(!memoryGraphBody.requiresCapabilities.includes("fs"));
assert.equal(memoryGraphBody.targetSupport.fsAvailable, true);
assert.equal(memoryGraphBody.targetSupport.requiredCapabilitySupport.status, "supported");
assert.equal(memoryGraphBody.functions.filter((item) => item.public).length, 5);
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
assert.equal(lexerTokensBody.syntax, "canonical");
assert.deepEqual(lexerTokensBody.tokens.slice(0, 4).map((token) => `${token.kind}:${token.text}`), [
  "word:use",
  "word:std",
  "symbol:.",
  "word:mem",
]);
assert.deepEqual(lexerTokensBody.tokens.filter((token) => token.kind === "number").map((token) => token.text), ["123", "0xff", "0b101", "42_u8"]);
assert.deepEqual(lexerTokensBody.tokens.filter((token) => token.kind === "string" || token.kind === "char").map((token) => `${token.kind}:${token.text}`), [
  'string:"hi"',
  "char:'x'",
]);
assert.equal(lexerTokensBody.tokens[0].line, 1);
assert.equal(lexerTokensBody.tokens[0].column, 1);
assert.equal(lexerTokensBody.tokens[0].offset, 0);
assert.equal(lexerTokensBody.tokens[0].length, 3);
assert.equal(lexerTokensBody.tokens[5].offset, 12);
assert.equal(lexerTokensBody.tokens[5].length, 1);
assert.equal(lexerTokensBody.tokens[8].kind, "word");
assert.equal(lexerTokensBody.tokens[8].text, "main");
assert.equal(lexerTokensBody.tokens[8].line, 3);
assert.equal(lexerTokensBody.tokens[8].column, 8);
assert.equal(lexerTokensBody.tokens.at(-1).kind, "eof");
assert.equal(lexerTokensBody.tokens.at(-1).length, 0);

const parseTree = await execFileAsync(zero, ["parse", "--json", "conformance/format/functions-blocks.0"]);
const parseTreeBody = JSON.parse(parseTree.stdout);
assert.equal(parseTreeBody.schemaVersion, 1);
assert.equal(parseTreeBody.root.kind, "module");
assert.equal(parseTreeBody.root.shapeCount, 0);
assert.equal(parseTreeBody.root.enumCount, 0);
assert.equal(parseTreeBody.root.choiceCount, 0);
assert.equal(parseTreeBody.root.functionCount, 2);
assert.equal(parseTreeBody.functions[0].name, "helper");
assert.equal(parseTreeBody.functions[0].paramCount, 1);
assert.deepEqual(parseTreeBody.functions[0].bodyKinds, ["if"]);
assert.equal(parseTreeBody.functions[1].name, "main");
assert.equal(parseTreeBody.functions[1].paramCount, 0);
assert.deepEqual(parseTreeBody.functions[1].bodyKinds, ["let", "while"]);

const constGraph = await execFileAsync(zero, ["inspect", "--json", "examples/const-arithmetic.0"]);
const constGraphBody = JSON.parse(constGraph.stdout);
assert(constGraphBody.consts.some((item) => item.name === "answer" && item.type === "i32"));

const genericPairGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/check/pass/generic-shape-multi.0"]);
const genericPairGraphBody = JSON.parse(genericPairGraph.stdout);
assert(genericPairGraphBody.functions.some((item) => item.name === "makePair" && item.generic === true && item.returnType === "Pair<T, U>"));
assert(genericPairGraphBody.shapes.some((item) => item.name === "Pair" && item.generic === true && item.typeParams.join(",") === "T,U"));

const staticValueGraph = await execFileAsync(zero, ["inspect", "--json", "examples/static-value-params.0"]);
const staticValueGraphBody = JSON.parse(staticValueGraph.stdout);
const fixedVecShape = staticValueGraphBody.shapes.find((item) => item.name === "FixedVec");
assert(fixedVecShape);
assert(fixedVecShape.staticParams.some((item) => item.name === "N" && item.type === "usize" && item.staticDispatch === true));
assert(staticValueGraphBody.functions.some((item) => item.name === "first" && item.staticParams.some((param) => param.name === "N")));

const fixedVecGraph = await execFileAsync(zero, ["inspect", "--json", "examples/fixed-vec.0"]);
const fixedVecGraphBody = JSON.parse(fixedVecGraph.stdout);
const fixedVecMethodsShape = fixedVecGraphBody.shapes.find((item) => item.name === "FixedVec");
assert(fixedVecMethodsShape);
const pushMethod = fixedVecMethodsShape.methods.find((item) => item.name === "push");
assert(pushMethod);
assert.equal(pushMethod.inheritedShapeParams, true);
assert.deepEqual(pushMethod.shapeTypeParams, ["T", "N"]);
assert(pushMethod.shapeStaticParams.some((item) => item.name === "N" && item.staticDispatch === true));

const aliasGraph = await execFileAsync(zero, ["inspect", "--json", "examples/type-alias.0"]);
const aliasGraphBody = JSON.parse(aliasGraph.stdout);
assert(aliasGraphBody.aliases.some((item) => item.name === "BytePair" && item.target === "Pair<u8, u8>"));

const staticMethodGraph = await execFileAsync(zero, ["inspect", "--json", "examples/static-method.0"]);
const staticMethodGraphBody = JSON.parse(staticMethodGraph.stdout);
const counterShape = staticMethodGraphBody.shapes.find((item) => item.name === "Counter");
assert(counterShape);
assert(counterShape.methods.some((item) => item.name === "add" && item.staticDispatch === true && item.returnType === "i32"));

const staticInterfaceGraph = await execFileAsync(zero, ["inspect", "--json", "examples/static-interface.0"]);
const staticInterfaceGraphBody = JSON.parse(staticInterfaceGraph.stdout);
assert(staticInterfaceGraphBody.interfaces.some((item) => item.name === "Readable" && item.staticOnly === true));
assert(staticInterfaceGraphBody.functions.some((item) => item.name === "readValue" && item.constraints.some((constraint) => constraint.interface === "Readable<T>" && constraint.staticDispatch === true)));

const callResolutionGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/check/pass/call-resolution-inspection.0"]);
const callResolutionGraphBody = JSON.parse(callResolutionGraph.stdout);
const callFacts = callResolutionGraphBody.callResolution;
assert.equal(callFacts.schemaVersion, 1);
for (const kind of ["function", "stdlib", "receiver", "shape_namespace", "constrained_interface", "concrete_constrained_shape", "choice_constructor"]) {
  assert(callFacts.supportedKinds.includes(kind));
  assert(callFacts.calls.some((item) => item.kind === kind), `missing call-resolution fact for ${kind}`);
}
assert(callFacts.calls.some((item) => item.kind === "function" && item.calleeName === "add" && item.returnType === "i32" && item.expectedArgCount === 2));
assert(callFacts.calls.some((item) => item.kind === "function" && item.calleeName === "id" && item.bindings.some((binding) => binding.name === "T" && binding.type === "i32")));
assert(callFacts.calls.some((item) => item.kind === "function" && item.calleeName === "id" && item.owner === "wrap" && item.instantiationDepth === 1 && item.instantiatedBy === "main" && item.returnType === "i32"));
assert(callFacts.calls.some((item) => item.kind === "stdlib" && item.calleeName === "std.mem.len" && item.returnType === "usize" && item.args.some((arg) => arg.actualType === "String")));
assert(callFacts.calls.some((item) => item.kind === "choice_constructor" && item.calleeName === "Event.key" && item.choice === "Event" && item.choiceCase === "key"));
assert(callFacts.calls.some((item) => item.kind === "shape_namespace" && item.calleeName === "read" && item.shape === "Counter"));
assert(callFacts.calls.some((item) => item.kind === "receiver" && item.calleeName === "bump" && item.shape === "Counter" && item.paramOffset === 1));
assert(callFacts.calls.some((item) => item.kind === "constrained_interface" && item.calleeName === "read" && item.interface === "Readable" && item.owner === "readValue"));
assert(callFacts.calls.some((item) => item.kind === "concrete_constrained_shape" && item.calleeName === "read" && item.shape === "Counter" && item.instantiatedBy === "main"));
assert(callFacts.calls.some((item) => item.calleeName === "add" && item.path === "call-resolution-inspection.0"));
assert(callFacts.calls.some((item) => item.kind === "function" && item.calleeName === "defaultCount" && item.owner === "Counter.value" && item.returnType === "i32"));
assert(callFacts.calls.some((item) => item.kind === "function" && item.calleeName === "risky" && item.fallible === true && item.errors.includes("BadInput")));
assert(callFacts.calls.some((item) => item.kind === "receiver" && item.calleeName === "checkedRead" && item.fallible === true && item.errors.includes("EmptyCounter")));

const callResolutionMemGetGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/native/pass/checked-bounds-get.0"]);
const callResolutionMemGetFacts = JSON.parse(callResolutionMemGetGraph.stdout).callResolution;
assert(callResolutionMemGetFacts.calls.some((item) => item.kind === "stdlib" && item.calleeName === "std.mem.get" && item.returnType === "Maybe<u8>" && item.args.some((arg) => arg.paramIndex === 0 && arg.actualType === "[3]u8")));

const callResolutionGenericShapeGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/check/pass/generic-shape-methods.0"]);
const callResolutionGenericShapeFacts = JSON.parse(callResolutionGenericShapeGraph.stdout).callResolution;
assert(callResolutionGenericShapeFacts.calls.some((item) =>
  item.kind === "stdlib" &&
  item.calleeName === "std.mem.get" &&
  item.owner === "FixedVec.get" &&
  item.instantiationDepth === 1 &&
  item.instantiatedBy === "main" &&
  item.returnType === "Maybe<u8>" &&
  item.args.some((arg) => arg.paramIndex === 0 && arg.actualType === "[4]u8")
));

const callResolutionFsReadGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/native/pass/std-fs-resource.0"]);
const callResolutionFsReadFacts = JSON.parse(callResolutionFsReadGraph.stdout).callResolution;
assert(callResolutionFsReadFacts.calls.some((item) => item.kind === "stdlib" && item.calleeName === "std.fs.read" && item.returnType === "Maybe<usize>" && item.args.some((arg) => arg.paramIndex === 0 && arg.expectedType === "mutref<File>" && arg.actualType === "mutref<File>")));

const callResolutionPackageGraph = await execFileAsync(zero, ["inspect", "--json", "examples/systems-package"]);
const callResolutionPackageFacts = JSON.parse(callResolutionPackageGraph.stdout).callResolution;
assert(callResolutionPackageFacts.calls.some((item) => item.calleeName === "cleanup" && item.path === "src/main.0" && item.line === 12));

const callResolutionEdgeGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/check/pass/call-resolution-edge-cases.0"]);
const callResolutionEdgeFacts = JSON.parse(callResolutionEdgeGraph.stdout).callResolution;
assert(callResolutionEdgeFacts.calls.some((item) => item.kind === "function" && item.calleeName === "add" && item.owner === "constTotal" && item.returnType === "i32"));
assert(callResolutionEdgeFacts.calls.some((item) => item.kind === "stdlib" && item.calleeName === "std.mem.len" && item.owner === "main" && item.args.some((arg) => arg.paramIndex === 0 && arg.actualType === "String")));

const programGraphBody = JSON.parse((await execFileAsync(zero, ["inspect", "--json", "examples/hello.0"])).stdout).programGraph;
const programGraphBodyAgain = JSON.parse((await execFileAsync(zero, ["inspect", "--json", "examples/hello.0"])).stdout).programGraph;
const programGraphDump = (await execFileAsync(zero, ["dump", "examples/hello.0"])).stdout;
const programGraphDumpAgain = (await execFileAsync(zero, ["dump", "examples/hello.0"])).stdout;
const programGraphDumpJson = JSON.parse((await execFileAsync(zero, ["dump", "--json", "examples/hello.0"])).stdout);
const programGraphDumpPath = `${outDir}/hello.program-graph`;
const programGraphDumpJsonPath = `${outDir}/hello.dump-json.program-graph`;
const programGraphCanonicalPath = `${outDir}/hello.canonical.program-graph`;
const programGraphViewPath = `${outDir}/hello.graph-view.0`;
const programGraphArtifactRoundtripPath = `${outDir}/hello.roundtrip.program-graph`;
const programGraphSourceFixturePath = "conformance/program-graph/hello.0";
const programGraphSourceFixturePackage = "conformance/program-graph";
const programGraphSourceFixtureStorePath = "conformance/program-graph/zero.graph";
const programGraphSourceFixtureRunPath = `${outDir}/program-graph-fixture-run`;
const programGraphSourceFreePackage = `${outDir}/program-graph-source-free`;
const programGraphSourceFreeBuildPath = `${outDir}/program-graph-source-free-build`;
const programGraphSourceFreeRunPath = `${outDir}/program-graph-source-free-run`;
const programGraphSourceFreeStdStrPackage = `${outDir}/program-graph-source-free-std-str`;
const programGraphCrmApiBuildPath = `${outDir}/program-graph-crm-api-build`;
const programGraphAuthoringPackage = `${outDir}/program-graph-authoring`;
const programGraphAuthoringRunPath = `${outDir}/program-graph-authoring-run`;
const programGraphAuthoringRunAfterHumanEditPath = `${outDir}/program-graph-authoring-run-human-edit`;
const programGraphBuilderOpsPackage = `${outDir}/program-graph-builder-ops`;
const programGraphBuilderOpsRunPath = `${outDir}/program-graph-builder-ops-run`;
const programGraphLoopTestPackage = `${outDir}/program-graph-loop-test`;
const programGraphBlockBodyPackage = `${outDir}/program-graph-block-body`;
const programGraphBlockBodyRunPath = `${outDir}/program-graph-block-body-run`;
const programGraphAuthoringCliPackage = `${outDir}/program-graph-authoring-cli`;
const programGraphAuthoringCliGraphBuildPath = `${outDir}/program-graph-authoring-cli-graph-build`;
const programGraphAuthoringCliBuildPath = `${outDir}/program-graph-authoring-cli-build`;
const programGraphAuthoringCliRunPath = `${outDir}/program-graph-authoring-cli-run`;
const programGraphAuthoringCliRunAfterHumanEditPath = `${outDir}/program-graph-authoring-cli-run-human-edit`;
const programGraphSourceFreeCImportPackage = `${outDir}/program-graph-source-free-c-import`;
const programGraphSourceFreeCImportRunPath = `${outDir}/program-graph-source-free-c-import-run`;
const programGraphSourceFreeCImportCwdPackage = `${outDir}/program-graph-source-free-c-import-cwd`;
const programGraphSourceFreeCImportCwdBuildPath = `${outDir}/program-graph-source-free-c-import-cwd-build`;
const programGraphIdentityMismatchPackage = `${outDir}/program-graph-identity-mismatch`;
const programGraphMissingPackageNamePackage = `${outDir}/program-graph-missing-package-name`;
const programGraphBadProjectionPackage = `${outDir}/program-graph-bad-projection`;
const programGraphSourceFixtureDriftPackage = `${outDir}/program-graph-fixture-drift`;
const programGraphMissingStorePackage = `${outDir}/program-graph-missing-store`;
const programGraphInvalidStorePackage = `${outDir}/program-graph-invalid-store`;
const programGraphTargetWebbitsPackage = `${outDir}/program-graph-target-webbits`;
const programGraphTargetIncompatiblePackage = `${outDir}/program-graph-target-incompatible`;
const programGraphTargetCapabilityPackage = `${outDir}/program-graph-target-capability`;
const programGraphRichPath = `${outDir}/open-ended-slices.program-graph`;
const programGraphRichViewPath = `${outDir}/open-ended-slices.graph-view.0`;
const programGraphCharPath = `${outDir}/float-char-casts.program-graph`;
const programGraphCharViewPath = `${outDir}/float-char-casts.graph-view.0`;
await rm(programGraphDumpPath, { force: true });
await rm(programGraphDumpJsonPath, { force: true });
await rm(programGraphCanonicalPath, { force: true });
await rm(programGraphViewPath, { force: true });
await rm(programGraphArtifactRoundtripPath, { force: true });
await rm(programGraphSourceFixtureRunPath, { force: true });
await rm(programGraphSourceFreePackage, { recursive: true, force: true });
await rm(programGraphSourceFreeBuildPath, { force: true });
await rm(programGraphSourceFreeRunPath, { force: true });
await rm(programGraphSourceFreeStdStrPackage, { recursive: true, force: true });
await rm(programGraphCrmApiBuildPath, { force: true });
await rm(programGraphAuthoringPackage, { recursive: true, force: true });
await rm(programGraphAuthoringRunPath, { force: true });
await rm(programGraphAuthoringRunAfterHumanEditPath, { force: true });
await rm(programGraphBuilderOpsPackage, { recursive: true, force: true });
await rm(programGraphBuilderOpsRunPath, { force: true });
await rm(programGraphLoopTestPackage, { recursive: true, force: true });
await rm(programGraphBlockBodyPackage, { recursive: true, force: true });
await rm(programGraphBlockBodyRunPath, { force: true });
await rm(programGraphAuthoringCliPackage, { recursive: true, force: true });
await rm(programGraphAuthoringCliGraphBuildPath, { force: true });
await rm(programGraphAuthoringCliBuildPath, { force: true });
await rm(programGraphAuthoringCliRunPath, { force: true });
await rm(programGraphAuthoringCliRunAfterHumanEditPath, { force: true });
await rm(programGraphSourceFreeCImportPackage, { recursive: true, force: true });
await rm(programGraphSourceFreeCImportRunPath, { force: true });
await rm(programGraphSourceFreeCImportCwdPackage, { recursive: true, force: true });
await rm(programGraphSourceFreeCImportCwdBuildPath, { force: true });
await rm(programGraphIdentityMismatchPackage, { recursive: true, force: true });
await rm(programGraphMissingPackageNamePackage, { recursive: true, force: true });
await rm(programGraphBadProjectionPackage, { recursive: true, force: true });
await rm(programGraphSourceFixtureDriftPackage, { recursive: true, force: true });
await rm(programGraphMissingStorePackage, { recursive: true, force: true });
await rm(programGraphInvalidStorePackage, { recursive: true, force: true });
await rm(programGraphTargetWebbitsPackage, { recursive: true, force: true });
await rm(programGraphTargetIncompatiblePackage, { recursive: true, force: true });
await rm(programGraphTargetCapabilityPackage, { recursive: true, force: true });
await rm(programGraphRichPath, { force: true });
await rm(programGraphRichViewPath, { force: true });
await rm(programGraphCharPath, { force: true });
await rm(programGraphCharViewPath, { force: true });
const programGraphDumpOut = await execFileAsync(zero, ["dump", "--out", programGraphDumpPath, "examples/hello.0"]);
const programGraphDumpOutJson = JSON.parse((await execFileAsync(zero, ["dump", "--json", "--out", programGraphDumpJsonPath, "examples/hello.0"])).stdout);
const programGraphDumpFile = await readFile(programGraphDumpPath, "utf8");
const programGraphDumpJsonFile = await readFile(programGraphDumpJsonPath, "utf8");
const programGraphValidate = await execFileAsync(zero, ["validate", programGraphDumpPath]);
const programGraphDumpJsonValidate = await execFileAsync(zero, ["validate", programGraphDumpJsonPath]);
const programGraphValidateJson = JSON.parse((await execFileAsync(zero, ["validate", "--json", "--out", programGraphCanonicalPath, programGraphDumpPath])).stdout);
const programGraphCanonicalFile = await readFile(programGraphCanonicalPath, "utf8");
const programGraphView = (await execFileAsync(zero, ["view", programGraphDumpPath])).stdout;
const programGraphViewAgain = (await execFileAsync(zero, ["view", programGraphDumpPath])).stdout;
const programGraphViewJson = JSON.parse((await execFileAsync(zero, ["view", "--json", programGraphDumpPath])).stdout);
const programGraphViewOut = await execFileAsync(zero, ["view", "--out", programGraphViewPath, programGraphDumpPath]);
const programGraphViewFile = await readFile(programGraphViewPath, "utf8");
const programGraphViewOutJson = JSON.parse((await execFileAsync(zero, ["view", "--json", "--out", programGraphViewPath, programGraphDumpPath])).stdout);
const programGraphRoundtrip = await execFileAsync(zero, ["roundtrip", "examples/hello.0"]);
const programGraphRoundtripJson = JSON.parse((await execFileAsync(zero, ["roundtrip", "--json", "examples/hello.0"])).stdout);
const programGraphArtifactRoundtrip = await execFileAsync(zero, ["roundtrip", programGraphDumpPath]);
const programGraphArtifactRoundtripJson = JSON.parse((await execFileAsync(zero, ["roundtrip", "--json", "--out", programGraphArtifactRoundtripPath, programGraphDumpPath])).stdout);
const programGraphSourceFixtureText = await readFile(programGraphSourceFixturePath, "utf8");
const programGraphSourceFixtureStoreBytes = await readFile(programGraphSourceFixtureStorePath);
const programGraphSourceFixturePackageCheckJson = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphSourceFixturePackage])).stdout);
const programGraphSourceFixturePackageStatusJson = JSON.parse((await execFileAsync(zero, ["status", "--json", programGraphSourceFixturePackage])).stdout);
const programGraphSourceFixturePackageRun = await execFileAsync(zero, ["run", "--out", programGraphSourceFixtureRunPath, programGraphSourceFixturePackage]);
await mkdir(programGraphSourceFreePackage, { recursive: true });
await writeFile(`${programGraphSourceFreePackage}/zero.toml`, await readFile(`${programGraphSourceFixturePackage}/zero.toml`, "utf8"));
await writeFile(`${programGraphSourceFreePackage}/zero.graph`, programGraphSourceFixtureStoreBytes);
const programGraphSourceFreeCheckJson = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphSourceFreePackage])).stdout);
const programGraphSourceFreeSizeJson = JSON.parse((await execFileAsync(zero, ["size", "--json", "--target", "linux-musl-x64", programGraphSourceFreePackage])).stdout);
const programGraphSourceFreeBuildJson = JSON.parse((await execFileAsync(zero, ["build", "--json", "--target", "linux-musl-x64", "--out", programGraphSourceFreeBuildPath, programGraphSourceFreePackage])).stdout);
const programGraphSourceFreeMappedMirCacheFiles = (await readdir(`${programGraphSourceFreePackage}/.zero/cache/native`)).filter((name) => name.startsWith("mir-") && name.endsWith(".zmir"));
const programGraphSourceFreeRun = await execFileAsync(zero, ["run", "--out", programGraphSourceFreeRunPath, programGraphSourceFreePackage]);
const programGraphSourceFreeTestJson = JSON.parse((await execFileAsync(zero, ["test", "--json", programGraphSourceFreePackage])).stdout);
const programGraphSourceFreeMemJson = JSON.parse((await execFileAsync(zero, ["mem", "--json", programGraphSourceFreePackage])).stdout);
const programGraphSourceFreeVerify = await execFileAsync(zero, ["verify-projection", "--json", programGraphSourceFreePackage]).catch((error) => error);
const programGraphSourceFreeExport = JSON.parse((await execFileAsync(zero, ["export", "--json", programGraphSourceFreePackage])).stdout);
const programGraphSourceFreeVerifyAfter = JSON.parse((await execFileAsync(zero, ["verify-projection", "--json", programGraphSourceFreePackage])).stdout);
await mkdir(programGraphSourceFreeStdStrPackage, { recursive: true });
await writeZeroToml(programGraphSourceFreeStdStrPackage, {
  package: { name: "program-graph-source-free-std-str", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
await writeFile(`${programGraphSourceFreeStdStrPackage}/main.0`, await readFile("examples/std-str.0", "utf8"));
const programGraphSourceFreeStdStrSync = JSON.parse((await execFileAsync(zero, ["import", "--json", programGraphSourceFreeStdStrPackage])).stdout);
await rm(`${programGraphSourceFreeStdStrPackage}/main.0`, { force: true });
const programGraphSourceFreeStdStrCheckJson = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphSourceFreeStdStrPackage])).stdout);
const programGraphCrmApiCheckJson = JSON.parse((await execFileAsync(zero, ["check", "--json", "examples/crm-api"])).stdout);
const programGraphCrmApiStatusJson = JSON.parse((await execFileAsync(zero, ["status", "--json", "examples/crm-api"])).stdout);
const programGraphCrmApiBuildJson = JSON.parse((await execFileAsync(zero, ["build", "--json", "--out", programGraphCrmApiBuildPath, "examples/crm-api"])).stdout);
const programGraphCrmApiHealth = await execFileAsync(programGraphCrmApiBuildPath, ["GET /health\n\n"]);
const programGraphCrmApiAccounts = await execFileAsync(programGraphCrmApiBuildPath, ["GET /crm/accounts?tenant=demo\n\n"]);
const programGraphCrmApiDealUpdate = await execFileAsync(programGraphCrmApiBuildPath, ["POST /crm/deals/42/update\ncontent-type: application/json\n\n{\"stage\":\"won\"}"]);
const programGraphCrmApiMissing = await execFileAsync(programGraphCrmApiBuildPath, ["GET /missing\n\n"]);
const programGraphAuthoringInit = JSON.parse((await execFileAsync(zero, ["init", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringProjectionExistsAfterInit = await fileExists(`${programGraphAuthoringPackage}/src/main.0`);
const programGraphAuthoringPatch = JSON.parse((await execFileAsync(zero, [
  "patch",
  "--json",
  "--op",
  "addMain",
  "--op",
  "addFunction name=\"add\" ret=\"i32\"",
  "--op",
  "addParam fn=\"add\" name=\"left\" type=\"i32\"",
  "--op",
  "addParam fn=\"add\" name=\"right\" type=\"i32\"",
  "--op",
  "addReturnBinary fn=\"add\" name=\"+\" left=\"left\" right=\"right\" type=\"i32\"",
  "--op",
  "addCheckWrite fn=\"main\" text=\"graph authoring ok\\n\"",
  "--op",
  "addTest name=\"addition works\" call=\"add\" arg0=\"40\" arg1=\"2\" expect=\"42\" type=\"i32\"",
], { cwd: programGraphAuthoringPackage })).stdout);
const programGraphAuthoringProjectionExistsAfterPatch = await fileExists(`${programGraphAuthoringPackage}/src/main.0`);
const programGraphAuthoringStatusMissing = JSON.parse((await execFileAsync(zero, ["status", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringVerifyMissing = await execFileAsync(zero, ["verify-projection", "--json", programGraphAuthoringPackage]).catch((error) => error);
const programGraphAuthoringCheck = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringRun = await execFileAsync(zero, ["run", "--out", programGraphAuthoringRunPath, programGraphAuthoringPackage]);
const programGraphAuthoringTest = JSON.parse((await execFileAsync(zero, ["test", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringExport = JSON.parse((await execFileAsync(zero, ["export", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringProjectionText = await readFile(`${programGraphAuthoringPackage}/src/main.0`, "utf8");
const programGraphAuthoringVerifyAfter = JSON.parse((await execFileAsync(zero, ["verify-projection", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringCheckAfter = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringEditedText = programGraphAuthoringProjectionText.replace("graph authoring ok", "human edit ok");
await writeFile(`${programGraphAuthoringPackage}/src/main.0`, programGraphAuthoringEditedText);
const programGraphAuthoringStatusAfterHumanEdit = JSON.parse((await execFileAsync(zero, ["status", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringImport = JSON.parse((await execFileAsync(zero, ["import", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringCheckAfterHumanEdit = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphAuthoringPackage])).stdout);
const programGraphAuthoringRunAfterHumanEdit = await execFileAsync(zero, ["run", "--out", programGraphAuthoringRunAfterHumanEditPath, programGraphAuthoringPackage]);
const programGraphBuilderOpsInit = JSON.parse((await execFileAsync(zero, ["init", "--json", programGraphBuilderOpsPackage])).stdout);
const programGraphBuilderOpsPatch = JSON.parse((await execFileAsync(zero, [
  "patch",
  "--json",
  "--op",
  "addMain",
  "--op",
  "addLetLiteral fn=\"main\" name=\"message\" type=\"String\" value=\"graph value write ok\\n\"",
  "--op",
  "addCheckWriteValue fn=\"main\" value=\"message\" type=\"String\"",
  "--op",
  "addFunction name=\"add_twice\" ret=\"u32\"",
  "--op",
  "addParam fn=\"add_twice\" name=\"x\" type=\"u32\"",
  "--op",
  "addParam fn=\"add_twice\" name=\"y\" type=\"u32\"",
  "--op",
  "addLetBinary fn=\"add_twice\" name=\"first\" type=\"u32\" operator=\"+\" left=\"x\" right=\"y\"",
  "--op",
  "addLetBinary fn=\"add_twice\" name=\"total\" type=\"u32\" operator=\"+\" left=\"first\" right=\"y\"",
  "--op",
  "addReturnValue fn=\"add_twice\" value=\"total\" type=\"u32\"",
  "--op",
  "addTest name=\"add twice\" call=\"add_twice\" arg0=\"3\" arg1=\"2\" expect=\"7\" type=\"u32\"",
], { cwd: programGraphBuilderOpsPackage })).stdout);
const programGraphBuilderOpsProjectionExistsAfterPatch = await fileExists(`${programGraphBuilderOpsPackage}/src/main.0`);
const programGraphBuilderOpsQuery = JSON.parse((await execFileAsync(zero, ["query", "--json", programGraphBuilderOpsPackage])).stdout);
const programGraphBuilderOpsView = (await execFileAsync(zero, ["view", programGraphBuilderOpsPackage])).stdout;
const programGraphBuilderOpsCheck = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphBuilderOpsPackage])).stdout);
const programGraphBuilderOpsTest = JSON.parse((await execFileAsync(zero, ["test", "--json", programGraphBuilderOpsPackage])).stdout);
const programGraphBuilderOpsRun = await execFileAsync(zero, ["run", "--out", programGraphBuilderOpsRunPath, programGraphBuilderOpsPackage]);
const programGraphBuilderOpsSync = JSON.parse((await execFileAsync(zero, ["export", "--json", programGraphBuilderOpsPackage])).stdout);
const programGraphBuilderOpsProjectionText = await readFile(`${programGraphBuilderOpsPackage}/src/main.0`, "utf8");
const programGraphLoopTestInit = JSON.parse((await execFileAsync(zero, ["init", "--json", programGraphLoopTestPackage])).stdout);
const programGraphLoopTestPatch = JSON.parse((await execFileAsync(zero, [
  "patch",
  "--json",
  "--op",
  "addMain",
  "--op",
  "addFunction name=\"count_to\" ret=\"u32\"",
  "--op",
  "addParam fn=\"count_to\" name=\"n\" type=\"u32\"",
  "--op",
  "addParam fn=\"count_to\" name=\"start\" type=\"u32\"",
  "--op",
  "addReturnBinary fn=\"count_to\" name=\"+\" left=\"n\" right=\"start\" type=\"u32\"",
], { cwd: programGraphLoopTestPackage })).stdout);
const programGraphLoopTestBodyQuery = JSON.parse((await execFileAsync(zero, ["query", "--json", programGraphLoopTestPackage])).stdout);
const programGraphLoopTestBodyPatchText = [
  "zero-program-graph-patch v1",
  `expect graphHash "${programGraphLoopTestBodyQuery.graphHash}"`,
  "replaceFunctionBody count_to",
  "  var i u32 = start",
  "  while i < n",
  "    i = i + 1",
  "  return i",
  "end",
  "",
].join("\n");
const programGraphLoopTestBodyPatch = JSON.parse((await execFileAsync(zero, ["patch", "--json", programGraphLoopTestPackage, "--patch-text", programGraphLoopTestBodyPatchText])).stdout);
const programGraphLoopTestAddTest = JSON.parse((await execFileAsync(zero, [
  "patch",
  "--json",
  "--op",
  "addTest name=\"graph while assignment\" call=\"count_to\" arg0=\"4\" arg1=\"0\" expect=\"4\" type=\"u32\"",
], { cwd: programGraphLoopTestPackage })).stdout);
const programGraphLoopTestCheck = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphLoopTestPackage])).stdout);
const programGraphLoopTestRun = JSON.parse((await execFileAsync(zero, ["test", "--json", programGraphLoopTestPackage])).stdout);
const programGraphLoopTestView = (await execFileAsync(zero, ["view", programGraphLoopTestPackage])).stdout;
const programGraphBlockBodyInit = JSON.parse((await execFileAsync(zero, ["init", "--json", programGraphBlockBodyPackage])).stdout);
const programGraphBlockBodyMainPatch = JSON.parse((await execFileAsync(zero, [
  "patch",
  "--json",
  "--op",
  "addMain",
], { cwd: programGraphBlockBodyPackage })).stdout);
const programGraphBlockBodyMainQuery = JSON.parse((await execFileAsync(zero, ["query", "--json", programGraphBlockBodyPackage])).stdout);
const programGraphBlockBodyGreetingPatchText = [
  "zero-program-graph-patch v1",
  `expect graphHash "${programGraphBlockBodyMainQuery.graphHash}"`,
  "replaceFunctionBody main",
  "  let name Maybe<String> = std.args.get 1",
  "  if name.has",
  "    check world.out.write \"hello \"",
  "    check world.out.write name.value",
  "    check world.out.write \"\\n\"",
  "  else",
  "    check world.out.write \"hello anonymous\\n\"",
  "end",
  "",
].join("\n");
const programGraphBlockBodyGreetingPatch = JSON.parse((await execFileAsync(zero, ["patch", "--json", programGraphBlockBodyPackage, "--patch-text", programGraphBlockBodyGreetingPatchText])).stdout);
const programGraphBlockBodyBlocks = JSON.parse((await execFileAsync(zero, ["query", "--json", "--find", "Block", programGraphBlockBodyPackage])).stdout);
const programGraphBlockBodyThen = programGraphBlockBodyBlocks.matches.find((node) => node.kind === "Block" && node.name === "then");
assert(programGraphBlockBodyThen, "expected row-patched greeting body to expose a then block handle");
const programGraphBlockBodyPatchText = [
  "zero-program-graph-patch v1",
  `expect graphHash "${programGraphBlockBodyBlocks.graphHash}"`,
  `replaceBlockBody ${programGraphBlockBodyThen.id}`,
  "  check world.out.write \"name + value: \"",
  "  check world.out.write name.value",
  "  check world.out.write \"\\n\"",
  "end",
  "",
].join("\n");
const programGraphBlockBodyDryRun = JSON.parse((await execFileAsync(zero, ["patch", "--json", "--check-only", programGraphBlockBodyPackage, "--patch-text", programGraphBlockBodyPatchText])).stdout);
const programGraphBlockBodyPatch = JSON.parse((await execFileAsync(zero, ["patch", "--json", programGraphBlockBodyPackage, "--patch-text", programGraphBlockBodyPatchText])).stdout);
const programGraphBlockBodyView = (await execFileAsync(zero, ["view", programGraphBlockBodyPackage])).stdout;
const programGraphBlockBodyCheck = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphBlockBodyPackage])).stdout);
const programGraphBlockBodyRun = await execFileAsync(zero, ["run", "--out", programGraphBlockBodyRunPath, programGraphBlockBodyPackage, "--", "Ada"]);
const programGraphAuthoringCliInit = JSON.parse((await execFileAsync(zero, ["init", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliProjectionExistsAfterInit = await fileExists(`${programGraphAuthoringCliPackage}/src/main.0`);
const programGraphAuthoringCliPatch = JSON.parse((await execFileAsync(zero, [
  "patch",
  "--json",
  "--op",
  "addMain",
  "--op",
  "addFunction name=\"add_u32\" ret=\"u32\"",
  "--op",
  "addParam fn=\"add_u32\" name=\"x\" type=\"u32\"",
  "--op",
  "addParam fn=\"add_u32\" name=\"y\" type=\"u32\"",
  "--op",
  "addReturnBinary fn=\"add_u32\" name=\"+\" left=\"x\" right=\"y\" type=\"u32\"",
], { cwd: programGraphAuthoringCliPackage })).stdout);
const programGraphAuthoringCliBodyQuery = JSON.parse((await execFileAsync(zero, ["query", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliBodyPatchText = [
  "zero-program-graph-patch v1",
  `expect graphHash "${programGraphAuthoringCliBodyQuery.graphHash}"`,
  "replaceFunctionBody main",
  "  let x Maybe<u32> = std.args.parseU32 1",
  "  let y Maybe<u32> = std.args.parseU32 2",
  "  if x.has && y.has",
  "    let result u32 = add_u32 x.value y.value",
  "    var out [10]u8 = repeat 0_u8 10",
  "    let text Maybe<Span<u8>> = std.fmt.u32 out result",
  "    if text.has",
  "      check world.out.write text.value",
  "      check world.out.write \"\\n\"",
  "  else",
  "    check world.err.write \"usage: zero run . -- <x> <y>\\n\"",
  "end",
  "",
].join("\n");
const programGraphAuthoringCliBodyPatch = JSON.parse((await execFileAsync(zero, ["patch", "--json", programGraphAuthoringCliPackage, "--patch-text", programGraphAuthoringCliBodyPatchText])).stdout);
const programGraphAuthoringCliProjectionExistsAfterPatch = await fileExists(`${programGraphAuthoringCliPackage}/src/main.0`);
const programGraphAuthoringCliStaleAddPatch = JSON.parse((await execFileAsync(zero, [
  "patch",
  "--json",
  "--op",
  "addFunction name=\"add\" ret=\"i32\"",
  "--op",
  "addParam fn=\"add\" name=\"x\" type=\"i32\"",
  "--op",
  "addParam fn=\"add\" name=\"y\" type=\"i32\"",
  "--op",
  "addReturnBinary fn=\"add\" name=\"+\" left=\"x\" right=\"y\" type=\"i32\"",
  "--op",
  "addTest name=\"add works\" call=\"add\" arg0=\"40\" arg1=\"2\" expect=\"42\" type=\"i32\"",
], { cwd: programGraphAuthoringCliPackage })).stdout);
const programGraphAuthoringCliFindAdd = JSON.parse((await execFileAsync(zero, ["query", "--json", "--find", "add", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliNodeAdd = JSON.parse((await execFileAsync(zero, ["query", "--json", "--node", "#fn_add", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliCleanupPatch = JSON.parse((await execFileAsync(zero, [
  "patch",
  "--json",
  "--op",
  "delete node=\"#fn___zero_test_0\"",
  "--op",
  "delete node=\"#fn_add\"",
  "--op",
  "addTest name=\"add_u32 works\" call=\"add_u32\" arg0=\"40\" arg1=\"2\" expect=\"42\" type=\"u32\"",
], { cwd: programGraphAuthoringCliPackage })).stdout);
const programGraphAuthoringCliCallsText = (await execFileAsync(zero, ["query", "--fn", "main", "--calls", "std", programGraphAuthoringCliPackage])).stdout;
const programGraphAuthoringCliRefsText = (await execFileAsync(zero, ["query", "--refs", "add_u32", programGraphAuthoringCliPackage])).stdout;
const programGraphAuthoringCliQuery = JSON.parse((await execFileAsync(zero, ["query", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliCheck = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliGraphBuild = await execFileAsync(zero, ["build", "--out", programGraphAuthoringCliGraphBuildPath, programGraphAuthoringCliPackage]);
const programGraphAuthoringCliBuild = await execFileAsync(zero, ["build", "--out", programGraphAuthoringCliBuildPath, programGraphAuthoringCliPackage]);
const programGraphAuthoringCliTest = JSON.parse((await execFileAsync(zero, ["test", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliGraphTest = await execFileAsync(zero, ["test", programGraphAuthoringCliPackage]);
const programGraphAuthoringCliRun = await execFileAsync(zero, ["run", "--out", programGraphAuthoringCliRunPath, programGraphAuthoringCliPackage, "--", "40", "2"]);
const programGraphAuthoringCliGraphRun = await execFileAsync(zero, ["run", programGraphAuthoringCliPackage, "--", "7", "8"]);
const programGraphAuthoringCliSize = JSON.parse((await execFileAsync(zero, ["size", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliSync = JSON.parse((await execFileAsync(zero, ["export", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliProjectionText = await readFile(`${programGraphAuthoringCliPackage}/src/main.0`, "utf8");
const programGraphAuthoringCliEditedProjectionText = programGraphAuthoringCliProjectionText.replace("usage: zero run . -- <x> <y>\\n", "usage: zero run . -- <left> <right>\\n");
await writeFile(`${programGraphAuthoringCliPackage}/src/main.0`, programGraphAuthoringCliEditedProjectionText);
const programGraphAuthoringCliStatusAfterHumanEdit = JSON.parse((await execFileAsync(zero, ["status", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliImport = JSON.parse((await execFileAsync(zero, ["import", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliQueryAfterHumanEdit = JSON.parse((await execFileAsync(zero, ["query", "--json", "--calls", "std", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliFindUsageText = (await execFileAsync(zero, ["query", "--find", "usage", programGraphAuthoringCliPackage])).stdout;
const programGraphAuthoringCliVerifyAfterHumanEdit = JSON.parse((await execFileAsync(zero, ["verify-projection", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliCheckAfterHumanEdit = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliTestAfterHumanEdit = JSON.parse((await execFileAsync(zero, ["test", "--json", programGraphAuthoringCliPackage])).stdout);
const programGraphAuthoringCliRunAfterHumanEdit = await execFileAsync(zero, ["run", "--out", programGraphAuthoringCliRunAfterHumanEditPath, programGraphAuthoringCliPackage, "--", "5", "6"]);
await mkdir(`${programGraphSourceFreeCImportPackage}/src`, { recursive: true });
await mkdir(`${programGraphSourceFreeCImportPackage}/vendor/include`, { recursive: true });
await writeZeroToml(programGraphSourceFreeCImportPackage, {
  package: { name: "program-graph-source-free-c-import", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  c: {
    libs: {
      ext: { headers: ["vendor/include/zero_ext.h"], include: ["vendor/include"], lib: [], link: [], mode: "static" },
    },
  },
});
await writeFile(`${programGraphSourceFreeCImportPackage}/vendor/include/zero_ext.h`, "int zero_ext_add(int a, int b);\n");
await writeFile(`${programGraphSourceFreeCImportPackage}/src/main.0`, `extern c "vendor/include/zero_ext.h" as c

pub fn main(world: World) -> Void raises {
    check world.out.write("source-free c import ok\\n")
}
`);
const programGraphSourceFreeCImportSync = JSON.parse((await execFileAsync(zero, ["import", "--json", programGraphSourceFreeCImportPackage])).stdout);
await rm(`${programGraphSourceFreeCImportPackage}/src`, { recursive: true, force: true });
const programGraphSourceFreeCImportCheck = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphSourceFreeCImportPackage])).stdout);
const programGraphSourceFreeCImportRun = await execFileAsync(zero, ["run", "--out", programGraphSourceFreeCImportRunPath, programGraphSourceFreeCImportPackage]);
await mkdir(programGraphSourceFreeCImportCwdPackage, { recursive: true });
await writeZeroToml(programGraphSourceFreeCImportCwdPackage, {
  package: { name: "program-graph-source-free-c-import-cwd", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
});
const programGraphSourceFreeCImportCwdBuild = JSON.parse((await execFileAsync(resolve(zero), [
  "build",
  "--json",
  "--out",
  resolve(programGraphSourceFreeCImportCwdBuildPath),
  resolve(programGraphSourceFreeCImportPackage),
], { cwd: programGraphSourceFreeCImportCwdPackage })).stdout);
await mkdir(programGraphIdentityMismatchPackage, { recursive: true });
await writeZeroToml(programGraphIdentityMismatchPackage, {
  package: { name: "program-graph-wrong-package", version: "9.9.9" },
  targets: { cli: { kind: "exe", main: "hello.0" } },
});
await writeFile(`${programGraphIdentityMismatchPackage}/zero.graph`, programGraphSourceFixtureStoreBytes);
const programGraphIdentityMismatchCheck = await execFileAsync(zero, ["check", "--json", programGraphIdentityMismatchPackage]).catch((error) => error);
const programGraphIdentityMismatchSize = await execFileAsync(zero, ["size", "--json", programGraphIdentityMismatchPackage]).catch((error) => error);
await mkdir(programGraphMissingPackageNamePackage, { recursive: true });
await writeZeroToml(programGraphMissingPackageNamePackage, {
  targets: { cli: { kind: "exe", main: "hello.0" } },
});
await writeFile(`${programGraphMissingPackageNamePackage}/zero.graph`, programGraphSourceFixtureStoreBytes);
const programGraphMissingPackageNameCheck = await execFileAsync(zero, ["check", "--json", programGraphMissingPackageNamePackage]).catch((error) => error);
await mkdir(programGraphBadProjectionPackage, { recursive: true });
await writeFile(`${programGraphBadProjectionPackage}/zero.toml`, await readFile(`${programGraphSourceFixturePackage}/zero.toml`, "utf8"));
await writeFile(`${programGraphBadProjectionPackage}/hello.0`, programGraphSourceFixtureText);
await execFileAsync(zero, ["import", "--format", "text", programGraphBadProjectionPackage]);
const programGraphBadProjectionStoreText = await readFile(`${programGraphBadProjectionPackage}/zero.graph`, "utf8");
await writeFile(`${programGraphBadProjectionPackage}/zero.graph`, programGraphBadProjectionStoreText.replace(
  /^projection path:"hello\.0" text:.*$/m,
  `projection path:"hello.0" text:${JSON.stringify("pub fn broken( {\n")}`,
));
const programGraphBadProjectionStatus = JSON.parse((await execFileAsync(zero, ["status", "--json", programGraphBadProjectionPackage])).stdout);
const programGraphBadProjectionCheck = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphBadProjectionPackage])).stdout);
const programGraphBadProjectionSync = await execFileAsync(zero, ["export", "--json", programGraphBadProjectionPackage]).catch((error) => error);
await mkdir(programGraphMissingStorePackage, { recursive: true });
await writeZeroToml(programGraphMissingStorePackage, {
  package: { name: "program-graph-missing-store", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
await writeFile(`${programGraphMissingStorePackage}/main.0`, "pub fn main() -> i32 { return 0 }\n");
const programGraphMissingStoreCheck = await execFileAsync(zero, ["check", "--json", programGraphMissingStorePackage]).catch((error) => error);
await mkdir(programGraphInvalidStorePackage, { recursive: true });
await writeZeroToml(programGraphInvalidStorePackage, {
  package: { name: "program-graph-invalid-store", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
await writeFile(`${programGraphInvalidStorePackage}/main.0`, "pub fn main() -> i32 { return 0 }\n");
await writeFile(`${programGraphInvalidStorePackage}/zero.graph`, "not a repository graph\n");
const programGraphInvalidStoreCheck = await execFileAsync(zero, ["check", "--json", programGraphInvalidStorePackage]).catch((error) => error);
await mkdir(programGraphSourceFixtureDriftPackage, { recursive: true });
await writeFile(`${programGraphSourceFixtureDriftPackage}/zero.toml`, await readFile(`${programGraphSourceFixturePackage}/zero.toml`, "utf8"));
await writeFile(`${programGraphSourceFixtureDriftPackage}/zero.graph`, programGraphSourceFixtureStoreBytes);
await writeFile(`${programGraphSourceFixtureDriftPackage}/hello.0`, programGraphSourceFixtureText.replace("hello from zero", "hello from drift"));
const programGraphSourceFixtureDriftCheck = JSON.parse((await execFileAsync(zero, ["check", "--json", programGraphSourceFixtureDriftPackage])).stdout);
const programGraphSourceFixtureDriftVerify = await execFileAsync(zero, ["verify-projection", "--json", programGraphSourceFixtureDriftPackage]).catch((error) => error);
await mkdir(programGraphTargetWebbitsPackage, { recursive: true });
await writeFile(`${programGraphTargetWebbitsPackage}/zero.toml`, await readFile("conformance/packages/target-webbits/zero.toml", "utf8"));
await mkdir(`${programGraphTargetIncompatiblePackage}/src`, { recursive: true });
await writeZeroToml(programGraphTargetIncompatiblePackage, {
  package: { name: "program-graph-target-incompatible", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  dependencies: { "target-webbits": { path: "../program-graph-target-webbits", version: "0.1.0", targets: ["win32-x64.exe"] } },
});
await writeFile(`${programGraphTargetIncompatiblePackage}/src/main.0`, `pub fn main(world: World) -> Void raises {
    check world.out.write("target incompatible\\n")
}
`);
const programGraphTargetIncompatibleSync = JSON.parse((await execFileAsync(zero, ["import", "--json", programGraphTargetIncompatiblePackage])).stdout);
const programGraphTargetIncompatibleCheck = await execFileAsync(zero, ["check", "--json", "--target", "linux-musl-x64", programGraphTargetIncompatiblePackage]).catch((error) => error);
await mkdir(programGraphTargetCapabilityPackage, { recursive: true });
await writeZeroToml(programGraphTargetCapabilityPackage, {
  package: { name: "program-graph-target-capability", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
await writeFile(`${programGraphTargetCapabilityPackage}/main.0`, `pub fn main(world: World) -> Void raises {
    let fs: Fs = std.fs.host()
    if std.fs.writeFile(fs, ".zero/out/program-graph-target-capability.txt", "ok\\n") {
        check world.out.write("ok\\n")
    }
}
`);
const programGraphTargetCapabilitySync = JSON.parse((await execFileAsync(zero, ["import", "--json", programGraphTargetCapabilityPackage])).stdout);
const programGraphTargetCapabilityCheck = await execFileAsync(zero, ["check", "--json", "--target", "linux-arm64", programGraphTargetCapabilityPackage]).catch((error) => error);
await execFileAsync(zero, ["dump", "--out", programGraphRichPath, "conformance/native/pass/open-ended-slices.0"]);
await execFileAsync(zero, ["view", "--out", programGraphRichViewPath, programGraphRichPath]);
const programGraphRichView = await readFile(programGraphRichViewPath, "utf8");
await execFileAsync(zero, ["dump", "--out", programGraphCharPath, "conformance/native/pass/float-char-casts.0"]);
await execFileAsync(zero, ["view", "--out", programGraphCharViewPath, programGraphCharPath]);
const programGraphCharView = await readFile(programGraphCharViewPath, "utf8");
const programGraphViewCoverage = [
  ["compile-time-v1", "examples/compile-time-v1.0", [/const field_type: String = meta fieldType\(Point, "x"\)/, /readGate<enabled, selected>\(&gate\)/]],
  ["array-repeat-literal", "conformance/native/pass/array-repeat-literal.0", [/\[7_u8; 8\]/, /\[0_u8; 16\]/]],
  ["explicit-casts", "conformance/native/pass/explicit-casts.0", [/let byte: u8 = big as u8/, /let offset: isize = small as isize/]],
  ["generic-multi-specialization", "conformance/native/pass/generic-multi-specialization.0", [/first<i32, u8>\(21, 7\)/, /second<i32, u8>\(a, 6\)/]],
  ["generic-shape-nested-defaults-alias", "conformance/native/pass/generic-shape-nested-defaults-alias.0", [/Box \{ value: 42 \}/, /Slot \{ item: 7_u8 \}/]],
  ["float-primitives", "conformance/native/pass/float-primitives.0", [/let precise: f64 = 1\.0e-3/]],
  ["meta-typed-target-type", "conformance/native/pass/meta-typed-target-type.0", [/const computed: usize = meta 2 \+ 2/]],
  ["nested-lvalues", "conformance/native/pass/nested-lvalues.0", [/Point \{ x: 3, y: 4 \}/, /Point \{ x: 5, y: 6 \}/]],
  ["radix-suffix-literals", "conformance/native/pass/radix-suffix-literals.0", [/return 0x20_usize/, /1_024_usize/, /0x2a_u8/]],
  ["test-blocks", "conformance/native/pass/test-blocks.0", [/test "addition works"/]],
  ["type-alias-basic", "conformance/native/pass/type-alias-basic.0", [/let count: ByteCount = 4_usize/]],
  ["c-abi-export", "conformance/native/pass/c-abi-export.0", [/extern type CPoint/, /export c fn zero_add\(a: i32, b: i32\) -> i32/]],
  ["const-layout", "conformance/native/pass/const-layout.0", [/extern type CPoint/, /packed type Header/]],
  ["c-header-import", "conformance/check/pass/c-header-import.0", [/extern c "conformance\/c\/simple\.h" as c/]],
  ["constructors-defaults", "conformance/native/pass/constructors-defaults.0", [/FixedVec\.init<u8, 4>/]],
  ["direct-call-add", "examples/direct-call-add.0", [/export c fn main\(a: i32, b: i32\) -> i32/]],
  ["generic-static-explicit-shadowing", "conformance/check/pass/generic-static-explicit-shadowing.0", [/Helper\.needsSame<N>\(left, right\)/]],
  ["systems-package", "examples/systems-package", [/use std\.codec/, /pub fn main\(world: World\) -> Void raises/]],
  ["std-math", "examples/std-math.0", [/pub fn main\(world: World\) -> Void raises/, /std\.math\.minU32\(8, 3\)/]],
];
for (const [name, fixture, patterns] of programGraphViewCoverage) {
  const graphPath = `${outDir}/${name}.program-graph`;
  const viewPath = `${outDir}/${name}.graph-view.0`;
  await rm(graphPath, { force: true });
  await rm(viewPath, { force: true });
  await execFileAsync(zero, ["dump", "--out", graphPath, fixture]);
  await execFileAsync(zero, ["view", "--out", viewPath, graphPath]);
  const view = await readFile(viewPath, "utf8");
  for (const pattern of patterns) assert.match(view, pattern);
  assert.doesNotMatch(view, /fn __zero_test_/);
  if (name === "systems-package") {
    assert.doesNotMatch(view, /^use (helpers|types)$/m);
    await execFileAsync(zero, ["import", "--format", "binary", "--out", graphSidecarPath(viewPath), viewPath]);
    assert.equal((await execFileAsync(zero, ["check", viewPath])).stdout, "ok\n");
  }
  if (name === "std-math") assert.doesNotMatch(view, /fn __zero_std_/);
}
for (const fixture of [
  "conformance/native/pass/open-ended-slices.0",
  "conformance/native/pass/float-char-casts.0",
  "conformance/check/pass/call-resolution-edge-cases.0",
  "conformance/native/pass/match-payload-binding.0",
  "conformance/native/pass/match-scalar-guards.0",
  "conformance/native/pass/rescue-check.0",
  "examples/result-choice.0",
  "examples/compile-time-v1.0",
  "examples/config-shape.0",
  "examples/direct-enum-match.0",
  "conformance/native/pass/test-blocks.0",
  "examples/std-math.0",
  "examples/systems-package",
  "std/math.0",
  "std/path.0",
  "std/str.0",
]) {
  const roundtrip = JSON.parse((await execFileAsync(zero, ["roundtrip", "--json", fixture])).stdout);
  assert.equal(roundtrip.ok, true);
  assert.equal(roundtrip.semanticStable, true);
  assert.equal(roundtrip.roundtripModuleIdentity, roundtrip.moduleIdentity);
  assert.equal(roundtrip.comparison.ok, true);
  assert.deepEqual(roundtrip.semanticCounts.original, roundtrip.semanticCounts.roundtrip);
}
const stdPathRecursiveView = (await execFileAsync(zero, ["view", "conformance/native/pass/std-path-io-breadth.0"])).stdout;
assert.match(stdPathRecursiveView, /std\.path\.relative\(rel_buf, "src", "src\/main\.0"\)/);
assert.doesNotMatch(stdPathRecursiveView, /__zero_std_/);
assert.equal(programGraphBody.schemaVersion, 1);
assert.equal(programGraphBody.canonicalSource, false);
assert.equal(programGraphBody.moduleIdentity, "module:hello");
assert.deepEqual(programGraphBodyAgain, programGraphBody);
assert.equal(programGraphBody.resolution.state, "resolved");
assert.equal(programGraphBody.resolution.ok, true);
assert(programGraphBody.resolution.scopes.some((scope) => scope.kind === "module" && scope.name === "hello" && scope.bindings.some((binding) => binding.kind === "function" && binding.name === "main")));
assert(programGraphBody.resolution.references.some((reference) => reference.kind === "identifier" && reference.name === "world" && reference.targetKind === "param" && reference.symbolId === "symbol:hello::value.main/param.world"));
assert(programGraphBody.resolution.references.some((reference) => reference.kind === "call" && reference.qualifiedName === "world.out.write" && reference.targetKind === "member"));
assert.equal(programGraphBody.semantics.state, "typed-facts");
assert.equal(programGraphBody.semantics.ok, true);
assert(programGraphBody.semantics.functions.some((item) => item.name === "main" && item.returnType === "Void" && item.fallible === true && item.sourceRange.path === "hello.0" && item.params.some((param) => param.name === "world" && param.type === "World" && param.sourceRange.path === "hello.0")));
assert(programGraphBody.semantics.calls.some((item) => item.qualifiedName === "world.out.write" && item.returnType === "Void" && item.fallible === true && item.checked === true && item.sourceRange.path === "hello.0" && item.contract.kind === "worldStreamWrite" && item.contract.capability === "io" && item.contract.targetSupport === "world-io" && item.contract.repair?.id === "check-fallible-call" && item.resolution.targetKind === "member" && item.resolution.symbolId === "symbol:hello::value.main/param.world"));
assert(programGraphBody.semantics.ownership.some((item) => item.name === "world" && item.ownership === "resource-handle" && item.resource === true));
assert(programGraphBody.semantics.resources.some((item) => item.kind === "capabilityUse" && item.resourceKind === "world-io" && item.qualifiedName === "world.out.write"));
assert(programGraphBody.semantics.targetRequirements.some((item) => item.qualifiedName === "world.out.write" && item.capability === "io" && item.targetSupport === "world-io"));
assert(programGraphBody.semantics.repairs.some((item) => item.qualifiedName === "world.out.write" && item.requiresCheck === false && item.repair.id === "check-fallible-call"));
assert.equal(programGraphDumpAgain, programGraphDump);
assert.equal(programGraphDumpOut.stdout, "");
assert.equal(programGraphDumpFile, programGraphDump);
assert.deepEqual(programGraphDumpOutJson, programGraphDumpJson);
assert.equal(programGraphDumpJsonFile, programGraphDump);
assert.equal(programGraphValidate.stdout, "program graph ok\n");
assert.equal(programGraphDumpJsonValidate.stdout, "program graph ok\n");
assert.equal(programGraphCanonicalFile, programGraphDump);
assert.equal(programGraphValidateJson.ok, true);
assert.equal(programGraphValidateJson.moduleIdentity, "module:hello");
assert.equal(programGraphValidateJson.graphHash, programGraphBody.graphHash);
assert.equal(programGraphValidateJson.saved.path, programGraphCanonicalPath);
assert.equal(programGraphViewAgain, programGraphView);
assert.equal(programGraphViewOut.stdout, "");
assert.equal(programGraphViewFile, programGraphView);
assert.equal(programGraphViewJson.ok, true);
assert.equal(programGraphViewJson.canonicalSource, false);
assert.equal(programGraphViewJson.moduleIdentity, "module:hello");
assert.equal(programGraphViewJson.graphHash, programGraphBody.graphHash);
assert.equal(programGraphViewJson.view, programGraphView);
assert.equal(programGraphViewOutJson.ok, true);
assert.equal(programGraphViewOutJson.saved.path, programGraphViewPath);
assert.equal(programGraphViewOutJson.view, null);
assert.equal(programGraphRoundtrip.stdout, "program graph roundtrip ok\n");
assert.equal(programGraphRoundtripJson.ok, true);
assert.equal(programGraphRoundtripJson.canonicalSource, false);
assert.equal(programGraphRoundtripJson.semanticStable, true);
assert.equal(programGraphRoundtripJson.lowering, "direct-program-graph");
assert.equal(programGraphRoundtripJson.moduleIdentity, "module:hello");
assert.equal(programGraphRoundtripJson.roundtripModuleIdentity, "module:hello");
assert.equal(programGraphRoundtripJson.originalGraphHash, programGraphBody.graphHash);
assert.equal(programGraphRoundtripJson.roundtripGraphHash, programGraphBody.graphHash);
assert.deepEqual(programGraphRoundtripJson.semanticCounts.original, programGraphRoundtripJson.semanticCounts.roundtrip);
assert.equal(programGraphRoundtripJson.comparison.ok, true);
assert.equal(programGraphRoundtripJson.view, null);
assert.equal(programGraphArtifactRoundtrip.stdout, "program graph roundtrip ok\n");
assert.equal(programGraphArtifactRoundtripJson.ok, true);
assert.equal(programGraphArtifactRoundtripJson.artifact, programGraphDumpPath);
assert.equal(programGraphArtifactRoundtripJson.semanticStable, true);
assert.equal(programGraphArtifactRoundtripJson.lowering, "direct-program-graph");
assert.equal(programGraphArtifactRoundtripJson.originalGraphHash, programGraphBody.graphHash);
assert.equal(programGraphArtifactRoundtripJson.roundtripGraphHash, programGraphBody.graphHash);
assert.equal(programGraphArtifactRoundtripJson.saved.path, programGraphArtifactRoundtripPath);
assert.equal(programGraphArtifactRoundtripJson.saved.kind, "program-graph");
assert.equal(programGraphArtifactRoundtripJson.view, null);
assert.equal(await readFile(programGraphArtifactRoundtripPath, "utf8"), programGraphDump);
assert.equal(programGraphSourceFixtureText, await readFile("examples/hello.0", "utf8"));
assert.equal(programGraphSourceFixturePackageCheckJson.ok, true);
assert.equal(programGraphSourceFixturePackageCheckJson.sourceFile, programGraphSourceFixtureStorePath);
assert.equal(programGraphSourceFixturePackageCheckJson.package.name, "program-graph-fixture");
assert.equal(programGraphSourceFixturePackageCheckJson.graph.artifact, programGraphSourceFixtureStorePath);
assert.equal(programGraphSourceFixturePackageCheckJson.graph.canonicalSource, false);
assert.equal(programGraphSourceFixturePackageCheckJson.graph.moduleIdentity, "package:program-graph-fixture@0.1.0");
assert.match(programGraphSourceFixturePackageCheckJson.graph.graphHash, /^graph:[0-9a-f]{16}$/);
assert.equal(programGraphSourceFixturePackageCheckJson.graph.lowering, "graph-native-check");
assert.equal(programGraphSourceFixturePackageStatusJson.store.encoding, "binary");
assert.equal(programGraphSourceFixturePackageStatusJson.storage.encoding, "single-file-binary");
assertRepositoryGraphNativeCheck(programGraphSourceFixturePackageCheckJson);
assert.equal(programGraphSourceFixturePackageRun.stdout, "hello from zero\n");
assert.equal(programGraphSourceFreeCheckJson.ok, true);
assert.equal(programGraphSourceFreeCheckJson.sourceFile, `${programGraphSourceFreePackage}/zero.graph`);
assert.equal(programGraphSourceFreeCheckJson.graph.artifact, `${programGraphSourceFreePackage}/zero.graph`);
assert.equal(programGraphSourceFreeCheckJson.graph.sourceProjectionState, "missing");
assertRepositoryGraphNativeCheck(programGraphSourceFreeCheckJson, "missing");
assertProgramGraphCompilerInput(programGraphSourceFreeCheckJson, `${programGraphSourceFreePackage}/zero.graph`);
assertSourceGraph(programGraphSourceFreeSizeJson, `${programGraphSourceFreePackage}/zero.graph`, "package:program-graph-fixture@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(programGraphSourceFreeSizeJson, `${programGraphSourceFreePackage}/zero.graph`);
assert.equal(programGraphSourceFreeBuildJson.sourceFile, `${programGraphSourceFreePackage}/zero.graph`);
assertSourceGraph(programGraphSourceFreeBuildJson, `${programGraphSourceFreePackage}/zero.graph`, "package:program-graph-fixture@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(programGraphSourceFreeBuildJson, `${programGraphSourceFreePackage}/zero.graph`);
assert(programGraphSourceFreeMappedMirCacheFiles.some((path) => path.endsWith(".zmir")), "repository graph build should write a mapped MIR cache");
assert.equal(programGraphSourceFreeRun.stdout, "hello from zero\n");
assert.equal(programGraphSourceFreeTestJson.ok, true);
assertSourceGraph(programGraphSourceFreeTestJson, `${programGraphSourceFreePackage}/zero.graph`, "package:program-graph-fixture@0.1.0", "direct-program-graph", false, "missing");
assert.equal(programGraphSourceFreeTestJson.testBackend, "direct-program-graph");
assert.equal(programGraphSourceFreeTestJson.testDiscovery.mode, "package-graph");
assertSourceGraph(programGraphSourceFreeMemJson, `${programGraphSourceFreePackage}/zero.graph`, "package:program-graph-fixture@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(programGraphSourceFreeMemJson, `${programGraphSourceFreePackage}/zero.graph`);
assert.notEqual(programGraphSourceFreeVerify.code, 0);
const programGraphSourceFreeVerifyBody = JSON.parse(programGraphSourceFreeVerify.stdout);
assert.equal(programGraphSourceFreeVerifyBody.diagnostics[0].code, "RGP006");
assert.equal(programGraphSourceFreeVerifyBody.diagnostics[0].actual, "missing source projection file");
assert.match(programGraphSourceFreeVerifyBody.repairCommands.join("\n"), /zero export/);
assert.equal(programGraphSourceFreeExport.ok, true);
assert.deepEqual(programGraphSourceFreeExport.changedPaths, [`${programGraphSourceFreePackage}/hello.0`]);
assert.equal(await readFile(`${programGraphSourceFreePackage}/hello.0`, "utf8"), programGraphSourceFixtureText);
assert.equal(programGraphSourceFreeVerifyAfter.ok, true);
assert.equal(programGraphSourceFreeVerifyAfter.repositoryGraph.projectionValidity, "clean");
assert.equal(programGraphSourceFreeStdStrSync.ok, true);
assert.equal(programGraphSourceFreeStdStrCheckJson.ok, true);
assertSourceGraph(programGraphSourceFreeStdStrCheckJson, `${programGraphSourceFreeStdStrPackage}/zero.graph`, "package:program-graph-source-free-std-str@0.1.0", "graph-native-check", false, "missing");
assertProgramGraphCompilerInput(programGraphSourceFreeStdStrCheckJson, `${programGraphSourceFreeStdStrPackage}/zero.graph`);
assertRepositoryGraphNativeCheck(programGraphSourceFreeStdStrCheckJson, "missing");
assert.equal(programGraphSourceFreeStdStrCheckJson.targetReadiness.ok, true);
assert.equal(programGraphSourceFreeStdStrCheckJson.targetReadiness.diagnostics.length, 0);
assert(programGraphSourceFreeStdStrCheckJson.graphCompiler.semanticFacts.calls.some((call) => call.qualifiedName === "std.str.reverse" && call.contract.kind === "stdlib" && call.resolution.targetKind === "stdlib" && call.returnType === "Maybe<Span<u8>>"));
assert.equal(programGraphCrmApiCheckJson.ok, true);
assert.equal(programGraphCrmApiStatusJson.store.encoding, "binary");
assert.equal(programGraphCrmApiStatusJson.storage.encoding, "single-file-binary");
assert.equal(programGraphCrmApiStatusJson.repositoryGraph.projectionValidity, "clean");
assert.equal(programGraphCrmApiCheckJson.sourceFile, "examples/crm-api/zero.graph");
assertSourceGraph(programGraphCrmApiCheckJson, "examples/crm-api/zero.graph", "package:crm-api@0.1.0", "graph-native-check", false, "clean");
assertRepositoryGraphNativeCheck(programGraphCrmApiCheckJson, "clean");
assert.equal(programGraphCrmApiBuildJson.sourceFile, "examples/crm-api/zero.graph");
assertSourceGraph(programGraphCrmApiBuildJson, "examples/crm-api/zero.graph", "package:crm-api@0.1.0", "mapped-final-mir", false, "clean");
assert.equal(programGraphCrmApiBuildJson.generatedCBytes, 0);
assert.equal(programGraphCrmApiBuildJson.incrementalInvalidation.sourceKind, "program-graph");
assert.equal(programGraphCrmApiBuildJson.incrementalInvalidation.graphInput.parserArtifactsInKey, false);
assert.equal(programGraphCrmApiHealth.stdout, "HTTP/1.1 200 OK\r\ncontent-type: application/json\r\nconnection: close\r\ncontent-length: 27\r\n\r\n{\"ok\":true,\"service\":\"crm\"}");
assert.match(programGraphCrmApiAccounts.stdout, /"accounts":\[/);
assert.match(programGraphCrmApiDealUpdate.stdout, /"updated":true/);
assert.match(programGraphCrmApiMissing.stdout, /^HTTP\/1\.1 404 Not Found\r\n/);
assert.equal(programGraphAuthoringInit.ok, true);
assert.equal(programGraphAuthoringInit.compilerInput, "repository-graph");
assert.equal(programGraphAuthoringInit.sourceProjection.path, "src/main.0");
assert.equal(programGraphAuthoringInit.sourceProjection.materialized, false);
assert.equal(programGraphAuthoringProjectionExistsAfterInit, false);
assert.equal(programGraphAuthoringPatch.ok, true);
assert.equal(programGraphAuthoringPatch.operationCount, 7);
assert.equal(programGraphAuthoringPatch.artifact, "./zero.graph");
assert.equal(programGraphAuthoringPatch.saved.path, "./zero.graph");
assert.equal(programGraphAuthoringProjectionExistsAfterPatch, false);
assert.equal(programGraphAuthoringStatusMissing.repositoryGraph.projectionState, "source-missing");
assert.equal(programGraphAuthoringStatusMissing.repositoryGraph.projectionValidity, "missing");
assert.notEqual(programGraphAuthoringVerifyMissing.code, 0);
const programGraphAuthoringVerifyMissingBody = JSON.parse(programGraphAuthoringVerifyMissing.stdout);
assert.equal(programGraphAuthoringVerifyMissingBody.diagnostics[0].code, "RGP006");
assert.equal(programGraphAuthoringVerifyMissingBody.diagnostics[0].actual, "missing source projection file");
assert.match(programGraphAuthoringVerifyMissingBody.repairCommands.join("\n"), /zero export/);
assert.equal(programGraphAuthoringCheck.ok, true);
assert.equal(programGraphAuthoringCheck.sourceFile, `${programGraphAuthoringPackage}/zero.graph`);
assert.equal(programGraphAuthoringCheck.graph.sourceProjectionState, "missing");
assertRepositoryGraphNativeCheck(programGraphAuthoringCheck, "missing");
assertProgramGraphCompilerInput(programGraphAuthoringCheck, `${programGraphAuthoringPackage}/zero.graph`);
assert.equal(programGraphAuthoringRun.stdout, "graph authoring ok\n");
assert.equal(programGraphAuthoringTest.ok, true);
assert.equal(programGraphAuthoringTest.testDiscovery.mode, "package-graph");
assert.equal(programGraphAuthoringTest.passedTests, 1);
assert.equal(programGraphAuthoringExport.ok, true);
assert.equal(programGraphAuthoringExport.repositoryGraph.projectionState, "clean");
assert.deepEqual(programGraphAuthoringExport.changedPaths, [`${programGraphAuthoringPackage}/src/main.0`]);
assert.match(programGraphAuthoringProjectionText, /pub fn main\(world: World\) -> Void raises \{/);
assert.match(programGraphAuthoringProjectionText, /fn add\(left: i32, right: i32\) -> i32/);
assert.match(programGraphAuthoringProjectionText, /test "addition works"/);
assert.equal(programGraphAuthoringVerifyAfter.ok, true);
assert.equal(programGraphAuthoringVerifyAfter.repositoryGraph.projectionState, "clean");
assert.equal(programGraphAuthoringCheckAfter.ok, true);
assert.equal(programGraphAuthoringCheckAfter.graph.sourceProjectionState, "clean");
assert.equal(programGraphAuthoringStatusAfterHumanEdit.repositoryGraph.projectionState, "source-stale");
assert.equal(programGraphAuthoringImport.ok, true);
assert.equal(programGraphAuthoringImport.repositoryGraph.projectionState, "clean");
assert.deepEqual(programGraphAuthoringImport.changedPaths, [`${programGraphAuthoringPackage}/zero.graph`]);
assert.equal(programGraphAuthoringCheckAfterHumanEdit.ok, true);
assert.equal(programGraphAuthoringCheckAfterHumanEdit.graph.sourceProjectionState, "clean");
assert.equal(programGraphAuthoringRunAfterHumanEdit.stdout, "human edit ok\n");
assert.equal(programGraphBuilderOpsInit.ok, true);
assert.equal(programGraphBuilderOpsInit.sourceProjection.materialized, false);
assert.equal(programGraphBuilderOpsPatch.ok, true);
assert.equal(programGraphBuilderOpsPatch.operationCount, 10);
assert.equal(programGraphBuilderOpsProjectionExistsAfterPatch, false);
assert.equal(programGraphBuilderOpsQuery.ok, true);
assert(programGraphBuilderOpsQuery.functions.some((fun) => fun.name === "add_twice" && fun.returnType === "u32"));
assert(programGraphBuilderOpsQuery.patchOperations.includes("addLetLiteral fn=\"main\" name=\"count\" type=\"u32\" value=\"0\""));
assert(programGraphBuilderOpsQuery.patchOperations.includes("addLetBinary fn=\"add\" name=\"sum\" type=\"i32\" operator=\"+\" left=\"left\" right=\"right\""));
assert(programGraphBuilderOpsQuery.patchOperations.includes("addReturnValue fn=\"identity\" value=\"input\" type=\"i32\""));
assert(programGraphBuilderOpsQuery.patchOperations.includes("addCheckWriteValue fn=\"main\" value=\"message\" type=\"String\""));
assert.match(programGraphBuilderOpsView, /let message: String = "graph value write ok\\n"/);
assert.match(programGraphBuilderOpsView, /check world\.out\.write\(message\)/);
assert.match(programGraphBuilderOpsView, /let first: u32 = x \+ y/);
assert.match(programGraphBuilderOpsView, /let total: u32 = first \+ y/);
assert.match(programGraphBuilderOpsView, /return total/);
assert.equal(programGraphBuilderOpsCheck.ok, true);
assert.equal(programGraphBuilderOpsCheck.graph.sourceProjectionState, "missing");
assertRepositoryGraphNativeCheck(programGraphBuilderOpsCheck, "missing");
assert.equal(programGraphBuilderOpsTest.ok, true);
assert.equal(programGraphBuilderOpsTest.passedTests, 1);
assert.equal(programGraphBuilderOpsRun.stdout, "graph value write ok\n");
assert.equal(programGraphBuilderOpsSync.ok, true);
assert.deepEqual(programGraphBuilderOpsSync.changedPaths, [`${programGraphBuilderOpsPackage}/src/main.0`]);
assert.equal(programGraphBuilderOpsProjectionText, programGraphBuilderOpsView);
assert.equal(programGraphLoopTestInit.ok, true);
assert.equal(programGraphLoopTestPatch.ok, true);
assert.equal(programGraphLoopTestBodyPatch.ok, true);
assert.equal(programGraphLoopTestBodyPatch.operations[0].op, "replaceFunctionBody");
assert.equal(programGraphLoopTestAddTest.ok, true);
assert.equal(programGraphLoopTestCheck.ok, true);
assert.equal(programGraphLoopTestCheck.sourceFile, `${programGraphLoopTestPackage}/zero.graph`);
assertRepositoryGraphNativeCheck(programGraphLoopTestCheck, "missing");
assert.equal(programGraphLoopTestRun.ok, true);
assert.equal(programGraphLoopTestRun.testBackend, "direct-program-graph");
assert.equal(programGraphLoopTestRun.passedTests, 1);
assert.match(programGraphLoopTestView, /while i < n \{/);
assert.match(programGraphLoopTestView, /i = i \+ 1/);
assert.equal(programGraphBlockBodyInit.ok, true);
assert.equal(programGraphBlockBodyMainPatch.ok, true);
assert.equal(programGraphBlockBodyMainPatch.operationCount, 1);
assert.equal(programGraphBlockBodyGreetingPatch.ok, true);
assert.equal(programGraphBlockBodyGreetingPatch.operationCount, 1);
assert.equal(programGraphBlockBodyGreetingPatch.operations[0].op, "replaceFunctionBody");
assert.equal(programGraphBlockBodyBlocks.ok, true);
assert(programGraphBlockBodyBlocks.patchOperations.some((op) => op.startsWith("replaceBlockBody #block_id\n")));
assert.equal(programGraphBlockBodyDryRun.ok, true);
assert.equal(programGraphBlockBodyDryRun.checkOnly, true);
assert.equal(programGraphBlockBodyDryRun.saved, null);
assert.equal(programGraphBlockBodyPatch.ok, true);
assert.equal(programGraphBlockBodyPatch.operationCount, 1);
assert.equal(programGraphBlockBodyPatch.operations[0].op, "replaceBlockBody");
assert.match(programGraphBlockBodyView, /if name\.has \{/);
assert.match(programGraphBlockBodyView, /check world\.out\.write\("name \+ value: "\)/);
assert.match(programGraphBlockBodyView, /check world\.out\.write\("hello anonymous\\n"\)/);
assert.equal(programGraphBlockBodyCheck.ok, true);
assert.equal(programGraphBlockBodyCheck.graph.sourceProjectionState, "missing");
assertRepositoryGraphNativeCheck(programGraphBlockBodyCheck, "missing");
assert.equal(programGraphBlockBodyRun.stdout, "name + value: Ada\n");
assert.equal(programGraphAuthoringCliInit.ok, true);
assert.equal(programGraphAuthoringCliInit.sourceProjection.materialized, false);
assert.equal(programGraphAuthoringCliPatch.ok, true);
assert.equal(programGraphAuthoringCliPatch.operationCount, 5);
assert.equal(programGraphAuthoringCliBodyPatch.ok, true);
assert.equal(programGraphAuthoringCliBodyPatch.operationCount, 1);
assert.equal(programGraphAuthoringCliBodyPatch.operations[0].op, "replaceFunctionBody");
assert.equal(programGraphAuthoringCliProjectionExistsAfterInit, false);
assert.equal(programGraphAuthoringCliProjectionExistsAfterPatch, false);
assert.equal(programGraphAuthoringCliStaleAddPatch.ok, true);
assert.equal(programGraphAuthoringCliStaleAddPatch.operationCount, 5);
assert.equal(programGraphAuthoringCliFindAdd.ok, true);
assert(programGraphAuthoringCliFindAdd.matches.some((node) => node.id === "#fn_add" && node.kind === "Function"));
assert(programGraphAuthoringCliFindAdd.matches.some((node) => node.id === "#fn___zero_test_0" && node.kind === "Function" && node.value === "add works"));
assert.equal(programGraphAuthoringCliNodeAdd.ok, true);
assert.equal(programGraphAuthoringCliNodeAdd.query.node, "#fn_add");
assert.equal(programGraphAuthoringCliNodeAdd.node.selected.id, "#fn_add");
assert(programGraphAuthoringCliNodeAdd.node.parents.some((edge) => edge.kind === "function" && edge.from === "#mod_main"));
assert(programGraphAuthoringCliNodeAdd.node.children.some((edge) => edge.kind === "body" && edge.to === "#block_add_body"));
assert(programGraphAuthoringCliNodeAdd.node.children.some((edge) => edge.kind === "param" && edge.to === "#param_add_x"));
assert.equal(programGraphAuthoringCliCleanupPatch.ok, true);
assert.equal(programGraphAuthoringCliCleanupPatch.operationCount, 3);
assert.match(programGraphAuthoringCliCallsText, /query: fn:main calls:std/);
assert.match(programGraphAuthoringCliCallsText, /qualified:std\.args\.parseU32/);
assert.match(programGraphAuthoringCliCallsText, /qualified:std\.fmt\.u32/);
assert.match(programGraphAuthoringCliCallsText, /resolved:true/);
assert.match(programGraphAuthoringCliRefsText, /query: refs:add_u32/);
assert.match(programGraphAuthoringCliRefsText, /target:function node:#fn_add_u32/);
assert.match(programGraphAuthoringCliRefsText, /fn:main name:add_u32/);
assert.equal(programGraphAuthoringCliQuery.ok, true);
assert.equal(programGraphAuthoringCliQuery.inputKind, "repository-graph");
assert(programGraphAuthoringCliQuery.functions.some((fun) => fun.name === "main" && fun.public && fun.fallible));
assert(programGraphAuthoringCliQuery.functions.some((fun) => fun.name === "add_u32" && fun.returnType === "u32" && fun.params.length === 2));
assert(!programGraphAuthoringCliQuery.functions.some((fun) => fun.name === "add"));
assert(programGraphAuthoringCliQuery.functions.some((fun) => fun.test === "add_u32 works"));
assert(!programGraphAuthoringCliQuery.patchOperations.some((op) => op.includes("setMain")));
assert(programGraphAuthoringCliQuery.patchOperations.some((op) => op.startsWith("replaceFunctionBody main\n")));
assert.equal(programGraphAuthoringCliCheck.ok, true);
assert.equal(programGraphAuthoringCliCheck.sourceFile, `${programGraphAuthoringCliPackage}/zero.graph`);
assert.equal(programGraphAuthoringCliCheck.graph.sourceProjectionState, "missing");
assertRepositoryGraphNativeCheck(programGraphAuthoringCliCheck, "missing");
assert.match(programGraphAuthoringCliGraphBuild.stdout, /program-graph-authoring-cli-graph-build/);
assert.match(programGraphAuthoringCliBuild.stdout, /program-graph-authoring-cli-build/);
assert.equal(programGraphAuthoringCliTest.ok, true);
assert.equal(programGraphAuthoringCliTest.passedTests, 1);
assert.equal(programGraphAuthoringCliGraphTest.stdout, "1 test(s) ok\n");
assert.equal(programGraphAuthoringCliRun.stdout, "42\n");
assert.equal(programGraphAuthoringCliGraphRun.stdout, "15\n");
assertSourceGraph(programGraphAuthoringCliSize, `${programGraphAuthoringCliPackage}/zero.graph`, "package:program-graph-authoring-cli@0.1.0", "mapped-final-mir", false, "missing");
assert(programGraphAuthoringCliSize.sizeBreakdown.stdlibHelpers.some((helper) => helper.name === "std.args.parseU32"));
assert(programGraphAuthoringCliSize.sizeBreakdown.stdlibHelpers.some((helper) => helper.name === "std.fmt.u32"));
assert.equal(programGraphAuthoringCliSync.ok, true);
assert.deepEqual(programGraphAuthoringCliSync.changedPaths, [`${programGraphAuthoringCliPackage}/src/main.0`]);
assert.match(programGraphAuthoringCliProjectionText, /fn add_u32\(x: u32, y: u32\) -> u32/);
assert.match(programGraphAuthoringCliProjectionText, /test "add_u32 works"/);
assert.doesNotMatch(programGraphAuthoringCliProjectionText, /fn add\(x: i32, y: i32\) -> i32/);
assert.equal(programGraphAuthoringCliStatusAfterHumanEdit.repositoryGraph.projectionState, "source-stale");
assert.equal(programGraphAuthoringCliImport.ok, true);
assert.deepEqual(programGraphAuthoringCliImport.changedPaths, [`${programGraphAuthoringCliPackage}/zero.graph`]);
assert.equal(programGraphAuthoringCliImport.store.nodes, 96);
assert.equal(programGraphAuthoringCliImport.store.sources, 1);
assert.equal(programGraphAuthoringCliQueryAfterHumanEdit.ok, true);
assert.equal(programGraphAuthoringCliQueryAfterHumanEdit.counts.nodes, 96);
assert.deepEqual(programGraphAuthoringCliQueryAfterHumanEdit.modules.map((module) => module.name), ["main"]);
assert(programGraphAuthoringCliQueryAfterHumanEdit.calls.some((call) => call.qualifiedName === "std.args.parseU32" && call.targetKind === "stdlib"));
assert(programGraphAuthoringCliQueryAfterHumanEdit.calls.some((call) => call.qualifiedName === "std.fmt.u32" && call.targetKind === "stdlib"));
assert(!programGraphAuthoringCliQueryAfterHumanEdit.calls.some((call) => call.targetKind === "graphBackedStdlib"));
assert.match(programGraphAuthoringCliFindUsageText, /value:usage: zero run \. -- <left> <right>\\n path:/);
assert.equal(programGraphAuthoringCliVerifyAfterHumanEdit.ok, true);
assert.equal(programGraphAuthoringCliVerifyAfterHumanEdit.repositoryGraph.projectionState, "clean");
assert.equal(programGraphAuthoringCliCheckAfterHumanEdit.ok, true);
assert.equal(programGraphAuthoringCliCheckAfterHumanEdit.sourceFile, `${programGraphAuthoringCliPackage}/zero.graph`);
assertRepositoryGraphNativeCheck(programGraphAuthoringCliCheckAfterHumanEdit, "clean");
assert.equal(programGraphAuthoringCliTestAfterHumanEdit.ok, true);
assert.equal(programGraphAuthoringCliTestAfterHumanEdit.passedTests, 1);
assert.equal(programGraphAuthoringCliRunAfterHumanEdit.stdout, "11\n");
assert.equal(programGraphSourceFreeCImportSync.ok, true);
assert.equal(programGraphSourceFreeCImportSync.repositoryGraph.projectionValidity, "clean");
assert.equal(programGraphSourceFreeCImportCheck.ok, true);
assert.equal(programGraphSourceFreeCImportCheck.graph.sourceProjectionState, "missing");
assertRepositoryGraphNativeCheck(programGraphSourceFreeCImportCheck, "missing");
assert.equal(programGraphSourceFreeCImportRun.stdout, "source-free c import ok\n");
assert.equal(programGraphSourceFreeCImportCwdBuild.sourceFile, resolve(`${programGraphSourceFreeCImportPackage}/zero.graph`));
assert.equal(programGraphSourceFreeCImportCwdBuild.graph.artifact, resolve(`${programGraphSourceFreeCImportPackage}/zero.graph`));
assert.equal(programGraphSourceFreeCImportCwdBuild.graph.sourceProjectionState, "missing");
assert.notEqual(programGraphIdentityMismatchCheck.code, 0);
const programGraphIdentityMismatchCheckBody = JSON.parse(programGraphIdentityMismatchCheck.stdout);
assert.equal(programGraphIdentityMismatchCheckBody.diagnostics[0].code, "RGP007");
assert.equal(programGraphIdentityMismatchCheckBody.diagnostics[0].expected, "package:program-graph-wrong-package@9.9.9");
assert.equal(programGraphIdentityMismatchCheckBody.diagnostics[0].actual, "package:program-graph-fixture@0.1.0");
assert.notEqual(programGraphIdentityMismatchSize.code, 0);
assert.equal(JSON.parse(programGraphIdentityMismatchSize.stdout).diagnostics[0].code, "RGP007");
assert.notEqual(programGraphMissingPackageNameCheck.code, 0);
const programGraphMissingPackageNameBody = JSON.parse(programGraphMissingPackageNameCheck.stdout);
assert.equal(programGraphMissingPackageNameBody.diagnostics[0].code, "RGP007");
assert.equal(programGraphMissingPackageNameBody.diagnostics[0].message, "repository graph compiler input requires package.name");
assert.match(programGraphMissingPackageNameBody.diagnostics[0].actual, /package:program-graph-fixture@0\.1\.0/);
assert.equal(programGraphBadProjectionStatus.repositoryGraph.projectionState, "conflict");
assert.equal(programGraphBadProjectionStatus.repositoryGraph.projectionValidity, "conflict");
assert.equal(programGraphBadProjectionCheck.ok, true);
assert.equal(programGraphBadProjectionCheck.graph.sourceProjectionState, "conflict");
assert.notEqual(programGraphBadProjectionSync.code, 0);
assert.equal(JSON.parse(programGraphBadProjectionSync.stdout).diagnostics[0].code, "RGP004");
assert.notEqual(programGraphMissingStoreCheck.code, 0);
const programGraphMissingStoreBody = JSON.parse(programGraphMissingStoreCheck.stdout);
assert.equal(programGraphMissingStoreBody.ok, false);
assert.equal(programGraphMissingStoreBody.mode, "compiler-input");
assert.equal(programGraphMissingStoreBody.repositoryGraph.storePresent, false);
assert.equal(programGraphMissingStoreBody.diagnostics[0].code, "RGP001");
assert.equal(programGraphMissingStoreBody.diagnostics[0].path, programGraphMissingStorePackage);
assert.match(programGraphMissingStoreBody.repairCommands.join("\n"), /zero import/);
assert.notEqual(programGraphInvalidStoreCheck.code, 0);
const programGraphInvalidStoreBody = JSON.parse(programGraphInvalidStoreCheck.stdout);
assert.equal(programGraphInvalidStoreBody.ok, false);
assert.equal(programGraphInvalidStoreBody.mode, "compiler-input");
assert.equal(programGraphInvalidStoreBody.repositoryGraph.storePresent, true);
assert.equal(programGraphInvalidStoreBody.repositoryGraph.storeValid, false);
assert.equal(programGraphInvalidStoreBody.diagnostics[0].code, "RGP003");
assert.equal(programGraphInvalidStoreBody.diagnostics[0].path, programGraphInvalidStorePackage);
assert.match(programGraphInvalidStoreBody.repairCommands.join("\n"), /zero import/);
assert.equal(programGraphSourceFixtureDriftCheck.ok, true);
assert.equal(programGraphSourceFixtureDriftCheck.graph.lowering, "graph-native-check");
assert.equal(programGraphSourceFixtureDriftCheck.graph.sourceProjectionState, "stale");
assertRepositoryGraphNativeCheck(programGraphSourceFixtureDriftCheck, "stale");
assert.notEqual(programGraphSourceFixtureDriftVerify.code, 0);
const programGraphSourceFixtureDriftBody = JSON.parse(programGraphSourceFixtureDriftVerify.stdout);
assert.equal(programGraphSourceFixtureDriftBody.ok, false);
assert.equal(programGraphSourceFixtureDriftBody.mode, "verify-projection");
assert.equal(programGraphSourceFixtureDriftBody.repositoryGraph.compilerInput, "repository-graph");
assert.equal(programGraphSourceFixtureDriftBody.diagnostics[0].code, "RGP006");
assert.match(programGraphSourceFixtureDriftBody.repairCommands.join("\n"), /zero import/);
assert.match(programGraphSourceFixtureDriftBody.repairCommands.join("\n"), /zero export/);
assert.equal(programGraphTargetIncompatibleSync.ok, true);
assert.notEqual(programGraphTargetIncompatibleCheck.code, 0);
const programGraphTargetIncompatibleBody = JSON.parse(programGraphTargetIncompatibleCheck.stdout);
assert.equal(programGraphTargetIncompatibleBody.ok, false);
assert.equal(programGraphTargetIncompatibleBody.diagnostics[0].code, "PKG004");
assert.match(programGraphTargetIncompatibleBody.diagnostics[0].actual, /target-webbits targets/);
assert.equal(programGraphTargetCapabilitySync.ok, true);
assert.notEqual(programGraphTargetCapabilityCheck.code, 0);
const programGraphTargetCapabilityBody = JSON.parse(programGraphTargetCapabilityCheck.stdout);
assert.equal(programGraphTargetCapabilityBody.ok, false);
assert.equal(programGraphTargetCapabilityBody.diagnostics[0].code, "TAR002");
assert.match(programGraphTargetCapabilityBody.diagnostics[0].actual, /lacks Fs/);
const programGraphBackendMismatchCheck = JSON.parse((await execFileAsync(zero, ["check", "--json", "--backend", "zero-coff-x64", "--target", "linux-musl-x64", programGraphSourceFixturePackage])).stdout);
assert.equal(programGraphBackendMismatchCheck.ok, true);
assert.equal(programGraphBackendMismatchCheck.targetReadiness.ok, false);
assert.equal(programGraphBackendMismatchCheck.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(programGraphBackendMismatchCheck.targetReadiness.diagnostics[0].backendBlocker.backend, "zero-coff-x64");
const programGraphRepositoryStatus = JSON.parse((await execFileAsync(zero, ["status", "--json", "--target", "linux-musl-x64", programGraphSourceFixturePackage])).stdout);
assert.equal(programGraphRepositoryStatus.repositoryGraph.storePresent, true);
assert.equal(programGraphRepositoryStatus.repositoryGraph.storeValid, true);
assert.equal(programGraphRepositoryStatus.repositoryGraph.projectionState, "clean");
assert.equal(programGraphRepositoryStatus.repositoryGraph.compilerInput, "repository-graph");
const programGraphRepositoryVerify = JSON.parse((await execFileAsync(zero, ["verify-projection", "--json", "--target", "linux-musl-x64", programGraphSourceFixturePackage])).stdout);
assert.equal(programGraphRepositoryVerify.ok, true);
assert.equal(programGraphRepositoryVerify.writes, false);
assert.deepEqual(programGraphDumpJson, programGraphBody);
assert.match(programGraphDump, /^zero-graph v1\n/);
assert.match(programGraphDump, /origin source-text/);
assert.match(programGraphDump, /module "hello"/);
assert.match(programGraphDump, /hash "graph:[0-9a-f]{16}"/);
assert.doesNotMatch(programGraphDump, /validation "shape-valid" ok/);
assert.doesNotMatch(programGraphDump, /idStrategy/);
assert.doesNotMatch(programGraphDump, /canonicalSource/);
assert.doesNotMatch(programGraphDump, /moduleIdentity/);
assert.doesNotMatch(programGraphDump, /graphHash/);
assert.doesNotMatch(programGraphDump, /counts nodes=/);
assert.doesNotMatch(programGraphDump, /node id=/);
assert.match(programGraphDump, /node #mod_[0-9a-f]{8} Module name:"hello"/);
assert.match(programGraphDump, /edge #mod_[0-9a-f]{8} function #decl_[0-9a-f]{8} order:0/);
assert.match(programGraphView, /^pub fn main\(world: World\) -> Void raises \{\n/);
assert.match(programGraphView, /check world\.out\.write\("hello from zero\\n"\)/);
assert.match(programGraphRichView, /bytesTail\(bytesSpan\)\[1\]/);
assert.match(programGraphRichView, /numbers\[2\.\.\]/);
assert.match(programGraphCharView, /again == 'A'/);
assert.match(programGraphBody.graphHash, /^graph:[0-9a-f]{16}$/);
assert.equal(programGraphBody.validation.ok, true);
assert.equal(programGraphBody.validation.state, "shape-valid");
assert(programGraphBody.counts.nodes > 0);
assert(programGraphBody.counts.edges > 0);
assert(programGraphBody.nodes.every((item) => /^#[a-z][a-z0-9]*_[0-9a-f]{8}(-[0-9a-f]{4}(-[0-9]+)?)?$/.test(item.id) && /^nodehash:[0-9a-f]{16}$/.test(item.nodeHash)));
assert(programGraphBody.edges.every((item) => item.target === "node"));
assert(programGraphBody.nodes.some((item) => item.kind === "Module" && item.name === "hello" && item.symbolId === "symbol:hello::module"));
assert(programGraphBody.nodes.some((item) => item.kind === "Function" && item.name === "main" && item.public === true && item.fallible === true && item.symbolId === "symbol:hello::value.main" && /^type:[0-9a-f]{16}$/.test(item.typeId)));
assert(programGraphBody.nodes.some((item) => item.kind === "Param" && item.name === "world" && item.type === "World" && item.symbolId === "symbol:hello::value.main/param.world"));
assert(programGraphBody.nodes.some((item) => item.kind === "EffectRef" && item.name === "error" && /^effect:[0-9a-f]{16}$/.test(item.effectId)));
assert(programGraphBody.nodes.some((item) => item.kind === "Check"));
assert(programGraphBody.nodes.some((item) => item.kind === "MethodCall"));
assert(programGraphBody.edges.some((item) => item.kind === "body"));
const programGraphWrongSchemaPath = `${outDir}/wrong-schema.program-graph`;
await writeFile(programGraphWrongSchemaPath, "zero-graph v2\n");
const programGraphWrongSchema = await execFileAsync(zero, ["validate", "--json", programGraphWrongSchemaPath]).catch((error) => error);
assert(programGraphWrongSchema.code);
assert.equal(JSON.parse(programGraphWrongSchema.stdout).diagnostics[0].message, "unknown program graph schema version");
const programGraphFailedArtifactPath = `${outDir}/failed-validation.program-graph`;
await writeFile(programGraphFailedArtifactPath, [
  "zero-graph v1",
  "origin source-text",
  "module \"main\"",
  "hash \"\"",
  "validation \"decoded\" failed",
  "diagnostic code:\"GRF001\" message:\"program graph construction failed\"",
  "",
].join("\n"));
const programGraphFailedArtifact = await execFileAsync(zero, ["validate", "--json", programGraphFailedArtifactPath]).catch((error) => error);
assert(programGraphFailedArtifact.code);
assert.equal(JSON.parse(programGraphFailedArtifact.stdout).diagnostics[0].message, "program graph input reports failed validation");
const programGraphTrailingArtifactPath = `${outDir}/trailing-content.program-graph`;
await writeFile(programGraphTrailingArtifactPath, `${programGraphDump}\nextra\n`);
const programGraphTrailingArtifact = await execFileAsync(zero, ["validate", "--json", programGraphTrailingArtifactPath]).catch((error) => error);
assert(programGraphTrailingArtifact.code);
assert.equal(JSON.parse(programGraphTrailingArtifact.stdout).diagnostics[0].message, "unexpected content after graph header");
assert(programGraphBody.edges.some((item) => item.kind === "statement" && item.order === 0));

const programGraphDuplicateIdFixture = `${outDir}/program-graph-duplicate-id-stress.0`;
const programGraphDuplicateIdPath = `${outDir}/program-graph-duplicate-id-stress.program-graph`;
await writeFile(programGraphDuplicateIdFixture, [
  "pub fn main() -> Void {",
  ...Array.from({ length: 700 }, (_, index) => `    let x${index}: i32 = 1`),
  "}",
  "",
].join("\n"));
const programGraphDuplicateIdDump = await execFileAsync(zero, ["import", "--format", "text", "--out", programGraphDuplicateIdPath, programGraphDuplicateIdFixture]);
assert.equal(programGraphDuplicateIdDump.stdout, "");
assert.equal((await execFileAsync(zero, ["validate", programGraphDuplicateIdPath])).stdout, "program graph ok\n");
const programGraphDuplicateIdText = await readFile(programGraphDuplicateIdPath, "utf8");
const programGraphDuplicateIds = [...programGraphDuplicateIdText.matchAll(/^node (#[^ ]+)/gm)].map((match) => match[1]);
assert.equal(new Set(programGraphDuplicateIds).size, programGraphDuplicateIds.length);
assert(programGraphDuplicateIds.some((id) => /^#[a-z][a-z0-9]*_[0-9a-f]{8}-[0-9a-f]{4}(-[0-9]+)?$/.test(id)));

const programGraphControlFixture = `${outDir}/program-graph-control.0`;
const programGraphControlPath = await writeGraphFixture(programGraphControlFixture, "pub fn main(world: World) -> Void raises {\n    check world.out.write(\"\\x01 ok\\n\")\n}\n");
const programGraphControl = JSON.parse((await execFileAsync(zero, ["inspect", "--json", programGraphControlPath])).stdout).programGraph;
assert(programGraphControl.nodes.some((item) => item.kind === "Literal" && item.value === "\u0001 ok\n"));

const programGraphMatchRanges = JSON.parse((await execFileAsync(zero, ["inspect", "--json", "conformance/native/pass/match-scalar-guards.0"])).stdout).programGraph;
const programGraphRangeArm = programGraphMatchRanges.nodes.find((item) => item.kind === "MatchArm" && item.name === "1");
assert(programGraphRangeArm);
const programGraphRangeEdge = programGraphMatchRanges.edges.find((item) => item.from === programGraphRangeArm.id && item.kind === "rangeEnd");
assert(programGraphRangeEdge);
assert(programGraphMatchRanges.nodes.some((item) => item.id === programGraphRangeEdge.to && item.kind === "Literal" && item.value === "3"));

const programGraphShapeDefaults = JSON.parse((await execFileAsync(zero, ["inspect", "--json", "conformance/check/pass/shape-field-defaults.0"])).stdout).programGraph;
const programGraphDefaultField = programGraphShapeDefaults.nodes.find((item) => item.kind === "Field" && item.name === "left");
assert(programGraphDefaultField);
const programGraphDefaultEdge = programGraphShapeDefaults.edges.find((item) => item.from === programGraphDefaultField.id && item.kind === "default");
assert(programGraphDefaultEdge);
assert(programGraphShapeDefaults.nodes.some((item) => item.id === programGraphDefaultEdge.to && item.kind === "Literal" && item.value === "1"));

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
assert(memorySizeBody.runtimeShims.some((shim) => shim.name === "stdio-world" && shim.payAsUsed === true));

const genericPairSize = await execFileAsync(zero, ["size", "--json", "examples/generic-pair.0"]);
const genericPairSizeBody = JSON.parse(genericPairSize.stdout);
assert.equal(genericPairSizeBody.graph.artifact, "examples/generic-pair.graph");
assert.equal(genericPairSizeBody.graph.lowering, "mapped-final-mir");
assert.equal(genericPairSizeBody.cBridgeFallback, false);
assert(genericPairSizeBody.loweredIrBytes > 0);

const genericInferredSize = await execFileAsync(zero, ["size", "--json", "conformance/native/pass/generic-inferred-specialized-call.0"]);
const genericInferredSizeBody = JSON.parse(genericInferredSize.stdout);
assert.equal(genericInferredSizeBody.graph.artifact, "conformance/native/pass/generic-inferred-specialized-call.graph");
assert.equal(genericInferredSizeBody.graph.lowering, "mapped-final-mir");
assert.equal(genericInferredSizeBody.cBridgeFallback, false);
assert(genericInferredSizeBody.loweredIrBytes > 0);

const genericStaticForwardedSize = await execFileAsync(zero, ["size", "--json", "conformance/native/pass/generic-static-forwarded-array-specialization.0"]);
const genericStaticForwardedSizeBody = JSON.parse(genericStaticForwardedSize.stdout);
assert.equal(genericStaticForwardedSizeBody.graph.artifact, "conformance/native/pass/generic-static-forwarded-array-specialization.graph");
assert.equal(genericStaticForwardedSizeBody.graph.lowering, "mapped-final-mir");
assert.equal(genericStaticForwardedSizeBody.cBridgeFallback, false);
assert(genericStaticForwardedSizeBody.loweredIrBytes > 0);

const targetsJson = await execFileAsync(zero, ["targets"]);
const targetsBody = JSON.parse(targetsJson.stdout);
const linuxMuslTarget = targetsBody.targets.find((item) => item.name === "linux-musl-x64");
const windowsMsvcTarget = targetsBody.targets.find((item) => item.aliases.includes("x86_64-windows-msvc"));
const linuxGnuTarget = targetsBody.targets.find((item) => item.name === "linux-x64");
const darwinArm64Target = targetsBody.targets.find((item) => item.name === "darwin-arm64");
const darwinX64Target = targetsBody.targets.find((item) => item.name === "darwin-x64");
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
assert.equal(linuxGnuTarget.directBackend.objectEmitter, "zero-elf64");
assert.equal(darwinArm64Target.directBackend.objectEmitter, "zero-macho64");
assert.equal(darwinArm64Target.directBackend.exeSupported, true);
assert.equal(darwinArm64Target.directBackend.exeEmitter, "zero-macho64-exe");
assert.equal(darwinX64Target.directBackend.objectEmitter, "zero-macho-x64");
assert.equal(darwinX64Target.directBackend.exeSupported, true);
assert.equal(darwinX64Target.directBackend.exeEmitter, "zero-macho-x64-exe");
assert.equal(darwinArm64Target.httpRuntime.provider, targetsBody.host === "darwin-arm64" ? "curl" : null);
assert.equal(darwinArm64Target.httpRuntime.tlsVerification, targetsBody.host === "darwin-arm64");
if (targetsBody.host === "darwin-arm64") {
  assert.equal(darwinArm64Target.httpRuntime.customCa.env, "ZERO_HTTP_TEST_CA_BUNDLE");
}
assert.equal(linuxArm64Target.directBackend.status, "native-exe");
assert.equal(linuxArm64Target.directBackend.objectEmitter, "zero-elf-aarch64");
assert.equal(linuxArm64Target.directBackend.exeEmitter, "zero-elf-aarch64-exe");
assert.match(linuxArm64Target.directBackend.reason, /direct object and executable backend available/);

const zlsSelfTest = await execFileAsync("node", [
  "--experimental-strip-types",
  "--disable-warning=ExperimentalWarning",
  "scripts/zls.mts",
  "--self-test",
]);
assert.match(zlsSelfTest.stdout, /zls self-test ok/);

const cHeaderGraph = await execFileAsync(zero, ["inspect", "--json", "conformance/check/pass/c-header-import.0"]);
const cHeaderGraphBody = JSON.parse(cHeaderGraph.stdout);
assert(cHeaderGraphBody.cImports.some((item) => item.header === "conformance/c/simple.h" && item.imports.functions >= 1 && item.cacheKey));
const simpleHeaderImport = cHeaderGraphBody.cImports.find((item) => item.header === "conformance/c/simple.h");
assert(simpleHeaderImport.typedModel.functions.some((item) => item.name === "zero_c_add" && item.params.length === 2));
assert(simpleHeaderImport.typedModel.constants.some((item) => item.name === "ZERO_C_ANSWER" && item.value === "42"));
assert(simpleHeaderImport.typedModel.structs.some((item) => item.name === "zero_c_point" && item.fields.some((field) => field.name === "y")));
assert(simpleHeaderImport.typedModel.enums.some((item) => item.name === "zero_c_color" && item.cases.some((entry) => entry.name === "ZERO_C_BLUE")));
assert(simpleHeaderImport.typedModel.typedefs.some((item) => item.name === "zero_c_int" && item.target === "int"));
assert.equal(typeof simpleHeaderImport.cache.target, "string");

const cImportTargetLinux = await execFileAsync(zero, ["check", "--json", "--emit", "obj", "--target", "linux-musl-x64", "conformance/check/pass/c-import-target-linux.0"]);
const cImportTargetLinuxBody = JSON.parse(cImportTargetLinux.stdout);
assert.equal(cImportTargetLinuxBody.ok, true);
assert.equal(cImportTargetLinuxBody.targetReadiness.ok, true);
assert.equal(cImportTargetLinuxBody.targetReadiness.buildable, true);
const cImportTargetWin = await execFileAsync(zero, ["check", "--json", "--emit", "obj", "--target", "win32-x64.exe", "conformance/check/pass/c-import-target-win.0"]);
const cImportTargetWinBody = JSON.parse(cImportTargetWin.stdout);
assert.equal(cImportTargetWinBody.ok, true);
assert.equal(cImportTargetWinBody.targetReadiness.ok, true);
assert.equal(cImportTargetWinBody.targetReadiness.buildable, true);
const cImportTargetLinuxGraph = await execFileAsync(zero, ["inspect", "--json", "--target", "linux-musl-x64", "conformance/check/pass/c-import-target-linux.0"]);
const cImportTargetLinuxModel = JSON.parse(cImportTargetLinuxGraph.stdout).cImports.find((item) => item.header === "conformance/c/target-conditional.h").typedModel;
assert(cImportTargetLinuxModel.functions.some((item) => item.name === "zero_c_linux_add"));
assert(cImportTargetLinuxModel.functions.some((item) => item.name === "zero_c_not_windows"));
assert(!cImportTargetLinuxModel.functions.some((item) => item.name === "zero_c_windows_add"));
const cImportTargetWinGraph = await execFileAsync(zero, ["inspect", "--json", "--target", "win32-x64.exe", "conformance/check/pass/c-import-target-win.0"]);
const cImportTargetWinModel = JSON.parse(cImportTargetWinGraph.stdout).cImports.find((item) => item.header === "conformance/c/target-conditional.h").typedModel;
assert(cImportTargetWinModel.functions.some((item) => item.name === "zero_c_windows_add"));
assert(!cImportTargetWinModel.functions.some((item) => item.name === "zero_c_linux_add"));
assert(!cImportTargetWinModel.functions.some((item) => item.name === "zero_c_not_windows"));

const cImportTypeShadowReadiness = await execFileAsync(zero, ["check", "--json", "--emit", "obj", "conformance/native/pass/c-import-type-shadowing.0"]);
const cImportTypeShadowReadinessBody = JSON.parse(cImportTypeShadowReadiness.stdout);
assert.equal(cImportTypeShadowReadinessBody.ok, true);
assert.equal(cImportTypeShadowReadinessBody.targetReadiness.buildable, false);
assert.equal(cImportTypeShadowReadinessBody.targetReadiness.diagnostics[0].backendBlocker.unsupportedFeature, "ref<Self>");
const cImportLaterLocalReadiness = await execFileAsync(zero, ["check", "--json", "--emit", "obj", "conformance/native/pass/c-import-alias-later-local.0"]);
const cImportLaterLocalReadinessBody = JSON.parse(cImportLaterLocalReadiness.stdout);
assert.equal(cImportLaterLocalReadinessBody.ok, true);
assert.equal(cImportLaterLocalReadinessBody.targetReadiness.ok, true);
assert.equal(cImportLaterLocalReadinessBody.targetReadiness.buildable, true);
assert.equal(cImportLaterLocalReadinessBody.targetReadiness.diagnostics.length, 0);
const cImportMissingLinkReadiness = await execFileAsync(zero, ["check", "--json", "conformance/native/pass/c-import-alias-later-local.0"]);
const cImportMissingLinkReadinessBody = JSON.parse(cImportMissingLinkReadiness.stdout);
assert.equal(cImportMissingLinkReadinessBody.ok, true);
assert.equal(cImportMissingLinkReadinessBody.targetReadiness.ok, false);
assert.equal(cImportMissingLinkReadinessBody.targetReadiness.buildable, false);
assert.equal(cImportMissingLinkReadinessBody.targetReadiness.diagnostics[0].code, "CIMP005");
const cImportMissingLinkBuild = await execFileAsync(zero, ["build", "--json", "conformance/native/pass/c-import-alias-later-local.0", "--out", `.zero/out/c-import-missing-link-${process.pid}`]).catch((error) => error);
assert.notEqual(cImportMissingLinkBuild.code, 0);
assert.equal(JSON.parse(cImportMissingLinkBuild.stdout).diagnostics[0].code, "CIMP005");

const cImportPartialLinkRoot = `/tmp/zero-c-import-partial-link-${process.pid}`;
await rm(cImportPartialLinkRoot, { recursive: true, force: true });
await mkdir(`${cImportPartialLinkRoot}/src`, { recursive: true });
await mkdir(`${cImportPartialLinkRoot}/vendor/include`, { recursive: true });
await writeFile(`${cImportPartialLinkRoot}/vendor/include/a.h`, "int zero_a_add(int left, int right);\n");
await writeFile(`${cImportPartialLinkRoot}/vendor/include/b.h`, "int zero_b_add(int left, int right);\n");
await writeZeroToml(cImportPartialLinkRoot, {
  package: { name: "c-import-partial-link", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  c: { libs: {
    a: { headers: ["vendor/include/a.h"], include: ["vendor/include"], lib: ["vendor/lib/a.o"], link: [], mode: "static" },
    b: { headers: ["vendor/include/b.h"], include: ["vendor/include"], lib: [], link: [], mode: "static" }
  } }
});
await writeFile(`${cImportPartialLinkRoot}/src/main.0`, `extern c "vendor/include/a.h" as a
extern c "vendor/include/b.h" as b

export c fn main() -> i32 {
    return a.zero_a_add(1, 2) + b.zero_b_add(3, 4)
}
`);
await importPackageGraph(cImportPartialLinkRoot);
const cImportPartialLinkReadiness = await execFileAsync(zero, ["check", "--json", cImportPartialLinkRoot]);
const cImportPartialLinkReadinessBody = JSON.parse(cImportPartialLinkReadiness.stdout);
assert.equal(cImportPartialLinkReadinessBody.ok, true);
assert.equal(cImportPartialLinkReadinessBody.targetReadiness.ok, false);
assert.equal(cImportPartialLinkReadinessBody.targetReadiness.buildable, false);
assert.equal(cImportPartialLinkReadinessBody.targetReadiness.diagnostics[0].code, "CIMP005");
assert.match(cImportPartialLinkReadinessBody.targetReadiness.diagnostics[0].actual, /b\.h/);
const cImportPartialLinkBuild = await execFileAsync(zero, ["build", "--json", cImportPartialLinkRoot, "--out", `${cImportPartialLinkRoot}/partial-link`]).catch((error) => error);
assert.notEqual(cImportPartialLinkBuild.code, 0);
const cImportPartialLinkBuildBody = JSON.parse(cImportPartialLinkBuild.stdout);
assert.equal(cImportPartialLinkBuildBody.diagnostics[0].code, "CIMP005");
assert.match(cImportPartialLinkBuildBody.diagnostics[0].actual, /b\.h/);
await rm(cImportPartialLinkRoot, { recursive: true, force: true });

const externCallRoot = `/tmp/zero-extern-c-call-${process.pid}`;
const externCallShadowRoot = `/tmp/zero-extern-c-shadow-${process.pid}`;
const externCallScalarRoot = `/tmp/zero-extern-c-scalar-${process.pid}`;
await rm(externCallRoot, { recursive: true, force: true });
await rm(externCallShadowRoot, { recursive: true, force: true });
await rm(externCallScalarRoot, { recursive: true, force: true });
await mkdir(`${externCallRoot}/src`, { recursive: true });
await mkdir(`${externCallRoot}/vendor/include`, { recursive: true });
await mkdir(`${externCallRoot}/vendor/lib`, { recursive: true });
await mkdir(`${externCallShadowRoot}/vendor/include`, { recursive: true });
await mkdir(`${externCallScalarRoot}/src`, { recursive: true });
const externCallObjectRel = "vendor/lib/zero ext's.o";
const externCallDirtyObjectRel = "vendor/lib/zero_ext_dirty.o";
const externCallSymbolPrefix = process.platform === "darwin" ? "_" : "";
let externCallDirtyAsm = "";
if (process.arch === "x64") {
  externCallDirtyAsm = `.text
.globl ${externCallSymbolPrefix}zero_ext_dirty_u8
${externCallSymbolPrefix}zero_ext_dirty_u8:
    movq $257, %rax
    ret
`;
} else if (process.arch === "arm64") {
  externCallDirtyAsm = `.text
.globl ${externCallSymbolPrefix}zero_ext_dirty_u8
${externCallSymbolPrefix}zero_ext_dirty_u8:
    movz x0, #257
    ret
`;
}
await writeFile(`${externCallRoot}/vendor/include/zero_ext.h`, `#ifdef __cplusplus
extern "C" {
#endif

int
zero_ext_add(
    int left,
    int right
);
// int zero_ext_commented(int value);
/*
int zero_ext_block_commented(int value);
*/
#if 0
int zero_ext_disabled(int value);
#endif
int zero_ext_inline_left(int value); int zero_ext_inline_right(int value);
unsigned char zero_ext_dirty_u8(void);

#ifdef __cplusplus
}
#endif
`);
await writeFile(`${externCallShadowRoot}/vendor/include/zero_ext.h`, "int zero_ext_wrong(int, int);\n");
await writeFile(`${externCallRoot}/vendor/lib/zero_ext.c`, '#include "zero_ext.h"\nint zero_ext_add(int a, int b) { return a + b; }\nint zero_ext_inline_left(int value) { return value + 1; }\nint zero_ext_inline_right(int value) { return value + 2; }\n');
await execFileAsync("cc", ["-I", `${externCallRoot}/vendor/include`, "-c", `${externCallRoot}/vendor/lib/zero_ext.c`, "-o", `${externCallRoot}/${externCallObjectRel}`]);
if (externCallDirtyAsm) {
  await writeFile(`${externCallRoot}/vendor/lib/zero_ext_dirty.S`, externCallDirtyAsm);
  await execFileAsync("cc", ["-c", `${externCallRoot}/vendor/lib/zero_ext_dirty.S`, "-o", `${externCallRoot}/${externCallDirtyObjectRel}`]);
}
await writeZeroToml(externCallRoot, {
  package: { name: "extern-c-call", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  c: { libs: {
    ext: { headers: ["vendor/include/zero_ext.h"], include: ["vendor/include"], lib: [externCallObjectRel, ...(externCallDirtyAsm ? [externCallDirtyObjectRel] : [])], link: [], mode: "static" },
    unused: { headers: ["vendor/include/unused.h"], include: ["vendor/include"], lib: ["vendor/lib/missing-unused.o"], link: [], mode: "static" }
  } }
});
await writeZeroToml(externCallScalarRoot, {
  package: { name: "extern-c-scalar", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  c: { libs: { ext: { headers: [`${externCallRoot}/vendor/include/zero_ext.h`], include: [`${externCallRoot}/vendor/include`], lib: [`${externCallRoot}/${externCallObjectRel}`], link: [], mode: "static" } } }
});
await writeFile(`${externCallRoot}/src/main.0`, `extern c "vendor/include/zero_ext.h" as c

pub fn main(world: World) -> Void raises {
    let total: i32 = c.zero_ext_add(20, 20) + c.zero_ext_inline_right(0)
    if total == 42${externCallDirtyAsm ? " && c.zero_ext_dirty_u8() == 1_u8" : ""} {
        check world.out.write("extern c call ok\\n")
    } else {
        check world.out.write("extern c call failed\\n")
    }
}
`);
await writeFile(`${externCallRoot}/src/disabled.0`, `extern c "vendor/include/zero_ext.h" as c

pub fn main() -> i32 {
    return c.zero_ext_disabled(1)
}
`);
await writeFile(`${externCallRoot}/src/commented.0`, `extern c "vendor/include/zero_ext.h" as c

pub fn main() -> i32 {
    return c.zero_ext_commented(1)
}
`);
await writeFile(`${externCallScalarRoot}/src/main.0`, `extern c "${externCallRoot}/vendor/include/zero_ext.h" as c

export c fn main() -> i32 {
    return c.zero_ext_add(20, 22)
}
`);
await importPackageGraph(externCallRoot);
await importPackageGraph(externCallScalarRoot);
const externCallShadowCheck = await execFileAsync(zero, ["check", "--json", externCallRoot], { cwd: externCallShadowRoot });
const externCallShadowCheckBody = JSON.parse(externCallShadowCheck.stdout);
assert.equal(externCallShadowCheckBody.ok, true);
const externCallDisabledCheck = await execFileAsync(zero, ["import", "--json", "--format", "binary", "--out", "src/disabled.graph", "src/disabled.0"], { cwd: externCallRoot }).catch((error) => error);
assert.notEqual(externCallDisabledCheck.code, 0);
const externCallDisabledCheckBody = JSON.parse(externCallDisabledCheck.stdout);
assert.equal(externCallDisabledCheckBody.diagnostics[0].code, "CIMP004");
const externCallCommentedCheck = await execFileAsync(zero, ["import", "--json", "--format", "binary", "--out", "src/commented.graph", "src/commented.0"], { cwd: externCallRoot }).catch((error) => error);
assert.notEqual(externCallCommentedCheck.code, 0);
const externCallCommentedCheckBody = JSON.parse(externCallCommentedCheck.stdout);
assert.equal(externCallCommentedCheckBody.diagnostics[0].code, "CIMP004");
assert.match(externCallCommentedCheckBody.diagnostics[0].message, /not declared/);
const externCallScalarCrossReadiness = await execFileAsync(zero, ["check", "--json", "--target", "linux-musl-arm64", externCallScalarRoot]);
const externCallScalarCrossReadinessBody = JSON.parse(externCallScalarCrossReadiness.stdout);
assert.equal(externCallScalarCrossReadinessBody.ok, true);
assert.equal(externCallScalarCrossReadinessBody.targetReadiness.ok, true);
assert.equal(externCallScalarCrossReadinessBody.targetReadiness.buildable, true);
assert.equal(externCallScalarCrossReadinessBody.targetReadiness.diagnostics.length, 0);
const externCallGraph = await execFileAsync(zero, ["inspect", "--json", externCallRoot]);
const externCallGraphBody = JSON.parse(externCallGraph.stdout);
const externCallImport = externCallGraphBody.cImports.find((item) => item.header === "vendor/include/zero_ext.h");
assert(externCallImport);
assert.equal(externCallImport.alias, "c");
assert.match(externCallImport.cache.headerHash, /^[0-9a-f]{16}$/);
assert(Array.isArray(externCallImport.typedModel.functions));
assert(!externCallImport.typedModel.functions.some((item) => item.name === "zero_ext_commented"));
assert(!externCallImport.typedModel.functions.some((item) => item.name === "zero_ext_block_commented"));
assert(!externCallImport.typedModel.functions.some((item) => item.name === "zero_ext_disabled"));
const externCallGraphSizeArtifact = `${externCallRoot}/extern-call-size-metadata.json`;
const externCallGraphSize = await execFileAsync(zero, ["size", "--json", "--out", externCallGraphSizeArtifact, externCallRoot]);
assert.equal(JSON.parse(externCallGraphSize.stdout).graph.moduleIdentity, "package:extern-c-call@0.1.0");
const externCallBuildOut = `${externCallRoot}/extern-call`;
const externCallBuild = await execFileAsync(zero, ["build", "--json", externCallRoot, "--out", externCallBuildOut]);
const externCallBuildBody = JSON.parse(externCallBuild.stdout);
assert.equal(externCallBuildBody.emit, "exe");
assert.notEqual(externCallBuildBody.objectBackend.objectEmission.path, "none");
assert.notEqual(externCallBuildBody.objectBackend.linking.externalToolchain, "none");
assert.match(externCallBuildBody.objectBackend.linking.targetLibraries, /zero-runtime/);
assert.match(externCallBuildBody.objectBackend.linking.targetLibraries, /c-imports/);
assert(externCallBuildBody.objectBackend.linkerPlan.staticLibraries.some((item) => item.endsWith(externCallObjectRel)));
if (externCallDirtyAsm) assert(externCallBuildBody.objectBackend.linkerPlan.staticLibraries.some((item) => item.endsWith(externCallDirtyObjectRel)));
assert(!externCallBuildBody.objectBackend.linkerPlan.staticLibraries.some((item) => item.endsWith("vendor/lib/missing-unused.o")));
const externCallObjectOverhead = externCallBuildBody.objectBackend.linking.objectFormat === "coff"
  ? (externCallBuildBody.objectBackend.objectEmission.dataSections ? 2 : 1)
  : (externCallBuildBody.objectBackend.objectEmission.dataSections ? 1 : 0);
const externCallExpectedExternalSymbols = externCallDirtyAsm ? 3 : 2;
assert.equal(
  externCallBuildBody.objectBackend.objectEmission.symbolCount,
  externCallBuildBody.objectBackend.directFacts.functionCount +
    externCallBuildBody.objectBackend.directFacts.runtimeHelperCount +
    externCallExpectedExternalSymbols +
    externCallObjectOverhead
);
assert.equal(externCallBuildBody.releaseTargetContract.selectedEmitter, externCallBuildBody.releaseTargetContract.directObjectEmitter);
assert.equal(externCallBuildBody.releaseTargetContract.libc.artifactMode, externCallBuildBody.releaseTargetContract.libc.targetMode);
const externCallRun = await execFileAsync(zero, ["run", externCallRoot]);
assert.equal(externCallRun.stdout, "extern c call ok\n");
const externCallScalarBuildOut = `${externCallScalarRoot}/extern-scalar`;
const externCallScalarBuild = await execFileAsync(zero, ["build", "--json", externCallScalarRoot, "--out", externCallScalarBuildOut]);
const externCallScalarBuildBody = JSON.parse(externCallScalarBuild.stdout);
assert.equal(externCallScalarBuildBody.emit, "exe");
assert.match(externCallScalarBuildBody.objectBackend.linking.targetLibraries, /c-imports/);
assert.doesNotMatch(externCallScalarBuildBody.objectBackend.linking.targetLibraries, /zero-runtime/);
assert.notEqual(externCallScalarBuildBody.objectBackend.linking.externalToolchain, "none");
assert(!externCallScalarBuildBody.objectBackend.linkerPlan.staticLibraries.some((item) => item === "zero_runtime.o"));
assert(externCallScalarBuildBody.objectBackend.linkerPlan.staticLibraries.some((item) => item.endsWith(externCallObjectRel)));
assert.equal(externCallScalarBuildBody.releaseTargetContract.selectedEmitter, externCallScalarBuildBody.releaseTargetContract.directObjectEmitter);
assert.equal(externCallScalarBuildBody.releaseTargetContract.libc.artifactMode, externCallScalarBuildBody.releaseTargetContract.libc.targetMode);
await rm(externCallRoot, { recursive: true, force: true });
await rm(externCallShadowRoot, { recursive: true, force: true });
await rm(externCallScalarRoot, { recursive: true, force: true });

const unsafeExternLinkRoot = `/tmp/zero-extern-c-unsafe-link-${process.pid}`;
await rm(unsafeExternLinkRoot, { recursive: true, force: true });
await mkdir(`${unsafeExternLinkRoot}/src`, { recursive: true });
await mkdir(`${unsafeExternLinkRoot}/vendor/include`, { recursive: true });
await writeFile(`${unsafeExternLinkRoot}/vendor/include/zero_ext.h`, "int zero_ext_add(int left, int right);\n");
await writeZeroToml(unsafeExternLinkRoot, {
  package: { name: "extern-c-unsafe-link", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  c: { libs: { ext: { headers: ["vendor/include/zero_ext.h"], include: ["vendor/include"], lib: [], link: ["zero_ext;touch"], mode: "static" } } }
});
await writeFile(`${unsafeExternLinkRoot}/src/main.0`, `extern c "vendor/include/zero_ext.h" as c

pub fn main() -> i32 {
    return c.zero_ext_add(20, 22)
}
`);
await importPackageGraph(unsafeExternLinkRoot);
const unsafeExternLinkReadiness = await execFileAsync(zero, ["check", "--json", unsafeExternLinkRoot]);
const unsafeExternLinkReadinessBody = JSON.parse(unsafeExternLinkReadiness.stdout);
assert.equal(unsafeExternLinkReadinessBody.ok, true);
assert.equal(unsafeExternLinkReadinessBody.targetReadiness.buildable, false);
assert.equal(unsafeExternLinkReadinessBody.targetReadiness.diagnostics[0].code, "CIMP005");
assert.match(unsafeExternLinkReadinessBody.targetReadiness.diagnostics[0].actual, /unsafe library name/);
const unsafeExternLinkBuild = await execFileAsync(zero, ["build", "--json", unsafeExternLinkRoot, "--out", `${unsafeExternLinkRoot}/extern-link`]).catch((error) => error);
assert.notEqual(unsafeExternLinkBuild.code, 0);
assert.equal(JSON.parse(unsafeExternLinkBuild.stdout).diagnostics[0].code, "CIMP005");
await rm(unsafeExternLinkRoot, { recursive: true, force: true });

const runtimeManifestRoot = `/tmp/zero-runtime-manifest-link-${process.pid}`;
await rm(runtimeManifestRoot, { recursive: true, force: true });
await mkdir(`${runtimeManifestRoot}/src`, { recursive: true });
await writeZeroToml(runtimeManifestRoot, {
  package: { name: "runtime-manifest-link", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  c: { libs: { unused: { headers: ["vendor/include/unused.h"], include: ["vendor/include"], lib: ["vendor/lib/missing.o"], link: ["zero_missing_system"], mode: "static" } } }
});
await writeFile(`${runtimeManifestRoot}/src/main.0`, `pub fn main(world: World) -> Void raises {
    check world.out.write("runtime manifest ignored\\n")
}
`);
await importPackageGraph(runtimeManifestRoot);
const runtimeManifestBuildOut = `${runtimeManifestRoot}/runtime-manifest`;
const runtimeManifestBuild = await execFileAsync(zero, ["build", "--json", runtimeManifestRoot, "--out", runtimeManifestBuildOut]);
const runtimeManifestBuildBody = JSON.parse(runtimeManifestBuild.stdout);
assert(!runtimeManifestBuildBody.objectBackend.linkerPlan.staticLibraries.some((item) => item.endsWith("vendor/lib/missing.o")));
assert(!runtimeManifestBuildBody.objectBackend.linkerPlan.systemLibraries.includes("zero_missing_system"));
const runtimeManifestRun = await execFileAsync(zero, ["run", runtimeManifestRoot]);
assert.equal(runtimeManifestRun.stdout, "runtime manifest ignored\n");
await rm(runtimeManifestRoot, { recursive: true, force: true });

const cInteropGraph = await execFileAsync(zero, ["inspect", "--json", "examples/c-interop"]);
const cInteropGraphBody = JSON.parse(cInteropGraph.stdout);
const mathLib = cInteropGraphBody.cLibraries.find((item) => item.name === "math");
assert(mathLib);
assert.equal(mathLib.linkMode, "static");
assert.equal(mathLib.targetValidation.vendoredLibraries, true);
assert.equal(mathLib.targetValidation.vendoredHeaders, true);
assert.equal(mathLib.targetValidation.pkgConfigTargetSafe, true);
const cInteropCrossGraph = await execFileAsync(zero, ["inspect", "--json", "--target", "linux-musl-x64", "examples/c-interop"]);
const cInteropCrossGraphBody = JSON.parse(cInteropCrossGraph.stdout);
assert.equal(cInteropCrossGraphBody.cLibraries[0].targetValidation.pkgConfigTargetSafe, true);
assert.equal(cInteropCrossGraphBody.cLibraries[0].targetValidation.implicitHostDiscovery, false);
assert.equal(cInteropCrossGraphBody.cLibraries[0].linkPlan.hostDiscovery, "none");
const hostLeakGraph = await execFileAsync(zero, ["inspect", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package"]);
const hostLeakGraphBody = JSON.parse(hostLeakGraph.stdout);
assert.equal(hostLeakGraphBody.cLibraries[0].targetValidation.hostHeaderLeakage, true);
assert.equal(hostLeakGraphBody.cLibraries[0].targetValidation.implicitHostDiscovery, true);
assert.equal(hostLeakGraphBody.cLibraries[0].targetValidation.status, "blocked");
const hostLeakReadiness = await execFileAsync(zero, ["check", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package"]);
const hostLeakReadinessBody = JSON.parse(hostLeakReadiness.stdout);
assert.equal(hostLeakReadinessBody.ok, true);
assert.equal(hostLeakReadinessBody.diagnostics.length, 0);
assert.equal(hostLeakReadinessBody.targetReadiness.ok, false);
assert.equal(hostLeakReadinessBody.targetReadiness.buildable, false);
assert.equal(hostLeakReadinessBody.targetReadiness.diagnostics[0].code, "CIMP003");
assert.match(hostLeakReadinessBody.targetReadiness.diagnostics[0].help, /target sysroot|vendored/);
const hostLeakBuild = await execFileAsync(zero, ["build", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package", "--out", ".zero/out/host-leak-package"], { encoding: "utf8" }).catch((error) => error);
assert.notEqual(hostLeakBuild.code, 0);
const hostLeakBuildBody = JSON.parse(hostLeakBuild.stdout);
assert.equal(hostLeakBuildBody.diagnostics[0].code, "CIMP003");
assert.match(hostLeakBuildBody.diagnostics[0].help, /target sysroot|vendored/);

const depGraph = await execFileAsync(zero, ["inspect", "--json", "--target", "linux-musl-x64", "conformance/packages/dep-app"]);
const depGraphBody = JSON.parse(depGraph.stdout);
assert.equal(depGraphBody.package.name, "dep-app");
assert.equal(depGraphBody.package.version, "0.1.0");
assert.equal(depGraphBody.package.resolver.deterministic, true);
assert.match(depGraphBody.package.lockfile.path, /\.zero\/package-locks\/[0-9a-f]+\.lock\.json/);
assert(depGraphBody.package.dependencies.some((item) => item.name === "dep-lib" && item.status === "path-resolved" && item.targetCompatible === true));
assert(depGraphBody.package.dependencies.some((item) => item.name === "remote-tools" && item.status === "registry-reference" && item.version === "1.2.3"));
assert.equal(depGraphBody.packageCache.cacheKeyInputs.compilerVersion, "0.3.1");
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

const malformedTomlArrayRoot = `${outDir}/malformed-toml-array`;
await rm(malformedTomlArrayRoot, { recursive: true, force: true });
await mkdir(malformedTomlArrayRoot, { recursive: true });
await writeFile(`${malformedTomlArrayRoot}/zero.toml`, `[package]
name = "malformed-toml-array"
version = "0.1.0"

[targets.cli]
kind = "exe"
main = "src/main.0"

[c.libs.demo]
headers = ["unterminated]
`);
const malformedTomlArrayStatus = await execFileAsync(zero, ["status", "--json", malformedTomlArrayRoot]);
const malformedTomlArrayBody = JSON.parse(malformedTomlArrayStatus.stdout);
assert.equal(malformedTomlArrayBody.ok, true);
assert.equal(malformedTomlArrayBody.repositoryGraph.storePresent, false);
assert.deepEqual(malformedTomlArrayBody.diagnostics, []);

const zeroTestRun = await execFileAsync(zero, ["test", "conformance/native/pass/test-blocks.0"]);
assert.equal(zeroTestRun.stdout, "1 test(s) ok\n");

const zeroTestingHelpersRun = await execFileAsync(zero, ["test", "conformance/native/pass/std-testing-helpers-test.0"]);
assert.equal(zeroTestingHelpersRun.stdout, "1 test(s) ok\n");

const zeroTestJsonRun = await execFileAsync(zero, ["test", "--json", "--filter", "addition", "conformance/native/pass/test-blocks.0"]);
const zeroTestJsonBody = JSON.parse(zeroTestJsonRun.stdout);
assert.equal(zeroTestJsonBody.ok, true);
assert.equal(zeroTestJsonBody.discoveredTests, 1);
assert.equal(zeroTestJsonBody.selectedTests, 1);
assert.equal(zeroTestJsonBody.passedTests, 1);
assert.equal(zeroTestJsonBody.graph.artifact, "conformance/native/pass/test-blocks.graph");
assert.equal(zeroTestJsonBody.graph.canonicalSource, false);
assert.equal(zeroTestJsonBody.graph.moduleIdentity, "module:test-blocks");
assert.equal(zeroTestJsonBody.graph.lowering, "direct-program-graph");
assert.equal(zeroTestJsonBody.testDiscovery.mode, "program-graph");
assert.equal(zeroTestJsonBody.testDiscovery.filter, "addition");
assert.equal(zeroTestJsonBody.fixtures.snapshotKey, "zero-test-graph-native-v1");
assert.equal(zeroTestJsonBody.results[0].status, "passed");

const zeroPackageTestJsonRun = await execFileAsync(zero, ["test", "--json", "conformance/packages/test-app"]);
const zeroPackageTestBody = JSON.parse(zeroPackageTestJsonRun.stdout);
assert.equal(zeroPackageTestBody.ok, true);
assert.equal(zeroPackageTestBody.graph.artifact, "conformance/packages/test-app/zero.graph");
assert.equal(zeroPackageTestBody.graph.canonicalSource, false);
assert.equal(zeroPackageTestBody.graph.moduleIdentity, "package:test-app@0.1.0");
assert.equal(zeroPackageTestBody.graph.lowering, "direct-program-graph");
assert.equal(zeroPackageTestBody.testDiscovery.mode, "package-graph");
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

const fmtRun = await execFileAsync(zero, ["fmt", "conformance/native/pass/test-blocks.0"]);
assert.equal(fmtRun.stdout, await readFile("conformance/native/pass/test-blocks.0", "utf8"));

const fmtFallibilityRun = await execFileAsync(zero, ["fmt", "conformance/native/pass/fallibility-check-value.0"]);
assert.equal(fmtFallibilityRun.stdout, await readFile("conformance/native/pass/fallibility-check-value.0", "utf8"));

const fmtImportsRun = await execFileAsync(zero, ["fmt", "conformance/check/pass/imports/src/main.0"]);
assert.match(fmtImportsRun.stdout, /use math/);
assert.match(fmtImportsRun.stdout, /use types/);

const fmtCoreRun = await execFileAsync(zero, ["fmt", "conformance/check/pass/fmt-core-usability.0"]);
assert.equal(fmtCoreRun.stdout, await readFile("conformance/check/pass/fmt-core-usability.0", "utf8"));

const fmtMessyRun = await execFileAsync(zero, ["fmt", "conformance/format/messy.0"]);
assert.equal(fmtMessyRun.stdout, await readFile("conformance/format/messy.0", "utf8"));

const fmtIdempotentPath = `${outDir}/fmt-idempotent.0`;
await writeFile(fmtIdempotentPath, fmtMessyRun.stdout);
const fmtIdempotentRun = await execFileAsync(zero, ["fmt", fmtIdempotentPath]);
assert.equal(fmtIdempotentRun.stdout, fmtMessyRun.stdout);

const fmtFunctionsBlocksRun = await execFileAsync(zero, ["fmt", "conformance/format/functions-blocks.0"]);
assert.equal(fmtFunctionsBlocksRun.stdout, await readFile("conformance/format/functions-blocks.0", "utf8"));

const fmtGenericsStaticRun = await execFileAsync(zero, ["fmt", "conformance/format/generics-static.0"]);
assert.equal(fmtGenericsStaticRun.stdout, await readFile("conformance/format/generics-static.0", "utf8"));

const fmtMatchRun = await execFileAsync(zero, ["fmt", "conformance/format/match-expressions.0"]);
assert.equal(fmtMatchRun.stdout, await readFile("conformance/format/match-expressions.0", "utf8"));

const helloRunArgs = runnableExeArgs("conformance/run/pass/hello.0", `${outDir}/hello`);
if (helloRunArgs) {
  await rm(`${outDir}/hello`, { force: true });
  await execFileAsync(zero, helloRunArgs);
  const run = await execFileAsync(`${outDir}/hello`, []);
  assert.match(run.stdout, /hello conformance/);
}

const packageGraphJson = await execFileAsync(zero, ["inspect", "--json", "conformance/check/pass/package"]);
const packageGraph = JSON.parse(packageGraphJson.stdout);
assert.deepEqual(packageGraph.sourceFiles.sort(), [
  "src/main.0",
  "src/types.0",
  "std/codec.0",
]);
assert(packageGraph.requiresCapabilities.includes("codec"));
assert(packageGraph.requiresCapabilities.includes("parse"));
assert(packageGraph.requiresCapabilities.includes("time"));
assert.equal(packageGraph.selfHostRouting.cBridge.policy, "removed");

for (const runtimeFixture of [
  ["conformance/native/pass/break-continue.0", "break-continue", { stdout: "loop tick\nloop tick\n" }],
  ["conformance/native/pass/nested-break-continue.0", "nested-break-continue", { stdout: "inner tick\ninner tick\nouter tick\ninner tick\ninner tick\nnested break continue ok\n" }],
  ["conformance/native/pass/mutref-shape-param.0", "mutref-shape-param", { stdout: "mutref x ok\nmutref y ok\nref sum ok\n" }],
  ["conformance/native/pass/mutref-shape-param-nested.0", "mutref-shape-param-nested", { stdout: "nested count ok\nnested total ok\nnested flag ok\nnested copy ok\nnested bytes ok\ngeneric mutref ok\n" }],
  ["conformance/native/pass/untyped-literal-adoption.0", "untyped-literal-adoption", { stdout: "usize literal adoption ok\nflipped literal adoption ok\nliteral arithmetic adoption ok\nu8 literal adoption ok\n" }],
  ["conformance/native/pass/for-range.0", "for-range", { stdout: "range tick\nrange tick\nrange tick\n" }],
  ["conformance/native/pass/match-payload-binding.0", "match-payload-binding", { stdout: /payload binding ok/ }],
  ["conformance/native/pass/match-fallback.0", "match-fallback", { stdout: "match fallback ok\n" }],
  ["conformance/native/pass/match-choice-fallback.0", "match-choice-fallback", { stdout: "choice fallback ok\n" }],
  ["conformance/native/pass/null-maybe.0", "null-maybe", { stdout: /null maybe ok/ }],
  ["conformance/native/pass/std-args.0", "std-args", { stdout: "alpha\n", args: ["alpha", "beta"] }],
  ["conformance/native/pass/std-env.0", "std-env", { stdout: "env ok\n", env: { ZERO_CONFORMANCE_ENV: "agent-env" } }],
  ["conformance/native/pass/std-hosted-cli.0", "std-hosted-cli", { stdout: "std hosted cli ok\n", args: ["run", "7", "--json", "--name", "agent", "--count", "3"], env: { ZERO_CONFORMANCE_MODE: "test", ZERO_CONFORMANCE_VERBOSE: "true", ZERO_CONFORMANCE_LIMIT: "9" } }],
  ["conformance/native/pass/std-fs.0", "std-fs", { stdout: "fs ok\n", file: { name: "std-fs-write.txt", text: "zero write\n" } }],
  ["conformance/native/pass/std-fs-bytes.0", "std-fs-bytes", { stdout: "fs bytes ok\n", stderr: "fs bytes err ok\n" }],
  ["conformance/native/pass/frame-large-locals.0", "frame-large-locals", { stdout: "frame large locals ok alpha\n", args: ["alpha"] }],
  ["conformance/native/pass/frame-limit-boundary.0", "frame-limit-boundary", { stdout: "frame limit boundary ok\n" }],
  ["conformance/native/pass/std-fs-resource.0", "std-fs-resource", { stdout: "fs resource ok\n", file: { name: "std-fs-resource.txt", text: "zero file\n" } }],
  ["conformance/native/pass/std-fs-file-helpers.0", "std-fs-file-helpers", { stdout: "std fs file helpers ok\n" }],
  ["conformance/native/pass/std-io-lines.0", "std-io-lines", { stdout: "std io lines ok\n" }],
  ["conformance/native/pass/integer-widths.0", "integer-widths", { stdout: "integer widths ok\n" }],
  ["conformance/native/pass/std-codec-widths.0", "std-codec-widths", { stdout: "codec widths ok\n" }],
  ["conformance/native/pass/std-codec-json-url.0", "std-codec-json-url", { stdout: "std codec json url ok\n" }],
  ["conformance/native/pass/std-crypto-hmac32.0", "std-crypto-hmac32", { stdout: "crypto hmac32 ok\n" }],
  ["conformance/native/pass/parse-integers.0", "parse-integers", { stdout: "parse integers ok\n" }],
  ["conformance/native/pass/std-parse-text.0", "std-parse-text", { stdout: "std parse text ok\n" }],
  ["conformance/native/pass/explicit-casts.0", "explicit-casts", { stdout: "explicit casts ok\n" }],
  ["conformance/native/pass/float-char-casts.0", "float-char-casts", { stdout: "float char casts ok\n" }],
  ["conformance/native/pass/radix-suffix-literals.0", "radix-suffix-literals", { stdout: "radix suffix literals ok\n" }],
  ["conformance/native/pass/char-literals.0", "char-literals", { stdout: "char literals ok\n" }],
  ["conformance/native/pass/float-primitives.0", "float-primitives", { stdout: "float primitives ok\n" }],
  ["conformance/native/pass/recursive-fibonacci.0", "recursive-fibonacci", { stdout: "recursive fibonacci ok\n" }],
  ["conformance/native/pass/scratch-nested-index.0", "scratch-nested-index", { stdout: "scratch nested index ok\n" }],
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

await assertDirectRuntimeRequired("conformance/native/pass/untyped-literal-adoption.0", "untyped-literal-adoption-required", { stdout: "usize literal adoption ok\nflipped literal adoption ok\nliteral arithmetic adoption ok\nu8 literal adoption ok\n" });

const literalAdoptionInitOverflowFixture = `${outDir}/untyped-literal-adoption-init-overflow.0`;
const literalAdoptionInitOverflowBody = await writeImportFailureFixture(literalAdoptionInitOverflowFixture, `pub fn main(world: World) -> Void raises {
    var small: u8 = 300
    check world.out.write("unreachable\\n")
}
`);
assert.equal(literalAdoptionInitOverflowBody.diagnostics[0].code, "TYP016");
assert.equal(literalAdoptionInitOverflowBody.diagnostics[0].expected, "u8");
assert.equal(literalAdoptionInitOverflowBody.diagnostics[0].actual, "300 overflows u8");

const literalAdoptionCompareOverflowFixture = `${outDir}/untyped-literal-adoption-compare-overflow.0`;
const literalAdoptionCompareOverflowBody = await writeImportFailureFixture(literalAdoptionCompareOverflowFixture, `pub fn main(world: World) -> Void raises {
    var small: u8 = 7
    if 300 > small {
        check world.out.write("unreachable\\n")
    }
}
`);
assert.equal(literalAdoptionCompareOverflowBody.diagnostics[0].code, "TYP016");
assert.equal(literalAdoptionCompareOverflowBody.diagnostics[0].expected, "u8");
assert.equal(literalAdoptionCompareOverflowBody.diagnostics[0].actual, "300 overflows u8");
assert.match(literalAdoptionCompareOverflowBody.diagnostics[0].help, /smaller literal or a wider integer type/);

await assertDirectRuntimeRequired("conformance/native/pass/break-continue.0", "break-continue-required", { stdout: "loop tick\nloop tick\n" });
await assertDirectRuntimeRequired("conformance/native/pass/nested-break-continue.0", "nested-break-continue-required", { stdout: "inner tick\ninner tick\nouter tick\ninner tick\ninner tick\nnested break continue ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/mutref-shape-param.0", "mutref-shape-param-required", { stdout: "mutref x ok\nmutref y ok\nref sum ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/mutref-shape-param-nested.0", "mutref-shape-param-nested-required", { stdout: "nested count ok\nnested total ok\nnested flag ok\nnested copy ok\nnested bytes ok\ngeneric mutref ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/generic-function-basic.0", "generic-function-basic-required", { stdout: "generic function ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/generic-nested-calls.0", "generic-nested-calls-required", { stdout: "generic nested calls ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/generic-inferred-specialized-call.0", "generic-inferred-specialized-call-required", { stdout: "generic inferred specialized call ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/generic-nested-local-specialization.0", "generic-nested-local-specialization-required", { stdout: "generic nested local specialization ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/generic-static-array-specialization.0", "generic-static-array-specialization-required", { stdout: "generic static array specialization ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/generic-static-forwarded-array-specialization.0", "generic-static-forwarded-array-specialization-required", { stdout: "generic static forwarded array specialization ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/explicit-cast-narrow-direct.0", "explicit-cast-narrow-direct-required", { stdout: "explicit cast narrow direct ok\n" });
await assertDirectRuntimeRequired("conformance/native/pass/frame-large-locals.0", "frame-large-locals-required", { stdout: "frame large locals ok alpha\n", args: ["alpha"] });
await assertDirectRuntimeRequired("conformance/native/pass/frame-limit-boundary.0", "frame-limit-boundary-required", { stdout: "frame limit boundary ok\n" });

const frameLimitOverFixture = `${outDir}/frame-limit-over.0`;
const frameLimitOverBody = await writeImportFailureFixture(frameLimitOverFixture, `pub fn main(world: World) -> Void raises {
    var buffer: [262144]u8 = [0; 262144]
    buffer[0] = 1
    check world.out.write("unreachable\\n")
}
`);
assert.equal(frameLimitOverBody.diagnostics[0].code, "MEM003");
assert.match(frameLimitOverBody.diagnostics[0].expected, /131072 bytes of locals/);
assert.match(frameLimitOverBody.diagnostics[0].actual, /262144 bytes of locals/);
assert.match(frameLimitOverBody.diagnostics[0].help, /pageAlloc/);

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
  ["conformance/native/pass/byte-view-call-single-eval.0", "byte-view-call-single-eval", { stdout: "byte view call single eval ok\n" }],
  ["conformance/native/pass/std-math-breadth.0", "std-math-breadth", { stdout: "std math breadth ok\n" }],
  ["conformance/native/pass/std-numeric-random-time.0", "std-numeric-random-time", { stdout: "std numeric random time ok\n" }],
  ["conformance/native/pass/std-str-breadth.0", "std-str-breadth", { stdout: "std str breadth ok\n" }],
  ["conformance/native/pass/std-mem-generic-items.0", "std-mem-generic-items", { stdout: "std mem generic items ok\n" }],
  ["conformance/native/pass/std-mem-field-items.0", "std-mem-field-items", { stdout: "std mem field items ok\n" }],
  ["conformance/native/pass/std-mem-byte-field-copy.0", "std-mem-byte-field-copy", { stdout: "std mem byte field copy ok\n" }],
  ["conformance/native/pass/std-mem-bool-copy-items.0", "std-mem-bool-copy-items", { stdout: "std mem bool copy items ok\n" }],
  ["conformance/native/pass/std-mem-u64-contains.0", "std-mem-u64-contains", { stdout: "std mem u64 contains ok\n" }],
  ["conformance/native/pass/std-mem-u64-copy-items.0", "std-mem-u64-copy-items", { stdout: "std mem u64 copy items ok\n" }],
  ["conformance/native/pass/std-mem-local-u64-copy-items.0", "std-mem-local-u64-copy-items", { stdout: "std mem local u64 copy items ok\n" }],
  ["conformance/native/pass/std-path-helper-name-collision.0", "std-path-helper-name-collision", { stdout: "std path helper collision ok\n" }],
  ["conformance/native/pass/byte-view-params.0", "byte-view-params", { stdout: "byte view params ok\n" }],
  ["conformance/native/pass/bool-arrays.0", "bool-arrays", { stdout: "bool arrays ok\n" }],
  ["conformance/native/pass/generic-spans.0", "generic-spans", { stdout: "generic spans ok\n" }],
  ["conformance/native/pass/open-ended-slices.0", "open-ended-slices", { stdout: "open ended slices ok\n" }],
  ["conformance/native/pass/string-slices.0", "string-slices", { stdout: "string slices ok\n" }],
  ["conformance/native/pass/indexed-mutation.0", "indexed-mutation", { stdout: "indexed mutation ok\n" }],
  ["conformance/native/pass/dynamic-indexed-store-scratch.0", "dynamic-indexed-store-scratch", { stdout: "dynamic indexed store ok\n" }],
  ["conformance/native/pass/nested-lvalues.0", "nested-lvalues", { stdout: "nested lvalues ok\n" }],
  ["conformance/native/pass/mutable-spans.0", "mutable-spans", { stdout: "mutable spans ok\n" }],
  ["conformance/native/pass/mutref-indexed-lvalues.0", "mutref-indexed-lvalues", { stdout: "mutref indexed lvalues ok\n" }],
  ["conformance/native/pass/generic-mem.0", "generic-mem", { stdout: "generic mem ok\n" }],
  ["conformance/native/pass/generic-nested-calls.0", "generic-nested-calls", { stdout: "generic nested calls ok\n" }],
  ["conformance/native/pass/generic-multi-specialization.0", "generic-multi-specialization", { stdout: "generic multi specialization ok\n" }],
  ["conformance/native/pass/generic-inferred-specialized-call.0", "generic-inferred-specialized-call", { stdout: "generic inferred specialized call ok\n" }],
  ["conformance/native/pass/generic-nested-local-specialization.0", "generic-nested-local-specialization", { stdout: "generic nested local specialization ok\n" }],
  ["conformance/native/pass/generic-static-array-specialization.0", "generic-static-array-specialization", { stdout: "generic static array specialization ok\n" }],
  ["conformance/native/pass/generic-static-forwarded-array-specialization.0", "generic-static-forwarded-array-specialization", { stdout: "generic static forwarded array specialization ok\n" }],
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

finishAggregateAssert(assert, { suite: "conformance", reportPath: `${outDir}/failures.json` });
console.log("conformance ok");
