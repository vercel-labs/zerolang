import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const fixture = "conformance/check/fail/unknown-name.0";

type Diagnostic = {
  code: string;
  path: string;
  line: number;
  column: number;
  repair: {
    id: string;
    agentRepair: {
      kind: string;
      safeDefault: string;
      transactionContract: string;
      commands: Array<{ purpose: string; required: boolean; argv: string[] }>;
      resultFields: string[];
    };
  };
  graphLookup: {
    kind: string;
    commands: Array<{ purpose: string; required: boolean; argv: string[] }>;
    spanMatch: { path: string; line: number; column: number; length: number; nodeFields: string[] };
    fallbacks: string[];
  };
};

const entrypointProofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean }> = [];
const graphLookupProofs: Array<{ purpose: string; required: boolean; argv: string[]; ok: boolean }> = [];
const entrypointResultFields = new Set<string>();
const graphLookupSpanFields = new Set<string>();
let graphLookupTargetProfilePreserved = false;
let graphLookupRepairFallback = false;
let entrypointRollbackActionFields = false;
let checkFailureFollowupsStructured = false;
let graphCheckFailureFollowupStructured = false;
let checkReadPolicyAuditable = false;

async function json(args: string[]) {
  try {
    const { stdout } = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 });
    return JSON.parse(stdout);
  } catch (error) {
    const stdout = error.stdout?.toString() ?? "";
    return JSON.parse(stdout);
  }
}

function assertCommand(command: { purpose: string; required: boolean; argv: string[] }, purpose: string, argv: string[], required = true) {
  assert.equal(command.purpose, purpose);
  assert.equal(command.required, required);
  assert.deepEqual(command.argv, argv);
  entrypointProofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: true });
}

function recordGraphLookupCommand(command: { purpose: string; required: boolean; argv: string[] }) {
  graphLookupProofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: true });
  if (command.argv.includes("--target") && command.argv.includes("linux-musl-x64") &&
    command.argv.includes("--profile") && command.argv.includes("dev")) {
    graphLookupTargetProfilePreserved = true;
  }
}

