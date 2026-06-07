import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent dev/doc command smoke requires ZERO_BIN or ${defaultZeroBin}`);

const input = "examples/hello.0";

async function zeroJson(args: string[]) {
  const { stdout } = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 });
  return JSON.parse(stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const result = await execFileAsync(zero, command.argv.slice(1), { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: !result.code });
  }
  return proofs;
}

const doc = await zeroJson(["doc", "--json", input]);
assert.equal(doc.agentCommand.kind, "agent-doc-command-contract");
assert.deepEqual(doc.agentCommand.verificationCommands.map((command) => command.purpose), ["doc-audit", "source-check"]);
assert(doc.agentCommand.auditFields.includes("publicationGate"));
assert(doc.agentCommand.stateFields.includes("symbols[].targetSupport"));
assert.equal(doc.agentCommand.recommendedNextCommands[0].purpose, "graph-find");
assert.equal(doc.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(doc.agentCommand.recommendedNextCommands[0].when, "publicationGate.missingExampleCount > 0 or symbol audit needs semantic location");
assert.equal(doc.agentCommand.recommendedNextCommands[0].inputField, "symbols[].name");
assert.deepEqual(doc.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "find", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "--symbol", "<symbols[].name>", "<input>"]);
assert(doc.agentCommand.recommendedNextCommands[0].resultFields.includes("graphHash"));
assert(doc.agentCommand.recommendedNextCommands[0].resultFields.includes("matches[].id"));
assert(doc.agentCommand.recommendedNextCommands[0].resultFields.includes("matches[].nodeHash"));
assert(doc.agentCommand.recommendedNextCommands[0].resultFields.includes("resolution.status"));
assert(doc.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands"));
assert(doc.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert(doc.symbols.some((symbol) => symbol.name === "main" && symbol.kind === "function"));
assert.equal(doc.publicationGate.requiresExamplesForPublicApi, true);
assert(doc.publicationGate.missingExampleCount >= 1);

const dev = await zeroJson(["dev", "--json", input]);
assert.equal(dev.agentCommand.kind, "agent-dev-command-contract");
assert.deepEqual(dev.agentCommand.verificationCommands.map((command) => command.purpose), ["dev-plan", "source-check"]);
assert(dev.agentCommand.auditFields.includes("watch"));
assert(dev.agentCommand.auditFields.includes("incrementalInvalidation"));
assert(dev.agentCommand.stateFields.includes("partialDiagnostics.stable"));
assert.equal(dev.mode, "watch-plan");
assert.equal(dev.partialDiagnostics.stable, true);
assert.equal(dev.watch.planOnly, true);
assert.equal(dev.affected.modules, 1);
assert(dev.actions.some((action) => action.kind === "restart" && action.enabled === true));

const proofs = [
  ...await runRequiredCommands(doc.agentCommand.verificationCommands),
  ...await runRequiredCommands(dev.agentCommand.verificationCommands),
];
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 4);
assert.deepEqual(proofAudit.purposes, ["dev-plan", "doc-audit", "source-check"]);
assert.deepEqual(proofAudit.passedPurposes, ["dev-plan", "doc-audit", "source-check"]);

const workflowAudit = {
  docCommandReplayable: JSON.stringify(doc.agentCommand.command.argv) ===
    JSON.stringify(doc.agentCommand.verificationCommands.find((command) => command.purpose === "doc-audit")?.argv),
  devCommandReplayable: JSON.stringify(dev.agentCommand.command.argv) ===
    JSON.stringify(dev.agentCommand.verificationCommands.find((command) => command.purpose === "dev-plan")?.argv),
  publicationGateStructured: doc.agentCommand.auditFields.includes("publicationGate") &&
    doc.agentCommand.stateFields.includes("symbols[].targetSupport") &&
    doc.publicationGate.requiresExamplesForPublicApi === true &&
    typeof doc.publicationGate.missingExampleCount === "number",
  docSymbolGraphFindFollowupStructured: doc.agentCommand.recommendedNextCommands[0].purpose === "graph-find" &&
    doc.agentCommand.recommendedNextCommands[0].inputField === "symbols[].name" &&
    doc.agentCommand.recommendedNextCommands[0].argv.includes("<symbols[].name>") &&
    doc.agentCommand.recommendedNextCommands[0].resultFields.includes("matches[].nodeHash") &&
    doc.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands"),
  devPlanStructured: dev.agentCommand.auditFields.includes("watch") &&
    dev.agentCommand.auditFields.includes("incrementalInvalidation") &&
    dev.agentCommand.stateFields.includes("partialDiagnostics.stable") &&
    dev.watch.planOnly === true &&
    dev.partialDiagnostics.stable === true &&
    dev.actions.some((action) => action.kind === "restart" && action.enabled === true),
};
assert.equal(workflowAudit.docCommandReplayable, true);
assert.equal(workflowAudit.devCommandReplayable, true);
assert.equal(workflowAudit.publicationGateStructured, true);
assert.equal(workflowAudit.docSymbolGraphFindFollowupStructured, true);
assert.equal(workflowAudit.devPlanStructured, true);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-dev-doc-command-smoke",
  doc: {
    symbols: doc.symbols.length,
    missingExampleCount: doc.publicationGate.missingExampleCount,
  },
  dev: {
    mode: dev.mode,
    affectedModules: dev.affected.modules,
    partialDiagnosticsStable: dev.partialDiagnostics.stable,
  },
  workflowAudit,
  proofAudit,
}, null, 2));
