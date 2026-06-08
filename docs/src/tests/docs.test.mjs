import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { access, readFile, readdir } from "node:fs/promises";
import { join, resolve } from "node:path";
import { describe, it } from "node:test";
import ts from "typescript";

const docsSiteRoot = resolve(import.meta.dirname, "../..");

function loadDocsRegistry() {
  const sourcePath = join(docsSiteRoot, "lib/docs.ts");
  const source = readFileSync(sourcePath, "utf8");
  const { outputText } = ts.transpileModule(source, {
    compilerOptions: {
      module: ts.ModuleKind.CommonJS,
      target: ts.ScriptTarget.ES2022,
    },
    fileName: sourcePath,
  });
  const module = { exports: {} };
  const require = (specifier) => {
    throw new Error(`Unexpected runtime import while loading docs registry: ${specifier}`);
  };
  new Function("exports", "module", "require", outputText)(module.exports, module, require);
  return module.exports.docs;
}

function loadDocsModule(relativePath) {
  const sourcePath = join(docsSiteRoot, relativePath);
  const source = readFileSync(sourcePath, "utf8");
  const { outputText } = ts.transpileModule(source, {
    compilerOptions: {
      module: ts.ModuleKind.CommonJS,
      target: ts.ScriptTarget.ES2022,
    },
    fileName: sourcePath,
  });
  const module = { exports: {} };
  new Function("exports", "module", outputText)(module.exports, module);
  return module.exports;
}

const docs = loadDocsRegistry();
const publicTargets = [
  "darwin-arm64",
  "darwin-x64",
  "linux-arm64",
  "linux-musl-arm64",
  "linux-musl-x64",
  "linux-x64",
  "win32-arm64.exe",
  "win32-x64.exe",
];

