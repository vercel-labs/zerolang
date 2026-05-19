#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const root = process.cwd();
const outDir = join(root, ".zero", "native-test-sandbox");
mkdirSync(outDir, { recursive: true });
loadDotEnvFiles([".env", ".env.local"]);
const args = process.argv.slice(2);

if (args.includes("--help") || args.includes("-h")) {
  console.log(`Run native test commands inside Vercel Sandbox.

Usage:
  pnpm run native:test:sandbox
  node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/native-test-sandbox.mts -- pnpm run conformance

Environment:
  ZERO_NATIVE_TEST_SANDBOX_SNAPSHOT_ID       Reuse a prepared native-test snapshot
  ZERO_BENCH_SANDBOX_SNAPSHOT_ID             Fallback reusable snapshot id from benchmark setup
  ZERO_NATIVE_TEST_SANDBOX_REFRESH=1         Ignore the snapshot id and rebuild
  ZERO_NATIVE_TEST_SANDBOX_PROJECT_DIR       Remote project directory
  ZERO_NATIVE_TEST_SANDBOX_RUNTIME           Vercel Sandbox runtime, defaults to node24
  ZERO_NATIVE_TEST_SANDBOX_TIMEOUT_MS        Sandbox timeout, defaults to 600000
  ZERO_NATIVE_TEST_SANDBOX_VCPUS             Sandbox vCPU count, defaults to 16
  ZERO_SANDBOX_COMMAND                       Command to run when no -- command is passed

The runner uploads a source archive that excludes generated build output and never copies native test binaries back to the local machine.`);
  process.exit(0);
}

function shellQuote(value) {
  if (/^[A-Za-z0-9_/:=.,@%+-]+$/.test(value)) return value;
  return `'${value.replace(/'/g, "'\\''")}'`;
}

function sandboxCommand() {
  const separator = args.indexOf("--");
  if (separator !== -1 && separator + 1 < args.length) {
    return args.slice(separator + 1).map(shellQuote).join(" ");
  }
  return process.env.ZERO_SANDBOX_COMMAND?.trim() || "bash scripts/test-native.sh";
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

function sandboxProjectDir() {
  return process.env.ZERO_NATIVE_TEST_SANDBOX_PROJECT_DIR ||
    process.env.ZERO_BENCH_SANDBOX_PROJECT_DIR ||
    "/vercel/sandbox/zero-lang";
}

function sandboxRuntime() {
  return process.env.ZERO_NATIVE_TEST_SANDBOX_RUNTIME ||
    process.env.ZERO_BENCH_SANDBOX_RUNTIME ||
    "node24";
}

function sandboxTimeout() {
  return parsePositiveInt(
    process.env.ZERO_NATIVE_TEST_SANDBOX_TIMEOUT_MS ?? process.env.ZERO_BENCH_SANDBOX_TIMEOUT_MS,
    10 * 60 * 1000,
  );
}

function sandboxVcpus() {
  return parsePositiveInt(
    process.env.ZERO_NATIVE_TEST_SANDBOX_VCPUS ?? process.env.ZERO_BENCH_SANDBOX_VCPUS,
    16,
  );
}

function snapshotFromEnv() {
  const refresh = (process.env.ZERO_NATIVE_TEST_SANDBOX_REFRESH ?? "").toLowerCase();
  if (["1", "true", "yes"].includes(refresh)) return null;
  const snapshotId = process.env.ZERO_NATIVE_TEST_SANDBOX_SNAPSHOT_ID?.trim() ||
    process.env.ZERO_BENCH_SANDBOX_SNAPSHOT_ID?.trim();
  if (!snapshotId) return null;
  return {
    snapshotId,
    projectDir: sandboxProjectDir(),
    timeout: sandboxTimeout(),
    vcpus: sandboxVcpus(),
    reused: true,
  };
}

function ensureVercelAuthEnv() {
  if (process.env.VERCEL_OIDC_TOKEN?.trim()) return;
  throw new Error(
    "native:test:sandbox requires VERCEL_OIDC_TOKEN. Put it in .env or .env.local, matching the benchmark sandbox runner, or export it before running.",
  );
}

function writeSnapshotEnv(snapshot) {
  const snapshotPath = join(outDir, "snapshot.env");
  const lines = [
    `ZERO_NATIVE_TEST_SANDBOX_SNAPSHOT_ID=${snapshot.snapshotId}`,
    `ZERO_NATIVE_TEST_SANDBOX_PROJECT_DIR=${snapshot.projectDir}`,
    "",
  ];
  writeFileSync(snapshotPath, lines.join("\n"));
  process.stderr.write(`Created Vercel Sandbox native-test snapshot: ${snapshot.snapshotId}\n`);
  process.stderr.write(`Saved reusable snapshot env to ${snapshotPath}\n`);
  process.stderr.write("Add ZERO_NATIVE_TEST_SANDBOX_SNAPSHOT_ID to .env to reuse it, or set ZERO_NATIVE_TEST_SANDBOX_REFRESH=1 to rebuild it.\n");
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
    ".next",
    "dist",
    "coverage",
    "node_modules",
    "docs/.next",
    "docs/out",
    "extensions/*/node_modules",
    "docs/node_modules",
  ];
  const excludeArgs = excludes.flatMap((exclude) => [`--exclude=${exclude}`, `--exclude=./${exclude}`]);
  const metadataArgs = process.platform === "darwin" ? ["--no-xattrs"] : [];
  const result = spawnSync("tar", [...metadataArgs, ...excludeArgs, "-czf", archivePath, "."], {
    cwd: root,
    encoding: "utf8",
    env: { ...process.env, COPYFILE_DISABLE: "1" },
  });
  if (result.status !== 0) throw new Error(`failed to create native-test source archive\n${result.stderr.trim()}`);
  return archivePath;
}

