#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { spawn } from "node:child_process";
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const nodeArgs = ["--experimental-strip-types", "--disable-warning=ExperimentalWarning"];
const reportDir = ".zero/validation";

type Phase = {
  name: string;
  command: string;
  args: string[];
  setup?: boolean;
};

type Suite = {
  setup: Phase[];
  phases: Phase[];
  defaultJobs?: number;
};

type PhaseResult = {
  name: string;
  command: string;
  ok: boolean;
  code: number | null;
  signal: NodeJS.Signals | null;
  durationMs: number;
  stdout: string;
  stderr: string;
};

const suites: Record<string, Suite> = {
  conformance: {
    setup: [
      { name: "native-build", command: "make", args: ["-C", "native/zero-c"], setup: true },
    ],
    phases: [
      { name: "graph-input-policy", command: process.execPath, args: [...nodeArgs, "scripts/graph-input-policy.mts"] },
      { name: "native-contracts", command: process.execPath, args: [...nodeArgs, "scripts/native-contracts.mts"] },
      { name: "provenance-guardrails", command: process.execPath, args: [...nodeArgs, "scripts/provenance-guardrails.mts"] },
      { name: "type-core-smoke", command: process.execPath, args: [...nodeArgs, "scripts/type-core-smoke.mts"] },
      { name: "mir-verifier-smoke", command: process.execPath, args: [...nodeArgs, "scripts/mir-verifier-smoke.mts"] },
      { name: "program-graph-smoke", command: process.execPath, args: [...nodeArgs, "scripts/program-graph-smoke.mts"] },
      { name: "program-graph-parity", command: process.execPath, args: [...nodeArgs, "scripts/program-graph-parity.mts"] },
      { name: "canonical-text-smoke", command: process.execPath, args: [...nodeArgs, "scripts/canonical-text-smoke.mts"] },
      { name: "conformance-run", command: process.execPath, args: ["conformance/run.mjs"] },
    ],
  },
  "command-contracts": {
    setup: [],
    phases: [
      { name: "graph-input-policy", command: process.execPath, args: [...nodeArgs, "scripts/graph-input-policy.mts"] },
      { name: "snapshot-command-contracts", command: process.execPath, args: [...nodeArgs, "scripts/snapshot-command-contracts.mts"] },
    ],
  },
};

function usage() {
  const suiteNames = Object.keys(suites).join("|");
  console.log(`Run validation phases with aggregate failure reporting.

Usage:
  node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/validation-suite.mts <${suiteNames}> [options]

Options:
  --list                 List phases and exit.
  --shard <index/count>  Run only this 1-based phase shard after setup.
  --jobs <count>         Run selected phases concurrently. Defaults to 1.
  --fail-fast            Stop after the first failing phase.

Environment:
  ZERO_VALIDATION_SHARD       Default shard, for example 1/4.
  ZERO_VALIDATION_JOBS        Default jobs count.
  ZERO_VALIDATION_FAIL_FAST=1 Stop after the first failing phase.`);
}

const rawArgs = process.argv.slice(2);
if (rawArgs.includes("--help") || rawArgs.includes("-h")) {
  usage();
  process.exit(0);
}

const suiteName = rawArgs.find((arg) => !arg.startsWith("--"));
if (!suiteName || !suites[suiteName]) {
  usage();
  process.exit(2);
}

function optionValue(name: string) {
  const index = rawArgs.indexOf(name);
  return index === -1 ? undefined : rawArgs[index + 1];
}

