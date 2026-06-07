import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent graph artifact command smoke requires ZERO_BIN or ${defaultZeroBin}`);

const outDir = join(".zero", "agent-graph-artifact-command-smoke", String(process.pid));
const artifactPath = join(outDir, "hello.program-graph");
const viewPath = join(outDir, "hello-view.0");
const roundtripPath = join(outDir, "hello-roundtrip.program-graph");
const validatePath = join(outDir, "hello-validated.program-graph");
rmSync(outDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });

async function zeroJson(args: string[]) {
  const { stdout } = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 });
  return JSON.parse(stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const result = await execFileAsync(zero, command.argv.slice(1), { maxBuffer: 8 * 1024 * 1024 }).catch((error) => error);
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: !result.code });
  }
  return proofs;
}

function artifactWritePolicyStructured(report) {
  return report.agentCommand.auditFields.includes("agentCommand.writePolicy") &&
    report.agentCommand.writePolicy?.name === "writes-artifact" &&
    report.agentCommand.writePolicy?.writesSource === false &&
    report.agentCommand.writePolicy?.writesArtifacts === true &&
    report.agentCommand.writePolicy?.artifactPathField === report.agentCommand.artifact.pathField &&
    report.agentCommand.writePolicy?.artifactPathField === "saved.path" &&
    report.agentCommand.writePolicy?.requiresVerification === true &&
    report.agentCommand.writePolicy?.verificationField === "agentCommand.verificationCommands" &&
    report.agentCommand.writePolicy?.rollbackField === "saved.path" &&
    report.agentCommand.writePolicy?.rollbackPolicy === "agent-enforced-delete-or-replace" &&
    report.agentCommand.writePolicy?.rollbackActions?.some((action) =>
      action.kind === "delete-path" &&
      action.pathField === "saved.path" &&
      action.condition === "created-by-command" &&
      action.requiresVerification === true);
}

const imported = await zeroJson(["graph", "import", "--json", "--out", artifactPath, "examples/hello.0"]);
assert.equal(imported.agentCommand.kind, "agent-graph-import-command-contract");
assert.deepEqual(imported.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-import", "artifact-validate"]);
assert.equal(imported.saved.path, artifactPath);
assert.equal(imported.saved.byteStable, true);
assert.equal(imported.validation.ok, true);
assert(imported.graphHash);
assert(existsSync(artifactPath));
assert.equal(imported.agentCommand.recommendedNextCommands[0].purpose, "graph-view");
assert.equal(imported.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(imported.agentCommand.recommendedNextCommands[0].when, "saved.path is present");
assert.equal(imported.agentCommand.recommendedNextCommands[0].inputField, "saved.path");
assert.deepEqual(imported.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "view", "--json", "<saved.path>"]);
assert(imported.agentCommand.recommendedNextCommands[0].resultFields.includes("view"));
assert(imported.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.equal(imported.agentCommand.recommendedNextCommands[1].purpose, "graph-roundtrip");
assert.equal(imported.agentCommand.recommendedNextCommands[1].required, false);
assert.equal(imported.agentCommand.recommendedNextCommands[1].inputField, "saved.path");
assert.deepEqual(imported.agentCommand.recommendedNextCommands[1].argv, ["zero", "graph", "roundtrip", "--json", "<saved.path>"]);
assert(imported.agentCommand.recommendedNextCommands[1].resultFields.includes("semanticStable"));
assert(imported.agentCommand.recommendedNextCommands[1].resultFields.includes("comparison.ok"));

const viewed = await zeroJson(["graph", "view", "--json", artifactPath]);
assert.equal(viewed.agentCommand.kind, "agent-graph-view-command-contract");
assert.deepEqual(viewed.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-view"]);
assert.equal(viewed.agentCommand.writePolicy.name, "preview-only");
assert.equal(viewed.graphHash, imported.graphHash);
assert.equal(viewed.moduleIdentity, imported.moduleIdentity);
assert.match(viewed.view, /hello from zero/);
assert.equal(viewed.agentCommand.recommendedNextCommands[0].purpose, "graph-check");
assert.equal(viewed.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(viewed.agentCommand.recommendedNextCommands[0].when, "after reviewing the source view when target-aware validation is needed");
assert.equal(viewed.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(viewed.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "check", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<program-graph>"]);
assert(viewed.agentCommand.recommendedNextCommands[0].resultFields.includes("graphHash"));
assert(viewed.agentCommand.recommendedNextCommands[0].resultFields.includes("moduleIdentity"));
assert(viewed.agentCommand.recommendedNextCommands[0].resultFields.includes("check"));
assert(viewed.agentCommand.recommendedNextCommands[0].resultFields.includes("targetReadiness"));
assert(viewed.agentCommand.recommendedNextCommands[0].resultFields.includes("diagnostics"));
assert(viewed.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));

const viewedOut = await zeroJson(["graph", "view", "--json", "--out", viewPath, artifactPath]);
assert.equal(viewedOut.agentCommand.kind, "agent-graph-view-command-contract");
assert.equal(viewedOut.saved.path, viewPath);
assert.equal(viewedOut.saved.byteStable, true);
assert.equal(viewedOut.view, null);
assert.equal(artifactWritePolicyStructured(viewedOut), true);
assert(existsSync(viewPath));

const graphCheck = await zeroJson(["graph", "check", "--json", artifactPath]);
assert.equal(graphCheck.agentCommand.kind, "agent-graph-check-command-contract");
assert.equal(graphCheck.ok, true);
assert.equal(graphCheck.graphHash, imported.graphHash);
assert.equal(graphCheck.agentCommand.recommendedNextCommands[1].purpose, "graph-size");
assert.equal(graphCheck.agentCommand.recommendedNextCommands[1].required, false);
assert.equal(graphCheck.agentCommand.recommendedNextCommands[1].when, "after graph check passes when size or build budget review is needed");
assert.equal(graphCheck.agentCommand.recommendedNextCommands[1].inputField, "command.argv input");
assert.deepEqual(graphCheck.agentCommand.recommendedNextCommands[1].argv, ["zero", "graph", "size", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<program-graph>"]);
assert(graphCheck.agentCommand.recommendedNextCommands[1].resultFields.includes("sizeBreakdown"));
assert(graphCheck.agentCommand.recommendedNextCommands[1].resultFields.includes("profileBudget"));
assert(graphCheck.agentCommand.recommendedNextCommands[1].resultFields.includes("profileSemantics"));
assert(graphCheck.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"));

const roundtrip = await zeroJson(["graph", "roundtrip", "--json", artifactPath]);
assert.equal(roundtrip.agentCommand.kind, "agent-graph-roundtrip-command-contract");
assert.deepEqual(roundtrip.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-roundtrip"]);
assert.equal(roundtrip.agentCommand.writePolicy.name, "preview-only");
assert.equal(roundtrip.semanticStable, true);
assert.equal(roundtrip.comparison.ok, true);
assert.equal(roundtrip.originalGraphHash, imported.graphHash);
assert.equal(roundtrip.roundtripGraphHash, imported.graphHash);
assert.equal(roundtrip.agentCommand.recommendedNextCommands[0].purpose, "graph-view");
assert.equal(roundtrip.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(roundtrip.agentCommand.recommendedNextCommands[0].when, "saved.path is present or source artifact is inspectable");
assert.equal(roundtrip.agentCommand.recommendedNextCommands[0].inputField, "saved.path or command.argv input");
assert.deepEqual(roundtrip.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "view", "--json", "<saved.path-or-input>"]);
assert(roundtrip.agentCommand.recommendedNextCommands[0].resultFields.includes("view"));
assert(roundtrip.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));

const roundtripOut = await zeroJson(["graph", "roundtrip", "--json", "--out", roundtripPath, artifactPath]);
assert.equal(roundtripOut.agentCommand.kind, "agent-graph-roundtrip-command-contract");
assert.equal(roundtripOut.saved.path, roundtripPath);
assert.equal(roundtripOut.saved.kind, "program-graph");
assert.equal(roundtripOut.saved.byteStable, true);
assert.equal(artifactWritePolicyStructured(roundtripOut), true);
assert(existsSync(roundtripPath));

const validatedOut = await zeroJson(["graph", "validate", "--json", "--out", validatePath, artifactPath]);
assert.equal(validatedOut.agentCommand.kind, "agent-graph-validate-command-contract");
assert.equal(validatedOut.saved.path, validatePath);
assert.equal(validatedOut.saved.byteStable, true);
assert.equal(validatedOut.validation.ok, true);
assert.equal(artifactWritePolicyStructured(validatedOut), true);
assert(existsSync(validatePath));
assert.equal(validatedOut.agentCommand.recommendedNextCommands[0].purpose, "graph-view");
assert.equal(validatedOut.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(validatedOut.agentCommand.recommendedNextCommands[0].when, "after artifact validation when a token-bounded source view is needed");
assert.equal(validatedOut.agentCommand.recommendedNextCommands[0].inputField, "saved.path or command.argv input");
assert.deepEqual(validatedOut.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "view", "--json", "<saved.path-or-input>"]);
assert(validatedOut.agentCommand.recommendedNextCommands[0].resultFields.includes("graphHash"));
assert(validatedOut.agentCommand.recommendedNextCommands[0].resultFields.includes("moduleIdentity"));
assert(validatedOut.agentCommand.recommendedNextCommands[0].resultFields.includes("view"));
assert(validatedOut.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));

const proofs = [
  ...await runRequiredCommands(imported.agentCommand.verificationCommands),
  ...await runRequiredCommands(viewed.agentCommand.verificationCommands),
  ...await runRequiredCommands(roundtrip.agentCommand.verificationCommands),
  ...await runRequiredCommands(validatedOut.agentCommand.verificationCommands),
];
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

const commandAudit = {
  artifactWritePolicyAuditable: [imported, viewedOut, roundtripOut, validatedOut].every(artifactWritePolicyStructured) &&
    viewed.agentCommand.writePolicy?.name === "preview-only" &&
    roundtrip.agentCommand.writePolicy?.name === "preview-only",
  importFollowupsStructured: imported.agentCommand.recommendedNextCommands[0].purpose === "graph-view" &&
    imported.agentCommand.recommendedNextCommands[0].inputField === "saved.path" &&
    imported.agentCommand.recommendedNextCommands[1].purpose === "graph-roundtrip" &&
    imported.agentCommand.recommendedNextCommands[1].resultFields.includes("comparison.ok"),
  roundtripFollowupStructured: roundtrip.agentCommand.recommendedNextCommands[0].purpose === "graph-view" &&
    roundtrip.agentCommand.recommendedNextCommands[0].inputField === "saved.path or command.argv input" &&
    roundtrip.agentCommand.recommendedNextCommands[0].resultFields.includes("view") &&
    roundtrip.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"),
  validateFollowupStructured: validatedOut.agentCommand.recommendedNextCommands[0].purpose === "graph-view" &&
    validatedOut.agentCommand.recommendedNextCommands[0].inputField === "saved.path or command.argv input" &&
    validatedOut.agentCommand.recommendedNextCommands[0].argv.includes("<saved.path-or-input>") &&
    validatedOut.agentCommand.recommendedNextCommands[0].resultFields.includes("view") &&
    validatedOut.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"),
  viewCheckFollowupStructured: viewed.agentCommand.recommendedNextCommands[0].purpose === "graph-check" &&
    viewed.agentCommand.recommendedNextCommands[0].inputField === "command.argv input" &&
    viewed.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    viewed.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    viewed.agentCommand.recommendedNextCommands[0].resultFields.includes("check") &&
    viewed.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"),
  graphCheckSizeFollowupStructured: graphCheck.agentCommand.recommendedNextCommands[1].purpose === "graph-size" &&
    graphCheck.agentCommand.recommendedNextCommands[1].inputField === "command.argv input" &&
    graphCheck.agentCommand.recommendedNextCommands[1].argv.includes("<same-target>") &&
    graphCheck.agentCommand.recommendedNextCommands[1].argv.includes("<same-profile>") &&
    graphCheck.agentCommand.recommendedNextCommands[1].resultFields.includes("sizeBreakdown") &&
    graphCheck.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"),
};
assert.equal(commandAudit.importFollowupsStructured, true);
assert.equal(commandAudit.roundtripFollowupStructured, true);
assert.equal(commandAudit.validateFollowupStructured, true);
assert.equal(commandAudit.viewCheckFollowupStructured, true);
assert.equal(commandAudit.graphCheckSizeFollowupStructured, true);
assert.equal(commandAudit.artifactWritePolicyAuditable, true);

assert.equal(proofAudit.required, 5);
assert.deepEqual(proofAudit.purposes, ["artifact-validate", "graph-import", "graph-roundtrip", "graph-view"]);
assert.deepEqual(proofAudit.passedPurposes, ["artifact-validate", "graph-import", "graph-roundtrip", "graph-view"]);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-graph-artifact-command-smoke",
  artifactPath,
  graphHash: imported.graphHash,
  moduleIdentity: imported.moduleIdentity,
  semanticStable: roundtrip.semanticStable,
  commandAudit,
  proofAudit,
}, null, 2));
