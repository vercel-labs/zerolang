import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent proof receipt smoke requires ZERO_BIN or ${defaultZeroBin}`);
const outDir = join(".zero", "agent-proof-receipt-smoke", String(process.pid));
const sourcePath = join(outDir, "src", "hello.0");
const graphPath = join(outDir, "hello.program-graph");
const graphBuildPath = join(outDir, "hello-graph-build");
const sourceShipPath = join(outDir, "hello-release");
const graphShipPath = join(outDir, "hello-graph-release");

mkdirSync(dirname(sourcePath), { recursive: true });
writeFileSync(sourcePath, readFileSync("examples/hello.0", "utf8"));

async function zeroJson(args: string[]) {
  const startedAt = new Date().toISOString();
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  const finishedAt = new Date().toISOString();
  const stdout = result.stdout?.toString() ?? "";
  const body = JSON.parse(stdout);
  return { body, startedAt, finishedAt, exitCode: result.code ?? 0 };
}

function hasDeclaredField(body: any, field: string) {
  const parts = field.split(".");
  let current = body;
  for (const part of parts) {
    if (part.endsWith("[]")) {
      current = current?.[part.slice(0, -2)];
      if (!Array.isArray(current)) return false;
      current = current[0] ?? {};
    } else {
      if (current == null || !(part in current)) return false;
      current = current[part];
    }
  }
  return true;
}

const manifestRun = await zeroJson(["agent", "protocol", "--json"]);
const manifest = manifestRun.body;
const policy = manifest.proofReceiptPolicy;
assert.equal(policy.owner, "external-agent");
assert.equal(policy.source, "compiler-declared-verificationCommands");
for (const field of ["purpose", "required", "command.argv", "exitCode", "ok", "resultFields", "observedFields", "startedAt", "finishedAt"]) {
  assert(policy.requiredFields.includes(field), `proofReceiptPolicy.requiredFields missing ${field}`);
}

const receipts = [];
async function appendReceipts(source: string, commands: any[]) {
  for (const command of commands.filter((item) => item.required)) {
    assert(manifest.proofPurposes.includes(command.purpose), `unknown proof purpose ${command.purpose}`);
    assert.equal(command.argv[0], "zero");
    const result = await zeroJson(command.argv.slice(1));
    const resultFields = command.resultFields ?? manifest.proofResultFields[command.purpose];
    assert(Array.isArray(resultFields) && resultFields.length > 0, `missing result fields for ${command.purpose}`);
    const observedFields = resultFields.filter((field) => hasDeclaredField(result.body, field));
    assert.deepEqual(observedFields.sort(), [...resultFields].sort());
    receipts.push({
      source,
      purpose: command.purpose,
      required: command.required,
      command: { argv: command.argv },
      exitCode: result.exitCode,
      ok: result.exitCode === 0,
      resultFields,
      observedFields,
      startedAt: result.startedAt,
      finishedAt: result.finishedAt,
    });
  }
}

await appendReceipts("protocol-manifest", manifest.agentCommand.verificationCommands);
const sourceCheck = await zeroJson(["check", "--json", "examples/hello.0"]);
await appendReceipts("source-check", sourceCheck.body.agentCommand.verificationCommands);
const graphInspect = await zeroJson(["graph", "inspect", "--json", "examples/hello.0"]);
await appendReceipts("graph-inspect", graphInspect.body.agentCommand.verificationCommands);
const build = await zeroJson(["build", "--json", sourcePath]);
assert.equal(build.body.agentCommand.kind, "agent-build-command-contract");
assert(build.body.agentCommand.verificationCommands.some((command) =>
  command.purpose === "artifact-validate" && command.resultFields?.includes("artifactHash")));
await appendReceipts("artifact-build", build.body.agentCommand.verificationCommands);
const graphDump = await zeroJson(["graph", "dump", "--json", "--out", graphPath, sourcePath]);
assert.equal(typeof graphDump.body.graphHash, "string");
assert.equal(existsSync(graphPath), true);
const graphBuild = await zeroJson(["graph", "build", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", graphBuildPath, graphPath]);
assert.equal(graphBuild.body.agentCommand.kind, "agent-graph-build-command-contract");
assert(graphBuild.body.agentCommand.verificationCommands.some((command) =>
  command.purpose === "artifact-validate" && command.resultFields?.includes("artifactHash")));
await appendReceipts("graph-artifact-build", graphBuild.body.agentCommand.verificationCommands);
const sourceShip = await zeroJson(["ship", "--json", "--target", "win32-x64.exe", "--profile", "release", "--out", sourceShipPath, sourcePath]);
assert.equal(sourceShip.body.agentCommand.kind, "agent-ship-command-contract");
assert(sourceShip.body.agentCommand.verificationCommands.some((command) =>
  command.purpose === "artifact-validate" && command.resultFields?.includes("checksum")));
await appendReceipts("artifact-ship", sourceShip.body.agentCommand.verificationCommands);
const graphShip = await zeroJson(["graph", "ship", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--out", graphShipPath, graphPath]);
assert.equal(graphShip.body.agentCommand.kind, "agent-ship-command-contract");
assert(graphShip.body.agentCommand.verificationCommands.some((command) =>
  command.purpose === "artifact-validate" && command.resultFields?.includes("checksum")));
await appendReceipts("graph-artifact-ship", graphShip.body.agentCommand.verificationCommands);
const sourceTest = await zeroJson(["test", "--json", "--target", "win32-x64.exe", "--profile", "release", sourcePath]);
assert.equal(sourceTest.body.agentCommand.kind, "agent-test-command-contract");
await appendReceipts("source-test", sourceTest.body.agentCommand.verificationCommands);
const graphTest = await zeroJson(["graph", "test", "--json", "--target", "linux-musl-x64", "--profile", "dev", graphPath]);
assert.equal(graphTest.body.agentCommand.kind, "agent-graph-test-command-contract");
await appendReceipts("graph-test", graphTest.body.agentCommand.verificationCommands);

for (const receipt of receipts) {
  assert.equal(receipt.ok, true);
  for (const field of policy.requiredFields) {
    assert(hasDeclaredField(receipt, field), `receipt missing required policy field ${field}`);
  }
}

const report = {
  ok: true,
  kind: "agent-proof-receipt-smoke",
  receipts: receipts.length,
  receiptPurposes: receipts.map((receipt) => receipt.purpose),
  receiptSources: receipts.map((receipt) => receipt.source),
  policyRequiredFields: policy.requiredFields.length,
  declaredFieldsMatched: receipts.every((receipt) => receipt.resultFields.length === receipt.observedFields.length),
  argvBound: receipts.every((receipt) => receipt.command.argv[0] === "zero" && receipt.command.argv.length >= 3),
  sourceWorkflowReceipts: receipts.some((receipt) => receipt.purpose === "source-check") &&
    receipts.some((receipt) => receipt.purpose === "graph-inspect"),
  artifactWorkflowReceipts: receipts.some((receipt) =>
    receipt.source === "artifact-build" && receipt.purpose === "artifact-validate") &&
    receipts.some((receipt) => receipt.source === "artifact-build" && receipt.purpose === "size-analysis"),
  graphArtifactWorkflowReceipts: receipts.some((receipt) =>
    receipt.source === "graph-artifact-build" && receipt.purpose === "artifact-validate") &&
    receipts.some((receipt) => receipt.source === "graph-artifact-build" && receipt.purpose === "graph-size"),
  artifactShipWorkflowReceipts: receipts.some((receipt) =>
    receipt.source === "artifact-ship" && receipt.purpose === "artifact-validate") &&
    receipts.some((receipt) => receipt.source === "artifact-ship" && receipt.purpose === "size-analysis"),
  graphArtifactShipWorkflowReceipts: receipts.some((receipt) =>
    receipt.source === "graph-artifact-ship" && receipt.purpose === "artifact-validate") &&
    receipts.some((receipt) => receipt.source === "graph-artifact-ship" && receipt.purpose === "graph-size"),
  testWorkflowReceipts: receipts.some((receipt) =>
    receipt.source === "source-test" && receipt.purpose === "test-run") &&
    receipts.some((receipt) => receipt.source === "source-test" && receipt.purpose === "source-check"),
  graphTestWorkflowReceipts: receipts.some((receipt) =>
    receipt.source === "graph-test" && receipt.purpose === "graph-test") &&
    receipts.some((receipt) => receipt.source === "graph-test" && receipt.purpose === "graph-check"),
  timingRecorded: receipts.every((receipt) => receipt.startedAt && receipt.finishedAt),
  rejectRules: policy.rejectIf,
};

console.log(JSON.stringify(report, null, 2));
