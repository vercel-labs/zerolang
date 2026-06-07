import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const executedProofs = [];

async function zeroJson(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 });
  return JSON.parse(result.stdout);
}

async function zeroJsonAllowFailure(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 }).catch((error) => error);
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const results = [];
  for (const command of commands) {
    assert.equal(command.argv[0], "zero");
    if (!command.required) continue;
    const body = await zeroJson(command.argv.slice(1));
    const proof = { purpose: command.purpose, ok: body.ok ?? true };
    results.push(proof);
    executedProofs.push(proof);
  }
  return results;
}

const input = "examples/functions.0";
const inspect = await zeroJson(["graph", "inspect", "--json", input]);
assert.equal(inspect.agentQuery.lookupSurfaces.editImpact.command, "zero graph impact --json --node <node-id> <input>");
assert(inspect.agentQuery.lookupSurfaces.editImpact.fields.includes("directCallSites[].id"));
assert(inspect.agentQuery.lookupSurfaces.editImpact.useBefore.includes("removeFunction"));

const find = await zeroJson(["graph", "find", "--json", "--symbol", "greeting", input]);
const greeting = find.matches.find((node) => node.kind === "Function" && node.name === "greeting");
assert(greeting);

const impact = await zeroJson(["graph", "impact", "--json", "--node", greeting.id, input]);
assert.equal(impact.ok, true);
assert.equal(impact.graphHash, inspect.programGraph.graphHash);
assert.equal(impact.target.id, greeting.id);
assert.equal(impact.target.nodeHash, greeting.nodeHash);
assert.equal(impact.agentCommand.kind, "agent-graph-impact-command-contract");
assert.equal(impact.agentCommand.readPolicy.name, "semantic-graph-query-read");
assert.equal(impact.agentCommand.readPolicy.readsSource, true);
assert.equal(impact.agentCommand.readPolicy.writesSource, false);
assert.equal(impact.agentCommand.readPolicy.writesArtifacts, false);
assert.equal(impact.agentCommand.readPolicy.fullSourceRequired, false);
assert.equal(impact.agentCommand.readPolicy.tokenStrategyField, "tokenStrategy");
assert(impact.agentCommand.auditFields.includes("agentCommand.readPolicy"));
assert.equal(impact.agentCommand.verificationCommands[0].purpose, "graph-impact");
assert.deepEqual(impact.agentCommand.command.argv, impact.agentCommand.verificationCommands[0].argv);
assert.equal(impact.tokenStrategy.readFullSourceRequired, false);
assert.equal(impact.tokenStrategy.preferPatchPreconditions, true);
assert(impact.directCallSites.some((node) => node.kind === "Call" && node.name === "greeting"));
assert(impact.nameReferences.some((node) => node.kind === "Identifier" && node.name === "greeting"));
assert(impact.editGuards.includes("removeFunction-blocked-by-direct-call-sites"));
assert(impact.editGuards.includes("renameSymbol-updates-references"));
assert.equal(impact.agentCommand.recommendedNextCommands[0].purpose, "graph-patch");
assert.equal(impact.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(impact.agentCommand.recommendedNextCommands[0].when, "after-reviewing-editGuards");
assert.equal(impact.agentCommand.recommendedNextCommands[0].inputField, "target.id");
assert.deepEqual(impact.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "patch", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>", "<patch-file>"]);
assert.equal(impact.agentCommand.recommendedNextCommands[0].resultContract, "agentTransaction");
assert(impact.agentCommand.recommendedNextCommands[0].resultFields.includes("agentTransaction.proofLedger"));
assert(impact.agentCommand.recommendedNextCommands[0].resultFields.includes("agentTransaction.rollback.actions[]"));
assert(impact.agentCommand.recommendedNextCommands[0].resultFields.includes("operations[].retryCommands"));
assert(impact.directCallSites.length + impact.nameReferences.length < inspect.programGraph.nodes.length);
assert.deepEqual((await runRequiredCommands(impact.agentCommand.verificationCommands)).map((item) => item.purpose), ["graph-impact"]);

const targetFind = await zeroJson(["graph", "find", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--symbol", "greeting", input]);
const targetGreeting = targetFind.matches.find((node) => node.kind === "Function" && node.name === "greeting");
assert(targetGreeting);
const targetImpact = await zeroJson(["graph", "impact", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--node", targetGreeting.id, input]);
assert.deepEqual(targetImpact.agentCommand.command.argv, ["zero", "graph", "impact", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--node", targetGreeting.id, input]);
assert.deepEqual(targetImpact.agentCommand.command.argv, targetImpact.agentCommand.verificationCommands[0].argv);
assert.deepEqual((await runRequiredCommands(targetImpact.agentCommand.verificationCommands)).map((item) => item.purpose), ["graph-impact"]);

const missingImpact = await zeroJsonAllowFailure(["graph", "impact", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--node", "stale-node-id", input]);
assert.equal(missingImpact.ok, false);
assert.equal(missingImpact.agentCommand.kind, "agent-command-failure-contract");
const missingImpactInspect = missingImpact.agentCommand.recommendedNextCommands.find((item) => item.purpose === "graph-inspect");
assert(missingImpactInspect);
assert.equal(missingImpactInspect.required, false);
assert.equal(missingImpactInspect.inputField, "diagnostics[].actual");
assert.deepEqual(missingImpactInspect.argv, ["zero", "graph", "inspect", "--json", "--target", "linux-musl-x64", "--profile", "dev", input]);
assert(missingImpactInspect.resultFields.includes("programGraph.nodes[].id"));
assert(missingImpactInspect.resultFields.includes("agentCommand.verificationCommands"));

const commandAudit = {
  replayable: [impact, targetImpact].every((item) =>
    JSON.stringify(item.agentCommand.command.argv) === JSON.stringify(item.agentCommand.verificationCommands[0].argv)),
  targetProfilePreserved: targetImpact.agentCommand.command.argv.includes("--target") &&
    targetImpact.agentCommand.command.argv.includes("linux-musl-x64") &&
    targetImpact.agentCommand.command.argv.includes("--profile") &&
    targetImpact.agentCommand.command.argv.includes("dev"),
  graphPatchFollowupStructured: [impact, targetImpact].every((item) =>
    item.agentCommand.recommendedNextCommands[0].purpose === "graph-patch" &&
    item.agentCommand.recommendedNextCommands[0].resultContract === "agentTransaction" &&
    item.agentCommand.recommendedNextCommands[0].resultFields.includes("operations[].retryCommands")),
  graphPatchFollowupTargetProfilePreserved: [impact, targetImpact].every((item) =>
    item.agentCommand.recommendedNextCommands[0].argv.includes("--target") &&
    item.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    item.agentCommand.recommendedNextCommands[0].argv.includes("--profile") &&
    item.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>")),
  missingNodeInspectFollowupStructured: missingImpactInspect.purpose === "graph-inspect" &&
    missingImpactInspect.argv.includes("linux-musl-x64") &&
    missingImpactInspect.argv.includes("dev") &&
    missingImpactInspect.resultFields.includes("programGraph.nodes[].nodeHash"),
  readPolicyAuditable: [impact, targetImpact].every((item) =>
    item.agentCommand.readPolicy.name === "semantic-graph-query-read" &&
    item.agentCommand.readPolicy.writesSource === false &&
    item.agentCommand.readPolicy.writesArtifacts === false &&
    item.agentCommand.readPolicy.fullSourceRequired === false &&
    item.agentCommand.readPolicy.tokenStrategyField === "tokenStrategy" &&
    item.agentCommand.auditFields.includes("agentCommand.readPolicy")),
  commandKinds: [...new Set([impact.agentCommand.kind, targetImpact.agentCommand.kind])].sort(),
};
assert.equal(commandAudit.replayable, true);
assert.equal(commandAudit.targetProfilePreserved, true);
assert.equal(commandAudit.graphPatchFollowupStructured, true);
assert.equal(commandAudit.graphPatchFollowupTargetProfilePreserved, true);
assert.equal(commandAudit.missingNodeInspectFollowupStructured, true);
assert.equal(commandAudit.readPolicyAuditable, true);
assert.deepEqual(commandAudit.commandKinds, ["agent-graph-impact-command-contract"]);

const impactCommandAudit = {
  editGuardsStructured: impact.editGuards.includes("removeFunction-blocked-by-direct-call-sites") &&
    impact.editGuards.includes("renameSymbol-updates-references"),
  graphPatchFollowupStructured: commandAudit.graphPatchFollowupStructured,
  missingNodeInspectFollowupStructured: commandAudit.missingNodeInspectFollowupStructured,
};
assert.equal(impactCommandAudit.editGuardsStructured, true);
assert.equal(impactCommandAudit.graphPatchFollowupStructured, true);
assert.equal(impactCommandAudit.missingNodeInspectFollowupStructured, true);

const proofAudit = {
  required: executedProofs.length,
  passed: executedProofs.filter((item) => item.ok).length,
  failed: executedProofs.filter((item) => !item.ok).length,
  purposes: [...new Set(executedProofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(executedProofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
assert.deepEqual(proofAudit.passedPurposes, ["graph-impact"]);
console.log(JSON.stringify({ ok: true, kind: "agent-graph-impact-smoke", proofAudit, commandAudit, impactCommandAudit, impactAudit: { fullNodes: inspect.programGraph.nodes.length, directCallSites: impact.directCallSites.length, nameReferences: impact.nameReferences.length, editGuards: impact.editGuards } }, null, 2));
console.log("agent graph impact smoke ok");
