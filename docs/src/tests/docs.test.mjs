import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { access, readFile } from "node:fs/promises";
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

const docs = loadDocsRegistry();

describe("docs registry", () => {
  it("declares module pages with source files", async () => {
    const moduleDocs = docs.filter((doc) => doc.section === "Modules");

    assert.equal(moduleDocs.length, 15);
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
    assert.match(await readDoc("getting-started"), /```sh[\s\S]*zero new cli hello/);
    assert.match(await readDoc("getting-started"), /zero build --target linux-musl-x64/);
    assert.match(await readDoc("examples"), /bin\/zero check examples\/hello\.0/);
    const learnZero = await readDoc("learn-zero");
    for (const topic of ["main", "let", "Write Functions", "shape", "Field Defaults", "Span", "check", "Run Tests", "Cross Targets", "Diagnostics"]) {
      assert.match(learnZero, new RegExp(topic));
    }
    const diagnostics = await readDoc("diagnostics");
    assert.match(diagnostics, /JSON For Tools/);
    assert.match(diagnostics, /CIMP003/);
    assert.match(diagnostics, /configure-target-c-dependency/);
    assert.match(await readDoc("standard-library"), /zero graph --json/);
    assert.match(await readDoc("standard-library"), /usedStdlibHelpers/);
    assert.match(await readDoc("standard-library"), /ownershipNotes/);
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
    assert.match(await readDoc("cli-reference"), /expectedFailures/);
    assert.match(await readDoc("cli-reference"), /snapshotKey/);
    assert.match(await readDoc("cli-reference"), /BLD003/);
    const testing = await readDoc("testing");
    for (const testingTerm of ["zero test --json", "expectedFailures", "fixtures", "snapshotKey", "reliability:smoke", "native:sanitize", "fuzz", "crasher"]) {
      assert.match(testing, new RegExp(testingTerm));
    }
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
    const memModule = await readDoc("module-mem");
    for (const memTerm of ["NullAlloc", "FixedBufAlloc", "PageAlloc", "GeneralAlloc", "memoryBudgets", "allocatorFacts", "allocationInstrumentation", "collectionFacts", "heapBytes: 0", "hiddenHeapAllocation: false"]) {
      assert.match(memModule, new RegExp(memTerm));
    }
    const examples = await readDoc("examples");
    for (const example of [
      "examples/hello.0",
      "examples/add.0",
      "examples/point.0",
      "examples/fixed-vec.0",
      "examples/fallibility.0",
      "examples/ownership-cleanup.0",
      "examples/memory-primitives.0",
      "examples/allocator-collections.0",
      "examples/compile-time-v1.0",
      "examples/cli-file.0",
      "examples/std-path-io.0",
      "examples/std-data-formats.0",
      "examples/std-platform.0",
      "examples/file-copy.0",
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
    assert.match(learnZeroCleanup, /canonical non-raising `fun drop\(self: mutref<Self>\) -> Void`/);
    assert.doesNotMatch(learnZeroCleanup, /More advanced ownership and `?\.drop\(\)`? behavior is still implementation work/);
    assert.match(await readDoc("building-from-source"), /Building From Source/);
    for (const demoTerm of ["--release tiny", "fixed-capacity", "vtables", "generic registries", "hello-linux-musl", "hello-win32", "target report", "artifact size"]) {
      assert.match(examples, new RegExp(demoTerm, "i"));
    }
    for (const repairTerm of ["checks JSON diagnostics", "plans a repair", "applies the edit", "re-runs check"]) {
      assert.match(examples, new RegExp(repairTerm, "i"));
    }
    const homePage = await readFile(join(docsSiteRoot, "app/page.tsx"), "utf8");
    assert.match(homePage, /The programming language\s+<br \/>\s+for agents/);
    assert.match(homePage, /standard-library\s+first/);
    assert.match(homePage, /Agent-readable tooling/);
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
    for (const cTerm of ["generatedHeader.available", "typedModel", "header hash", "linkPlan", "CIMP003"]) {
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