describe("docs registry", () => {
  it("declares module pages with source files", async () => {
    const moduleDocs = docs.filter((doc) => doc.section === "Modules");
    const moduleFiles = (await readdir(join(docsSiteRoot, "articles/modules")))
      .filter((name) => name.endsWith(".md"));

    assert.equal(moduleDocs.length, moduleFiles.length);
    assert.ok(moduleDocs.every((doc) => doc.path.startsWith("/modules/")));

    await Promise.all(
      moduleDocs.map((doc) => access(join(docsSiteRoot, doc.sourcePath.slice(1))))
    );
  });

  it("declares the public docs set", () => {
    const slugs = new Set(docs.map((doc) => doc.slug));
    for (const slug of [
      "install",
      "getting-started",
      "learn-zero",
      "language-reference",
      "standard-library",
      "cli-reference",
      "testing",
      "optimization",
      "package-manifest",
      "cross-compilation",
      "c-interop",
      "diagnostics",
      "benchmarks",
      "examples",
    ]) {
      assert.ok(slugs.has(slug), `${slug} should be declared`);
    }
  });

  it("keeps public docs copyable and agent-friendly", async () => {
    const bySlug = new Map(docs.map((doc) => [doc.slug, doc]));
    const readDoc = async (slug) => {
      const doc = bySlug.get(slug);
      assert.ok(doc, `${slug} should be declared`);
      return readFile(join(docsSiteRoot, doc.sourcePath.slice(1)), "utf8");
    };

    assert.match(await readDoc("getting-started"), /curl -fsSL https:\/\/zerolang\.ai\/install\.sh \| bash/);
    const gettingStarted = await readDoc("getting-started");
    assert.match(gettingStarted, /Ask An Agent For Hello World/);
    assert.match(gettingStarted, /zero init hello/);
    assert.match(gettingStarted, /zero patch --op 'addMain'/);
    assert.match(gettingStarted, /`src\/main\.0` is the human-readable projection/);
    assert.match(await readDoc("getting-started"), /zero build --target linux-musl-x64/);
    assert.match(await readDoc("examples"), /bin\/zero check examples\/hello\.graph/);
    const learnZero = await readDoc("learn-zero");
    for (const topic of ["main", "let", "Write Functions", "type", "Field Defaults", "Span", "check", "Run Tests", "Cross Targets", "Diagnostics"]) {
      assert.match(learnZero, new RegExp(topic));
    }
    const diagnostics = await readDoc("diagnostics");
    assert.match(diagnostics, /JSON For Tools/);
    assert.match(diagnostics, /Agents should start with normal command output/);
    assert.doesNotMatch(diagnostics, /Use `--json` for agents/);
    assert.match(diagnostics, /direct `\.0` projection input to a graph-only compiler command/);
    assert.match(diagnostics, /diagnostic paths often name a `\.0` projection/);
    assert.match(diagnostics, /not as the agent write surface/);
    assert.match(diagnostics, /CIMP003/);
    assert.match(diagnostics, /configure-target-c-dependency/);
    assert.match(await readDoc("standard-library"), /zero inspect --json/);
    assert.match(await readDoc("standard-library"), /Graph-Backed Modules/);
    assert.match(await readDoc("standard-library"), /binary `std\/\*\.graph` stores/);
    assert.match(await readDoc("standard-library"), /reviewable\s+projections of graph-authored programs/);
    assert.match(await readDoc("standard-library"), /usedStdlibHelpers/);
    assert.match(await readDoc("standard-library"), /ownershipNotes/);
    const moduleDocs = docs.filter((doc) => doc.section === "Modules");
    for (const moduleDocInfo of moduleDocs) {
      const moduleDoc = await readFile(join(docsSiteRoot, moduleDocInfo.sourcePath.slice(1)), "utf8");
      assert.match(moduleDoc, /## Graph Surface/, `${moduleDocInfo.sourcePath} should explain its graph surface`);
      assert.match(moduleDoc, /This module is graph-backed/, `${moduleDocInfo.sourcePath} should be graph-first`);
      assert.match(moduleDoc, /human-readable projection/, `${moduleDocInfo.sourcePath} should explain .0 projection snippets`);
      assert.match(moduleDoc, /zero query <graph-input>/, `${moduleDocInfo.sourcePath} should point agents at graph inspection`);
    }
    for (const moduleSlug of ["module-io", "module-rand", "module-proc", "module-crypto", "module-net", "module-http"]) {
      const moduleDoc = await readDoc(moduleSlug);
      for (const label of ["effects", "allocation behavior", "target support", "error behavior", "ownership notes", "example"]) {
        assert.match(moduleDoc, new RegExp(label));
      }
    }
    assert.match(await readDoc("cli-reference"), /toolchain/);
    assert.match(await readDoc("cli-reference"), /targetToolchains/);
    assert.match(await readDoc("cli-reference"), /zero dev --json/);
    assert.match(await readDoc("cli-reference"), /zero dev --json --trace/);
    assert.match(await readDoc("cli-reference"), /interfaceFingerprints/);
    assert.match(await readDoc("cli-reference"), /document symbols/);
    assert.match(await readDoc("cli-reference"), /zero ship --json/);
    assert.match(await readDoc("cli-reference"), /release preview/);
    assert.match(await readDoc("cli-reference"), /releaseTargetContract/);
    assert.match(await readDoc("cli-reference"), /compileTime/);
    assert.match(await readDoc("cli-reference"), /integer\/Bool\/enum static values/);
    assert.match(await readDoc("cli-reference"), /repeat-build hash policy/);
    assert.match(await readDoc("cli-reference"), /checksum file/);
    assert.match(await readDoc("cli-reference"), /SBOM placeholder/);
    assert.match(await readDoc("cli-reference"), /zero test --json/);
    assert.match(await readDoc("cli-reference"), /Human projection to graph import/);
    assert.doesNotMatch(await readDoc("cli-reference"), /Source-to-ProgramGraph import/);
    assert.doesNotMatch(await readDoc("cli-reference"), /source\/package\/artifact inputs/);
    assert.match(await readDoc("cli-reference"), /expectedFailures/);
    assert.match(await readDoc("cli-reference"), /snapshotKey/);
    assert.match(await readDoc("cli-reference"), /BLD003/);
    const testing = await readDoc("testing");
    for (const testingTerm of ["zero test --json", "expectedFailures", "fixtures", "snapshotKey", "reliability:smoke", "native:sanitize", "fuzz", "crasher"]) {
      assert.match(testing, new RegExp(testingTerm));
    }
    assert.doesNotMatch(testing, /repair agents/);
    const optimization = await readDoc("optimization");
    for (const optimizationTerm of ["profileSemantics", "profileBudget", "sizeBreakdown", "retentionReasons", "optimizationHints", "memoryBudgets", "allocatorFacts", "allocationInstrumentation", "collectionFacts", "trend summary", "ZERO_BENCH_RUNS=1 pnpm run bench", "debug", "fast", "small", "tiny"]) {
      assert.match(optimization, new RegExp(optimizationTerm));
    }
    const install = await readDoc("install");
    assert.match(install, /https:\/\/zerolang\.ai\/install\.sh/);
    assert.match(install, /release checksum file/);
    assert.match(install, /targetToolchains/);
    const packageManifest = await readDoc("package-manifest");
    for (const packageTerm of ["dependencies", "package.lockfile", "packageCache.cacheKeyInputs", "PKG001", "PKG004", "publicationGate"]) {
      assert.match(packageManifest, new RegExp(packageTerm));
    }
    assert.match(packageManifest, /checked-in\s+`zero\.graph` repository graph store/);
    assert.match(packageManifest, /compatibility manifest format/);
    const crossCompilation = await readDoc("cross-compilation");
    for (const runtimeTerm of ["Direct Artifacts", "Sysroots And C Boundaries", "Current Boundary"]) {
      assert.match(crossCompilation, new RegExp(runtimeTerm));
    }
    const stdlib = await readDoc("standard-library");
    for (const label of ["std.crypto", "std.net", "std.http", "effects", "allocation behavior", "target support", "error behavior", "ownership notes", "example"]) {
      assert.match(stdlib, new RegExp(label));
    }
    const languageReference = await readDoc("language-reference");
    for (const compileTimeTerm of ["compileTime", "target.pointerWidth", "fieldType", "hasEnumCase", "MET001", "integer, `Bool`, and enum static values", "runtime registries", "raw token-string builders"]) {
      assert.match(languageReference, new RegExp(compileTimeTerm));
    }
    const buildingFromSource = await readDoc("building-from-source");
    for (const target of publicTargets) {
      const targetPattern = new RegExp(target.replace(".", "\\."));
      assert.match(languageReference, targetPattern);
      assert.match(buildingFromSource, targetPattern);
    }
    const memModule = await readDoc("module-mem");
    for (const memTerm of ["NullAlloc", "FixedBufAlloc", "PageAlloc", "GeneralAlloc", "memoryBudgets", "allocatorFacts", "allocationInstrumentation", "collectionFacts", "heapBytes: 0", "hiddenHeapAllocation: false"]) {
      assert.match(memModule, new RegExp(memTerm));
    }
    const examples = await readDoc("examples");
    for (const example of [
      "examples/hello.graph",
      "examples/add.graph",
      "examples/point.graph",
      "examples/fixed-vec.graph",
      "examples/fallibility.graph",
      "examples/ownership-cleanup.graph",
      "examples/memory-primitives.graph",
      "examples/allocator-collections.graph",
      "examples/compile-time-v1.graph",
      "examples/cli-file.graph",
      "examples/cli-config.graph",
      "examples/grep-scan.graph",
      "examples/std-path-io.graph",
      "examples/std-testing-log.graph",
      "examples/std-data-formats.graph",
      "examples/json-api-client.graph",
      "examples/json-api-router.graph",
      "examples/crm-api/",
      "examples/std-platform.graph",
      "examples/file-copy.graph",
      "examples/zero-hash/",
      "examples/readall-cli/",
      "examples/memory-package/",
      "examples/resource-cli/",
      "examples/error-tour/",
      "examples/agent-repair-demo/",
    ]) {
      assert.match(examples, new RegExp(example.replace(".", "\\.")));
    }
    for (const exampleTerm of ["zero-hash", "Build command", "Run command", "Expected output", "Size output", "Inspect metadata", "Benchmark case", "Cross-target status"]) {
      assert.match(examples, new RegExp(exampleTerm));
    }
    for (const releaseLoopTerm of ["Native Workflow Coverage", "arguments and environment", "filesystem resources", "deterministic exit status", "unhandled error exit path"]) {
      assert.match(examples, new RegExp(releaseLoopTerm));
    }
    const learnZeroCleanup = await readDoc("learn-zero");
    assert.match(learnZeroCleanup, /canonical non-raising `fn drop\(self: mutref<Self>\) -> Void`/);
    assert.doesNotMatch(learnZeroCleanup, /More advanced ownership and `?\.drop\(\)`? behavior is still implementation work/);
    assert.match(buildingFromSource, /Building From Source/);
    for (const demoTerm of ["--release tiny", "fixed-capacity", "vtables", "generic registries", "hello-linux-musl", "hello-win32", "target report", "artifact size"]) {
      assert.match(examples, new RegExp(demoTerm, "i"));
    }
    for (const repairTerm of ["checks JSON diagnostics", "plans a repair", "applies the edit", "re-runs check"]) {
      assert.match(examples, new RegExp(repairTerm, "i"));
    }
    const homePage = await readFile(join(docsSiteRoot, "app/page.tsx"), "utf8");
    assert.match(homePage, /The programming language\s+<br \/>\s+for agents/);
    assert.match(homePage, /Graph is the artifact\.\s+<br \/>\s+Source is the projection\./);
    assert.match(homePage, /Semantic navigation/);
    assert.match(homePage, /Precise edits/);
    assert.match(homePage, /Validated refactors/);
    assert.match(homePage, /Shorter feedback loop/);
    assert.match(homePage, /Token efficiency, low memory usage, fast startup, fast builds, low runtime latency, and zero dependencies/);
    assert.doesNotMatch(homePage, /format, reparse/);
    assert.match(homePage, /InstallCopy/);
    const installCopy = await readFile(join(docsSiteRoot, "components/install-copy.tsx"), "utf8");
    assert.match(installCopy, /curl -fsSL https:\/\/zerolang\.ai\/install\.sh \| bash/);
    const packageJson = JSON.parse(await readFile(resolve(docsSiteRoot, "..", "package.json"), "utf8"));
    assert.match(packageJson.scripts["docs:build"], /docs/);
    assert.doesNotMatch(JSON.stringify(packageJson.scripts), /self-host|no-c|bootstrap-stage2/);
    const benchmarks = await readDoc("benchmarks");
    for (const caseName of ["hello", "add", "structs", "params", "buffers", "parser", "codec", "parse", "slices", "arena", "fallibility", "branches", "module-package", "rescue", "fs-resource", "mem-copy-fill", "zero-hash"]) {
      assert.match(benchmarks, new RegExp(caseName));
    }
    for (const claim of ["build time", "artifact size", "compressed size", "runMs", "peakRssBytes", "ZERO_BENCH_RUNS=<n>", "Sandbox mode"]) {
      assert.match(benchmarks, new RegExp(claim));
    }
    const cInterop = await readDoc("c-interop");
    for (const cTerm of ["generatedHeader.available", "typedModel", "header hash", "linkPlan", "CIMP003", "CIMP005"]) {
      assert.match(cInterop, new RegExp(cTerm));
    }
  });

  it("does not advertise removed backend paths", async () => {
    for (const doc of docs) {
      const source = await readFile(join(docsSiteRoot, doc.sourcePath.slice(1)), "utf8");
      assert.doesNotMatch(source, /zero build[^\n`]*--emit c/, `${doc.sourcePath} should not advertise removed backend builds`);
      assert.doesNotMatch(source, /--legacy-backend/, `${doc.sourcePath} should not advertise the removed backend flag`);
    }
  });

  it("keeps public docs prose scannable", async () => {
    for (const doc of docs) {
      const source = await readFile(join(docsSiteRoot, doc.sourcePath.slice(1)), "utf8");
      let inFence = false;
      source.split("\n").forEach((line, index) => {
        if (line.startsWith("```")) inFence = !inFence;
        if (inFence || line.startsWith("|")) return;
        assert.ok(line.length <= 240, `${doc.sourcePath}:${index + 1} has an overlong prose line`);
      });
    }
  });

  it("serves a static installer for latest GitHub releases", async () => {
    const installerPath = join(docsSiteRoot, "public/install.sh");
    const installer = await readFile(installerPath, "utf8");
    assert.match(installer, /^#!\/bin\/sh/);
    assert.match(installer, /releases\/latest\/download/);
    assert.match(installer, /CHECKSUMS\.txt/);
    assert.match(installer, /sha256sum/);
    assert.match(installer, /shasum -a 256/);
    assert.match(installer, /ZERO_INSTALL_DIR/);
    assert.match(installer, /ZERO_LINUX_FLAVOR/);
    assert.match(installer, /zero-\$\{platform\}-\$\{cpu\}\$\{exe_suffix\}/);

    const syntax = spawnSync("sh", ["-n", installerPath], { encoding: "utf8" });
    assert.equal(syntax.status, 0, syntax.stderr);
  });

  it("keeps public docs free of internal process narratives", async () => {
    const internalNarrative = /\b(?:ZERO-WORK|milestone|roadmap|self-host(?:ed|ing)?|no-C|generatedCBytes|no-c:report|release matrix|proof report|Stage3)\b/i;
    for (const doc of docs) {
      const source = await readFile(join(docsSiteRoot, doc.sourcePath.slice(1)), "utf8");
      assert.doesNotMatch(source, internalNarrative, `${doc.sourcePath} should read like public docs`);
    }
  });
});

describe("docs highlighter", () => {
  it("highlights mut as canonical source keyword", () => {
    const { highlight } = loadDocsModule("lib/highlight.ts");
    const html = highlight("fn bump(value: &mut i32) -> Void {}", "zero");

    assert.match(html, /<span class="hl-keyword">mut<\/span>/);
  });
});
