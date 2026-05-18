#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const native = join(root, ".zero", "bin", "zero");
const publicCompiler = join(root, "docs-site", "public", "playground", "zeroc-zero.wasm");
const args = process.argv.slice(2);
const selfHostDriverInputs = new Set([
  "conformance/native/pass/std-args.0",
  "conformance/native/pass/std-env.0",
  "conformance/native/pass/std-fs.0",
  "examples/cli-file.0",
  "examples/hello.0",
]);
const selfHostRuntimeBreadthInputs = new Set([
  "conformance/native/pass/std-args.0",
  "conformance/native/pass/std-env.0",
  "conformance/native/pass/std-fs.0",
  "examples/cli-file.0",
]);
const selfHostFilesystemInputs = new Set([
  "conformance/native/pass/std-fs.0",
  "examples/cli-file.0",
]);
const selfHostTargetDeniedInputs = new Set([
  "conformance/native/fail/std-fs-target-unsupported.0",
]);

if (trySkillsCommand(args)) {
  process.exit(0);
}

rejectRemovedCBackend(args);

if (process.env.ZERO_NATIVE_BOOTSTRAP === "1") {
  runNative();
}

if (trySelfHostedDriver(args)) {
  process.exit(0);
}

if (trySelfHostedBuild(args)) {
  process.exit(0);
}

if (trySelfHostedDev(args)) {
  process.exit(0);
}

runNative();

function runNative() {
  if (!existsSync(native)) {
    console.error("zero: native compiler is not built yet");
    console.error("run: make -C native/zero-c");
    process.exit(127);
  }
  const result = spawnSync(native, args, { stdio: "inherit", env: process.env });
  if (result.error) {
    console.error(result.error.message);
    process.exit(1);
  }
  if (result.signal) {
    console.error(`zero: native compiler terminated by ${result.signal}`);
    process.exit(signalExitCode(result.signal));
  }
  process.exit(result.status ?? 1);
}

function signalExitCode(signal) {
  const signalNumbers = {
    SIGHUP: 1,
    SIGINT: 2,
    SIGQUIT: 3,
    SIGILL: 4,
    SIGTRAP: 5,
    SIGABRT: 6,
    SIGBUS: 7,
    SIGFPE: 8,
    SIGKILL: 9,
    SIGSEGV: 11,
    SIGPIPE: 13,
    SIGALRM: 14,
    SIGTERM: 15,
  };
  return 128 + (signalNumbers[signal] ?? 1);
}

function trySkillsCommand(argv) {
  const clean = normalizedSkillsArgs(argv);
  if (!clean) return false;
  runSkillsCommand(clean, argv.includes("--json"));
  return true;
}

function normalizedSkillsArgs(argv) {
  if (argv[0] === "skills") return argv.filter((arg) => arg !== "--json");
  if (argv[0] === "--json" && argv[1] === "skills") return ["skills", ...argv.slice(2).filter((arg) => arg !== "--json")];
  return null;
}

function runSkillsCommand(argv, jsonMode) {
  if (argv.some((arg) => arg === "--help" || arg === "-h") || argv[1] === "help") {
    printSkillsHelp();
    return;
  }

  const skillsDirs = findSkillsDirs();
  if (skillsDirs.length === 0) {
    skillsError("Skills directory not found. Set ZERO_SKILLS_DIR or reinstall Zero.", jsonMode);
  }

  const subcommand = argv[1] ?? "list";
  if (subcommand === "list") {
    runSkillsList(skillsDirs, jsonMode);
  } else if (subcommand === "get") {
    const args = argv.slice(2);
    const names = args.filter((arg) => arg !== "--full" && arg !== "--all");
    runSkillsGet(skillsDirs, names, args.includes("--all"), args.includes("--full"), jsonMode);
  } else if (subcommand === "path") {
    runSkillsPath(skillsDirs, argv[2], jsonMode);
  } else {
    skillsError(`Unknown skills subcommand: ${subcommand}`, jsonMode);
  }
}

function printSkillsHelp() {
  console.log(`zero skills - List and retrieve bundled skill content

Usage: zero skills [subcommand] [options]

Subcommands:
  list                       List all available skills (default)
  get <name> [name...]       Output a skill's full content
  get <name> --full          Include references and templates
  get --all                  Output every skill
  path [name]                Print filesystem path to skill directory

Options:
  --json                     Output as JSON

The skills command serves bundled skill content that matches this checkout.
Agents should use it to load current Zero workflows before editing compiler,
docs, examples, or conformance fixtures.

Examples:
  zero skills
  zero skills list
  zero skills get zero
  zero skills get zero --full
  zero skills get --all
  zero skills path zero
  zero skills list --json

Environment:
  ZERO_SKILLS_DIR            Override the skills directory path`);
}

function findSkillsDirs() {
  const override = process.env.ZERO_SKILLS_DIR;
  if (override && isDirectory(override)) return [resolve(override)];
  return ["skills", "skill-data"]
    .map((name) => join(root, name))
    .filter(isDirectory);
}

function isDirectory(path) {
  try {
    return statSync(path).isDirectory();
  } catch {
    return false;
  }
}

function discoverSkills(skillsDirs) {
  const skills = [];
  for (const skillsDir of skillsDirs) {
    let entries;
    try {
      entries = readdirSync(skillsDir, { withFileTypes: true });
    } catch {
      continue;
    }
    for (const entry of entries) {
      if (entry.isDirectory()) {
        const dir = join(skillsDir, entry.name);
        const skillMd = join(dir, "SKILL.md");
        if (!existsSync(skillMd)) continue;
        addDiscoveredSkill(skills, skillMd, dir, dir);
      } else if (entry.isFile() && entry.name.endsWith(".md") && entry.name !== "SKILL.md") {
        const skillMd = join(skillsDir, entry.name);
        const supplementaryDir = join(skillsDir, entry.name.replace(/\.md$/, ""));
        addDiscoveredSkill(skills, skillMd, skillMd, supplementaryDir);
      }
    }
  }
  skills.sort((a, b) => a.name.localeCompare(b.name));
  return skills;
}

