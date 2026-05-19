#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const outDir = ".zero/reliability-smoke";
const zero = "bin/zero";
const rows = [];

await mkdir(outDir, { recursive: true });

async function run(args, { allowFailure = false } = {}) {
  try {
    const result = await execFileAsync(zero, args);
    return { code: 0, stdout: result.stdout, stderr: result.stderr };
  } catch (error) {
    if (!allowFailure) throw error;
    return {
      code: error.code ?? error.status ?? 1,
      stdout: error.stdout?.toString() ?? "",
      stderr: error.stderr?.toString() ?? "",
    };
  }
}

async function json(args, options = {}) {
  const result = await run(args, options);
  return { ...result, body: JSON.parse(result.stdout) };
}

function record(id, kind, ok, facts = {}) {
  rows.push({ id, kind, ok, ...facts });
  assert.equal(ok, true, `${id} failed`);
}

const packageTests = await json(["test", "--json", "conformance/packages/test-app"]);
record("package-test-golden", "golden", packageTests.body.ok === true && packageTests.body.stdout === "3 test(s) ok\n", {
  selectedTests: packageTests.body.selectedTests,
  expectedFailures: packageTests.body.expectedFailures,
  snapshotKey: packageTests.body.fixtures?.snapshotKey,
});

const filteredTests = await json(["test", "--json", "--filter", "helper", "conformance/packages/test-app"]);
record("package-test-filter", "golden", filteredTests.body.selectedTests === 2 && filteredTests.body.discoveredTests === 3, {
  stdout: filteredTests.body.stdout,
});

const expectedFail = await json(["test", "--json", "conformance/native/pass/test-expected-fail.0"]);
record("expected-fail-contract", "golden", expectedFail.body.expectedFailures === 1 && expectedFail.body.failedTests === 0, {
  status: expectedFail.body.results?.[0]?.status,
});

const unexpectedPass = await json(["test", "--json", "conformance/native/fail/test-unexpected-pass.0"], { allowFailure: true });
record("unexpected-pass-contract", "golden", unexpectedPass.code !== 0 && unexpectedPass.body.unexpectedPasses === 1, {
  stderr: unexpectedPass.body.stderr,
});

const validFuzz = join(outDir, "valid-fuzz.0");
await writeFile(validFuzz, `fun add(a: i32, b: i32) -> i32 {
    return a + b
}

test "fuzz valid arithmetic" {
    expect(add(21, 21) == 42)
}
`);
const validTokens = await json(["tokens", "--json", validFuzz]);
const validParse = await json(["parse", "--json", validFuzz]);
const validCheck = await json(["check", "--json", validFuzz]);
record("parser-checker-fuzz-valid", "fuzz", validTokens.body.tokens.length > 0 && validParse.body.root.kind === "module" && validCheck.body.ok === true, {
  tokenCount: validTokens.body.tokens.length,
});

const invalidFuzz = join(outDir, "invalid-fuzz.0");
await writeFile(invalidFuzz, "pub fun main( -> Void {\n");
const invalidCheck = await json(["check", "--json", invalidFuzz], { allowFailure: true });
record("parser-checker-fuzz-invalid", "fuzz", invalidCheck.code !== 0 && invalidCheck.body.diagnostics?.length > 0, {
  diagnostic: invalidCheck.body.diagnostics?.[0]?.code,
});

const fmtFixture = await run(["fmt", "conformance/native/pass/test-blocks.0"]);
const fmtExpected = await readFile("conformance/native/pass/test-blocks.0", "utf8");
record("formatter-snapshot", "snapshot", fmtFixture.stdout === fmtExpected, {
  bytes: fmtFixture.stdout.length,
});

const stdlibJson = await json(["size", "--json", "examples/std-data-formats.0"]);
record("stdlib-parser-golden", "golden", stdlibJson.body.usedStdlibHelpers?.some((helper) => helper.name === "std.json.parse"), {
  helperCount: stdlibJson.body.usedStdlibHelpers?.length ?? 0,
});

const crasher = join(outDir, "crasher-repro-unclosed-string.0");
await writeFile(crasher, "pub fun main() -> Void { let value = \"unterminated\n");
const crasherResult = await json(["check", "--json", crasher], { allowFailure: true });
record("minimized-crasher-repro", "crasher", crasherResult.code !== 0 && crasherResult.body.diagnostics?.length > 0, {
  diagnostic: crasherResult.body.diagnostics?.[0]?.code,
});

const report = {
  schemaVersion: 1,
  ok: rows.every((row) => row.ok),
  rows,
  conventions: {
    fuzzCorpus: [validFuzz, invalidFuzz, "conformance/parse/compiler-smoke.0", "examples/std-data-formats.0"],
    goldenOutputs: ["zero test --json conformance/packages/test-app", "zero fmt conformance/native/pass/test-blocks.0"],
    crasherRepros: [crasher],
    sanitizerGate: "pnpm run native:sanitize",
  },
};

await writeFile(join(outDir, "report.json"), `${JSON.stringify(report, null, 2)}\n`);
console.log("reliability smoke ok");
