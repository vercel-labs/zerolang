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
  try {
    const result = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 });
    return { body: JSON.parse(result.stdout), code: 0 };
  } catch (error) {
    const failed = error as { stdout?: string; code?: number };
    assert(failed.stdout);
    return { body: JSON.parse(failed.stdout), code: failed.code ?? 1 };
  }
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

const inspect = await zeroJson(["graph", "inspect", "--json", "examples/hello.0"]);
assert.equal(inspect.agentQuery.lookupSurfaces.symbol.command, "zero graph find --json --symbol <symbol-or-name> <input>");
assert.equal(inspect.agentQuery.lookupSurfaces.symbol.followup, "zero graph slice --json --node <node-id> <input>");
assert(inspect.agentQuery.lookupSurfaces.symbol.fields.includes("matches[].nodeHash"));

const findByName = await zeroJson(["graph", "find", "--json", "--symbol", "main", "examples/hello.0"]);
assert.equal(findByName.ok, true);
assert.equal(findByName.agentCommand.kind, "agent-graph-find-command-contract");
assert.equal(findByName.agentCommand.readPolicy.name, "semantic-graph-query-read");
assert.equal(findByName.agentCommand.readPolicy.readsSource, true);
assert.equal(findByName.agentCommand.readPolicy.writesSource, false);
assert.equal(findByName.agentCommand.readPolicy.writesArtifacts, false);
assert.equal(findByName.agentCommand.readPolicy.fullSourceRequired, false);
assert.equal(findByName.agentCommand.readPolicy.tokenStrategyField, "tokenStrategy");
assert(findByName.agentCommand.auditFields.includes("agentCommand.readPolicy"));
assert.equal(findByName.agentCommand.verificationCommands[0].purpose, "graph-find");
assert.deepEqual(findByName.agentCommand.command.argv, findByName.agentCommand.verificationCommands[0].argv);
assert.equal(findByName.graphHash, inspect.programGraph.graphHash);
assert.equal(findByName.tokenStrategy.readFullSourceRequired, false);
assert.equal(findByName.tokenStrategy.preferFollowupSlice, true);
assert.equal(findByName.resolution.status, "unique-name");
assert.equal(findByName.resolution.unique, true);
assert.equal(findByName.resolution.ambiguous, false);
assert.equal(findByName.resolution.nameMatches, 1);
assert.equal(findByName.counts.matches, findByName.matches.length);
assert(findByName.agentCommand.auditFields.includes("resolution"));
assert(findByName.agentCommand.stateFields.includes("resolution.status"));
assert(findByName.agentCommand.stateFields.includes("resolution.ambiguous"));
assert.equal(findByName.agentCommand.recommendedNextCommands[0].purpose, "graph-slice");
assert.equal(findByName.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(findByName.agentCommand.recommendedNextCommands[0].when, "resolution.requiresFollowupSlice");
assert.equal(findByName.agentCommand.recommendedNextCommands[0].inputField, "matches[].id");
assert.deepEqual(findByName.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "slice", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "--node", "<matches[].id>", "<input>"]);
assert(findByName.agentCommand.recommendedNextCommands[0].resultFields.includes("center.nodeHash"));
assert(findByName.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert(findByName.matches.some((node) => node.kind === "Function" && node.name === "main" && node.symbolId.endsWith("::value.main")));
assert(findByName.matches.length < inspect.programGraph.nodes.length);

const main = findByName.matches.find((node) => node.kind === "Function" && node.name === "main");
assert(main);
const findBySymbol = await zeroJson(["graph", "find", "--json", "--symbol", main.symbolId, "examples/hello.0"]);
assert.equal(findBySymbol.matches.length, 1);
assert.equal(findBySymbol.resolution.status, "unique-symbol");
assert.equal(findBySymbol.resolution.exactSymbolMatches, 1);
assert.equal(findBySymbol.resolution.unique, true);
assert.equal(findBySymbol.matches[0].id, main.id);
assert.equal(findBySymbol.matches[0].nodeHash, main.nodeHash);
assert.deepEqual((await runRequiredCommands(findByName.agentCommand.verificationCommands)).map((item) => item.purpose), ["graph-find"]);

const slice = await zeroJson(["graph", "slice", "--json", "--node", main.id, "examples/hello.0"]);
assert.equal(slice.graphHash, findBySymbol.graphHash);
assert.equal(slice.center.id, main.id);
assert.equal(slice.center.nodeHash, main.nodeHash);
assert(slice.nodes.some((node) => node.kind === "Param" && node.name === "world"));

const ambiguousFind = await zeroJson(["graph", "find", "--json", "--symbol", "greeting", "examples/functions.0"]);
assert.equal(ambiguousFind.resolution.status, "ambiguous");
assert.equal(ambiguousFind.resolution.unique, false);
assert.equal(ambiguousFind.resolution.ambiguous, true);
assert.equal(ambiguousFind.resolution.exactSymbolMatches, 0);
assert(ambiguousFind.resolution.nameMatches > 1);
assert.equal(ambiguousFind.counts.matches, ambiguousFind.matches.length);
assert(ambiguousFind.matches.some((node) => node.kind === "Function" && node.symbolId?.endsWith("::value.greeting")));
assert.equal(ambiguousFind.agentCommand.recommendedNextCommands[1].purpose, "graph-slice");
assert.equal(ambiguousFind.agentCommand.recommendedNextCommands[1].required, false);
assert.equal(ambiguousFind.agentCommand.recommendedNextCommands[1].when, "resolution.ambiguous");
assert.equal(ambiguousFind.agentCommand.recommendedNextCommands[1].inputField, "matches[].id");
assert.deepEqual(ambiguousFind.agentCommand.recommendedNextCommands[1].argv, ["zero", "graph", "slice", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "--node", "<matches[].id>", "<input>"]);
assert(ambiguousFind.agentCommand.recommendedNextCommands[1].resultFields.includes("center.nodeHash"));
assert(ambiguousFind.agentCommand.recommendedNextCommands[1].resultFields.includes("nodes[]"));
assert(ambiguousFind.agentCommand.recommendedNextCommands[1].resultFields.includes("edges[]"));
assert(ambiguousFind.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"));

const missingFind = await zeroJsonAllowFailure(["graph", "find", "--json", "--symbol", "does_not_exist", "examples/hello.0"]);
assert.equal(missingFind.code, 1);
assert.equal(missingFind.body.ok, true);
assert.equal(missingFind.body.resolution.status, "not-found");
assert.equal(missingFind.body.resolution.unique, false);
assert.equal(missingFind.body.resolution.ambiguous, false);
assert.equal(missingFind.body.counts.matches, 0);
assert.equal(missingFind.body.agentCommand.recommendedNextCommands[2].purpose, "graph-inspect");
assert.equal(missingFind.body.agentCommand.recommendedNextCommands[2].required, false);
assert.equal(missingFind.body.agentCommand.recommendedNextCommands[2].when, "resolution.status == not-found");
assert.equal(missingFind.body.agentCommand.recommendedNextCommands[2].inputField, "command.argv input");
assert.deepEqual(missingFind.body.agentCommand.recommendedNextCommands[2].argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert(missingFind.body.agentCommand.recommendedNextCommands[2].resultFields.includes("programGraph.nodes[].symbolId"));
assert(missingFind.body.agentCommand.recommendedNextCommands[2].resultFields.includes("agentQuery.lookupSurfaces.symbol"));
assert(missingFind.body.agentCommand.recommendedNextCommands[2].resultFields.includes("agentCommand.verificationCommands"));

const targetFind = await zeroJson(["graph", "find", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--symbol", "main", "examples/hello.0"]);
assert.deepEqual(targetFind.agentCommand.command.argv, ["zero", "graph", "find", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--symbol", "main", "examples/hello.0"]);
assert.deepEqual(targetFind.agentCommand.command.argv, targetFind.agentCommand.verificationCommands[0].argv);
assert.deepEqual((await runRequiredCommands(targetFind.agentCommand.verificationCommands)).map((item) => item.purpose), ["graph-find"]);

const identityAudit = {
  exactSymbolUnique: findBySymbol.resolution.status === "unique-symbol" &&
    findBySymbol.matches.length === 1 &&
    findBySymbol.matches[0].id === main.id &&
    findBySymbol.matches[0].nodeHash === main.nodeHash,
  ambiguousNameStructured: ambiguousFind.resolution.status === "ambiguous" &&
    ambiguousFind.resolution.ambiguous === true &&
    ambiguousFind.counts.matches === ambiguousFind.matches.length,
  ambiguousSliceFollowupStructured: ambiguousFind.agentCommand.recommendedNextCommands[1].purpose === "graph-slice" &&
    ambiguousFind.agentCommand.recommendedNextCommands[1].when === "resolution.ambiguous" &&
    ambiguousFind.agentCommand.recommendedNextCommands[1].inputField === "matches[].id" &&
    ambiguousFind.agentCommand.recommendedNextCommands[1].argv.includes("<matches[].id>") &&
    ambiguousFind.agentCommand.recommendedNextCommands[1].resultFields.includes("center.nodeHash") &&
    ambiguousFind.agentCommand.recommendedNextCommands[1].resultFields.includes("agentCommand.verificationCommands"),
  notFoundInspectFollowupStructured: missingFind.body.resolution.status === "not-found" &&
    missingFind.body.agentCommand.recommendedNextCommands[2].purpose === "graph-inspect" &&
    missingFind.body.agentCommand.recommendedNextCommands[2].when === "resolution.status == not-found" &&
    missingFind.body.agentCommand.recommendedNextCommands[2].inputField === "command.argv input" &&
    missingFind.body.agentCommand.recommendedNextCommands[2].argv.includes("<same-target>") &&
    missingFind.body.agentCommand.recommendedNextCommands[2].argv.includes("<same-profile>") &&
    missingFind.body.agentCommand.recommendedNextCommands[2].resultFields.includes("programGraph.nodes[].symbolId"),
  followupSliceBound: slice.graphHash === findBySymbol.graphHash &&
    slice.center.id === findBySymbol.matches[0].id &&
    slice.center.nodeHash === findBySymbol.matches[0].nodeHash,
  contractDeclaresResolution: findByName.agentCommand.auditFields.includes("resolution") &&
    findByName.agentCommand.stateFields.includes("resolution.status") &&
    findByName.agentCommand.stateFields.includes("resolution.exactSymbolMatches"),
  contractDeclaresFollowupSliceCommand: findByName.agentCommand.recommendedNextCommands[0].purpose === "graph-slice" &&
    findByName.agentCommand.recommendedNextCommands[0].when === "resolution.requiresFollowupSlice" &&
    findByName.agentCommand.recommendedNextCommands[0].inputField === "matches[].id" &&
    findByName.agentCommand.recommendedNextCommands[0].resultFields.includes("center.nodeHash"),
};
assert.equal(identityAudit.exactSymbolUnique, true);
assert.equal(identityAudit.ambiguousNameStructured, true);
assert.equal(identityAudit.ambiguousSliceFollowupStructured, true);
assert.equal(identityAudit.notFoundInspectFollowupStructured, true);
assert.equal(identityAudit.followupSliceBound, true);
assert.equal(identityAudit.contractDeclaresResolution, true);
assert.equal(identityAudit.contractDeclaresFollowupSliceCommand, true);

const commandAudit = {
  replayable: [findByName, targetFind].every((item) =>
    JSON.stringify(item.agentCommand.command.argv) === JSON.stringify(item.agentCommand.verificationCommands[0].argv)),
  targetProfilePreserved: targetFind.agentCommand.command.argv.includes("--target") &&
    targetFind.agentCommand.command.argv.includes("linux-musl-x64") &&
    targetFind.agentCommand.command.argv.includes("--profile") &&
    targetFind.agentCommand.command.argv.includes("dev"),
  followupCommandStructured: [findByName, targetFind].every((item) =>
    item.agentCommand.recommendedNextCommands[0].purpose === "graph-slice" &&
    item.agentCommand.recommendedNextCommands[0].argv.includes("<matches[].id>")),
  readPolicyAuditable: [findByName, targetFind].every((item) =>
    item.agentCommand.readPolicy.name === "semantic-graph-query-read" &&
    item.agentCommand.readPolicy.writesSource === false &&
    item.agentCommand.readPolicy.writesArtifacts === false &&
    item.agentCommand.readPolicy.fullSourceRequired === false &&
    item.agentCommand.readPolicy.tokenStrategyField === "tokenStrategy" &&
    item.agentCommand.auditFields.includes("agentCommand.readPolicy")),
  commandKinds: [...new Set([findByName.agentCommand.kind, targetFind.agentCommand.kind])].sort(),
};
assert.equal(commandAudit.replayable, true);
assert.equal(commandAudit.targetProfilePreserved, true);
assert.equal(commandAudit.followupCommandStructured, true);
assert.equal(commandAudit.readPolicyAuditable, true);
assert.deepEqual(commandAudit.commandKinds, ["agent-graph-find-command-contract"]);

const proofAudit = {
  required: executedProofs.length,
  passed: executedProofs.filter((item) => item.ok).length,
  failed: executedProofs.filter((item) => !item.ok).length,
  purposes: [...new Set(executedProofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(executedProofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
assert.deepEqual(proofAudit.passedPurposes, ["graph-find"]);
console.log(JSON.stringify({ ok: true, kind: "agent-graph-find-smoke", proofAudit, commandAudit, identityAudit, findAudit: { fullNodes: inspect.programGraph.nodes.length, matches: findByName.matches.length, ambiguousMatches: ambiguousFind.matches.length, followupSliceNodes: slice.nodes.length } }, null, 2));
console.log("agent graph find smoke ok");