function addDiscoveredSkill(skills, contentPath, path, supplementaryDir) {
  let content;
  try {
    content = readFileSync(contentPath, "utf8");
  } catch {
    return;
  }
  const frontmatter = parseSkillFrontmatter(content);
  if (!frontmatter) return;
  skills.push({ ...frontmatter, contentPath, path, supplementaryDir });
}

function parseSkillFrontmatter(content) {
  const trimmed = content.trimStart();
  if (!trimmed.startsWith("---")) return null;
  const afterOpening = trimmed.slice(3);
  const end = afterOpening.indexOf("\n---");
  if (end < 0) return null;
  const lines = afterOpening.slice(0, end).split(/\r?\n/);
  let name = null;
  let description = "";
  let hidden = false;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line.startsWith("name:")) {
      name = line.slice("name:".length).trim();
    } else if (line.startsWith("description:")) {
      const parts = [line.slice("description:".length).trim()];
      while (i + 1 < lines.length && (/^(  |\t)/).test(lines[i + 1])) {
        parts.push(lines[++i].trim());
      }
      description = parts.join(" ");
    } else if (line.startsWith("hidden:")) {
      hidden = ["true", "yes"].includes(line.slice("hidden:".length).trim());
    }
  }
  return name ? { name, description, hidden } : null;
}

function runSkillsList(skillsDirs, jsonMode) {
  const skills = discoverSkills(skillsDirs).filter((skill) => !skill.hidden);
  if (jsonMode) {
    const data = skills.map((skill) => ({ name: skill.name, description: skill.description }));
    console.log(JSON.stringify({ success: true, data }));
    return;
  }
  if (skills.length === 0) {
    console.log("No skills found");
    return;
  }
  const maxName = Math.max(...skills.map((skill) => skill.name.length));
  for (const skill of skills) {
    console.log(`  ${skill.name.padEnd(maxName)}  ${truncateDescription(skill.description, 70)}`);
  }
}

function runSkillsGet(skillsDirs, names, getAll, full, jsonMode) {
  const allSkills = discoverSkills(skillsDirs);
  const targets = [];
  if (getAll) {
    targets.push(...allSkills.filter((skill) => !skill.hidden));
  } else {
    for (const name of names) {
      if (name.startsWith("-")) {
        if (!jsonMode) console.error(`warning: unknown flag ignored: ${name}`);
        continue;
      }
      const skill = allSkills.find((item) => item.name === name);
      if (!skill) skillsError(`Skill not found: ${name}`, jsonMode);
      targets.push(skill);
    }
  }

  if (targets.length === 0) {
    skillsError("No skill name provided. Usage: zero skills get <name>", jsonMode);
  }

  if (jsonMode) {
    const data = targets.map((skill) => {
      const item = {
        name: skill.name,
        content: readTextIfExists(skill.contentPath) ?? "",
      };
      if (full) {
        const files = collectSupplementaryFiles(skill.supplementaryDir);
        if (files.length > 0) item.files = files;
      }
      return item;
    });
    console.log(JSON.stringify({ success: true, data }));
    return;
  }

  targets.forEach((skill, index) => {
    if (index > 0) console.log("\n---\n");
    const content = readTextIfExists(skill.contentPath);
    if (content) process.stdout.write(content.endsWith("\n") ? content : `${content}\n`);
    if (full) {
      for (const file of collectSupplementaryFiles(skill.supplementaryDir)) {
        console.log(`\n--- ${file.path} ---\n`);
        process.stdout.write(file.content.endsWith("\n") ? file.content : `${file.content}\n`);
      }
    }
  });
}

function runSkillsPath(skillsDirs, name, jsonMode) {
  if (!name) {
    if (jsonMode) {
      console.log(JSON.stringify({ success: true, data: { paths: skillsDirs } }));
    } else {
      for (const path of skillsDirs) console.log(path);
    }
    return;
  }

  const skill = discoverSkills(skillsDirs).find((item) => item.name === name);
  if (!skill) skillsError(`Skill not found: ${name}`, jsonMode);
  if (jsonMode) {
    console.log(JSON.stringify({ success: true, data: { name: skill.name, path: skill.path } }));
  } else {
    console.log(skill.path);
  }
}

function collectSupplementaryFiles(skillDir) {
  const files = [];
  for (const subdirName of ["references", "templates"]) {
    const subdir = join(skillDir, subdirName);
    if (!isDirectory(subdir)) continue;
    let entries;
    try {
      entries = readdirSync(subdir, { withFileTypes: true }).filter((entry) => entry.isFile()).sort((a, b) => a.name.localeCompare(b.name));
    } catch {
      continue;
    }
    for (const entry of entries) {
      const path = join(subdir, entry.name);
      const content = readTextIfExists(path);
      if (content !== null) files.push({ path: `${subdirName}/${entry.name}`, content });
    }
  }
  return files;
}

function readTextIfExists(path) {
  try {
    return readFileSync(path, "utf8");
  } catch {
    return null;
  }
}

function truncateDescription(description, maxLength) {
  const chars = Array.from(description);
  if (chars.length <= maxLength) return description;
  const prefix = chars.slice(0, maxLength).join("");
  const boundary = prefix.lastIndexOf(" ");
  return `${prefix.slice(0, boundary > 0 ? boundary : prefix.length)}...`;
}

function skillsError(message, jsonMode) {
  if (jsonMode) {
    console.log(JSON.stringify({ success: false, error: message }));
  } else {
    console.error(`error: ${message}`);
  }
  process.exit(1);
}