function parsePositiveInt(value: string | undefined, fallback: number) {
  const parsed = Number.parseInt(value ?? "", 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

function parseShard(value: string | undefined) {
  const text = value || process.env.ZERO_VALIDATION_SHARD || "";
  if (!text) return null;
  const match = /^([1-9][0-9]*)\/([1-9][0-9]*)$/.exec(text);
  if (!match) throw new Error(`invalid shard ${text}; expected index/count, for example 1/4`);
  const index = Number.parseInt(match[1], 10);
  const count = Number.parseInt(match[2], 10);
  if (index > count) throw new Error(`invalid shard ${text}; index must be <= count`);
  return { index, count, text };
}

const suite = suites[suiteName];
const listOnly = rawArgs.includes("--list");
const shard = parseShard(optionValue("--shard"));
const jobs = parsePositiveInt(optionValue("--jobs") ?? process.env.ZERO_VALIDATION_JOBS, suite.defaultJobs ?? 1);
const failFast = rawArgs.includes("--fail-fast") || process.env.ZERO_VALIDATION_FAIL_FAST === "1";

const selectedPhases = shard
  ? suite.phases.filter((_, index) => index % shard.count === shard.index - 1)
  : suite.phases;

if (listOnly) {
  for (const phase of suite.setup) console.log(`${phase.name} setup`);
  for (const [index, phase] of suite.phases.entries()) {
    const selected = selectedPhases.includes(phase) ? "selected" : "skipped";
    console.log(`${index + 1}. ${phase.name} ${selected}`);
  }
  process.exit(0);
}

function runPhase(phase: Phase): Promise<PhaseResult> {
  const startedAt = Date.now();
  process.stderr.write(`validation phase start: ${phase.name}\n`);
  return new Promise((resolve) => {
    const child = spawn(phase.command, phase.args, {
      cwd: process.cwd(),
      env: process.env,
      stdio: ["ignore", "pipe", "pipe"],
    });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk) => {
      const text = chunk.toString();
      stdout += text;
      process.stdout.write(text);
    });
    child.stderr.on("data", (chunk) => {
      const text = chunk.toString();
      stderr += text;
      process.stderr.write(text);
    });
    child.on("error", (error) => {
      stderr += `${error.message}\n`;
    });
    child.on("close", (code, signal) => {
      const durationMs = Date.now() - startedAt;
      const ok = code === 0 && signal === null;
      process.stderr.write(`validation phase ${ok ? "ok" : "failed"}: ${phase.name} (${durationMs}ms)\n`);
      resolve({
        name: phase.name,
        command: [phase.command, ...phase.args].join(" "),
        ok,
        code,
        signal,
        durationMs,
        stdout,
        stderr,
      });
    });
  });
}

async function runSequential(phases: Phase[]) {
  const results: PhaseResult[] = [];
  for (const phase of phases) {
    const result = await runPhase(phase);
    results.push(result);
    if (!result.ok && failFast) break;
  }
  return results;
}

async function runConcurrent(phases: Phase[]) {
  const results: PhaseResult[] = [];
  for (let start = 0; start < phases.length; start += jobs) {
    const batch = phases.slice(start, start + jobs);
    const batchResults = await Promise.all(batch.map(runPhase));
    results.push(...batchResults);
    if (failFast && batchResults.some((result) => !result.ok)) break;
  }
  return results;
}

function printFailures(results: any[]) {
  const failures = results.filter((result) => !result.ok);
  if (failures.length === 0) return;
  process.stderr.write(`\n${suiteName} collected ${failures.length} failing phase(s):\n`);
  for (const [index, failure] of failures.entries()) {
    process.stderr.write(`\n${index + 1}. ${failure.name}\n`);
    process.stderr.write(`   command: ${failure.command}\n`);
    process.stderr.write(`   exit: ${failure.code ?? "signal"}${failure.signal ? ` signal ${failure.signal}` : ""}\n`);
    const detail = (failure.stderr || failure.stdout || "").trim().split("\n").slice(-40).join("\n");
    if (detail) process.stderr.write(`${detail}\n`);
  }
}

async function main() {
  mkdirSync(reportDir, { recursive: true });
  const startedAt = Date.now();
  const setupResults = await runSequential(suite.setup);
  const setupFailed = setupResults.some((result) => !result.ok);
  const phaseResults = setupFailed && failFast
    ? []
    : jobs > 1 ? await runConcurrent(selectedPhases) : await runSequential(selectedPhases);
  const results = [...setupResults, ...phaseResults];
  const ok = results.every((result) => result.ok);
  const report = {
    suite: suiteName,
    ok,
    shard: shard?.text ?? null,
    jobs,
    failFast,
    durationMs: Date.now() - startedAt,
    results,
  };
  const reportPath = join(reportDir, `${suiteName}${shard ? `-shard-${shard.index}-of-${shard.count}` : ""}.json`);
  writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
  printFailures(results);
  if (!ok) {
    process.stderr.write(`\nvalidation suite failed: ${suiteName}\nreport: ${reportPath}\n`);
    process.exit(1);
  }
  process.stderr.write(`validation suite ok: ${suiteName}\nreport: ${reportPath}\n`);
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack || error.message : String(error));
  process.exit(1);
});
