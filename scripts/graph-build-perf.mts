#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { execFile } from "node:child_process";
import { mkdir, rm } from "node:fs/promises";
import { join } from "node:path";
import { performance } from "node:perf_hooks";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN || "bin/zero";
const target = process.env.ZERO_GRAPH_BUILD_PERF_TARGET || "linux-musl-x64";
const outRoot = process.env.ZERO_GRAPH_BUILD_PERF_DIR || `/tmp/zero-graph-build-perf-${process.pid}`;
const maxBuffer = 128 * 1024 * 1024;

type Fixture = {
  name: string;
  input: string;
  target?: string;
  checkTarget?: string;
};

type CommandRun = {
  ok: boolean;
  command: string[];
  wallMs: number;
  body: any;
  stderr: string;
};

const fixtures: Fixture[] = [
  { name: "hello-artifact", input: "examples/hello.graph" },
  { name: "stdlib-heavy-artifact", input: "conformance/native/pass/stdlib-target-neutral.graph" },
  { name: "crm-api-package", input: "examples/crm-api" },
];

function selectedFixtures() {
  const raw = process.env.ZERO_GRAPH_BUILD_PERF_CASES;
  if (!raw) return fixtures;
  const wanted = new Set(raw.split(",").map((part) => part.trim()).filter(Boolean));
  return fixtures.filter((fixture) => wanted.has(fixture.name) || wanted.has(fixture.input));
}

function phaseMs(body: any, name: string) {
  const phase = Array.isArray(body?.compilerPhases)
    ? body.compilerPhases.find((item: any) => item?.name === name)
    : null;
  return typeof phase?.elapsedMs === "number" ? phase.elapsedMs : null;
}

function graphBuildTimings(body: any) {
  const timings = body?.graphBuild?.timings || {};
  return {
    graphLoadMs: timings.graphLoadMs ?? null,
    stdlibMergeMs: timings.stdlibMergeMs ?? null,
    readinessCheckMs: timings.readinessCheckMs ?? null,
    mirCacheLoadMs: timings.mirCacheLoadMs ?? null,
    mirLowerMs: timings.mirLowerMs ?? null,
    mirCacheWriteMs: timings.mirCacheWriteMs ?? null,
    mirCacheReloadMs: timings.mirCacheReloadMs ?? null,
    mirLoadOrLowerMs: timings.mirLoadOrLowerMs ?? null,
    lowerPhaseMs: timings.lowerPhaseMs ?? phaseMs(body, "lower"),
    codegenMs: timings.codegenMs ?? phaseMs(body, "codegen"),
    objectMs: timings.objectMs ?? phaseMs(body, "object"),
    linkMs: timings.linkMs ?? phaseMs(body, "link"),
  };
}

function graphCheckTimings(body: any) {
  const timings = body?.graphCompiler?.timings || {};
  const resolveMs = timings.resolveMs ?? phaseMs(body, "resolve");
  const checkMs = timings.checkMs ?? phaseMs(body, "check");
  const lowerMs = timings.lowerMs ?? phaseMs(body, "lower");
  return {
    graphLoadMs: timings.loadMs ?? null,
    resolveMs: resolveMs ?? null,
    checkMs: checkMs ?? null,
    readinessCheckMs: typeof resolveMs === "number" && typeof checkMs === "number" ? resolveMs + checkMs : null,
    readinessLowerMs: lowerMs ?? null,
    cacheMs: timings.cacheMs ?? null,
  };
}

function cacheFacts(body: any) {
  const caches = Array.isArray(body?.compilerCaches) ? body.compilerCaches : [];
  const mapped = caches.find((cache: any) => cache?.name === "mappedFinalMir") || body?.graphBuild?.mappedFinalMir || null;
  return {
    hits: caches.filter((cache: any) => cache?.hit === true).length,
    misses: caches.filter((cache: any) => cache?.hit === false).length,
    mappedFinalMir: mapped ? {
      hit: mapped.hit ?? null,
      written: mapped.written ?? null,
      byteLength: mapped.byteLength ?? null,
      memoryMapped: mapped.memoryMapped ?? null,
      codegenImmediate: mapped.codegenImmediate ?? null,
      programReconstructed: mapped.programReconstructed ?? null,
    } : null,
  };
}

async function runJson(args: string[], env: Record<string, string | undefined>): Promise<CommandRun> {
  const started = performance.now();
  try {
    const result = await execFileAsync(zero, args, {
      env: { ...process.env, ...env },
      maxBuffer,
    });
    const wallMs = Number((performance.now() - started).toFixed(3));
    return { ok: true, command: [zero, ...args], wallMs, body: JSON.parse(result.stdout), stderr: result.stderr };
  } catch (error: any) {
    const wallMs = Number((performance.now() - started).toFixed(3));
    let body: any = null;
    try {
      body = JSON.parse(error.stdout?.toString() ?? "");
    } catch {
      body = null;
    }
    return {
      ok: false,
      command: [zero, ...args],
      wallMs,
      body,
      stderr: error.stderr?.toString() ?? error.message ?? "",
    };
  }
}