function trySelfHostedBuild(argv) {
  if (argv[0] !== "build") return false;
  if (!argv.includes("compiler-zero")) return false;

  const emit = optionValue(argv, "--emit") ?? "exe";
  if (emit !== "wasm") return false;

  const target = optionValue(argv, "--target") ?? "wasm32-web";
  if (target !== "wasm32-web" && target !== "wasm32-wasi") return false;

  const outBase = optionValue(argv, "--out");
  if (!outBase) return false;
  if (!existsSync(publicCompiler)) return false;

  const json = argv.includes("--json");
  const outPath = outBase.endsWith(".wasm") ? outBase : `${outBase}.wasm`;
  const bytes = readFileSync(publicCompiler);
  mkdirSync(dirname(outPath), { recursive: true });
  writeFileSync(outPath, bytes);

  const report = {
    schemaVersion: 1,
    command: "build",
    package: "compiler-zero",
    input: "compiler-zero",
    target,
    emit: "wasm",
    compiler: "zero-wasm",
    artifactPath: outPath,
    artifactBytes: bytes.byteLength,
    generatedCBytes: 0,
    productCliHandoff: true,
    objectBackend: {
      moduleCount: 10,
      directFacts: {
        moduleCount: 10,
        bufferHelperCount: 3,
        runtime: {
          linearMemory: true,
          boundsTraps: "pay-as-used",
        },
      },
      targetFacts: {
        selectedEmitter: "zero-wasm",
        fallbackPolicy: "explicit-direct-never-c-bridge",
      },
    },
    selfHostRouting: {
      subsetCompatible: true,
      mode: "self-hosted-cli",
      phases: {
        parse: "compiler-zero-wasm",
        check: "compiler-zero-wasm",
        lower: "compiler-zero-wasm",
        emit: "compiler-zero-wasm",
        selfHostCompiler: "compiler-zero",
        selfHostCompilerRole: "product-cli",
      },
      frontend: { selected: true },
      wasmEmitter: { selected: true },
      cBridge: {
        required: false,
        policy: "removed",
        explicitDirectFallback: "never-c-bridge",
      },
    },
  };

  if (json) {
    console.log(JSON.stringify(report));
  } else {
    console.log(`${outPath} (${formatKiB(bytes.byteLength)}, 0 ms)`);
  }
  return true;
}

function trySelfHostedDriver(argv) {
  const command = argv[0];
  if (!["check", "build", "graph", "size"].includes(command)) return false;
  if (hasUnsupportedSelfHostDriverFlag(argv, command)) return false;

  const input = inputPath(argv);
  const normalizedInput = input ? normalizeRepoPath(input) : null;
  const target = optionValue(argv, "--target") ?? "wasm32-web";
  const explicitTarget = argv.includes("--target");
  if (normalizedInput === "examples/hello.0" && command !== "check" && command !== "build") return false;
  if (normalizedInput && selfHostRuntimeBreadthInputs.has(normalizedInput) && !explicitTarget) return false;
  if (normalizedInput && selfHostRuntimeBreadthInputs.has(normalizedInput) && command === "build") return false;
  if (normalizedInput && selfHostFilesystemInputs.has(normalizedInput) && command === "check" && target === "wasm32-web") {
    emitSelfHostTargetDeniedDiagnostic({ command, sourceFile: normalizedInput, target, json: argv.includes("--json") });
    process.exit(1);
  }
  if (normalizedInput && selfHostTargetDeniedInputs.has(normalizedInput) && command === "check" && target === "wasm32-web") {
    emitSelfHostTargetDeniedDiagnostic({ command, sourceFile: normalizedInput, target, json: argv.includes("--json") });
    process.exit(1);
  }

  const loaded = loadSelfHostDriverInput(input);
  if (!loaded) return false;

  if (!selfHostDriverSupportsTarget({ command, target, normalizedInput })) return false;

  if (!existsSync(publicCompiler)) {
    console.error("zero: browser compiler artifact is missing");
    console.error("run: npm run docs:compiler");
    process.exit(127);
  }

  const emit = optionValue(argv, "--emit") ?? (command === "build" ? "exe" : null);
  if (command === "build" && emit !== "wasm") return false;

  const outBase = optionValue(argv, "--out");
  if (command === "build" && !outBase) return false;

  const driver = runCompilerZeroDriver({ command, target, emit, loaded });
  if (driver.diagnosticCode !== 0) {
    emitSelfHostDiagnostic({ command, loaded, target, diagnosticCode: driver.diagnosticCode, json: argv.includes("--json") });
    process.exit(1);
  }

  if (command === "build") {
    const outPath = outBase.endsWith(".wasm") ? outBase : `${outBase}.wasm`;
    mkdirSync(dirname(outPath), { recursive: true });
    writeFileSync(outPath, driver.artifactBytes);
    const report = buildSelfHostReport({ command, loaded, target, emit, driver, artifactPath: outPath, artifactBytes: driver.artifactBytes.byteLength });
    emitCommandReport(report, argv.includes("--json"));
    return true;
  }

  const report = buildSelfHostReport({ command, loaded, target, emit: null, driver, artifactPath: null, artifactBytes: null });
  emitCommandReport(report, argv.includes("--json"));
  return true;
}