async function runSandboxCommand(sandbox, params, label) {
  const result = await sandbox.runCommand(params);
  const [stdout, stderr] = await Promise.all([result.stdout(), result.stderr()]);
  if (stdout) process.stdout.write(stdout);
  if (stderr) process.stderr.write(stderr);
  if (result.exitCode !== 0) {
    throw new Error(`${label} failed with exit code ${result.exitCode}`);
  }
  return { stdout, stderr };
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
  const snapshotExpiration = parseOptionalNonNegativeInt(process.env.ZERO_NATIVE_TEST_SANDBOX_SNAPSHOT_EXPIRATION_MS);
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
            "    as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential ca-certificates git gzip make tar",
            "  elif command -v apk >/dev/null 2>&1; then",
            "    as_root apk add --no-cache build-base ca-certificates git gzip make tar",
            "  elif command -v dnf >/dev/null 2>&1; then",
            "    as_root dnf install -y ca-certificates gcc git glibc-devel gzip make tar",
            "  elif command -v yum >/dev/null 2>&1; then",
            "    as_root yum install -y ca-certificates gcc git glibc-devel gzip make tar",
            "  else",
            "    echo 'no supported package manager found to install make and a C compiler' >&2",
            "    exit 127",
            "  fi",
            "}",
            "install_build_tools",
            "command -v make >/dev/null 2>&1",
            "command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1",
            `rm -rf ${projectDir}`,
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
      },
      "native-test sandbox setup",
    );
    const snapshot = await sandbox.snapshot(snapshotExpiration === undefined ? undefined : { expiration: snapshotExpiration });
    const preparedSnapshot = { snapshotId: snapshot.snapshotId, projectDir, timeout, vcpus, reused: false };
    writeSnapshotEnv(preparedSnapshot);
    return preparedSnapshot;
  } catch (error) {
    await sandbox.stop({ blocking: true }).catch(() => {});
    throw error;
  }
}

async function runSandboxTask(Sandbox, snapshot, command) {
  const archivePath = createSourceArchive();
  const sandbox = await Sandbox.create({
    source: { type: "snapshot", snapshotId: snapshot.snapshotId },
    timeout: snapshot.timeout,
    resources: { vcpus: snapshot.vcpus },
  });
  try {
    await sandbox.writeFiles([{ path: "/tmp/zero-lang-source-current.tar.gz", content: readFileSync(archivePath) }]);
    await runSandboxCommand(
      sandbox,
      {
        cmd: "bash",
        args: [
          "-lc",
          [
            "set -euxo pipefail",
            `cd ${snapshot.projectDir}`,
            "find . -mindepth 1 -maxdepth 1 ! -name node_modules -exec rm -rf {} +",
            `tar -xzf /tmp/zero-lang-source-current.tar.gz -C ${snapshot.projectDir}`,
            "corepack enable",
            "corepack prepare pnpm@11.1.3 --activate",
            "pnpm install --frozen-lockfile",
            "make -C native/zero-c",
            command,
          ].join("\n"),
        ],
        cwd: snapshot.projectDir,
        env: { ZERO_NATIVE_TEST_SANDBOX: "1" },
      },
      `${command} in Vercel Sandbox`,
    );
  } finally {
    await sandbox.stop({ blocking: true }).catch(() => {});
  }
}

async function main() {
  ensureVercelAuthEnv();
  const { Sandbox } = await import("@vercel/sandbox").catch((error) => {
    throw new Error(`native:test:sandbox requires @vercel/sandbox. Run pnpm install first.\n${error.message}`);
  });

  const reusableSnapshot = snapshotFromEnv();
  const snapshot = reusableSnapshot ?? (await createPreparedSnapshot(Sandbox));
  const reuseText = snapshot.reused ? "reused" : "fresh";
  const command = sandboxCommand();
  process.stderr.write(`Running ${command} in Vercel Sandbox using ${reuseText} snapshot ${snapshot.snapshotId}...\n`);
  await runSandboxTask(Sandbox, snapshot, command);
  process.stderr.write(`sandbox command ok: ${command}\n`);
}

main().catch((error) => {
  console.error(describeError(error));
  process.exit(1);
});
