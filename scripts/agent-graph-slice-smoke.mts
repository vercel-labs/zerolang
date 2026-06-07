import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";

type GraphNode = {
  id: string;
  kind: string;
  name?: string;
  type?: string;
  value?: string;
  symbolId?: string;
  nodeHash?: string;
  path?: string;
  line?: number;
  column?: number;
};

type GraphEdge = {
  from: string;
  to: string;
  kind: string;
};

type InspectJson = {
  agentQuery: {
    kind: string;
    stableKeys: string[];
    lookupSurfaces: {
      symbol: { command: string; fields: string[] };
      nodeNeighborhood: { command: string; fields: string[]; recommendedRadius: number };
      capability: { command: string; fields: string[]; indexField: string };
      diagnostic: { command: string; fields: string[]; repairPlanCommand: string; graphLookupField: string };
    };
    tokenStrategy: { readFullSourceRequired: boolean; preferNodeNeighborhood: boolean; recommendedRadius: number };
    repairLoop: {
      start: string;
      plan: string;
      inspect: string;
      patch: string;
      verify: string;
      retry: string;
      commandContracts: Record<string, { argv: string[] }>;
      stateFields: string[];
      auditFields: string[];
    };
    checkedEditSurface: {
      command: string;
      requiresNodeIds: boolean;
      supportedOperations: string[];
      transactionContract: {
        kind: string;
        phases: string[];
        resultFields: string[];
        failureClasses: Array<{ code: string; class: string; phase: string; retryCommand: string | null }>;
        rollback: {
          savedKind: string[];
          sourceVerification: string[];
          artifactVerification: string[];
          sourceVerificationCommands: Array<{ purpose: string; required: boolean; argv: string[] }>;
          artifactVerificationCommands: Array<{ purpose: string; required: boolean; argv: string[] }>;
        };
      };
      operationContracts: Array<{
        op: string;
        required: string[];
        requiredAny?: string[][];
        optional?: string[];
        targetKinds?: string[];
        expectChecks?: string;
      }>;
    };
  };
  programGraph: {
    graphHash: string;
    nodes: GraphNode[];
    edges: GraphEdge[];
  };
};

type GraphJson = {
  agentQuery: InspectJson["agentQuery"];
  agentLookupIndexes: {
    capabilities: Array<{ name: string; functions: string[]; stdlibHelpers: string[] }>;
  };
};

const { stdout } = await execFileAsync(zero, ["graph", "inspect", "--json", "examples/hello.0"], { maxBuffer: 8 * 1024 * 1024 });
const inspect = JSON.parse(stdout) as InspectJson;
const graph = inspect.programGraph;
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