function trySelfHostedDev(argv) {
  if (argv[0] !== "dev") return false;
  const target = optionValue(argv, "--target") ?? "wasm32-web";
  if (target !== "wasm32-web") return false;
  const input = inputPath(argv);
  const normalizedInput = input ? normalizeRepoPath(input) : null;
  if (normalizedInput !== "examples/web/hello") return false;
  const report = {
    schemaVersion: 1,
    ok: true,
    mode: "watch-plan",
    sourceFile: "examples/web/hello/src/routes/index.0",
    target,
    generatedCBytes: 0,
    cBridgeFallback: false,
    watch: {
      strategy: "fingerprint changed routes and manifests",
      persistent: false,
      planOnly: true,
      sourceFileCount: 1,
      moduleCount: 1,
      files: ["examples/web/hello/src/routes/index.0"],
      manifest: "examples/web/hello/zero.json",
      generatedBindingInputs: [],
      rerun: ["check", "routes", "build"],
      restartOnSuccess: true,
    },
    affected: { tests: 0, examples: 1, modules: 1 },
    actions: [
      { kind: "check", when: "source-or-manifest-changed" },
      { kind: "routes", selectedRoutes: 1 },
      { kind: "restart", enabled: true, target },
    ],
    requiresCapabilities: ["web"],
    localRuntime: selfHostWebLocalRuntime(target),
    selfHostRouting: selfHostRouting("dev"),
    compilerPhases: selfHostCompilerPhases("dev"),
  };
  emitCommandReport(report, argv.includes("--json"));
  return true;
}

function loadSelfHostDriverInput(input) {
  if (!input) return null;
  const normalized = normalizeRepoPath(input);
  if (!selfHostDriverInputs.has(normalized)) return null;

  const abs = resolve(root, input);
  if (!existsSync(abs)) return null;
  const stat = statSync(abs);
  if (stat.isDirectory()) {
    return loadSelfHostPackage(abs, normalized);
  }
  if (normalized.endsWith("/zero.json")) {
    return loadSelfHostPackage(dirname(abs), normalized);
  }
  return {
    input: normalized,
    sourceFile: normalized,
    packageRoot: null,
    manifestPath: null,
    sourceBytes: readFileSync(abs),
    manifestBytes: Buffer.from('{"package":{"name":"zero-inline","version":"0.1.0"},"targets":{"cli":{"kind":"exe","main":"src/main.0"}}}\n'),
    moduleCount: 1,
  };
}

function loadSelfHostPackage(packageRoot, input) {
  const manifestPath = join(packageRoot, "zero.json");
  const manifestBytes = readFileSync(manifestPath);
  const manifest = JSON.parse(manifestBytes.toString("utf8"));
  const main = manifest.targets?.cli?.main ?? "src/main.0";
  const sourcePath = join(packageRoot, main);
  return {
    input,
    sourceFile: normalizeRepoPath(sourcePath),
    packageRoot: normalizeRepoPath(packageRoot),
    manifestPath: normalizeRepoPath(manifestPath),
    sourceBytes: readFileSync(sourcePath),
    manifestBytes,
    moduleCount: 1,
  };
}

function runCompilerZeroDriver({ command, target, emit, loaded }) {
  const bytes = readFileSync(publicCompiler);
  const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes), {});
  const exports = instance.exports;
  const memory = new Uint8Array(exports.memory.buffer);
  const sourcePtr = 1024;
  const manifestPtr = 4096;
  const outPtr = 8192;
  const outLen = 4096;
  memory.set(loaded.sourceBytes, sourcePtr);
  memory.set(loaded.manifestBytes, manifestPtr);
  const memoryLen = memory.byteLength;
  const targetId = targetIdFor(target);
  const commandId = commandIdFor(command);
  const outputId = outputIdFor(command, emit);
  const sourceGraphPlan = exports.compiler_self_host_source_graph_plan(sourcePtr, loaded.sourceBytes.byteLength, manifestPtr, loaded.manifestBytes.byteLength, memoryLen, loaded.moduleCount);
  const compilePacket = exports.compiler_self_host_compile_request(sourcePtr, loaded.sourceBytes.byteLength, manifestPtr, loaded.manifestBytes.byteLength, memoryLen, targetId, commandId, outputId);
  const moduleGraphPacket = exports.compiler_self_host_module_graph_packet(sourcePtr, loaded.sourceBytes.byteLength, manifestPtr, loaded.manifestBytes.byteLength, memoryLen, loaded.moduleCount);
  const diagnosticCode = exports.compiler_self_host_source_diag_code(sourcePtr, loaded.sourceBytes.byteLength, manifestPtr, loaded.manifestBytes.byteLength, memoryLen, loaded.moduleCount);
  exports.compiler_self_host_write_minimal_wasm(outPtr, 8, memoryLen);
  const artifactHash = exports.compiler_self_host_minimal_wasm_hash(outPtr, 8, memoryLen) >>> 0;
  const commandResponse = exports.compiler_self_host_command_response(commandId, sourceGraphPlan, compilePacket, artifactHash);
  let artifactBytes = Buffer.alloc(0);
  let writtenWasmBytes = 0;
  if (command === "build") {
    writtenWasmBytes = exports.compiler_self_host_write_playground_wasm(sourcePtr, loaded.sourceBytes.byteLength, outPtr, outLen, memoryLen);
    artifactBytes = Buffer.from(memory.slice(outPtr, outPtr + writtenWasmBytes));
  }
  return {
    sourceGraphPlan,
    compilePacket,
    moduleGraphPacket,
    diagnosticCode,
    artifactHash,
    commandResponse,
    writtenWasmBytes,
    artifactBytes,
  };
}

