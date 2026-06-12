#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { spawn, spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

// Every example package with a committed zero.graph must pass `zero check`,
// build an executable for the canonical linux-musl-x64 target, and `zero run`
// cleanly. This closes the gap where a stale example keeps passing check while
// run-compilation fails. Host-backend gaps (for example fs programs on the
// Mach-O direct backend) are tolerated only after the linux build proves the
// package is not stale; hosts whose backend supports the package still run it.

const zeroBin = resolve("bin/zero");
const examplesDir = resolve("examples");
const buildTarget = "linux-musl-x64";
const runTimeoutMs = 120_000;

type Expectation = {
  exitCode?: number;
  output?: string;
  server?: { readyText: string };
};

const expectations: Record<string, Expectation> = {
  "batch3-cli": { output: "batch3 cli ok" },
  "direct-package-arrays": { exitCode: 13 },
  "direct-package-call-order": { exitCode: 27 },
  "memory-package": { output: "memory package ok" },
  "ping-pong-api": { server: { readyText: "listening on " } },
  "readall-cli": { output: "readall cli ok" },
  "resource-cli": { output: "resource cli ok" },
  "systems-package": { output: "systems package" },
  "zero-hash": { output: "zero-hash ok" },
};

const packages = readdirSync(examplesDir, { withFileTypes: true })
  .filter((entry) => entry.isDirectory() && existsSync(join(examplesDir, entry.name, "zero.graph")))
  .map((entry) => entry.name)
  .sort();

if (packages.length === 0) {
  console.error("examples gate found no example packages with committed zero.graph stores");
  process.exit(1);
}

const staleExpectations = Object.keys(expectations).filter((name) => !packages.includes(name));
if (staleExpectations.length > 0) {
  console.error(`examples gate expectations name missing packages: ${staleExpectations.join(", ")}`);
  process.exit(1);
}

function runZero(args: string[], cwd: string) {
  const result = spawnSync(zeroBin, args, {
    cwd,
    encoding: "utf8",
    timeout: runTimeoutMs,
    stdio: ["ignore", "pipe", "pipe"],
  });
  return {
    code: result.status,
    signal: result.signal,
    stdout: result.stdout ?? "",
    stderr: result.stderr ?? "",
  };
}

function isHostBackendGap(text: string) {
  return text.includes("BLD004") && text.includes("executable buildability subset");
}

async function probeServer(packagePath: string, readyText: string, workDir: string) {
  return await new Promise<{ ok: boolean; detail: string }>((settle) => {
    const child = spawn(zeroBin, ["run", packagePath], { cwd: workDir, detached: true, stdio: ["ignore", "pipe", "pipe"] });
    let output = "";
    let done = false;
    const finish = (ok: boolean, detail: string) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      if (child.pid !== undefined && child.exitCode === null) {
        try {
          process.kill(-child.pid, "SIGKILL");
        } catch {
          child.kill("SIGKILL");
        }
      }
      settle({ ok, detail });
    };
    const timer = setTimeout(() => finish(false, `server never printed '${readyText}'\n${output}`), runTimeoutMs);
    const onChunk = (chunk: Buffer) => {
      output += chunk.toString();
      if (output.includes(readyText)) finish(true, "server ready");
    };
    child.stdout.on("data", onChunk);
    child.stderr.on("data", onChunk);
    child.on("error", (error) => finish(false, error.message));
    child.on("exit", (code, signal) => finish(false, `server exited before readiness: code ${code} signal ${signal}\n${output}`));
  });
}

const failures: string[] = [];
const startedAt = Date.now();

for (const name of packages) {
  const packagePath = join(examplesDir, name);
  const expectation = expectations[name] ?? {};
  const workDir = mkdtempSync(join(tmpdir(), `zero-examples-gate-${name}-`));
  const steps: string[] = [];
  let failure = "";

  const check = runZero(["check", packagePath], workDir);
  if (check.code === 0) {
    steps.push("check");
  } else {
    failure = `zero check failed (exit ${check.code})\n${check.stderr || check.stdout}`;
  }

  let linuxBuildOk = false;
  if (!failure && !expectation.server) {
    const build = runZero(["build", "--emit", "exe", "--target", buildTarget, packagePath, "--out", join(workDir, `${name}-${buildTarget}`)], workDir);
    if (build.code === 0) {
      linuxBuildOk = true;
      steps.push(`build ${buildTarget}`);
    } else {
      failure = `zero build --target ${buildTarget} failed (exit ${build.code})\n${build.stderr || build.stdout}`;
    }
  }

  if (!failure && expectation.server) {
    const probe = await probeServer(packagePath, expectation.server.readyText, workDir);
    if (probe.ok) {
      steps.push("run server-ready");
    } else {
      failure = `zero run server probe failed: ${probe.detail}`;
    }
  } else if (!failure) {
    const expectedExit = expectation.exitCode ?? 0;
    // Arguments after the input belong to the program (zero run [input] -- <args>),
    // so --out must precede the package path or it lands in the program argv.
    const run = runZero(["run", "--out", join(workDir, `${name}-exe`), packagePath], workDir);
    const runText = `${run.stdout}${run.stderr}`;
    if (run.code !== expectedExit && linuxBuildOk && isHostBackendGap(runText)) {
      steps.push("run host-backend-gap");
    } else if (run.code !== expectedExit) {
      failure = `zero run exited ${run.code}${run.signal ? ` signal ${run.signal}` : ""}, expected ${expectedExit}\n${runText}`;
    } else if (expectation.output && !run.stdout.includes(expectation.output)) {
      failure = `zero run output missing '${expectation.output}'\n${runText}`;
    } else {
      steps.push("run");
    }
  }

  // Retry the teardown: macOS indexing can briefly repopulate the temp tree
  // while rm walks it, surfacing as ENOTEMPTY on an otherwise clean removal.
  rmSync(workDir, { force: true, recursive: true, maxRetries: 5, retryDelay: 100 });
  if (failure) {
    failures.push(`${name}: ${failure.trimEnd()}`);
    console.error(`examples gate FAIL: ${name}`);
  } else {
    console.log(`examples gate ok: ${name} (${steps.join(", ")})`);
  }
}

if (failures.length > 0) {
  console.error(`\nexamples gate failed for ${failures.length} package(s):\n`);
  for (const failure of failures) console.error(`${failure}\n`);
  process.exit(1);
}

console.log(`examples gate ok: ${packages.length} packages in ${((Date.now() - startedAt) / 1000).toFixed(1)}s`);