assert.equal(inspect.agentQuery.kind, "agent-program-graph-query-contract");
assert.equal(inspect.agentQuery.lookupSurfaces.symbol.command, "zero graph find --json --symbol <symbol-or-name> <input>");
assert.equal(inspect.agentQuery.lookupSurfaces.symbol.followup, "zero graph slice --json --node <node-id> <input>");
assert(inspect.agentQuery.lookupSurfaces.symbol.fields.includes("matches[].symbolId"));
assert.equal(inspect.agentQuery.lookupSurfaces.editImpact.command, "zero graph impact --json --node <node-id> <input>");
assert(inspect.agentQuery.lookupSurfaces.editImpact.fields.includes("editGuards[]"));
assert(inspect.agentQuery.lookupSurfaces.editImpact.useBefore.includes("renameSymbol"));
assert.equal(inspect.agentQuery.lookupSurfaces.nodeNeighborhood.command, "zero graph slice --json --node <node-id> <input>");
assert.equal(inspect.agentQuery.lookupSurfaces.nodeNeighborhood.recommendedRadius, 1);
assert(inspect.agentQuery.lookupSurfaces.nodeNeighborhood.fields.includes("edges[].kind"));
assert.equal(inspect.agentQuery.lookupSurfaces.capability.command, "zero graph --json <input>");
assert(inspect.agentQuery.lookupSurfaces.capability.fields.includes("functions[].requiresCapabilities"));
assert(inspect.agentQuery.lookupSurfaces.capability.fields.includes("agentLookupIndexes.capabilities[]"));
assert.equal(inspect.agentQuery.lookupSurfaces.capability.indexField, "agentLookupIndexes.capabilities");
assert.equal(inspect.agentQuery.lookupSurfaces.diagnostic.command, "zero check --json <input>");
assert.equal(inspect.agentQuery.lookupSurfaces.diagnostic.repairPlanCommand, "zero fix --plan --json <input>");
assert(inspect.agentQuery.lookupSurfaces.diagnostic.fields.includes("diagnostics[].repair"));
assert(inspect.agentQuery.lookupSurfaces.diagnostic.fields.includes("diagnostics[].graphLookup"));
assert.equal(inspect.agentQuery.lookupSurfaces.diagnostic.graphLookupField, "diagnostics[].graphLookup");
assert.equal(inspect.agentQuery.tokenStrategy.readFullSourceRequired, false);
assert.equal(inspect.agentQuery.tokenStrategy.preferNodeNeighborhood, true);
assert.equal(inspect.agentQuery.repairLoop.start, "zero check --json <input>");
assert.equal(inspect.agentQuery.repairLoop.plan, "zero fix --plan --json <input>");
assert.equal(inspect.agentQuery.repairLoop.inspect, "zero graph inspect --json <input>");
assert.equal(inspect.agentQuery.repairLoop.patch, "zero graph patch --json <input> <patch-file>");
assert.equal(inspect.agentQuery.repairLoop.verify, "agentTransaction.verificationCommands[]");
assert.equal(inspect.agentQuery.repairLoop.retry, "agentTransaction.failure.retryCommands[]");
assert.equal(inspect.agentQuery.repairLoop.operationRetry, "operations[].retryCommands[]");
assert.deepEqual(inspect.agentQuery.repairLoop.commandContracts.start.argv, ["zero", "check", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(inspect.agentQuery.repairLoop.commandContracts.plan.argv, ["zero", "fix", "--plan", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(inspect.agentQuery.repairLoop.commandContracts.inspect.argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert.deepEqual(inspect.agentQuery.repairLoop.commandContracts.patch.argv, ["zero", "graph", "patch", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>", "<patch-file>"]);
assert(inspect.agentQuery.repairLoop.stateFields.includes("programGraph.graphHash"));
assert(inspect.agentQuery.repairLoop.stateFields.includes("fixes[].graphPatchCandidates[].patch.graphHash"));
assert(inspect.agentQuery.repairLoop.auditFields.includes("fixes[].graphPatchCandidates[]"));
assert(inspect.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger"));
assert(inspect.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger.phases[].status"));
assert(inspect.agentQuery.repairLoop.auditFields.includes("agentTransaction.rollback"));
assert.equal(inspect.agentQuery.checkedEditSurface.command, "zero graph patch --json");
assert.equal(inspect.agentQuery.checkedEditSurface.transactionContract.kind, "compiler-mediated-graph-patch-transaction");
assert(inspect.agentQuery.checkedEditSurface.transactionContract.phases.includes("verify"));
assert(inspect.agentQuery.checkedEditSurface.transactionContract.resultFields.includes("agentTransaction.phaseAudit"));
assert(inspect.agentQuery.checkedEditSurface.transactionContract.resultFields.includes("agentTransaction.failure.retryCommands"));
assert(inspect.agentQuery.checkedEditSurface.transactionContract.failureClasses.some((item) => item.code === "GPH002" && item.phase === "inspect" && item.retryCommand === "zero graph dump --json <input>"));
assert(inspect.agentQuery.checkedEditSurface.transactionContract.failureClasses.some((item) => item.code === "GPH005" && item.class === "precondition-failed" && item.retryCommand === "operations[].retryCommands[] graph-impact"));
assert.deepEqual(inspect.agentQuery.checkedEditSurface.transactionContract.rollback.savedKind, ["source", "artifact"]);
assert.deepEqual(inspect.agentQuery.checkedEditSurface.transactionContract.rollback.sourceVerification, ["zero check --json <path>", "zero graph check --json <path>"]);
assert.deepEqual(inspect.agentQuery.checkedEditSurface.transactionContract.rollback.artifactVerification, ["zero graph validate --json <path>", "zero graph check --json <path>"]);
assert.deepEqual(inspect.agentQuery.checkedEditSurface.transactionContract.rollback.sourceVerificationCommands[0], { purpose: "rollback-source-check", required: true, argv: ["zero", "check", "--json", "<path>"], resultFields: ["ok", "diagnostics", "agentCommand.verificationCommands"] });
assert.deepEqual(inspect.agentQuery.checkedEditSurface.transactionContract.rollback.sourceVerificationCommands[1], { purpose: "rollback-graph-check", required: true, argv: ["zero", "graph", "check", "--json", "<path>"], resultFields: ["ok", "diagnostics", "graphHash", "agentCommand.verificationCommands"] });
assert.deepEqual(inspect.agentQuery.checkedEditSurface.transactionContract.rollback.artifactVerificationCommands[0], { purpose: "rollback-artifact-validate", required: true, argv: ["zero", "graph", "validate", "--json", "<path>"], resultFields: ["ok", "graphHash", "validation.ok", "agentCommand.verificationCommands"] });
assert.deepEqual(inspect.agentQuery.checkedEditSurface.transactionContract.rollback.artifactVerificationCommands[1], { purpose: "rollback-graph-check", required: true, argv: ["zero", "graph", "check", "--json", "<path>"], resultFields: ["ok", "diagnostics", "graphHash", "agentCommand.verificationCommands"] });
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("replaceCallee"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("addImport"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("addFunction"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("addParam"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("removeParam"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("removeFunction"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("removeImport"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("replaceImport"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("renameImportAlias"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("changeReturnType"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("changeParamType"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("renameSymbol"));
assert(inspect.agentQuery.checkedEditSurface.supportedOperations.includes("renameParam"));
const replaceCalleeContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "replaceCallee");
assert(replaceCalleeContract);
assert.deepEqual(replaceCalleeContract.required, ["node", "value"]);
assert.deepEqual(replaceCalleeContract.targetKinds, ["Call", "MethodCall"]);
assert.equal(replaceCalleeContract.expectChecks, "current callee name");
const addImportContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "addImport");
assert(addImportContract);
assert.deepEqual(addImportContract.required, ["name"]);
const addFunctionContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "addFunction");
assert(addFunctionContract);
assert.deepEqual(addFunctionContract.required, ["name", "type"]);
assert.deepEqual(addFunctionContract.targetKinds, ["Module"]);
assert(addFunctionContract.optional.includes("params"));
assert(addFunctionContract.creates.includes("Function node"));
assert(addFunctionContract.creates.some((item) => item.includes("Param")));
const addParamContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "addParam");
assert(addParamContract);
assert.deepEqual(addParamContract.required, ["node", "name", "type"]);
assert.deepEqual(addParamContract.targetKinds, ["Function"]);
assert(addParamContract.updates.includes("direct Call arg lists when value is supplied"));
const removeParamContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "removeParam");
assert(removeParamContract);
assert.deepEqual(removeParamContract.required, ["node"]);
assert.deepEqual(removeParamContract.targetKinds, ["Param"]);
assert(removeParamContract.rejects.includes("parameter still used in function body"));
const removeFunctionContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "removeFunction");
assert(removeFunctionContract);
assert.deepEqual(removeFunctionContract.required, ["node"]);
assert.deepEqual(removeFunctionContract.targetKinds, ["Function"]);
assert.equal(removeFunctionContract.expectChecks, "current function name");
assert(removeFunctionContract.rejects.includes("direct call sites"));
const removeImportContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "removeImport");
assert(removeImportContract);
assert.deepEqual(removeImportContract.required, []);
assert.deepEqual(removeImportContract.requiredAny, [["node"], ["name"]]);
assert.deepEqual(removeImportContract.targetKinds, ["Import"]);
assert.equal(removeImportContract.expectChecks, "current import module name");
const replaceImportContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "replaceImport");
assert(replaceImportContract);
assert.deepEqual(replaceImportContract.required, ["value"]);
assert.deepEqual(replaceImportContract.requiredAny, [["node"], ["name"]]);
assert.deepEqual(replaceImportContract.targetKinds, ["Import"]);
assert.equal(replaceImportContract.expectChecks, "current import module name");
const renameImportAliasContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "renameImportAlias");
assert(renameImportAliasContract);
assert.deepEqual(renameImportAliasContract.required, ["value"]);
assert.deepEqual(renameImportAliasContract.requiredAny, [["node"], ["name"]]);
assert.deepEqual(renameImportAliasContract.targetKinds, ["Import"]);
assert.equal(renameImportAliasContract.expectChecks, "current import alias");
const changeReturnTypeContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "changeReturnType");
assert(changeReturnTypeContract);
assert.deepEqual(changeReturnTypeContract.required, ["node", "value"]);
assert.deepEqual(changeReturnTypeContract.targetKinds, ["Function"]);
assert.equal(changeReturnTypeContract.expectChecks, "current function return type");
const changeParamTypeContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "changeParamType");
assert(changeParamTypeContract);
assert.deepEqual(changeParamTypeContract.required, ["node", "value"]);
assert.deepEqual(changeParamTypeContract.targetKinds, ["Param"]);
assert.equal(changeParamTypeContract.expectChecks, "current parameter type");
const renameSymbolContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "renameSymbol");
assert(renameSymbolContract);
assert.deepEqual(renameSymbolContract.required, ["node", "value"]);
assert.deepEqual(renameSymbolContract.targetKinds, ["Function"]);
const renameParamContract = inspect.agentQuery.checkedEditSurface.operationContracts.find((item) => item.op === "renameParam");
assert(renameParamContract);
assert.deepEqual(renameParamContract.required, ["node", "value"]);
assert.deepEqual(renameParamContract.targetKinds, ["Param"]);
assert.equal(renameParamContract.expectChecks, "current parameter name");
assert(inspect.agentQuery.stableKeys.includes("programGraph.nodes[].symbolId"));

const main = graph.nodes.find((node) => node.kind === "Function" && node.symbolId?.endsWith("::value.main"));
assert(main);
assert(main.id);
assert(main.nodeHash);
assert.equal(main.path?.endsWith("hello.0"), true);

const neighborhoodIds = new Set<string>([main.id]);
for (const edge of graph.edges) {
  if (edge.from === main.id) neighborhoodIds.add(edge.to);
  if (edge.to === main.id) neighborhoodIds.add(edge.from);
}
const oneHop = graph.nodes.filter((node) => neighborhoodIds.has(node.id));
assert(oneHop.some((node) => node.kind === "Param" && node.name === "world"));
assert(oneHop.some((node) => node.kind === "Block"));
const slice = await zeroJson(["graph", "slice", "--json", "--node", main.id, "examples/hello.0"]);
assert.equal(slice.ok, true);
assert.equal(slice.agentCommand.kind, "agent-graph-slice-command-contract");
assert.equal(slice.agentCommand.readPolicy.name, "semantic-graph-query-read");
assert.equal(slice.agentCommand.readPolicy.readsSource, true);
assert.equal(slice.agentCommand.readPolicy.writesSource, false);
assert.equal(slice.agentCommand.readPolicy.writesArtifacts, false);
assert.equal(slice.agentCommand.readPolicy.fullSourceRequired, false);
assert.equal(slice.agentCommand.readPolicy.tokenStrategyField, "tokenStrategy");
assert(slice.agentCommand.auditFields.includes("agentCommand.readPolicy"));
assert.equal(slice.agentCommand.verificationCommands[0].purpose, "graph-slice");
assert.deepEqual(slice.agentCommand.command.argv, slice.agentCommand.verificationCommands[0].argv);
assert.equal(slice.graphHash, graph.graphHash);
assert.equal(slice.center.id, main.id);
assert.equal(slice.tokenStrategy.readFullSourceRequired, false);
assert.equal(slice.agentCommand.recommendedNextCommands[0].purpose, "graph-impact");
assert.equal(slice.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(slice.agentCommand.recommendedNextCommands[0].when, "before patching a sliced edit target");
assert.equal(slice.agentCommand.recommendedNextCommands[0].inputField, "center.id");
assert.deepEqual(slice.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "impact", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "--node", "<center.id>", "<input>"]);
assert(slice.agentCommand.recommendedNextCommands[0].resultFields.includes("editGuards[]"));
assert(slice.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands"));
assert(slice.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.equal(slice.agentCommand.recommendedNextCommands[1].purpose, "graph-patch");
assert.equal(slice.agentCommand.recommendedNextCommands[1].required, false);
assert.equal(slice.agentCommand.recommendedNextCommands[1].when, "after-reviewing-neighborhood-and-impact");
assert.equal(slice.agentCommand.recommendedNextCommands[1].inputField, "center.id");
assert.deepEqual(slice.agentCommand.recommendedNextCommands[1].argv, ["zero", "graph", "patch", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>", "<patch-file>"]);
assert.equal(slice.agentCommand.recommendedNextCommands[1].resultContract, "agentTransaction");
assert(slice.agentCommand.recommendedNextCommands[1].resultFields.includes("agentTransaction.proofLedger"));
assert(slice.agentCommand.recommendedNextCommands[1].resultFields.includes("agentTransaction.rollback.actions[]"));
assert(slice.agentCommand.recommendedNextCommands[1].resultFields.includes("operations[].retryCommands"));
assert(slice.nodes.some((node) => node.kind === "Param" && node.name === "world"));
assert(slice.edges.every((edge) => edge.from === main.id || edge.to === main.id));
assert(slice.nodes.length < graph.nodes.length);
assert.deepEqual((await runRequiredCommands(slice.agentCommand.verificationCommands)).map((item) => item.purpose), ["graph-slice"]);

const targetFind = await zeroJson(["graph", "find", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--symbol", "main", "examples/hello.0"]);
const targetMain = targetFind.matches.find((node) => node.kind === "Function" && node.name === "main");
assert(targetMain);
const targetSlice = await zeroJson(["graph", "slice", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--node", targetMain.id, "examples/hello.0"]);
assert.deepEqual(targetSlice.agentCommand.command.argv, ["zero", "graph", "slice", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--node", targetMain.id, "examples/hello.0"]);
assert.deepEqual(targetSlice.agentCommand.command.argv, targetSlice.agentCommand.verificationCommands[0].argv);
assert.deepEqual((await runRequiredCommands(targetSlice.agentCommand.verificationCommands)).map((item) => item.purpose), ["graph-slice"]);

const missingSlice = await zeroJsonAllowFailure(["graph", "slice", "--json", "--target", "linux-musl-x64", "--profile", "dev", "--node", "stale-node-id", "examples/hello.0"]);
assert.equal(missingSlice.ok, false);
assert.equal(missingSlice.agentCommand.kind, "agent-command-failure-contract");
const missingSliceInspect = missingSlice.agentCommand.recommendedNextCommands.find((item) => item.purpose === "graph-inspect");
assert(missingSliceInspect);
assert.equal(missingSliceInspect.required, false);
assert.equal(missingSliceInspect.inputField, "diagnostics[].actual");
assert.deepEqual(missingSliceInspect.argv, ["zero", "graph", "inspect", "--json", "--target", "linux-musl-x64", "--profile", "dev", "examples/hello.0"]);
assert(missingSliceInspect.resultFields.includes("programGraph.nodes[].id"));
assert(missingSliceInspect.resultFields.includes("agentCommand.verificationCommands"));

const commandAudit = {
  replayable: [slice, targetSlice].every((item) =>
    JSON.stringify(item.agentCommand.command.argv) === JSON.stringify(item.agentCommand.verificationCommands[0].argv)),
  targetProfilePreserved: targetSlice.agentCommand.command.argv.includes("--target") &&
    targetSlice.agentCommand.command.argv.includes("linux-musl-x64") &&
    targetSlice.agentCommand.command.argv.includes("--profile") &&
    targetSlice.agentCommand.command.argv.includes("dev"),
  graphImpactFollowupStructured: [slice, targetSlice].every((item) =>
    item.agentCommand.recommendedNextCommands[0].purpose === "graph-impact" &&
    item.agentCommand.recommendedNextCommands[0].inputField === "center.id" &&
    item.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    item.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    item.agentCommand.recommendedNextCommands[0].argv.includes("<center.id>") &&
    item.agentCommand.recommendedNextCommands[0].resultFields.includes("editGuards[]") &&
    item.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands")),
  graphPatchFollowupStructured: [slice, targetSlice].every((item) =>
    item.agentCommand.recommendedNextCommands[1].purpose === "graph-patch" &&
    item.agentCommand.recommendedNextCommands[1].inputField === "center.id" &&
    item.agentCommand.recommendedNextCommands[1].resultContract === "agentTransaction" &&
    item.agentCommand.recommendedNextCommands[1].resultFields.includes("operations[].retryCommands")),
  graphPatchFollowupTargetProfilePreserved: [slice, targetSlice].every((item) =>
    item.agentCommand.recommendedNextCommands[1].argv.includes("--target") &&
    item.agentCommand.recommendedNextCommands[1].argv.includes("<same-target>") &&
    item.agentCommand.recommendedNextCommands[1].argv.includes("--profile") &&
    item.agentCommand.recommendedNextCommands[1].argv.includes("<same-profile>")),
  missingNodeInspectFollowupStructured: missingSliceInspect.purpose === "graph-inspect" &&
    missingSliceInspect.argv.includes("linux-musl-x64") &&
    missingSliceInspect.argv.includes("dev") &&
    missingSliceInspect.resultFields.includes("programGraph.nodes[].nodeHash"),
  readPolicyAuditable: [slice, targetSlice].every((item) =>
    item.agentCommand.readPolicy.name === "semantic-graph-query-read" &&
    item.agentCommand.readPolicy.writesSource === false &&
    item.agentCommand.readPolicy.writesArtifacts === false &&
    item.agentCommand.readPolicy.fullSourceRequired === false &&
    item.agentCommand.readPolicy.tokenStrategyField === "tokenStrategy" &&
    item.agentCommand.auditFields.includes("agentCommand.readPolicy")),
  commandKinds: [...new Set([slice.agentCommand.kind, targetSlice.agentCommand.kind])].sort(),
};
assert.equal(commandAudit.replayable, true);
assert.equal(commandAudit.targetProfilePreserved, true);
assert.equal(commandAudit.graphImpactFollowupStructured, true);
assert.equal(commandAudit.graphPatchFollowupStructured, true);
assert.equal(commandAudit.graphPatchFollowupTargetProfilePreserved, true);
assert.equal(commandAudit.missingNodeInspectFollowupStructured, true);
assert.equal(commandAudit.readPolicyAuditable, true);
assert.deepEqual(commandAudit.commandKinds, ["agent-graph-slice-command-contract"]);

const literal = graph.nodes.find((node) => node.kind === "Literal" && node.value === "hello from zero\n");
assert(literal);
assert(graph.edges.some((edge) => edge.to === literal.id && edge.kind === "arg"));
assert(graph.graphHash.startsWith("graph:"));

const { stdout: graphStdout } = await execFileAsync(zero, ["graph", "--json", "examples/memory-package"], { maxBuffer: 16 * 1024 * 1024 });
const graphJson = JSON.parse(graphStdout) as GraphJson;
assert.equal(graphJson.agentQuery.lookupSurfaces.capability.indexField, "agentLookupIndexes.capabilities");
assert.equal(graphJson.agentQuery.lookupSurfaces.nodeNeighborhood.command, "zero graph slice --json --node <node-id> <input>");
assert.deepEqual(graphJson.agentQuery.repairLoop.commandContracts.patch.argv, ["zero", "graph", "patch", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>", "<patch-file>"]);
assert(graphJson.agentQuery.repairLoop.stateFields.includes("agentTransaction.patchedGraphHash"));
assert(graphJson.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger"));
const memoryCapability = graphJson.agentLookupIndexes.capabilities.find((item) => item.name === "memory");
assert(memoryCapability);
assert(memoryCapability.functions.includes("prepare"));
assert(memoryCapability.functions.includes("messageOk"));
assert(memoryCapability.stdlibHelpers.includes("std.mem.copy"));
const worldCapability = graphJson.agentLookupIndexes.capabilities.find((item) => item.name === "world");
assert(worldCapability);
assert.deepEqual(worldCapability.functions, ["main"]);

const proofAudit = {
  required: executedProofs.length,
  passed: executedProofs.filter((item) => item.ok).length,
  failed: executedProofs.filter((item) => !item.ok).length,
  purposes: [...new Set(executedProofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(executedProofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
assert.deepEqual(proofAudit.passedPurposes, ["graph-slice"]);
console.log(JSON.stringify({ ok: true, kind: "agent-graph-slice-smoke", proofAudit, commandAudit, sliceAudit: { fullNodes: graph.nodes.length, sliceNodes: slice.nodes.length, sliceEdges: slice.edges.length } }, null, 2));
console.log("agent graph slice smoke ok");