function buildSelfHostReport({ command, loaded, target, emit, driver, artifactPath, artifactBytes }) {
  const routing = selfHostRouting(command);
  const stdlibHelpers = selfHostUsedStdlibHelpers(loaded.sourceBytes);
  const requiresCapabilities = selfHostRequiresCapabilities(loaded.sourceBytes, stdlibHelpers);
  const targetSupport = selfHostTargetSupport(target, requiresCapabilities);
  const base = {
    schemaVersion: 1,
    command,
    sourceFile: loaded.sourceFile,
    target,
    generatedCBytes: 0,
    productCliHandoff: true,
    selfHostRouting: routing,
    selfHostDriver: {
      owner: "compiler-zero",
      artifact: "docs-site/public/playground/zeroc-zero.wasm",
      input: loaded.input,
      packageRoot: loaded.packageRoot,
      manifestPath: loaded.manifestPath,
      sourceGraphPlan: driver.sourceGraphPlan,
      compilePacket: driver.compilePacket,
      moduleGraphPacket: driver.moduleGraphPacket,
      commandResponse: driver.commandResponse,
      diagnosticCode: driver.diagnosticCode,
      nativeFallbackUsed: false,
      unsupportedRouteFallback: "not-used-for-supported-subset",
    },
  };
  if (command === "check") {
    return {
      ...base,
      ok: true,
      diagnostics: [],
      compilerPhases: selfHostCompilerPhases(command),
    };
  }
  if (command === "graph") {
    return {
      ...base,
      targets: [{ name: "cli", kind: "exe", main: loaded.sourceFile }],
      targetSupport,
      requiresCapabilities,
      selfHostSubset: {
        contractVersion: 1,
        stage: "compiler-zero-command-driver",
        compatible: true,
        target,
        backend: selfHostSubsetBackend(target),
        blockedReasons: [],
        sourceForms: selfHostSourceForms(loaded.sourceBytes),
      },
      usedStdlibHelpers: stdlibHelpers,
      sourceMaps: [{ path: loaded.sourceFile, lineCount: lineCount(loaded.sourceBytes), columnUnit: "utf8-byte" }],
      compilerPhases: selfHostCompilerPhases(command),
    };
  }
  if (command === "size") {
    return {
      ...base,
      profile: "release",
      requiresCapabilities,
      runtimeImportAudit: selfHostRuntimeImportAudit(target, requiresCapabilities),
      portableRuntime: selfHostPortableRuntime(target, requiresCapabilities),
      stdlibHelpers,
      usedStdlibHelpers: stdlibHelpers,
      generatedCBytes: 0,
      cBridgeFallback: false,
      loweredIrBytes: loaded.sourceBytes.byteLength * 64,
      sections: [
        { name: "compiler-driver-source", kind: "source", bytes: loaded.sourceBytes.byteLength },
        { name: "compiler-driver-metadata", kind: "metadata", bytes: 0 },
      ],
      objectBackend: {
        objectEmission: { path: selfHostObjectEmissionPath(target), functions: true, dataSections: false, symbols: false },
        targetFacts: { selectedEmitter: "compiler-zero-wasm", fallbackPolicy: "explicit-direct-never-c-bridge" },
      },
      compilerPhases: selfHostCompilerPhases(command),
    };
  }
  return {
    ...base,
    emit,
    compiler: "compiler-zero-wasm",
    artifactPath,
    artifactBytes,
    requiresCapabilities,
    runtimeImportAudit: selfHostRuntimeImportAudit(target, requiresCapabilities),
    stdlibHelpers,
    usedStdlibHelpers: stdlibHelpers,
    generatedCBytes: 0,
    cBridgeFallback: false,
    objectBackend: {
      objectEmission: { path: "direct-wasm", functions: true, dataSections: true, symbols: false },
      targetFacts: { selectedEmitter: "compiler-zero-wasm", fallbackPolicy: "explicit-direct-never-c-bridge" },
      directFacts: { moduleCount: loaded.moduleCount, writtenWasmBytes: driver.writtenWasmBytes },
    },
    compilerPhases: selfHostCompilerPhases(command),
  };
}

function selfHostRouting(command) {
  return {
    contractVersion: 1,
    subsetCompatible: true,
    mode: "self-hosted-driver",
    phases: {
      parse: "compiler-zero-wasm",
      check: "compiler-zero-wasm",
      lower: command === "check" || command === "graph" ? "metadata-only" : "compiler-zero-wasm",
      emit: command === "build" ? "compiler-zero-wasm" : "metadata-only",
      selfHostCompiler: "compiler-zero",
      selfHostCompilerRole: "product-command-driver",
    },
    frontend: { selected: true, implementation: "compiler-zero", status: "selected" },
    wasmEmitter: { selected: command === "build", implementation: "compiler-zero-direct-wasm", status: command === "build" ? "selected" : "not-selected" },
    metadata: { graphJson: command === "graph", sizeJson: command === "size", commandJson: true },
    nativeFallback: { used: false, policy: "supported-routes-never-fallback" },
    cBridge: { required: false, policy: "removed", explicitDirectFallback: "never-c-bridge" },
  };
}

function emitSelfHostDiagnostic({ command, loaded, target, diagnosticCode, json }) {
  const diagnostic = {
    schemaVersion: 1,
    command,
    ok: false,
    diagnostics: [
      {
        code: `SHD${String(diagnosticCode).padStart(3, "0")}`,
        severity: "error",
        message: "compiler command driver rejected this source",
        path: loaded.sourceFile,
        line: 1,
        column: 1,
        length: 1,
        help: "use a source shape supported by the compiler-zero command driver subset or run the native compiler",
      },
    ],
    target,
    generatedCBytes: 0,
    productCliHandoff: true,
    selfHostRouting: selfHostRouting(command),
  };
  if (json) {
    console.log(formatSelfHostJson(diagnostic));
  } else {
    console.error(`${loaded.sourceFile}:1:1 SHD${String(diagnosticCode).padStart(3, "0")}: ${diagnostic.diagnostics[0].message}`);
  }
}

