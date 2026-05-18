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
const supportedTargets = [
  "darwin-arm64",
  "darwin-x64",
  "linux-musl-x64",
  "linux-musl-arm64",
  "linux-x64",
  "linux-arm64",
  "win32-x64.exe",
  "win32-arm64.exe",
  "wasm32-wasi",
  "wasm32-web",
];
const artifactSummaryPattern = /\((?:\d+ B|\d+\.\d KiB|\d+\.\d MiB|\d+\.\d GiB), (?:\d+ ms|\d+\.\d s)\)/;
const runnableDirectTarget =
  process.platform === "darwin" && process.arch === "arm64" ? "darwin-arm64" :
  process.platform === "linux" && process.arch === "x64" ? "linux-musl-x64" :
  null;

function runnableBuildArgs(input: string, out: string) {
  if (runnableDirectTarget) return ["build", "--emit", "exe", "--target", runnableDirectTarget, input, "--out", out];
  throw new Error("no runnable direct target for this host");
}

function runZero(args: string[], options: { cwd?: string; env?: NodeJS.ProcessEnv } = {}) {
  return execFileAsync(zero, args, { cwd: options.cwd ?? root, env: options.env ?? process.env });
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
        `pub fun main(world: World) -> Void raises {
    check world.out.write("hello from demo\\n")
}
`,
      );

      assert.match((await runZero(["check", project])).stdout, /ok/);
      await runZero(runnableBuildArgs(project, exe));
      assert.match((await execFileAsync(exe, [])).stdout, /hello from demo/);
    } finally {
      await rm(cwd, { force: true, recursive: true });
    }
  });

  it("creates secure templates for zero new", async () => {
    const workDir = await mkdtemp(join(tmpdir(), "zero-new-"));
    const keysDir = await mkdtemp(join(tmpdir(), "zero-keys-"));
    const env = {
      ...process.env,
      ZERO_KEYS_DIR: keysDir,
      ZERO_KEY_EMAIL: "dev@example.com",
      ZERO_KEY_NAME: "Dev Example",
      ZERO_TRUSTED_KEYS: trustedKeysPath,
      ZERO_TRUSTED_KEYS_SIG: trustedKeysSigPath,
    };

    try {
      const cli = await runZero(["new", "cli", "demo-cli"], { cwd: workDir, env: { ...env, ZERO_KEY_SEED: seedDefault } });

      const keyId = await readActiveKey(keysDir);
      const keyDir = join(keysDir, keyId);
      await access(join(keyDir, "public.key"));
      await access(join(keyDir, "private.key"));
      await access(join(keyDir, "identity.txt"));

      const publicKey = await readPublicKey(keysDir, keyId);
      assert.match(cli.stdout, /Here's your package:/);
      assert.match(cli.stdout, new RegExp(`pubkey: ${publicKey}`));

      const identity = await readIdentity(keysDir, keyId);
      assert.equal(identity.label, "default");
      assert.equal(identity.name, "Dev Example");
      assert.equal(identity.email, "dev@example.com");
      assert.equal(identity.keyId, keyId);
      const ledgerPath = join(workDir, "demo-cli", "ledger.json");
      const ledgerText = await readFile(ledgerPath, "utf8");
      const ledger = JSON.parse(ledgerText);
      assert.equal(ledger.schemaVersion, 1);
      assert.equal(ledger.format, "zero-ledger-v1");
      assert.equal(ledger.package.name, "demo-cli");

      const entry = ledger.entries[0];
      assert.equal(entry.kind, "keys");
      assert.equal(entry.seq, 1);
      assert.equal(entry.prevHash, "");
      assert.equal(entry.data.threshold, 1);
      assert.deepEqual(entry.data.revoked, []);
      assert.equal(entry.data.publishers.length, 1);
      assert.equal(entry.signatures.length, 1);
      assert.equal(entry.data.publishers[0].keyId, keyId);
      assert.equal(entry.data.publishers[0].publicKey, publicKey);
      assert.equal(entry.data.publishers[0].label, "default");
      assert.equal(entry.data.publishers[0].name, "Dev Example");
      assert.equal(entry.data.publishers[0].email, "dev@example.com");

      const dataText = ledgerDataText(ledgerText);
      const hash = createHash("sha256").update(dataText, "utf8").digest("hex");
      assert.equal(entry.hash, hash);

      if (process.platform !== "win32") {
        const publicMode = (await stat(join(keyDir, "public.key"))).mode & 0o777;
        const privateMode = (await stat(join(keyDir, "private.key"))).mode & 0o777;
        const identityMode = (await stat(join(keyDir, "identity.txt"))).mode & 0o777;
        assert.equal(publicMode, 0o644);
        assert.equal(privateMode, 0o600);
        assert.equal(identityMode, 0o644);
      }

      const lib = await runZero(["new", "lib", "demo-lib"], { cwd: workDir, env });
      assert.match(lib.stdout, new RegExp(`pubkey: ${publicKey}`));
      assert.match(lib.stdout, /next: zero check/);

      const pkg = await runZero(["new", "package", "demo-pkg"], { cwd: workDir, env });
      assert.match(pkg.stdout, new RegExp(`pubkey: ${publicKey}`));
      assert.match(pkg.stdout, /next: zero check/);
    } finally {
      await rm(workDir, { force: true, recursive: true });
      await rm(keysDir, { force: true, recursive: true });
    }
  });

  it("manages keys and ledger entries", async () => {
    const workDir = await mkdtemp(join(tmpdir(), "zero-ledger-"));
    const keysDir = await mkdtemp(join(tmpdir(), "zero-keys-ledger-"));
    const env = {
      ...process.env,
      ZERO_KEYS_DIR: keysDir,
      ZERO_KEY_EMAIL: "dev@example.com",
      ZERO_KEY_NAME: "Dev Example",
      ZERO_TRUSTED_KEYS: trustedKeysPath,
      ZERO_TRUSTED_KEYS_SIG: trustedKeysSigPath,
    };

    try {
      await runZero(["new", "package", "demo"], { cwd: workDir, env: { ...env, ZERO_KEY_SEED: seedDefault } });
      const ledgerPath = join(workDir, "demo", "ledger.json");
      const activeKey = await readActiveKey(keysDir);

      await runZero(["keys", "new", "--email", "backup@example.com", "--label", "backup", "--no-activate"], { cwd: workDir, env: { ...env, ZERO_KEY_SEED: seedBackup } });
      const list = JSON.parse((await runZero(["keys", "list", "--json"], { cwd: workDir, env })).stdout);
      const backupKey = list.keys.find((item: { label: string }) => item.label === "backup")?.id;
      assert.ok(backupKey);
      assert.equal(list.keys.find((item: { id: string }) => item.id === activeKey)?.active, true);

      const show = JSON.parse((await runZero(["keys", "show", "backup", "--json"], { cwd: workDir, env })).stdout);
      assert.equal(show.key.id, backupKey);

      await runZero(["keys", "use", "backup"], { cwd: workDir, env });
      assert.equal(await readActiveKey(keysDir), backupKey);
      await runZero(["keys", "use", activeKey], { cwd: workDir, env });

      await runZero(["keys", "authorize", "--ledger", ledgerPath, "--key", "backup"], { cwd: workDir, env });
      let ledger = JSON.parse(await readFile(ledgerPath, "utf8"));
      assert.equal(ledger.entries.length, 2);
      assert.equal(ledger.entries[1].data.publishers.some((item: { keyId: string }) => item.keyId === backupKey), true);

      await runZero(["keys", "revoke", "--ledger", ledgerPath, "--key", "backup", "--reason", "left"], { cwd: workDir, env });
      ledger = JSON.parse(await readFile(ledgerPath, "utf8"));
      assert.equal(ledger.entries.length, 3);
      assert.equal(ledger.entries[2].data.revoked.some((item: { keyId: string; reason: string }) => item.keyId === backupKey && item.reason === "left"), true);

      await runZero(["keys", "new", "--email", "third@example.com", "--label", "third", "--no-activate"], { cwd: workDir, env: { ...env, ZERO_KEY_SEED: seedThird } });
      await runZero(["keys", "rotate", "--ledger", ledgerPath, "--old", "backup", "--new", "third"], { cwd: workDir, env });
      ledger = JSON.parse(await readFile(ledgerPath, "utf8"));
      assert.equal(ledger.entries.length, 4);
      assert.equal(ledger.entries[3].data.publishers.some((item: { label: string }) => item.label === "third"), true);
      assert.equal(ledger.entries[3].data.revoked.some((item: { keyId: string; reason: string }) => item.keyId === backupKey && item.reason === "left"), true);
    } finally {
      await rm(workDir, { force: true, recursive: true });
      await rm(keysDir, { force: true, recursive: true });
    }
  });

  it("rejects tampered trusted keys", async () => {
    const workDir = await mkdtemp(join(tmpdir(), "zero-trust-"));
    const keysDir = await mkdtemp(join(tmpdir(), "zero-trust-keys-"));
    const env = {
      ...process.env,
      ZERO_KEYS_DIR: keysDir,
      ZERO_KEY_EMAIL: "dev@example.com",
      ZERO_KEY_NAME: "Dev Example",
      ZERO_TRUSTED_KEYS: trustedKeysPath,
      ZERO_TRUSTED_KEYS_SIG: trustedKeysSigPath,
    };
    const tamperedPath = join(workDir, "trusted-keys.json");

    try {
      const source = await readFile(trustedKeysPath, "utf8");
      await writeFile(tamperedPath, `${source}\n`);
      const result = await runZero(["new", "package", "demo"], {
        cwd: workDir,
        env: { ...env, ZERO_TRUSTED_KEYS: tamperedPath, ZERO_KEY_SEED: seedDefault },
      }).catch((error) => error);
      assert.notEqual(result.code, 0);
      assert.match(result.stderr ?? result.stdout, /trusted keys signature invalid/);
    } finally {
      await rm(workDir, { force: true, recursive: true });
      await rm(keysDir, { force: true, recursive: true });
    }
  });
  it("reports graph, size, routes, and targets", async () => {
    const graph = JSON.parse((await runZero(["graph", "--json", "examples/point.0"])).stdout);
    assert.equal(graph.shapes[0].name, "Point");
    assert.equal(graph.functions.some((item: { name: string }) => item.name === "main"), true);

    const size = JSON.parse((await runZero(["size", "--json", "examples/point.0"])).stdout);
    assert.equal(size.schemaVersion, 1);
    assert.equal(size.generatedCBytes, 0);

    const routes = JSON.parse((await runZero(["routes", "--json", "examples/web/hello"])).stdout);
    assert.equal(routes.routes[0].path, "/");

    const objOut = join(tmpdir(), `zero-target-${Date.now()}.o`);
    try {
      const objBuild = await runZero(["build", "--emit", "obj", "--target", "linux-musl-arm64", "examples/hello.0", "--out", objOut]);
      assert.ok(objBuild.stdout.includes(`${objOut} (`));
      assert.match(objBuild.stdout, artifactSummaryPattern);
      const bytes = await readFile(objOut);
      assert.equal(bytes[0], 0x7f);
      assert.equal(bytes[1], 0x45);
    } finally {
      await rm(objOut, { force: true });
    }

    const targets = JSON.parse((await runZero(["targets"])).stdout);
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
      "zero-agent",
      "zero-builds",
      "zero-diagnostics",
      "zero-language",
      "zero-packages",
      "zero-stdlib",
      "zero-testing",
    ]) {
      assert.equal(skillNames.has(name), true);
    }

    const zeroSkill = JSON.parse((await runZero(["skills", "get", "zero", "--full", "--json"])).stdout);
    assert.equal(zeroSkill.success, true);
    assert.match(zeroSkill.data[0].content, /# Zero/);
    assert.match(zeroSkill.data[0].content, /zero skills get zero --full/);
    assert.equal(zeroSkill.data[0].files, undefined);

    const languageSkill = JSON.parse((await runZero(["skills", "get", "zero-language", "--json"])).stdout);
    assert.equal(languageSkill.success, true);
    assert.match(languageSkill.data[0].content, /# Zero Language/);
    assert.match(languageSkill.data[0].content, /pub fun main/);

    const diagnosticSkill = JSON.parse((await runZero(["skills", "get", "zero-diagnostics", "--json"])).stdout);
    assert.equal(diagnosticSkill.success, true);
    assert.match(diagnosticSkill.data[0].content, /fixSafety/);

    const zeroPath = JSON.parse((await runZero(["skills", "path", "zero", "--json"])).stdout);
    assert.equal(zeroPath.success, true);
    assert.match(zeroPath.data.path, /skills\/zero$/);

    const languagePath = JSON.parse((await runZero(["skills", "path", "zero-language", "--json"])).stdout);
    assert.equal(languagePath.success, true);
    assert.match(languagePath.data.path, /skill-data\/zero-language\.md$/);

    const missing = await runZero(["skills", "get", "missing", "--json"]).catch((error) => error);
    assert.notEqual(missing.code, 0);
    assert.equal(JSON.parse(missing.stdout).success, false);
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
    const result = await runZero(["check", "--target", "mips-unknown-none", "examples/hello.0"]).catch((error) => error);
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
    const result = await runZero(["check", "conformance/native/fail/unknown-field.0"]).catch((error) => error);
    assert.notEqual(result.code, 0);
    assert.match(result.stderr, /FLD001/);
    assert.match(result.stderr, /explain: zero explain FLD001/);
  });
});