function assertAgentRepairEntrypoint(diagnostic: Diagnostic) {
  for (const field of diagnostic.repair.agentRepair.resultFields) entrypointResultFields.add(field);
  assert.equal(diagnostic.repair.agentRepair.kind, "diagnostic-repair-entrypoint");
  assert.equal(diagnostic.repair.agentRepair.safeDefault, "plan");
  assert.equal(diagnostic.repair.agentRepair.transactionContract, "agentTransaction");
  assertCommand(diagnostic.repair.agentRepair.commands[0], "repair-plan", ["zero", "fix", "--plan", "--json", diagnostic.path]);
  assertCommand(diagnostic.repair.agentRepair.commands[1], "repair-patch", ["zero", "fix", "--patch", "--json", diagnostic.path], false);
  assertCommand(diagnostic.repair.agentRepair.commands[2], "repair-apply", ["zero", "fix", "--apply", "--json", diagnostic.path], false);
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.command.argv"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.verificationCommands"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.failure.retryCommands"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.proofLedger"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.proofLedger.phases[].phase"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.proofLedger.phases[].status"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.proofLedger.phases[].evidenceFields"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.proofLedger.phases[].requiredPurposes"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.rollback.actions[]"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.rollback.actions[].kind"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.rollback.actions[].pathField"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.rollback.verificationCommands"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.rollback.verificationCommands[].purpose"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("agentTransaction.rollback.verificationCommands[].required"));
  entrypointRollbackActionFields = true;
  assert(diagnostic.repair.agentRepair.resultFields.includes("fixes[].graphPatchCandidates[].candidateContract"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("fixes[].graphPatchCandidates[].patch.text"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("fixes[].graphPatchCandidates[].patch.operations"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("fixes[].graphPatchCandidates[].verificationCommands"));
  assert(diagnostic.repair.agentRepair.resultFields.includes("diagnostics[].graphLookup"));
}

function assertGraphLookup(diagnostic: Diagnostic) {
  assert.equal(diagnostic.graphLookup.kind, "diagnostic-to-program-graph-lookup");
  assert.equal(diagnostic.graphLookup.commands[0].purpose, "graph-inspect");
  assert.equal(diagnostic.graphLookup.commands[0].required, true);
  assert.deepEqual(diagnostic.graphLookup.commands[0].argv, ["zero", "graph", "inspect", "--json", diagnostic.path]);
  recordGraphLookupCommand(diagnostic.graphLookup.commands[0]);
  assert.equal(diagnostic.graphLookup.commands[1].purpose, "graph-check");
  assert.equal(diagnostic.graphLookup.commands[1].required, true);
  assert.deepEqual(diagnostic.graphLookup.commands[1].argv, ["zero", "graph", "check", "--json", diagnostic.path]);
  recordGraphLookupCommand(diagnostic.graphLookup.commands[1]);
  assert.equal(diagnostic.graphLookup.spanMatch.path, diagnostic.path);
  assert.equal(diagnostic.graphLookup.spanMatch.line, diagnostic.line);
  assert.equal(diagnostic.graphLookup.spanMatch.column, diagnostic.column);
  for (const field of diagnostic.graphLookup.spanMatch.nodeFields) graphLookupSpanFields.add(field);
  assert(graphLookupSpanFields.has("programGraph.nodes[].path"));
  assert(graphLookupSpanFields.has("programGraph.nodes[].line"));
  assert(graphLookupSpanFields.has("programGraph.nodes[].column"));
  graphLookupRepairFallback = graphLookupRepairFallback || diagnostic.graphLookup.fallbacks.some((item) => item.includes("zero fix --plan --json"));
  assert(graphLookupRepairFallback);
}

function assertCheckFailureFollowups(report) {
  assert.equal(report.agentCommand.readPolicy.name, "semantic-diagnostic-read");
  assert.equal(report.agentCommand.readPolicy.readsSource, true);
  assert.equal(report.agentCommand.readPolicy.writesSource, false);
  assert.equal(report.agentCommand.readPolicy.writesArtifacts, false);
  assert.equal(report.agentCommand.readPolicy.fullSourceRequired, false);
  assert.equal(report.agentCommand.readPolicy.verificationField, "agentCommand.verificationCommands");
  assert(report.agentCommand.auditFields.includes("agentCommand.readPolicy"));
  checkReadPolicyAuditable = true;
  const followups = report.agentCommand.recommendedNextCommands;
  assert(Array.isArray(followups));
  const repairPlan = followups.find((item) => item.purpose === "repair-plan");
  assert(repairPlan);
  assert.equal(repairPlan.required, false);
  assert.equal(repairPlan.when, "diagnostics.length > 0");
  assert.equal(repairPlan.inputField, "command.argv input");
  assert.deepEqual(repairPlan.argv, ["zero", "fix", "--plan", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
  assert.equal(repairPlan.resultContract, "agentTransaction");
  assert(repairPlan.resultFields.includes("agentTransaction.proofLedger"));
  assert(repairPlan.resultFields.includes("agentTransaction.rollback.actions[]"));
  assert(repairPlan.resultFields.includes("agentTransaction.failure.retryCommands"));
  assert(repairPlan.resultFields.includes("fixes[].graphPatchCandidates[]"));
  assert(repairPlan.resultFields.includes("diagnostics[].graphLookup"));

  const graphInspect = followups.find((item) => item.purpose === "graph-inspect");
  assert(graphInspect);
  assert.equal(graphInspect.required, false);
  assert.equal(graphInspect.when, "diagnostics.length > 0");
  assert.equal(graphInspect.inputField, "diagnostics[].graphLookup or command.argv input");
  assert.deepEqual(graphInspect.argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
  assert(graphInspect.resultFields.includes("programGraph.graphHash"));
  assert(graphInspect.resultFields.includes("programGraph.nodes[].id"));
  assert(graphInspect.resultFields.includes("programGraph.nodes[].nodeHash"));
  assert(graphInspect.resultFields.includes("agentCommand.recommendedNextCommands"));
  assert(graphInspect.resultFields.includes("agentCommand.verificationCommands"));
  checkFailureFollowupsStructured = true;
}

function assertGraphCheckFailureFollowup(report) {
  const followup = report.agentCommand.recommendedNextCommands?.[0];
  assert(followup);
  assert.equal(followup.purpose, "graph-inspect");
  assert.equal(followup.required, false);
  assert.equal(followup.when, "diagnostics.length > 0");
  assert.equal(followup.inputField, "command.argv input");
  assert.deepEqual(followup.argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<program-graph>"]);
  assert(followup.resultFields.includes("programGraph.graphHash"));
  assert(followup.resultFields.includes("programGraph.nodes[].id"));
  assert(followup.resultFields.includes("programGraph.nodes[].nodeHash"));
  assert(followup.resultFields.includes("agentCommand.recommendedNextCommands"));
  assert(followup.resultFields.includes("agentCommand.verificationCommands"));
  graphCheckFailureFollowupStructured = true;
}

const check = await json(["check", "--json", fixture]);
assert.equal(check.ok, false);
assert.equal(check.diagnostics[0].code, "NAM003");
assertAgentRepairEntrypoint(check.diagnostics[0]);
assertGraphLookup(check.diagnostics[0]);
assertCheckFailureFollowups(check);
const targetCheck = await json(["check", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture]);
assertCheckFailureFollowups(targetCheck);
assertCommand(targetCheck.diagnostics[0].repair.agentRepair.commands[0], "repair-plan", ["zero", "fix", "--plan", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture]);
assertCommand(targetCheck.diagnostics[0].repair.agentRepair.commands[1], "repair-patch", ["zero", "fix", "--patch", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture], false);
assertCommand(targetCheck.diagnostics[0].repair.agentRepair.commands[2], "repair-apply", ["zero", "fix", "--apply", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture], false);
assert.equal(targetCheck.diagnostics[0].graphLookup.commands[0].purpose, "graph-inspect");
assert.equal(targetCheck.diagnostics[0].graphLookup.commands[0].required, true);
assert.deepEqual(targetCheck.diagnostics[0].graphLookup.commands[0].argv, ["zero", "graph", "inspect", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture]);
recordGraphLookupCommand(targetCheck.diagnostics[0].graphLookup.commands[0]);
assert.equal(targetCheck.diagnostics[0].graphLookup.commands[1].purpose, "graph-check");
assert.equal(targetCheck.diagnostics[0].graphLookup.commands[1].required, true);
assert.deepEqual(targetCheck.diagnostics[0].graphLookup.commands[1].argv, ["zero", "graph", "check", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture]);
recordGraphLookupCommand(targetCheck.diagnostics[0].graphLookup.commands[1]);

const plan = await json(["fix", "--plan", "--json", fixture]);
assert.equal(plan.ok, false);
assert.equal(plan.diagnostics[0].code, "NAM003");
assertAgentRepairEntrypoint(plan.diagnostics[0]);
assertGraphLookup(plan.diagnostics[0]);
const targetPlan = await json(["fix", "--plan", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture]);
assertCommand(targetPlan.diagnostics[0].repair.agentRepair.commands[0], "repair-plan", ["zero", "fix", "--plan", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture]);
assertCommand(targetPlan.diagnostics[0].repair.agentRepair.commands[1], "repair-patch", ["zero", "fix", "--patch", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture], false);
assertCommand(targetPlan.diagnostics[0].repair.agentRepair.commands[2], "repair-apply", ["zero", "fix", "--apply", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture], false);
assert.equal(targetPlan.diagnostics[0].graphLookup.commands[0].purpose, "graph-inspect");
assert.equal(targetPlan.diagnostics[0].graphLookup.commands[0].required, true);
assert.deepEqual(targetPlan.diagnostics[0].graphLookup.commands[0].argv, ["zero", "graph", "inspect", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture]);
recordGraphLookupCommand(targetPlan.diagnostics[0].graphLookup.commands[0]);
assert.equal(targetPlan.diagnostics[0].graphLookup.commands[1].purpose, "graph-check");
assert.equal(targetPlan.diagnostics[0].graphLookup.commands[1].required, true);
assert.deepEqual(targetPlan.diagnostics[0].graphLookup.commands[1].argv, ["zero", "graph", "check", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture]);
recordGraphLookupCommand(targetPlan.diagnostics[0].graphLookup.commands[1]);

const graphCheck = await json(["graph", "check", "--json", fixture]);
assert.equal(graphCheck.ok, false);
assert.equal(graphCheck.diagnostics[0].code, "NAM003");
assertGraphCheckFailureFollowup(graphCheck);

const entrypointAudit = {
  commands: entrypointProofs.length,
  required: entrypointProofs.filter((item) => item.required).length,
  optional: entrypointProofs.filter((item) => !item.required).length,
  purposes: [...new Set(entrypointProofs.map((item) => item.purpose))].sort(),
  resultFields: [...entrypointResultFields].sort(),
  rollbackActionFields: entrypointRollbackActionFields,
};
assert(entrypointAudit.required >= 4);
assert(entrypointAudit.optional >= 8);
assert.deepEqual(entrypointAudit.purposes, ["repair-apply", "repair-patch", "repair-plan"]);
const graphLookupAudit = {
  commands: graphLookupProofs.length,
  required: graphLookupProofs.filter((item) => item.required).length,
  purposes: [...new Set(graphLookupProofs.map((item) => item.purpose))].sort(),
  targetProfilePreserved: graphLookupTargetProfilePreserved,
  spanFields: [...graphLookupSpanFields].sort(),
  repairFallback: graphLookupRepairFallback,
  checkFailureFollowupsStructured,
  graphCheckFailureFollowupStructured,
  checkReadPolicyAuditable,
};
assert.equal(graphLookupAudit.commands, 8);
assert.equal(graphLookupAudit.required, 8);
assert.deepEqual(graphLookupAudit.purposes, ["graph-check", "graph-inspect"]);
assert.equal(graphLookupAudit.targetProfilePreserved, true);
assert.equal(graphLookupAudit.repairFallback, true);
assert.equal(graphLookupAudit.checkFailureFollowupsStructured, true);
assert.equal(graphLookupAudit.checkReadPolicyAuditable, true);
assert.equal(graphLookupAudit.graphCheckFailureFollowupStructured, true);
assert.equal(entrypointAudit.rollbackActionFields, true);

console.log(JSON.stringify({ ok: true, kind: "agent-diagnostic-entrypoint-smoke", entrypointAudit, graphLookupAudit }, null, 2));
console.log("agent diagnostic graph lookup smoke ok");