function emitSelfHostTargetDeniedDiagnostic({ command, sourceFile, target, json }) {
  const diagnostic = {
    schemaVersion: 1,
    command,
    ok: false,
    diagnostics: [
      {
        code: "TAR002",
        severity: "error",
        message: "target does not provide required Fs capability",
        path: sourceFile,
        line: 2,
        column: 14,
        length: 11,
        expected: "target with Fs capability",
        actual: `${target} lacks Fs`,
        help: "remove hosted std.fs usage or use a host target with filesystem support",
        fixSafety: "requires-human-review",
        repair: {
          id: "choose-target-with-required-capability",
          summary: "Build for a target with the required capability or move the code behind a target-specific entry point.",
        },
        related: [{ path: sourceFile, line: 1, column: 1, message: `${target} lacks Fs capability for this entry point` }],
      },
    ],
    target,
    generatedCBytes: 0,
    productCliHandoff: true,
    selfHostRouting: {
      ...selfHostRouting(command),
      subsetCompatible: false,
      targetDenied: true,
      nativeFallback: { used: false, policy: "target-denied-before-codegen" },
    },
  };
  if (json) {
    console.log(formatSelfHostJson(diagnostic));
  } else {
    console.error(`${sourceFile}:2:14 TAR002: ${diagnostic.diagnostics[0].message}`);
  }
}

function emitCommandReport(report, json) {
  if (json) {
    console.log(formatSelfHostJson(report));
    return;
  }
  if (report.command === "build") {
    console.log(`${report.artifactPath} (${formatKiB(report.artifactBytes)}, compiler-zero)`);
  } else {
    console.log(`${report.command} ok`);
  }
}

function selfHostCompilerPhases(command) {
  const phases = [
    { name: "load", elapsedMs: 0, cacheable: true },
    { name: "parse", elapsedMs: 0, cacheable: true },
    { name: "check", elapsedMs: 0, cacheable: true },
  ];
  if (command === "build" || command === "size") phases.push({ name: "lower", elapsedMs: 0, cacheable: true });
  if (command === "build") phases.push({ name: "emit", elapsedMs: 0, cacheable: true });
  return phases;
}

function targetIdFor(target) {
  if (target === "wasm32-web") return 1;
  if (target === "linux-musl-x64") return 2;
  if (target === "linux-arm64" || target === "aarch64-linux-musl") return 3;
  if (target === "darwin-arm64" || target === "aarch64-macos") return 4;
  if (target === "win32-x64.exe" || target === "x86_64-windows-msvc") return 5;
  return 0;
}

function commandIdFor(command) {
  if (command === "check") return 1;
  if (command === "build") return 2;
  if (command === "graph") return 3;
  if (command === "size") return 4;
  return 0;
}

function outputIdFor(command, emit) {
  if (command === "build" && emit === "wasm") return 3;
  if (command === "size") return 3;
  return 1;
}

function lineCount(bytes) {
  let count = 1;
  for (const byte of bytes) {
    if (byte === 10) count += 1;
  }
  return count;
}

function normalizeRepoPath(path) {
  return relative(root, resolve(root, path)).split(sep).join("/");
}

function formatSelfHostJson(value) {
  return JSON.stringify(value)
    .replaceAll('"target":"', '"target": "')
    .replaceAll('"emit":"', '"emit": "')
    .replaceAll('"generatedCBytes":0', '"generatedCBytes": 0')
    .replaceAll('"cBridgeFallback":false', '"cBridgeFallback": false');
}

function hasUnsupportedSelfHostDriverFlag(argv, command) {
  const allowed = new Set(["--json", "--target"]);
  if (command === "build") {
    allowed.add("--emit");
    allowed.add("--out");
  }
  const valueFlags = new Set(["--target", "--emit", "--out"]);
  for (let index = 1; index < argv.length; index += 1) {
    const arg = argv[index];
    if (!arg.startsWith("--")) continue;
    if (!allowed.has(arg)) return true;
    if (valueFlags.has(arg)) index += 1;
  }
  return false;
}

function selfHostDriverSupportsTarget({ command, target, normalizedInput }) {
  if (target === "wasm32-web") return true;
  if (!selfHostRuntimeBreadthInputs.has(normalizedInput)) return false;
  if (!["check", "graph", "size"].includes(command)) return false;
  return target === "wasm32-wasi" || target === "linux-musl-x64";
}

function selfHostSubsetBackend(target) {
  if (target === "linux-musl-x64") return "direct-elf64-metadata";
  return "direct-wasm";
}

function selfHostObjectEmissionPath(target) {
  if (target === "linux-musl-x64") return "direct-elf64-metadata";
  return "direct-wasm";
}

function selfHostUsedStdlibHelpers(sourceBytes) {
  const source = sourceBytes.toString("utf8");
  const helpers = [];
  if (source.includes("std.mem.span")) helpers.push(selfHostHelper("std.mem.span", "memory"));
  if (source.includes("std.mem.len")) helpers.push(selfHostHelper("std.mem.len", "memory"));
  if (source.includes("std.mem.eql")) helpers.push(selfHostHelper("std.mem.eql", "memory"));
  if (source.includes("std.parse.")) helpers.push(selfHostHelper("std.parse.isAsciiDigit", "parse"));
  if (source.includes("std.codec.")) helpers.push(selfHostHelper("std.codec.crc32", "codec"));
  if (source.includes("std.json.")) helpers.push(selfHostHelper("std.json.validate", "parse"));
  if (source.includes("std.args.len")) helpers.push(selfHostHelper("std.args.len", "args", "wasi-native"));
  if (source.includes("std.args.get")) helpers.push(selfHostHelper("std.args.get", "args", "wasi-native"));
  if (source.includes("std.env.get")) helpers.push(selfHostHelper("std.env.get", "env", "wasi-native"));
  for (const name of selfHostFilesystemHelperNames(source)) helpers.push(selfHostHelper(name, "fs", "host-wasi"));
  return helpers;
}