async function buildCompiler() {
  if (process.env.ZERO_GRAPH_BUILD_PERF_SKIP_BUILD === "1") return null;
  const started = performance.now();
  await execFileAsync("make", ["-C", "native/zero-c"], { maxBuffer });
  return Number((performance.now() - started).toFixed(3));
}

async function zeroVersion() {
  const result = await runJson(["--version", "--json"], {});
  if (!result.ok) throw new Error(`zero version failed: ${result.stderr}`);
  return result.body;
}

function requireOk(run: CommandRun) {
  if (run.ok && run.body?.ok !== false) return;
  const diagnostic = run.body?.diagnostics?.[0];
  const message = diagnostic ? `${diagnostic.code}: ${diagnostic.message}` : run.stderr || "command failed";
  throw new Error(`${run.command.join(" ")} failed: ${message}`);
}

function normalizeBuild(run: CommandRun) {
  requireOk(run);
  return {
    ok: true,
    wallMs: run.wallMs,
    compilerElapsedMs: run.body?.elapsedMs ?? null,
    graphHash: run.body?.graph?.graphHash ?? null,
    lowering: run.body?.graph?.lowering ?? null,
    artifactBytes: run.body?.artifactBytes ?? null,
    loweredIrBytes: run.body?.loweredIrBytes ?? null,
    timings: graphBuildTimings(run.body),
    caches: cacheFacts(run.body),
  };
}

function normalizeCheck(run: CommandRun) {
  requireOk(run);
  return {
    ok: true,
    wallMs: run.wallMs,
    graphHash: run.body?.graph?.graphHash ?? run.body?.graphHash ?? null,
    targetReady: run.body?.targetReadiness?.ok ?? null,
    timings: graphCheckTimings(run.body),
    caches: cacheFacts(run.body),
  };
}

async function runFixture(fixture: Fixture) {
  const fixtureTarget = fixture.target || target;
  const checkTarget = fixture.checkTarget || process.env.ZERO_GRAPH_BUILD_PERF_CHECK_TARGET || "";
  const fixtureRoot = join(outRoot, fixture.name);
  const buildCache = join(fixtureRoot, "build-cache");
  const checkCache = join(fixtureRoot, "check-cache");
  await mkdir(fixtureRoot, { recursive: true });

  async function runPass(pass: "cold" | "warm") {
    const artifactPath = join(fixtureRoot, `${fixture.name}-${pass}.o`);
    process.stderr.write(`graph perf ${fixture.name} ${pass} build\n`);
    const build = await runJson(["build", "--json", "--emit", "obj", "--target", fixtureTarget, fixture.input, "--out", artifactPath], {
      ZERO_CACHE_DIR: buildCache,
    });
    process.stderr.write(`graph perf ${fixture.name} ${pass} check\n`);
    const checkArgs = ["check", "--json"];
    if (checkTarget) checkArgs.push("--target", checkTarget);
    checkArgs.push(fixture.input);
    const check = await runJson(checkArgs, {
      ZERO_CACHE_DIR: checkCache,
    });
    return {
      build: normalizeBuild(build),
      check: normalizeCheck(check),
    };
  }

  return {
    name: fixture.name,
    input: fixture.input,
    target: fixtureTarget,
    checkTarget: checkTarget || "host-default",
    cold: await runPass("cold"),
    warm: await runPass("warm"),
  };
}

async function main() {
  await rm(outRoot, { recursive: true, force: true });
  await mkdir(outRoot, { recursive: true });
  const nativeBuildMs = await buildCompiler();
  const version = await zeroVersion();
  const cases = selectedFixtures();
  if (cases.length === 0) throw new Error("ZERO_GRAPH_BUILD_PERF_CASES selected no fixtures");
  const started = performance.now();
  const results = [];
  for (const fixture of cases) results.push(await runFixture(fixture));
  const elapsedMs = Number((performance.now() - started).toFixed(3));
  const report = {
    schemaVersion: 1,
    kind: "zero-graph-build-perf",
    generatedAt: new Date().toISOString(),
    zero: version,
    target,
    outDir: outRoot,
    nativeBuildMs,
    elapsedMs,
    cases: results,
  };
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
}

main().catch((error) => {
  process.stderr.write(`${error instanceof Error ? error.stack || error.message : String(error)}\n`);
  process.exit(1);
});
