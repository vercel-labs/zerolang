import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent graph inspect command smoke requires ZERO_BIN or ${defaultZeroBin}`);

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

const inspect = await zeroJson(["graph", "inspect", "--json", input]);
assert.equal(inspect.agentCommand.kind, "agent-graph-command-contract");
assert.equal(inspect.agentCommand.mode, "inspect");
assert.equal(inspect.agentCommand.readPolicy.name, "semantic-graph-read");
assert.equal(inspect.agentCommand.readPolicy.readsSource, true);
assert.equal(inspect.agentCommand.readPolicy.writesSource, false);
assert.equal(inspect.agentCommand.readPolicy.writesArtifacts, false);
assert.equal(inspect.agentCommand.readPolicy.fullSourceRequired, false);
assert.equal(inspect.agentCommand.readPolicy.tokenStrategyField, "agentQuery.tokenStrategy");
assert.deepEqual(inspect.agentCommand.verificationCommands.map((command) => command.purpose), ["graph-inspect"]);
assert(inspect.agentCommand.auditFields.includes("agentCommand.readPolicy"));
assert(inspect.agentCommand.auditFields.includes("agentQuery"));
assert(inspect.agentCommand.stateFields.includes("programGraph.graphHash"));
assert.equal(inspect.agentCommand.recommendedNextCommands[0].purpose, "graph-slice");
assert.equal(inspect.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(inspect.agentCommand.recommendedNextCommands[0].when, "after selecting programGraph.nodes[].id");
assert.equal(inspect.agentCommand.recommendedNextCommands[0].inputField, "programGraph.nodes[].id");
assert.deepEqual(inspect.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "slice", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "--node", "<programGraph.nodes[].id>", "<input>"]);
assert(inspect.agentCommand.recommendedNextCommands[0].resultFields.includes("center.nodeHash"));
assert(inspect.agentCommand.recommendedNextCommands[0].resultFields.includes("nodes[]"));
assert(inspect.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.equal(inspect.agentQuery.tokenStrategy.readFullSourceRequired, false);
assert.equal(inspect.agentQuery.tokenStrategy.preferNodeNeighborhood, true);
for (const contract of Object.values(inspect.agentQuery.lookupCommandContracts) as Array<{ argv: string[] }>) {
  assert(contract.argv.includes("--target"));
  assert(contract.argv.includes("<same-target>"));
  assert(contract.argv.includes("--profile"));
  assert(contract.argv.includes("<same-profile>"));
}
assert.deepEqual(inspect.agentQuery.lookupCommandContracts.symbol.argv, ["zero", "graph", "find", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "--symbol", "<symbol-or-name>", "<input>"]);
assert.deepEqual(inspect.agentQuery.lookupCommandContracts.editImpact.argv, ["zero", "graph", "impact", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "--node", "<node-id>", "<input>"]);
assert.deepEqual(inspect.agentQuery.lookupCommandContracts.nodeNeighborhood.argv, ["zero", "graph", "slice", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "--node", "<node-id>", "<input>"]);
assert.deepEqual(inspect.agentQuery.lookupCommandContracts.diagnosticRepairPlan.argv, ["zero", "fix", "--plan", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert(inspect.programGraph.graphHash.startsWith("graph:"));
assert(inspect.programGraph.nodes.some((node) => node.kind === "Function" && node.name === "main"));

const proofs = await runRequiredCommands(inspect.agentCommand.verificationCommands);
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 1);
assert.deepEqual(proofAudit.purposes, ["graph-inspect"]);
assert.deepEqual(proofAudit.passedPurposes, ["graph-inspect"]);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-graph-inspect-command-smoke",
  graphHash: inspect.programGraph.graphHash,
  nodes: inspect.programGraph.nodes.length,
  edges: inspect.programGraph.edges.length,
  readFullSourceRequired: inspect.agentQuery.tokenStrategy.readFullSourceRequired,
  commandAudit: {
    readPolicyAuditable: inspect.agentCommand.readPolicy.name === "semantic-graph-read" &&
      inspect.agentCommand.readPolicy.writesSource === false &&
      inspect.agentCommand.readPolicy.writesArtifacts === false &&
      inspect.agentCommand.readPolicy.fullSourceRequired === false &&
      inspect.agentCommand.readPolicy.tokenStrategyField === "agentQuery.tokenStrategy",
    sliceFollowupStructured: inspect.agentCommand.recommendedNextCommands[0].purpose === "graph-slice" &&
      inspect.agentCommand.recommendedNextCommands[0].inputField === "programGraph.nodes[].id" &&
      inspect.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
      inspect.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
      inspect.agentCommand.recommendedNextCommands[0].resultFields.includes("center.nodeHash"),
    lookupCommandContractsTargetProfilePreserved: Object.values(inspect.agentQuery.lookupCommandContracts).every((contract: any) =>
      contract.argv.includes("--target") &&
      contract.argv.includes("<same-target>") &&
      contract.argv.includes("--profile") &&
      contract.argv.includes("<same-profile>")),
  },
  proofAudit,
}, null, 2));