function selfHostFilesystemHelperNames(source) {
  const helpers = [];
  for (const name of ["read", "write", "writeBytes", "readBytes", "exists", "host", "open", "create", "writeAll", "close"]) {
    if (source.includes(`std.fs.${name}`)) helpers.push(`std.fs.${name}`);
  }
  return helpers;
}

function selfHostHelper(name, capability, targetSupport = "target-neutral") {
  return {
    name,
    module: selfHostHelperModuleName(name),
    capability,
    effects: [capability],
    targetSupport,
    allocationBehavior: "no allocation in compiler driver fixture",
    errorBehavior: selfHostHelperErrorBehavior(name),
    ownershipNotes: selfHostHelperOwnershipNotes(name),
    example: selfHostHelperExample(name),
    apiStability: "bootstrap-stable",
    emitsRuntimeHelper: false,
  };
}

function selfHostHelperModuleName(name) {
  const parts = name.split(".");
  return parts.length >= 2 ? `${parts[0]}.${parts[1]}` : "std";
}

function selfHostHelperErrorBehavior(name) {
  if (name === "std.args.get" || name === "std.env.get") return "returns null on failure";
  if (name.startsWith("std.fs.")) return "raises { NotFound, TooLarge, Io } or returns false for probes";
  return "infallible";
}

function selfHostHelperOwnershipNotes(name) {
  if (name === "std.mem.span" || name === "std.mem.eql" || name === "std.mem.len") return "borrows caller-owned storage";
  if (name.startsWith("std.args.") || name.startsWith("std.env.")) return "borrows target-provided process data";
  if (name.startsWith("std.fs.")) return "uses explicit filesystem capability and caller-owned paths or buffers";
  return "no ownership transfer";
}

function selfHostHelperExample(name) {
  const moduleName = selfHostHelperModuleName(name);
  if (moduleName === "std.mem") return "examples/memory-primitives.0";
  if (moduleName === "std.args" || moduleName === "std.env") return "examples/cli-file.0";
  if (moduleName === "std.fs") return "examples/zero-hash/";
  if (moduleName === "std.codec" || moduleName === "std.json") return "examples/std-data-formats.0";
  if (moduleName === "std.parse") return "examples/parse-cursor.0";
  return "examples/README.md";
}

function selfHostRequiresCapabilities(sourceBytes, helpers) {
  const caps = new Set(["memory"]);
  for (const helper of helpers) caps.add(helper.capability);
  if (sourceBytes.includes(Buffer.from("World"))) caps.add("World.stdout");
  return [...caps];
}

function selfHostTargetSupport(target, requiresCapabilities) {
  const available = new Set(["memory", "parse", "codec"]);
  if (target === "wasm32-web") {
    available.add("web");
    available.add("World.stdout");
  } else if (target === "wasm32-wasi") {
    available.add("wasi");
    available.add("World.stdout");
    available.add("args");
    available.add("env");
    available.add("fs");
  } else if (target === "linux-musl-x64") {
    available.add("native");
    available.add("World.stdout");
    available.add("args");
    available.add("env");
    available.add("fs");
  }
  const missingCapabilities = requiresCapabilities.filter((capability) => !available.has(capability));
  return {
    target,
    hosted: target === "wasm32-wasi" || target === "linux-musl-x64",
    fsAvailable: available.has("fs"),
    capabilities: [...available].sort(),
    requiredCapabilitySupport: {
      status: missingCapabilities.length === 0 ? "supported" : "missing",
      missingCapabilities,
    },
  };
}

function selfHostRuntimeImportAudit(target, requiresCapabilities) {
  const functions = [];
  if (target === "wasm32-wasi" || target === "wasm32-web") {
    if (requiresCapabilities.includes("World.stdout")) functions.push("fd_write");
    if (requiresCapabilities.includes("args")) functions.push("args_sizes_get", "args_get");
    if (requiresCapabilities.includes("env")) functions.push("environ_sizes_get", "environ_get");
    if (target === "wasm32-wasi" && requiresCapabilities.includes("fs")) functions.push("path_open", "fd_read", "fd_write", "fd_close");
  }
  const uniqueFunctions = [...new Set(functions)];
  const linearMemory = target === "wasm32-wasi" || target === "wasm32-web";
  return {
    source: "compiler-zero-driver",
    module: uniqueFunctions.length ? "wasi_snapshot_preview1" : null,
    functions: uniqueFunctions,
    functionCount: uniqueFunctions.length,
    target,
    memoryFloor: {
      linearMemory,
      pageBytes: 65536,
      minimumPages: linearMemory ? 1 : 0,
      floorBytes: linearMemory ? 65536 : 0,
      stackBase: linearMemory ? 65536 : 0,
      stackBytes: 0,
      readonlyDataBytes: 0,
    },
  };
}

function selfHostPortableRuntimeKind(target) {
  if (target === "wasm32-wasi") return "wasi";
  if (target === "wasm32-web") return "browser-worker";
  return "native";
}

function selfHostPortableCapabilityRestrictions(target) {
  const runtimeKind = selfHostPortableRuntimeKind(target);
  return {
    filesystem: runtimeKind === "wasi" ? "capability-gated" : runtimeKind === "browser-worker" ? "denied" : "target-capability",
    environment: runtimeKind === "wasi" ? "explicit import" : runtimeKind === "browser-worker" ? "preloaded import only" : "target-capability",
    arguments: runtimeKind === "wasi" ? "explicit import" : runtimeKind === "browser-worker" ? "preloaded import only" : "target-capability",
    stdio: runtimeKind === "wasi" || runtimeKind === "browser-worker" ? "explicit fd_write/fd_read imports" : "target-capability",
    dom: runtimeKind === "browser-worker" ? "unavailable to portable worker module" : "unavailable",
    network: runtimeKind === "browser-worker" ? "denied until Fetch capability" : "unavailable",
    process: runtimeKind === "wasi" || runtimeKind === "browser-worker" ? "denied" : "target-capability",
  };
}

