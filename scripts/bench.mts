#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { spawnSync } from "node:child_process";
import { chmodSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { performance } from "node:perf_hooks";
import { gzipSync } from "node:zlib";

const root = process.cwd();
const outDir = join(root, ".zero", "bench");
const runtimeOutDir = join(root, ".zero", "out");
mkdirSync(outDir, { recursive: true });
mkdirSync(runtimeOutDir, { recursive: true });
cleanStaleGeneratedCOutputs();
loadDotEnvFiles([".zero/bench/snapshot.env", ".env", ".env.local"]);

const args = process.argv.slice(2);
const workerMode = args.includes("--worker");
const modeArg = flagValue("--mode");
const mode = modeArg ?? process.env.ZERO_BENCH_MODE ?? "local";
const runCount = Number.parseInt(process.env.ZERO_BENCH_RUNS ?? "5", 10);
const directZeroMinimum = 18;

const cases = [
  { name: "baseline-hello", expectedStdout: "baseline hello", languages: ["zero"], sources: { zero: "benchmarks/zero/baseline-hello.0" } },
  { name: "baseline-file-cli", expectedStdout: "cli file ok", languages: ["zero"], skipLanguages: ["zero"], sources: { zero: "examples/cli-file.0" }, args: ["write"], env: { ZERO_CLI_FILE_MODE: "quiet" }, skipReason: "hosted filesystem CLI direct lowering remains a tracked std.fs breadth gap" },
  { name: "baseline-memory-package", expectedStdout: "memory package ok", languages: ["zero"], sources: { zero: "examples/memory-package" } },
  { name: "hello", expectedStdout: "hello benchmark" },
  { name: "add", expectedStdout: "math works" },
  { name: "structs", expectedStdout: "42" },
  { name: "params", expectedStdout: "42" },
  { name: "buffers", expectedStdout: "buffer" },
  { name: "parser", expectedStdout: "ident" },
  { name: "fileio", expectedStdout: "fileio" },
  { name: "codec", expectedStdout: "codec" },
  { name: "parse", expectedStdout: "parse" },
  { name: "slices", expectedStdout: "slices" },
  { name: "arena", expectedStdout: "arena" },
  { name: "fallibility", expectedStdout: "fallibility" },
  { name: "readall", expectedStdout: "readall", skipLanguages: ["zero"] },
  { name: "branches", expectedStdout: "branches" },
  { name: "module-package", expectedStdout: "module package", languages: ["zero"], sources: { zero: "benchmarks/zero/module-package" } },
  { name: "owned-file", expectedStdout: "owned file", languages: ["zero"], skipLanguages: ["zero"] },
  { name: "rescue", expectedStdout: "rescue", languages: ["zero"] },
  { name: "fs-resource", expectedStdout: "fs resource", languages: ["zero"], skipLanguages: ["zero"] },
  { name: "mem-copy-fill", expectedStdout: "mem copy fill", languages: ["zero"] },
  { name: "zero-hash", expectedStdout: "zero-hash ok", languages: ["zero"] },
  { name: "json", expectedStdout: "json", languages: ["zero"] },
  { name: "networking", expectedStdout: "networking", languages: ["zero"], skipLanguages: ["zero"] },
];

const languages = [
  {
    name: "zero",
    tool: "bin/zero",
    build: (benchCase) => ["bin/zero", ["build", "--json", "--emit", "exe", "--target", directZeroTarget(), sourcePath(benchCase, "zero", ".0"), "--out", `.zero/bench/zero-${benchCase.name}`]],
    exe: (benchCase) => `.zero/bench/zero-${benchCase.name}`,
  },
];

const languageByName = new Map(languages.map((language) => [language.name, language]));
const caseByName = new Map(cases.map((benchCase) => [benchCase.name, benchCase]));

function cleanStaleGeneratedCOutputs() {
  for (const entry of readdirSync(outDir)) {
    if (/^zero-.+\.c$/.test(entry)) rmSync(join(outDir, entry), { force: true });
  }
}

function loadDotEnvFiles(paths) {
  for (const path of paths) {
    const fullPath = join(root, path);
    if (!existsSync(fullPath)) continue;
    const lines = readFileSync(fullPath, "utf8").split(/\r?\n/);
    for (const line of lines) {
      const trimmed = line.trim();
      if (trimmed === "" || trimmed.startsWith("#")) continue;
      const match = /^(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)=(.*)$/.exec(trimmed);
      if (!match || process.env[match[1]] !== undefined) continue;
      process.env[match[1]] = parseDotEnvValue(match[2]);
    }
  }
}

function parseDotEnvValue(value) {
  const trimmed = value.trim();
  const quote = trimmed[0];
  if ((quote === "\"" || quote === "'") && trimmed.endsWith(quote)) {
    const inner = trimmed.slice(1, -1);
    return quote === "\"" ? inner.replace(/\\n/g, "\n").replace(/\\r/g, "\r").replace(/\\t/g, "\t").replace(/\\"/g, "\"").replace(/\\\\/g, "\\") : inner;
  }
  const commentStart = trimmed.search(/\s#/);
  return commentStart === -1 ? trimmed : trimmed.slice(0, commentStart).trimEnd();
}

function flagValue(name) {
  const index = args.indexOf(name);
  if (index === -1) return null;
  return args[index + 1] ?? null;
}

function sourcePath(benchCase, language, extension) {
  if (benchCase.sources?.[language]) return benchCase.sources[language];
  return `benchmarks/${language}/${benchCase.name}${extension}`;
}

function directZeroTarget() {
  if (process.platform === "darwin" && process.arch === "arm64") return "darwin-arm64";
  if (process.platform === "darwin" && process.arch === "x64") return "darwin-x64";
  if (process.platform === "linux" && process.arch === "x64") return "linux-musl-x64";
  if (process.platform === "linux" && process.arch === "arm64") return "linux-arm64";
  return null;
}

function zeroDirectCapable() {
  return directZeroTarget() !== null;
}

function hasTool(command) {
  if (command.includes("/")) {
    return spawnSync(command, ["--help"], { stdio: "ignore" }).status === 0 || spawnSync(command, [], { stdio: "ignore" }).status !== null;
  }
  return spawnSync("sh", ["-c", `command -v ${command}`], { stdio: "ignore" }).status === 0;
}

function timed(command, commandArgs) {
  const started = performance.now();
  const result = spawnSync(command, commandArgs, { encoding: "utf8" });
  const elapsedMs = performance.now() - started;
  return { result, elapsedMs: Number(elapsedMs.toFixed(3)) };
}

function parseJsonOutput(stdout) {
  try {
    return JSON.parse(stdout || "{}");
  } catch {
    return null;
  }
}

function toolVersion(command, commandArgs) {
  const result = spawnSync(command, commandArgs, { encoding: "utf8" });
  if (result.error || result.status !== 0) return null;
  return (result.stdout ?? "").trim().split("\n")[0] || null;
}

function timedRun(command, commandArgs = [], env = {}) {
  const runEnv = { ...process.env, ...env };
  if (process.platform === "darwin") {
    const started = performance.now();
    const result = spawnSync("/usr/bin/time", ["-l", command, ...commandArgs], { encoding: "utf8", env: runEnv });
    const elapsedMs = performance.now() - started;
    if (result.status !== 0 && result.stderr.includes("sysctl kern.clockrate")) {
      const fallback = timed(command, commandArgs);
      return {
        result: fallback.result,
        elapsedMs: fallback.elapsedMs,
        peakRssBytes: null,
        stdout: fallback.result.stdout.trim(),
        stderr: fallback.result.stderr.trim(),
      };
    }
    const rssMatch = /(\d+)\s+maximum resident set size/.exec(result.stderr);
    return {
      result,
      elapsedMs: Number(elapsedMs.toFixed(3)),
      peakRssBytes: rssMatch ? Number(rssMatch[1]) : null,
      stdout: result.stdout.trim(),
      stderr: result.stderr.replace(/\s*\d+\s+maximum resident set size[\s\S]*$/m, "").trim(),
    };
  }
  const run = timed(command, commandArgs);
  return {
    result: run.result,
    elapsedMs: run.elapsedMs,
    peakRssBytes: null,
    stdout: run.result.stdout.trim(),
    stderr: run.result.stderr.trim(),
  };
}

function fileSize(path) {
  try {
    return statSync(path).size;
  } catch {
    return null;
  }
}

function gzipFileSize(path) {
  try {
    return gzipSync(readFileSync(path)).length;
  } catch {
    return null;
  }
}

function median(values) {
  if (values.length === 0) return null;
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.floor(sorted.length / 2)];
}

function allJobs() {
  const jobs = [];
  for (const language of languages) {
    for (const benchCase of cases) jobs.push({ language: language.name, case: benchCase.name });
  }
  return jobs;
}

function isCaseDefinedForLanguage(benchCase, language) {
  if (benchCase.skipLanguages?.includes(language.name)) return false;
  return !benchCase.languages || benchCase.languages.includes(language.name);
}

function executableJobs() {
  return allJobs().filter((job) => {
    const language = languageByName.get(job.language);
    const benchCase = caseByName.get(job.case);
    return language && benchCase && isCaseDefinedForLanguage(benchCase, language);
  });
}

function skippedResults() {
  return allJobs().flatMap((job) => {
    const language = languageByName.get(job.language);
    const benchCase = caseByName.get(job.case);
    if (!language || !benchCase || isCaseDefinedForLanguage(benchCase, language)) return [];
    const reason = benchCase.skipLanguages?.includes(language.name)
      ? (benchCase.skipReason ?? "case is skipped for this benchmark target")
      : "case is not defined for this benchmark target";
    return [{ language: language.name, case: benchCase.name, status: "skipped", reason }];
  });
}

function sortResults(results) {
  const order = new Map(allJobs().map((job, index) => [`${job.language}:${job.case}`, index]));
  return [...results].sort((left, right) => {
    const leftOrder = order.get(`${left.language}:${left.case}`) ?? Number.MAX_SAFE_INTEGER;
    const rightOrder = order.get(`${right.language}:${right.case}`) ?? Number.MAX_SAFE_INTEGER;
    return leftOrder - rightOrder;
  });
}

function parseWorkerJobs() {
  const encoded = flagValue("--jobs");
  if (!encoded) return allJobs();
  return JSON.parse(Buffer.from(encoded, "base64url").toString("utf8"));
}

function runJobs(jobs) {
  const results = [];
  for (const job of jobs) {
    const language = languageByName.get(job.language);
    const benchCase = caseByName.get(job.case);
    if (!language || !benchCase) throw new Error(`unknown benchmark job ${JSON.stringify(job)}`);
    results.push(runCase(language, benchCase));
  }
  return results;
}

function runCase(language, benchCase) {
  const name = benchCase.name;
  if (!isCaseDefinedForLanguage(benchCase, language)) {
    return { language: language.name, case: name, status: "skipped", reason: "case is not defined for this language" };
  }
  if (!hasTool(language.tool)) {
    return { language: language.name, case: name, status: "missing", tool: language.tool };
  }

  const exe = language.exe(benchCase);
  if (language.name === "zero") {
    rmSync(exe, { force: true });
    rmSync(`${exe}.c`, { force: true });
  }

  const directZero = language.name === "zero" && zeroDirectCapable();
  const backendMode = directZero ? "direct" : "native-toolchain";
  let build;
  let buildBody = null;
  if (directZero) {
    const [buildCommand, buildArgs] = language.build(benchCase);
    build = timed(buildCommand, buildArgs);
    buildBody = parseJsonOutput(build.result.stdout);
  } else {
    const [buildCommand, buildArgs] = language.build(benchCase);
    build = timed(buildCommand, buildArgs);
  }
  if (build.result.status !== 0) {
    const diagnosticCode = buildBody?.diagnostics?.[0]?.code ?? null;
    if (language.name === "zero" && diagnosticCode === "CGEN004") {
      return {
        language: language.name,
        case: name,
        status: "skipped",
        reason: `direct executable runner unavailable for this case on ${directZeroTarget()}`,
        backendMode,
        target: directZero ? directZeroTarget() : null,
        buildMs: build.elapsedMs,
        buildJson: buildBody,
      };
    }
    return {
      language: language.name,
      case: name,
      status: "build-failed",
      backendMode,
      buildMs: build.elapsedMs,
      stderr: build.result.stderr.trim(),
      stdout: build.result.stdout?.trim(),
      buildJson: buildBody,
    };
  }

  if (directZero) {
    try {
      chmodSync(exe, 0o755);
    } catch {
      // The direct linker normally writes executable mode; chmod is best-effort for sandbox file systems.
    }
  }
  const runs = [];
  for (let index = 0; index < Math.max(1, runCount); index++) runs.push(timedRun(exe, benchCase.args ?? [], benchCase.env ?? {}));
  const run = runs[runs.length - 1];
  const runTimes = runs.map((item) => item.elapsedMs);
  const peakRssValues = runs.map((item) => item.peakRssBytes).filter((value) => value !== null);
  const stdout = run.stdout || run.stderr;
  const expected = benchCase.expectedStdout ?? null;
  const outputMatches = expected === null ? true : stdout.trim() === expected;
  return {
    language: language.name,
    case: name,
    status: runs.every((item) => item.result.status === 0) && outputMatches ? "ok" : (outputMatches ? "run-failed" : "semantic-failed"),
    backendMode,
    target: directZero ? directZeroTarget() : null,
    compiler: buildBody?.compiler ?? null,
    objectEmissionPath: buildBody?.objectBackend?.objectEmission?.path ?? null,
    cBridgeFallback: buildBody?.selfHostRouting?.cBridge?.required ?? buildBody?.cBridgeFallback ?? (directZero ? false : null),
    buildMs: build.elapsedMs,
    emitCMs: null,
    cCompileMs: null,
    runMs: median(runTimes),
    runMedianMs: median(runTimes),
    runMinMs: Math.min(...runTimes),
    runRuns: runs.map((item) => item.elapsedMs),
    artifactBytes: fileSize(exe),
    compressedArtifactBytes: gzipFileSize(exe),
    generatedCBytes: language.name === "zero" ? buildBody?.generatedCBytes ?? 0 : null,
    stdout,
    expectedStdout: expected,
    outputMatches,
    peakRssBytes: peakRssValues.length > 0 ? Math.max(...peakRssValues) : null,
  };
}

function average(values) {
  if (values.length === 0) return null;
  const total = values.reduce((sum, value) => sum + value, 0);
  return Number((total / values.length).toFixed(3));
}

function countBy(items, keyFn) {
  const counts = {};
  for (const item of items) {
    const key = keyFn(item) ?? "none";
    counts[key] = (counts[key] ?? 0) + 1;
  }
  return Object.fromEntries(Object.entries(counts).sort(([left], [right]) => left.localeCompare(right)));
}

function buildSummary(results) {
  const summary = {};
  for (const language of languages) {
    const languageResults = results.filter((item) => item.language === language.name);
    const ok = languageResults.filter((item) => item.status === "ok");
    const counts = {};
    for (const result of languageResults) counts[result.status] = (counts[result.status] ?? 0) + 1;
    summary[language.name] = {
      counts,
      averageBuildMs: average(ok.map((item) => item.buildMs).filter((value) => typeof value === "number")),
      averageArtifactBytes: average(ok.map((item) => item.artifactBytes).filter((value) => typeof value === "number")),
      averageCompressedArtifactBytes: average(ok.map((item) => item.compressedArtifactBytes).filter((value) => typeof value === "number")),
      averageRunMedianMs: average(ok.map((item) => item.runMedianMs).filter((value) => typeof value === "number")),
      byBackendMode: countBy(languageResults, (item) => item.backendMode),
    };
  }
  return summary;
}

function zeroResult(results, name) {
  return results.find((item) => item.language === "zero" && item.case === name && item.status === "ok") ?? null;
}

function measurementFact(results, name, measurement, status = "measured") {
  const result = zeroResult(results, name);
  return {
    name,
    measurement,
    status: result ? status : "missing",
    buildMs: result?.buildMs ?? null,
    runMedianMs: result?.runMedianMs ?? null,
    runMinMs: result?.runMinMs ?? null,
    peakRssBytes: result?.peakRssBytes ?? null,
    artifactBytes: result?.artifactBytes ?? null,
    compressedArtifactBytes: result?.compressedArtifactBytes ?? null,
  };
}

function buildMeasurementCoverage(results) {
  return {
    startup: [
      measurementFact(results, "baseline-hello", "cli-startup"),
    ],
    memory: [
      measurementFact(results, "baseline-hello", "runtime-memory-floor"),
      measurementFact(results, "baseline-memory-package", "peak-rss"),
      { name: "allocation-counts", measurement: "allocation-counts", status: "instrumentation-planned", note: "stdlib allocation APIs expose explicit allocators; runtime allocation counters need allocator instrumentation" },
    ],
    operations: [
      measurementFact(results, "slices", "slices"),
      measurementFact(results, "parser", "parsing"),
      measurementFact(results, "fileio", "file-io"),
      measurementFact(results, "codec", "codec"),
      measurementFact(results, "parse", "parse-primitives"),
      measurementFact(results, "branches", "branches"),
      measurementFact(results, "rescue", "fallible-rescue"),
      measurementFact(results, "networking", "networking"),
      measurementFact(results, "json", "json"),
    ],
  };
}

function buildEnvironment(extra = {}) {
  return {
    os: process.platform,
    arch: process.arch,
    node: process.version,
    ...extra,
  };
}

function buildReport(results, environment, runner) {
  const report = {
    generatedAt: new Date().toISOString(),
    methodology: {
      buildMs: "wall-clock process time measured by Node performance.now",
      runMs: "median wall-clock process time across repeated executions",
      runCount: Math.max(1, runCount),
      runMinMs: "minimum wall-clock process time across repeated executions",
      peakRssBytes: "maximum resident set size from /usr/bin/time -l on macOS; null when unavailable",
      compressedArtifactBytes: "gzip-compressed executable size in bytes for deployable-size baselines",
      generatedCBytes: "expected to be 0 for Zero benchmark rows",
      backendMode: "direct or native-toolchain per benchmark row",
      emitCMs: "retained as a nullable historical field; Zero no longer emits C",
      cCompileMs: "retained as a nullable historical field; Zero no longer invokes a C compiler backend",
      semanticChecks: "benchmark stdout must match the per-case expected output",
      runner,
    },
    claims: {
      buildTime: "buildMs records end-to-end compiler process time",
      artifactSize: "artifactBytes records output executable size for successful builds",
      compressedArtifactSize: "compressedArtifactBytes records gzip-compressed executable size for successful builds",
      runMedian: "runMedianMs records median process runtime across ZERO_BENCH_RUNS executions",
      generatedCBytes: "generatedCBytes is zero for Zero benchmarks",
      directZeroBenchmarks: "sandbox Linux runs use direct Zero artifacts for Zero benchmark cases",
      runtimeMeasurements: "measurements groups startup, memory, and operation benchmark facts; metadata-only entries call out runners that are not implemented yet",
    },
    limitations: [
      "ZERO_BENCH_RUNS=1 is a smoke test, not a publishable benchmark",
      "process-startup-heavy workloads are not general runtime performance claims",
      "skipped Zero cases are listed explicitly with a reason",
    ],
    environment,
    summary: buildSummary(results),
    measurements: buildMeasurementCoverage(results),
    results,
  };
  const typedReport = report as any;
  typedReport.trendSummary = buildBenchmarkTrendSummary(report);
  return typedReport;
}

function buildBenchmarkTrendSummary(report) {
  const zeroRows = report.results.filter((row) => row.language === "zero" && row.backendMode === "direct" && row.status === "ok");
  const artifactBytes = zeroRows.map((row) => row.artifactBytes).filter((value) => typeof value === "number");
  const compressedArtifactBytes = zeroRows.map((row) => row.compressedArtifactBytes).filter((value) => typeof value === "number");
  const buildMs = zeroRows.map((row) => row.buildMs).filter((value) => typeof value === "number");
  const runMedianMs = zeroRows.map((row) => row.runMedianMs).filter((value) => typeof value === "number");
  const startupRows = report.measurements.startup.filter((row) => row.status === "measured");
  const memoryRows = report.measurements.memory.filter((row) => row.status === "measured");
  const operationRows = report.measurements.operations.filter((row) => row.status === "measured");
  return {
    schemaVersion: 1,
    kind: "zero-benchmark-trend-summary",
    generatedAt: report.generatedAt,
    sourceReport: ".zero/bench/latest.json",
    runCount,
    runner: report.environment.runner,
    trackedMetrics: ["artifactBytes", "compressedArtifactBytes", "buildMs", "runMedianMs", "runMinMs", "peakRssBytes"],
    directZero: {
      rows: zeroRows.length,
      generatedCBytes: zeroRows.reduce((total, row) => total + (row.generatedCBytes ?? 0), 0),
      averageArtifactBytes: average(artifactBytes),
      averageCompressedArtifactBytes: average(compressedArtifactBytes),
      averageBuildMs: average(buildMs),
      averageRunMedianMs: average(runMedianMs),
      cases: zeroRows.map((row) => row.case).sort(),
    },
    coverage: {
      startup: startupRows.length,
      memory: memoryRows.length,
      operations: operationRows.length,
    },
    rows: zeroRows.map((row) => ({
      case: row.case,
      artifactBytes: row.artifactBytes ?? null,
      compressedArtifactBytes: row.compressedArtifactBytes ?? null,
      buildMs: row.buildMs ?? null,
      runMedianMs: row.runMedianMs ?? null,
      runMinMs: row.runMinMs ?? null,
      peakRssBytes: row.peakRssBytes ?? null,
      objectEmissionPath: row.objectEmissionPath ?? null,
    })),
  };
}

function assertBenchmarkGuardrails(report) {
  if (report.environment?.runner !== "vercel-sandbox") return;
  const guarded = ["baseline-hello", "hello", "add", "params", "buffers", "fileio", "codec", "parse", "fallibility", "branches", "rescue"];
  for (const name of guarded) {
    const result = report.results.find((item) => item.language === "zero" && item.case === name);
    if (!result) throw new Error(`missing benchmark result for ${name}`);
    if (result.status !== "ok") throw new Error(`${name} benchmark status was ${result.status}\n${formatFailedResult(result)}`);
    if (result.outputMatches !== true) throw new Error(`${name} benchmark stdout did not match\n${formatFailedResult(result)}`);
    if (result.backendMode !== "direct") throw new Error(`${name} did not use the direct backend\n${formatFailedResult(result)}`);
    if (result.generatedCBytes !== 0) throw new Error(`${name} direct benchmark emitted C bytes\n${formatFailedResult(result)}`);
    if (result.cBridgeFallback) throw new Error(`${name} direct benchmark used C bridge fallback\n${formatFailedResult(result)}`);
    if (typeof result.artifactBytes !== "number" || result.artifactBytes <= 0) throw new Error(`${name} missing artifact size\n${formatFailedResult(result)}`);
    if (typeof result.compressedArtifactBytes !== "number" || result.compressedArtifactBytes <= 0) throw new Error(`${name} missing compressed artifact size\n${formatFailedResult(result)}`);
    if (typeof result.runMedianMs !== "number" || result.runMedianMs <= 0) throw new Error(`${name} missing startup timing\n${formatFailedResult(result)}`);
    if (!("peakRssBytes" in result)) throw new Error(`${name} missing peak RSS field\n${formatFailedResult(result)}`);
  }
  for (const language of languages) {
    if (!report.summary[language.name]?.counts) throw new Error(`missing summary counts for ${language.name}`);
  }
}

function assertDirectBenchmarks(report) {
  if (report.environment?.runner !== "vercel-sandbox") return;
  for (const result of report.results.filter((item) => item.language === "zero" && item.status !== "skipped")) {
    const name = result.case;
    if (!result) throw new Error(`missing direct Zero benchmark result for ${name}`);
    if (result.status !== "ok") throw new Error(`${name} direct benchmark status was ${result.status}\n${formatFailedResult(result)}`);
    if (result.backendMode !== "direct") throw new Error(`${name} did not use the direct backend in sandbox benchmarks\n${formatFailedResult(result)}`);
    if (result.generatedCBytes !== 0) throw new Error(`${name} direct benchmark emitted C bytes\n${formatFailedResult(result)}`);
    if (result.cBridgeFallback) throw new Error(`${name} direct benchmark required a C bridge\n${formatFailedResult(result)}`);
    if (!result.objectEmissionPath?.startsWith("direct-")) throw new Error(`${name} missing direct object emission path\n${formatFailedResult(result)}`);
  }
}

function assertBenchmarkCoverage(report) {
  if (report.environment?.runner !== "vercel-sandbox") return;
  const zeroRows = report.results.filter((item) => item.language === "zero");
  const directRows = zeroRows.filter((item) => item.status === "ok" && item.backendMode === "direct");
  if (directRows.length < directZeroMinimum) {
    throw new Error(`Zero benchmark coverage requires at least ${directZeroMinimum} direct rows, found ${directRows.length}`);
  }
  for (const result of directRows) {
    if (result.outputMatches !== true) throw new Error(`${result.case} benchmark stdout did not match\n${formatFailedResult(result)}`);
    if (result.generatedCBytes !== 0) throw new Error(`${result.case} direct benchmark emitted C bytes\n${formatFailedResult(result)}`);
    if (result.cBridgeFallback) throw new Error(`${result.case} direct benchmark required a C bridge\n${formatFailedResult(result)}`);
    if (!result.objectEmissionPath?.startsWith("direct-")) throw new Error(`${result.case} missing direct object emission path\n${formatFailedResult(result)}`);
  }
  const removedBackendSkips = zeroRows.filter((item) => item.status === "skipped" && /removed C backend|C backend output/.test(item.reason ?? ""));
  if (removedBackendSkips.length > 0) {
    throw new Error(`removed-backend skip reasons remain: ${removedBackendSkips.map((item) => item.case).join(", ")}`);
  }
}

function formatFailedResult(result) {
  return JSON.stringify(
    {
      language: result.language,
      case: result.case,
      status: result.status,
      backendMode: result.backendMode ?? null,
      target: result.target ?? null,
      compiler: result.compiler ?? null,
      objectEmissionPath: result.objectEmissionPath ?? null,
      generatedCBytes: result.generatedCBytes ?? null,
      cBridgeFallback: result.cBridgeFallback ?? null,
      buildMs: result.buildMs ?? null,
      emitCMs: result.emitCMs ?? null,
      cCompileMs: result.cCompileMs ?? null,
      stdout: result.stdout ?? null,
      stderr: result.stderr ?? null,
      expectedStdout: result.expectedStdout ?? null,
      outputMatches: result.outputMatches ?? null,
    },
    null,
    2,
  );
}

function assertMeasurementCoverage(report) {
  for (const category of ["startup", "memory", "operations"]) {
    if (!Array.isArray(report.measurements?.[category]) || report.measurements[category].length === 0) {
      throw new Error(`missing ${category} measurement coverage`);
    }
  }
  if (report.environment?.runner !== "vercel-sandbox") return;
  for (const name of ["fileio", "codec", "parse", "branches", "rescue"]) {
    const fact = report.measurements.operations.find((item) => item.name === name);
    if (!fact || fact.status !== "measured") throw new Error(`missing measured operation benchmark for ${name}`);
  }
}

function benchmarkGuardrailsMarkdownReport(report) {
  const rows = ["# Benchmark Guardrails", "", "Timing is intentionally treated as noisy. The checked signals are semantic stdout, JSON shape, direct-output bytes, and artifact presence.", ""];
  for (const name of ["fs-resource", "mem-copy-fill"]) {
    const result = report.results.find((item) => item.language === "zero" && item.case === name);
    rows.push(`- ${name}: status=${result.status}, generatedCBytes=${result.generatedCBytes}, artifactBytes=${result.artifactBytes}, buildMs=${result.buildMs}, runMedianMs=${result.runMedianMs}`);
  }
  const directRows = report.results.filter((item) => item.language === "zero" && item.backendMode === "direct");
  const removedBackendSkips = report.results.filter((item) => item.language === "zero" && item.status === "skipped" && /removed C backend|C backend output/.test(item.reason ?? ""));
  rows.push(`- direct zero rows: ${directRows.length}, generatedCBytes=${directRows.reduce((total, row) => total + (row.generatedCBytes ?? 0), 0)}`);
  rows.push(`- direct zero threshold: ${directRows.length}/${directZeroMinimum}`);
  rows.push(`- removed-backend skip reasons: ${removedBackendSkips.length}`);
  rows.push("");
  rows.push(`- zero summary counts: ${JSON.stringify(report.summary.zero.counts)}`);
  rows.push("- timings are recorded for context; semantic output and artifact facts are the regression signals.");
  rows.push("");
  return `${rows.join("\n")}\n`;
}

function baselineMarkdownReport(report) {
  const rows = ["# Baseline Metrics", "", "These baselines track binary size, compressed size, startup timing, and peak RSS when available.", ""];
  for (const name of ["baseline-hello", "baseline-file-cli", "baseline-memory-package"]) {
    const result = report.results.find((item) => item.language === "zero" && item.case === name);
    if (result.status === "skipped") {
      rows.push(`- ${name}: status=skipped, reason=${result.reason}`);
    } else {
      rows.push(`- ${name}: artifactBytes=${result.artifactBytes}, compressedArtifactBytes=${result.compressedArtifactBytes}, runMedianMs=${result.runMedianMs}, runMinMs=${result.runMinMs}, peakRssBytes=${result.peakRssBytes}`);
    }
  }
  rows.push("");
  return `${rows.join("\n")}\n`;
}

function trendMarkdownReport(summary) {
  const rows = ["# Benchmark Trend Summary", "", "This file is a compact trend artifact. It is separate from the one-run smoke report so CI can archive it across runs.", ""];
  rows.push(`- runner: ${summary.runner}`);
  rows.push(`- runCount: ${summary.runCount}`);
  rows.push(`- direct Zero rows: ${summary.directZero.rows}`);
  rows.push(`- generatedCBytes: ${summary.directZero.generatedCBytes}`);
  rows.push(`- averageArtifactBytes: ${summary.directZero.averageArtifactBytes}`);
  rows.push(`- averageCompressedArtifactBytes: ${summary.directZero.averageCompressedArtifactBytes}`);
  rows.push(`- averageBuildMs: ${summary.directZero.averageBuildMs}`);
  rows.push(`- averageRunMedianMs: ${summary.directZero.averageRunMedianMs}`);
  rows.push(`- coverage: startup=${summary.coverage.startup}, memory=${summary.coverage.memory}, operations=${summary.coverage.operations}`);
  rows.push("");
  return `${rows.join("\n")}\n`;
}

function assertBenchmarkTrendSummary(report) {
  const summary = report.trendSummary;
  if (!summary || summary.kind !== "zero-benchmark-trend-summary") throw new Error("missing benchmark trend summary");
  if (!Array.isArray(summary.trackedMetrics) || !summary.trackedMetrics.includes("compressedArtifactBytes")) throw new Error("trend summary missing tracked metrics");
  if (summary.directZero.generatedCBytes !== 0) throw new Error("trend summary direct Zero C-output bytes changed");
  if (summary.directZero.rows <= 0 || summary.rows.length !== summary.directZero.rows) throw new Error("trend summary missing direct Zero rows");
  if (summary.coverage.startup <= 0 || summary.coverage.memory <= 0 || summary.coverage.operations <= 0) throw new Error("trend summary measurement coverage incomplete");
}

function writeReport(report) {
  report.trendSummary = buildBenchmarkTrendSummary(report);
  const output = JSON.stringify(report, null, 2);
  const outputPath = join(outDir, "latest.json");
  const trendDir = join(outDir, "trends");
  mkdirSync(trendDir, { recursive: true });
  writeFileSync(outputPath, `${output}\n`);
  writeFileSync(join(dirname(outputPath), "latest.pretty.json"), `${output}\n`);
  writeFileSync(join(dirname(outputPath), "benchmark-guardrails.md"), benchmarkGuardrailsMarkdownReport(report));
  writeFileSync(join(dirname(outputPath), "baseline-metrics.md"), baselineMarkdownReport(report));
  writeFileSync(join(trendDir, "latest.json"), `${JSON.stringify(report.trendSummary, null, 2)}\n`);
  writeFileSync(join(trendDir, "summary.md"), trendMarkdownReport(report.trendSummary));

  try {
    assertBenchmarkGuardrails(report);
    assertDirectBenchmarks(report);
    assertBenchmarkCoverage(report);
    assertMeasurementCoverage(report);
    assertBenchmarkTrendSummary(report);
  } catch (error) {
    const detail = error instanceof Error ? error.message : String(error);
    throw new Error(`${detail}\nfull benchmark report written to ${outputPath}`);
  }

  console.log(output);
}

function encodeJobs(jobs) {
  return Buffer.from(JSON.stringify(jobs), "utf8").toString("base64url");
}

function benchmarkJobGroups(jobs) {
  const groups = [];
  for (const language of languages) {
    const languageJobs = jobs.filter((job) => job.language === language.name);
    if (languageJobs.length > 0) groups.push({ language: language.name, jobs: languageJobs });
  }
  return groups;
}

function parsePositiveInt(value, fallback) {
  const parsed = Number.parseInt(value ?? "", 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

function parseOptionalNonNegativeInt(value) {
  if (value === undefined || value === "") return undefined;
  const parsed = Number.parseInt(value, 10);
  if (!Number.isFinite(parsed) || parsed < 0) throw new Error(`expected a non-negative integer, got ${value}`);
  return parsed;
}

function sandboxEnv() {
  return {
    ZERO_BENCH_RUNS: String(Math.max(1, runCount)),
  };
}

function sandboxSetupEnv() {
  return {};
}

function sandboxProjectDir() {
  return process.env.ZERO_BENCH_SANDBOX_PROJECT_DIR || "/vercel/sandbox/zero-lang";
}

function sandboxRuntime() {
  return process.env.ZERO_BENCH_SANDBOX_RUNTIME ?? "node24";
}

function sandboxTimeout() {
  return parsePositiveInt(process.env.ZERO_BENCH_SANDBOX_TIMEOUT_MS, 10 * 60 * 1000);
}

function sandboxVcpus() {
  return parsePositiveInt(process.env.ZERO_BENCH_SANDBOX_VCPUS, 16);
}

function snapshotFromEnv() {
  if (["1", "true", "yes"].includes((process.env.ZERO_BENCH_SANDBOX_REFRESH ?? "").toLowerCase())) return null;
  const snapshotId = process.env.ZERO_BENCH_SANDBOX_SNAPSHOT_ID?.trim();
  if (!snapshotId) return null;
  return {
    snapshotId,
    projectDir: sandboxProjectDir(),
    timeout: sandboxTimeout(),
    vcpus: sandboxVcpus(),
    reused: true,
  };
}

function writeSnapshotEnv(snapshot) {
  const snapshotPath = join(outDir, "snapshot.env");
  const lines = [
    `ZERO_BENCH_SANDBOX_SNAPSHOT_ID=${snapshot.snapshotId}`,
    `ZERO_BENCH_SANDBOX_PROJECT_DIR=${snapshot.projectDir}`,
    "",
  ];
  writeFileSync(snapshotPath, lines.join("\n"));
  process.stderr.write(`Created Vercel Sandbox benchmark snapshot: ${snapshot.snapshotId}\n`);
  process.stderr.write(`Saved reusable snapshot env to ${snapshotPath}\n`);
  process.stderr.write("Future benchmark runs will reuse it automatically unless ZERO_BENCH_SANDBOX_REFRESH is set.\n");
}

function createSourceArchive() {
  const archivePath = join(outDir, "source.tar.gz");
  rmSync(archivePath, { force: true });
  const excludes = [
    ".git",
    ".cursor",
    ".env",
    ".env.*",
    ".vercel",
    ".zero",
    "node_modules",
    "extensions/*/node_modules",
    "docs/node_modules",
  ];
  const excludeArgs = excludes.flatMap((exclude) => [`--exclude=${exclude}`, `--exclude=./${exclude}`]);
  const result = spawnSync("tar", [...excludeArgs, "-czf", archivePath, "."], {
    cwd: root,
    encoding: "utf8",
    env: { ...process.env, COPYFILE_DISABLE: "1" },
  });
  if (result.status !== 0) {
    throw new Error(`failed to create benchmark source archive\n${result.stderr.trim()}`);
  }
  return archivePath;
}

async function runSandboxCommand(sandbox, params, label) {
  const result = await sandbox.runCommand(params);
  const [stdout, stderr] = await Promise.all([result.stdout(), result.stderr()]);
  if (result.exitCode !== 0) {
    throw new Error(`${label} failed with exit code ${result.exitCode}\n${stderr || stdout}`);
  }
  return { result, stdout, stderr };
}

function describeError(error) {
  if (!(error instanceof Error)) return String(error);
  const parts = [error.message];
  if ("json" in error && error.json) parts.push(JSON.stringify(error.json));
  if ("text" in error && error.text) parts.push(String(error.text));
  return parts.filter(Boolean).join("\n");
}

async function createPreparedSnapshot(Sandbox) {
  const runtime = sandboxRuntime();
  const timeout = sandboxTimeout();
  const vcpus = sandboxVcpus();
  const snapshotExpiration = parseOptionalNonNegativeInt(process.env.ZERO_BENCH_SANDBOX_SNAPSHOT_EXPIRATION_MS);
  const archivePath = createSourceArchive();
  const sandbox = await Sandbox.create({ runtime, timeout, resources: { vcpus } });
  const projectDir = sandboxProjectDir();

  try {
    await sandbox.writeFiles([{ path: "/tmp/zero-lang-source.tar.gz", content: readFileSync(archivePath) }]);
    await runSandboxCommand(
      sandbox,
      {
        cmd: "bash",
        args: [
          "-lc",
          [
            "set -euo pipefail",
            "as_root() { if command -v sudo >/dev/null 2>&1; then sudo \"$@\"; else \"$@\"; fi; }",
            "install_build_tools() {",
            "  if command -v make >/dev/null 2>&1 && { command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1; }; then return; fi",
            "  if command -v apt-get >/dev/null 2>&1; then",
            "    as_root apt-get update",
            "    as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential ca-certificates curl git gzip make tar xz-utils",
            "  elif command -v apk >/dev/null 2>&1; then",
            "    as_root apk add --no-cache build-base ca-certificates curl git gzip make tar xz",
            "  elif command -v dnf >/dev/null 2>&1; then",
            "    # Vercel Sandbox currently runs on Amazon Linux, which exposes dnf/yum.",
            "    as_root dnf install -y ca-certificates gcc git glibc-devel gzip make tar xz",
            "  elif command -v yum >/dev/null 2>&1; then",
            "    # Older Amazon Linux images may expose yum instead of dnf.",
            "    as_root yum install -y ca-certificates gcc git glibc-devel gzip make tar xz",
            "  else",
            "    echo 'no supported package manager found to install make and a C compiler' >&2",
            "    exit 127",
            "  fi",
            "}",
            "install_build_tools",
            "command -v make >/dev/null 2>&1",
            "command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1",
            `mkdir -p ${projectDir}`,
            `tar -xzf /tmp/zero-lang-source.tar.gz -C ${projectDir}`,
            `cd ${projectDir}`,
            "corepack enable",
            "corepack prepare pnpm@11.1.3 --activate",
            "pnpm install --frozen-lockfile",
            "make -C native/zero-c",
            "node --version",
          ].join("\n"),
        ],
        env: sandboxSetupEnv(),
      },
      "benchmark sandbox setup",
    );
    let snapshot;
    try {
      snapshot = await sandbox.snapshot(snapshotExpiration === undefined ? undefined : { expiration: snapshotExpiration });
    } catch (error) {
      throw new Error(`benchmark sandbox snapshot failed\n${describeError(error)}`);
    }
    const preparedSnapshot = { snapshotId: snapshot.snapshotId, projectDir, timeout, vcpus, reused: false };
    writeSnapshotEnv(preparedSnapshot);
    return preparedSnapshot;
  } catch (error) {
    await sandbox.stop({ blocking: true }).catch(() => {});
    throw error;
  }
}

async function runSandboxLanguageGroup(Sandbox, snapshot, group, index) {
  const sandbox = await Sandbox.create({
    source: { type: "snapshot", snapshotId: snapshot.snapshotId },
    timeout: snapshot.timeout,
    resources: { vcpus: snapshot.vcpus },
  });
  try {
    await runSandboxCommand(sandbox, { cmd: "true" }, `${group.language} benchmark sandbox warmup`);
    const command = {
      cmd: "node",
      args: [
        "--experimental-strip-types",
        "--disable-warning=ExperimentalWarning",
        "scripts/bench.mts",
        "--worker",
        "--jobs",
        encodeJobs(group.jobs),
      ],
      cwd: snapshot.projectDir,
      env: sandboxEnv(),
    };
    const { stdout } = await runSandboxCommand(sandbox, command, `${group.language} benchmark sandbox`);
    return JSON.parse(stdout);
  } finally {
    await sandbox.stop({ blocking: true }).catch(() => {});
  }
}

async function runSandboxBench() {
  const { Sandbox } = await import("@vercel/sandbox").catch((error) => {
    throw new Error(`sandbox benchmark mode requires @vercel/sandbox. Run pnpm install before benchmarking.\n${error.message}`);
  });

  const jobs = executableJobs();
  const skipped = skippedResults();
  const groups = benchmarkJobGroups(jobs);
  const resultSlots = jobs.length + skipped.length;
  const skippedNote = skipped.length === 0 ? "" : ` (${skipped.length} skipped result slots recorded locally)`;
  const reusableSnapshot = snapshotFromEnv();
  if (reusableSnapshot) {
    process.stderr.write(`Using Vercel Sandbox benchmark snapshot ${reusableSnapshot.snapshotId} for ${jobs.length} executable jobs in ${groups.length} benchmark sandboxes${skippedNote}...\n`);
  } else {
    if (process.env.ZERO_BENCH_SANDBOX_SNAPSHOT_ID) process.stderr.write("Refreshing Vercel Sandbox benchmark snapshot because ZERO_BENCH_SANDBOX_REFRESH is set...\n");
    process.stderr.write(`Preparing Vercel Sandbox benchmark snapshot for ${jobs.length} executable jobs in ${groups.length} benchmark sandboxes${skippedNote}...\n`);
  }
  const snapshot = reusableSnapshot ?? (await createPreparedSnapshot(Sandbox));
  process.stderr.write(`Running ${jobs.length} executable benchmark jobs across ${groups.length} Vercel sandboxes for ${resultSlots} report rows...\n`);

  const workerReports = await Promise.all(groups.map((group, index) => runSandboxLanguageGroup(Sandbox, snapshot, group, index)));
  const results = sortResults([...workerReports.flatMap((report) => report.results), ...skipped]);
  const environment = {
    ...(workerReports[0]?.environment ?? buildEnvironment()),
    runner: "vercel-sandbox",
    sandboxRuntime: sandboxRuntime(),
    sandboxConcurrency: groups.length,
    sandboxShardStrategy: "zero-benchmark",
    sandboxBenchmarkGroups: groups.map((group) => group.language),
    sandboxSnapshotId: snapshot.snapshotId,
    sandboxSnapshotReused: snapshot.reused,
  };
  return buildReport(results, environment, "vercel-sandbox");
}

function runLocalBench() {
  const results = runJobs(allJobs());
  return buildReport(results, buildEnvironment({ runner: "local" }), "local");
}

async function main() {
  if (workerMode) {
    const results = runJobs(parseWorkerJobs());
    console.log(JSON.stringify({ environment: buildEnvironment({ runner: "vercel-sandbox-worker" }), results }));
    return;
  }

  const report = mode === "local" ? runLocalBench() : await runSandboxBench();
  writeReport(report);
}

main().catch((error) => {
  console.error(error.stack ?? error.message);
  process.exitCode = 1;
});