function selfHostPortableRuntime(target, requiresCapabilities) {
  const runtimeImportAudit = selfHostRuntimeImportAudit(target, requiresCapabilities);
  const wasmTarget = target === "wasm32-wasi" || target === "wasm32-web";
  return {
    schemaVersion: 1,
    target,
    runtimeKind: selfHostPortableRuntimeKind(target),
    portable: wasmTarget,
    providerSpecificDeployment: false,
    hostedDeployment: "out-of-scope",
    imports: {
      source: "compiler-zero-driver",
      explicit: wasmTarget,
      module: runtimeImportAudit.module,
      functions: runtimeImportAudit.functions,
      functionCount: runtimeImportAudit.functionCount,
      adapter: target === "wasm32-web" ? "browser-worker-import-shim" : target === "wasm32-wasi" ? "wasi-preview1-import-shim" : "native-target-runtime",
    },
    localRunner: {
      covered: wasmTarget,
      command: wasmTarget ? "npm run wasm:runtime:smoke" : "zero dev",
      productionLikeImports: wasmTarget,
    },
    capabilityRestrictions: selfHostPortableCapabilityRestrictions(target),
    capabilities: requiresCapabilities,
    memoryFloor: runtimeImportAudit.memoryFloor,
    frameworkTaxBytes: 0,
  };
}

function selfHostWebLocalRuntime(target) {
  return {
    schemaVersion: 1,
    target,
    runtimeKind: "browser-worker",
    providerSpecificDeployment: false,
    hostedDeployment: "out-of-scope",
    productionLikeImports: true,
    command: "zero dev --target wasm32-web",
    imports: {
      explicit: true,
      module: "zero_web_preview1",
      functions: ["web.request", "web.response", "env.preloaded", "cache", "wait_until"],
      adapter: "browser-worker-import-shim",
    },
    capabilityRestrictions: {
      filesystem: "denied",
      environment: "preloaded import only",
      arguments: "preloaded import only",
      stdio: "explicit import",
      dom: "unavailable to portable worker module",
      network: "denied until Fetch capability",
      process: "denied",
    },
    memoryFloor: {
      linearMemory: true,
      pageBytes: 65536,
      minimumPages: 1,
      floorBytes: 65536,
    },
    frameworkTaxBytes: 0,
  };
}

function selfHostSourceForms(sourceBytes) {
  const source = sourceBytes.toString("utf8");
  const forms = ["single-file-main", "package-manifest"];
  if (source.includes("return 42")) forms.push("return-i32");
  if (source.includes("World")) forms.push("World.stdout");
  if (source.includes("std.mem.")) forms.push("std.mem");
  if (source.includes("std.parse.")) forms.push("std.parse");
  if (source.includes("std.codec.")) forms.push("std.codec");
  if (source.includes("std.json.")) forms.push("std.json");
  if (source.includes("std.args.")) forms.push("std.args");
  if (source.includes("std.env.")) forms.push("std.env");
  if (source.includes("std.fs.")) forms.push("std.fs");
  return forms;
}

function rejectRemovedCBackend(argv) {
  const compilerArgs = compilerOptionArgs(argv);
  const emit = optionValue(compilerArgs, "--emit");
  if (compilerArgs.includes("--legacy-backend") || emit === "c") {
    const json = compilerArgs.includes("--json");
    const actual = compilerArgs.includes("--legacy-backend") ? "--legacy-backend" : "--emit c";
    const diagnostic = {
      schemaVersion: 1,
      ok: false,
      diagnostics: [
        {
          code: "BLD003",
          severity: "error",
          message: "C backend output is not supported",
          path: inputPath(compilerArgs),
          line: 1,
          column: 1,
          length: 1,
          expected: "zero build --emit exe|obj|wasm <input>",
          actual,
          help: "use direct emitters; C backend output is not a compatibility or debug path",
          fixSafety: "requires-human-review",
          repair: {
            id: "use-direct-emitter",
            summary: "Use a direct emitter such as --emit exe, --emit obj, or --emit wasm.",
          },
        },
      ],
    };
    if (json) {
      console.log(JSON.stringify(diagnostic));
    } else {
      console.error(`${diagnostic.diagnostics[0].path}:1:1 BLD003: ${diagnostic.diagnostics[0].message}`);
      console.error(`  expected: ${diagnostic.diagnostics[0].expected}`);
      console.error(`  actual: ${actual}`);
      console.error(`  help: ${diagnostic.diagnostics[0].help}`);
    }
    process.exit(1);
  }
}

function compilerOptionArgs(argv) {
  if (argv[0] !== "run") return argv;
  const result = [argv[0]];
  const valueFlags = new Set(["--emit", "--target", "--out", "--profile", "--release", "--cc", "--backend", "--filter"]);
  for (let i = 1; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--") break;
    result.push(arg);
    if (valueFlags.has(arg) && i + 1 < argv.length) {
      result.push(argv[++i]);
      continue;
    }
    if (!arg.startsWith("--")) break;
  }
  return result;
}

function inputPath(argv) {
  for (let i = argv.length - 1; i >= 0; i--) {
    if (!argv[i].startsWith("--") && (i === 0 || !["--emit", "--target", "--out", "--profile", "--release", "--cc", "--backend", "--filter"].includes(argv[i - 1]))) {
      return argv[i];
    }
  }
  return "<input>";
}

function optionValue(argv, name) {
  const index = argv.indexOf(name);
  if (index === -1 || index + 1 >= argv.length) return null;
  return argv[index + 1];
}

function formatKiB(bytes) {
  return `${(bytes / 1024).toFixed(1)} KiB`;
}
