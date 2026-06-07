import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent protocol eval requires ZERO_BIN or ${defaultZeroBin}`);
const stripTypeArgs = ["--experimental-strip-types", "--disable-warning=ExperimentalWarning"];

function jsonObjects(stdout: string) {
  const out = [];
  let start = -1;
  let depth = 0;
  let inString = false;
  let escaped = false;
  for (let i = 0; i < stdout.length; i++) {
    const ch = stdout[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch === "\\") escaped = true;
      else if (ch === "\"") inString = false;
      continue;
    }
    if (ch === "\"") inString = true;
    else if (ch === "{") {
      if (depth === 0) start = i;
      depth++;
    } else if (ch === "}" && depth > 0) {
      depth--;
      if (depth === 0 && start >= 0) out.push(JSON.parse(stdout.slice(start, i + 1)));
    }
  }
  return out;
}

async function nodeJson(script: string, kind: string) {
  const { stdout } = await execFileAsync(process.execPath, [...stripTypeArgs, script], { env: { ...process.env, ZERO_BIN: zero }, maxBuffer: 16 * 1024 * 1024 });
  const report = jsonObjects(stdout).find((item) => item.kind === kind);
  assert(report, `${script} should print a ${kind} JSON report`);
  return report;
}

async function zeroJson(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  return JSON.parse(result.stdout);
}

const protocol = await nodeJson("scripts/agent-protocol-smoke.mts", "agent-protocol-smoke");
const protocolManifest = await nodeJson("scripts/agent-protocol-manifest-smoke.mts", "agent-protocol-manifest-smoke");
const proofReceipt = await nodeJson("scripts/agent-proof-receipt-smoke.mts", "agent-proof-receipt-smoke");
const authoringReplay = await nodeJson("scripts/agent-authoring-replay-smoke.mts", "agent-authoring-replay-smoke");
const autoRepairBuildReplay = await nodeJson("scripts/agent-auto-repair-build-replay-smoke.mts", "agent-auto-repair-build-replay-smoke");
const rollbackReplay = await nodeJson("scripts/agent-rollback-replay-smoke.mts", "agent-rollback-replay-smoke");
const graphLoop = await nodeJson("scripts/agent-graph-repair-loop-smoke.mts", "agent-graph-repair-loop-smoke");
const graphPatch = await nodeJson("scripts/agent-graph-patch-transaction-smoke.mts", "agent-graph-patch-transaction-smoke");
const diagnosticEntrypoint = await nodeJson("scripts/agent-diagnostic-graph-lookup-smoke.mts", "agent-diagnostic-entrypoint-smoke");
const buildCommand = await nodeJson("scripts/agent-build-command-smoke.mts", "agent-build-command-smoke");
const sizeCommand = await nodeJson("scripts/agent-size-command-smoke.mts", "agent-size-command-smoke");
const shipCommand = await nodeJson("scripts/agent-ship-command-smoke.mts", "agent-ship-command-smoke");
const testCommand = await nodeJson("scripts/agent-test-command-smoke.mts", "agent-test-command-smoke");
const targetsCommand = await nodeJson("scripts/agent-targets-command-smoke.mts", "agent-targets-command-smoke");
const cleanCommand = await nodeJson("scripts/agent-clean-command-smoke.mts", "agent-clean-command-smoke");
const newCommand = await nodeJson("scripts/agent-new-command-smoke.mts", "agent-new-command-smoke");
const sourceCommand = await nodeJson("scripts/agent-source-command-smoke.mts", "agent-source-command-smoke");
const graphInspectCommand = await nodeJson("scripts/agent-graph-inspect-command-smoke.mts", "agent-graph-inspect-command-smoke");
const graphDumpCommand = await nodeJson("scripts/agent-graph-dump-command-smoke.mts", "agent-graph-dump-command-smoke");
const graphFindCommand = await nodeJson("scripts/agent-graph-find-smoke.mts", "agent-graph-find-smoke");
const graphImpactCommand = await nodeJson("scripts/agent-graph-impact-smoke.mts", "agent-graph-impact-smoke");
const graphSliceCommand = await nodeJson("scripts/agent-graph-slice-smoke.mts", "agent-graph-slice-smoke");
const graphCompareCommand = await nodeJson("scripts/agent-graph-compare-command-smoke.mts", "agent-graph-compare-command-smoke");
const graphArtifactCommand = await nodeJson("scripts/agent-graph-artifact-command-smoke.mts", "agent-graph-artifact-command-smoke");
const explainCommand = await nodeJson("scripts/agent-explain-command-smoke.mts", "agent-explain-command-smoke");
const runtimeAuditCommand = await nodeJson("scripts/agent-runtime-audit-command-smoke.mts", "agent-runtime-audit-command-smoke");
const devDocCommand = await nodeJson("scripts/agent-dev-doc-command-smoke.mts", "agent-dev-doc-command-smoke");
const doctorCommand = await nodeJson("scripts/agent-doctor-command-smoke.mts", "agent-doctor-command-smoke");
const skillsCommand = await nodeJson("scripts/agent-skills-command-smoke.mts", "agent-skills-command-smoke");
const versionCommand = await nodeJson("scripts/agent-version-command-smoke.mts", "agent-version-command-smoke");
const graphInspect = await zeroJson(["graph", "inspect", "--json", "examples/hello.0"]);
const tokenSample = "conformance/native/pass/std-codec-json-url.0";
const packageInspect = await zeroJson(["graph", "inspect", "--json", tokenSample]);

function utf8Bytes(value: string) {
  return Buffer.byteLength(value, "utf8");
}

function oneHopSlice(inspect, symbolName: string) {
  const nodes = inspect.programGraph.nodes;
  const edges = inspect.programGraph.edges;
  const root = nodes.find((node) => node.kind === "Function" && node.name === symbolName);
  assert(root, `expected function ${symbolName}`);
  const ids = new Set([root.id]);
  for (const edge of edges) {
    if (edge.from === root.id) ids.add(edge.to);
    if (edge.to === root.id) ids.add(edge.from);
  }
  return {
    graphHash: inspect.programGraph.graphHash,
    root: root.id,
    nodes: nodes
      .filter((node) => ids.has(node.id))
      .map((node) => ({
        id: node.id,
        kind: node.kind,
        name: node.name,
        type: node.type,
        symbolId: node.symbolId,
        nodeHash: node.nodeHash,
        path: node.path,
        line: node.line,
        column: node.column,
      })),
    edges: edges.filter((edge) => ids.has(edge.from) && ids.has(edge.to)),
  };
}

const packageSourceBytes = packageInspect.sourceFiles.reduce((sum: number, path: string) => sum + utf8Bytes(readFileSync(path, "utf8")), 0);
const packageInspectBytes = utf8Bytes(JSON.stringify(packageInspect));
const tokenSlice = oneHopSlice(packageInspect, "__zero_std_codec_hex_decoded_len");
const tokenSliceBytes = utf8Bytes(JSON.stringify(tokenSlice));

assert.equal(protocol.ok, true);
assert.equal(graphLoop.ok, true);
assert.equal(graphPatch.ok, true);
assert.equal(diagnosticEntrypoint.ok, true);
assert.equal(buildCommand.ok, true);
assert.equal(targetsCommand.ok, true);
assert.equal(cleanCommand.ok, true);
assert.equal(newCommand.ok, true);
assert.equal(sourceCommand.ok, true);
assert.equal(graphInspectCommand.ok, true);
assert.equal(graphDumpCommand.ok, true);
assert.equal(graphFindCommand.ok, true);
assert.equal(graphImpactCommand.ok, true);
assert.equal(graphSliceCommand.ok, true);
assert.equal(graphCompareCommand.ok, true);
assert.equal(graphArtifactCommand.ok, true);
assert.equal(explainCommand.ok, true);
assert.equal(runtimeAuditCommand.ok, true);
assert.equal(devDocCommand.ok, true);
assert.equal(doctorCommand.ok, true);
assert.equal(skillsCommand.ok, true);
assert.equal(graphInspect.agentQuery.tokenStrategy.readFullSourceRequired, false);
assert.equal(graphInspect.agentQuery.tokenStrategy.preferNodeNeighborhood, true);
assert.equal(packageInspect.agentQuery.tokenStrategy.readFullSourceRequired, false);
const graphInspectSliceFollowupStructured = graphInspect.agentCommand?.recommendedNextCommands?.[0]?.purpose === "graph-slice" &&
  graphInspect.agentCommand.recommendedNextCommands[0].inputField === "programGraph.nodes[].id" &&
  graphInspect.agentCommand.recommendedNextCommands[0].argv?.includes("<same-target>") &&
  graphInspect.agentCommand.recommendedNextCommands[0].argv?.includes("<same-profile>") &&
  graphInspect.agentCommand.recommendedNextCommands[0].resultFields?.includes("center.nodeHash") &&
  graphInspect.agentCommand.recommendedNextCommands[0].resultFields?.includes("agentCommand.verificationCommands");

const cases = [
  {
    id: "agent-protocol-manifest",
    passed: protocolManifest.entrypoints >= 11 &&
      protocolManifest.semanticOperations >= 17 &&
      protocolManifest.proofPurposes >= 35 &&
      protocolManifest.proofResultFieldsCatalogAuditable === true &&
      protocolManifest.proofReceiptPolicyAuditable === true &&
      protocolManifest.objectiveContractAuditable === true &&
      protocolManifest.commandAudit?.replayable === true &&
      protocolManifest.commandAudit?.readPolicyAuditable === true &&
      protocolManifest.commandAudit?.localGateCommandAuditable === true &&
      protocolManifest.proofAudit?.passedPurposes?.includes("protocol-read") &&
      protocolManifest.permissionBoundaryStructured === true &&
      protocolManifest.localGateSummaryFields >= 148 &&
      protocolManifest.localGates.includes("agent-contracts"),
    metrics: protocolManifest,
  },
  {
    id: "agent-authoring-replay",
    passed: authoringReplay.ok === true &&
      authoringReplay.audit?.graphFindTokenBounded === true &&
      authoringReplay.audit?.graphImpactTokenBounded === true &&
      authoringReplay.audit?.graphSliceTokenBounded === true &&
      authoringReplay.audit?.checkedGraphEdit === true &&
      authoringReplay.audit?.rollbackAvailable === true &&
      authoringReplay.audit?.compilerVerificationCommandsReplayed === true &&
      authoringReplay.audit?.sourceRewritten === true &&
      authoringReplay.audit?.behaviorVerified === true &&
      authoringReplay.audit?.artifactVerified === true &&
      authoringReplay.proofAudit?.required >= 7,
    metrics: authoringReplay,
  },
  {
    id: "agent-auto-repair-build-replay",
    passed: autoRepairBuildReplay.ok === true &&
      autoRepairBuildReplay.audit?.diagnosticEntrypointStructured === true &&
      autoRepairBuildReplay.audit?.repairPlanProofLedger === true &&
      autoRepairBuildReplay.audit?.graphPatchCandidateStructured === true &&
      autoRepairBuildReplay.audit?.checkedGraphPatchApplied === true &&
      autoRepairBuildReplay.audit?.rollbackAvailable === true &&
      autoRepairBuildReplay.audit?.sourceRewritten === true &&
      autoRepairBuildReplay.audit?.postRepairCheckVerified === true &&
      autoRepairBuildReplay.audit?.artifactVerified === true &&
      autoRepairBuildReplay.audit?.compilerVerificationCommandsReplayed === true &&
      autoRepairBuildReplay.proofAudit?.required >= 5,
    metrics: autoRepairBuildReplay,
  },
  {
    id: "agent-rollback-replay",
    passed: rollbackReplay.ok === true &&
      rollbackReplay.audit?.rollbackActionStructured === true &&
      rollbackReplay.audit?.originalStateCaptured === true &&
      rollbackReplay.audit?.sourceMutationObserved === true &&
      rollbackReplay.audit?.restoreApplied === true &&
      rollbackReplay.audit?.rollbackVerificationCommandsReplayed === true &&
      rollbackReplay.audit?.sourceCheckRestored === true &&
      rollbackReplay.audit?.graphCheckHashRestored === true &&
      rollbackReplay.audit?.semanticGraphHashRestored === true &&
      rollbackReplay.audit?.semanticIdentityRestored === true &&
      rollbackReplay.proofAudit?.required >= 2,
    metrics: rollbackReplay,
  },
  {
    id: "proof-receipt-replay-operational",
    passed: proofReceipt.declaredFieldsMatched === true &&
      proofReceipt.argvBound === true &&
      proofReceipt.timingRecorded === true &&
      proofReceipt.sourceWorkflowReceipts === true &&
      proofReceipt.receiptPurposes.includes("protocol-read") &&
      protocol.proofReceiptAudit?.declaredFieldsMatched === true,
    metrics: proofReceipt,
  },
  {
    id: "diagnostic-repair-entrypoint",
    passed: diagnosticEntrypoint.entrypointAudit.required >= 4 &&
      diagnosticEntrypoint.entrypointAudit.optional >= 8 &&
      diagnosticEntrypoint.entrypointAudit.purposes.includes("repair-plan") &&
      diagnosticEntrypoint.entrypointAudit.purposes.includes("repair-patch") &&
      diagnosticEntrypoint.entrypointAudit.purposes.includes("repair-apply") &&
      diagnosticEntrypoint.entrypointAudit.resultFields.includes("fixes[].graphPatchCandidates[].candidateContract") &&
      diagnosticEntrypoint.entrypointAudit.resultFields.includes("fixes[].graphPatchCandidates[].patch.text") &&
      diagnosticEntrypoint.entrypointAudit.resultFields.includes("fixes[].graphPatchCandidates[].patch.operations") &&
      diagnosticEntrypoint.entrypointAudit.resultFields.includes("fixes[].graphPatchCandidates[].verificationCommands") &&
      diagnosticEntrypoint.entrypointAudit.resultFields.includes("agentTransaction.proofLedger") &&
      diagnosticEntrypoint.entrypointAudit.rollbackActionFields === true &&
      diagnosticEntrypoint.entrypointAudit.resultFields.includes("diagnostics[].graphLookup") &&
      diagnosticEntrypoint.graphLookupAudit.commands >= 8 &&
      diagnosticEntrypoint.graphLookupAudit.required >= 8 &&
      diagnosticEntrypoint.graphLookupAudit.purposes.includes("graph-inspect") &&
      diagnosticEntrypoint.graphLookupAudit.purposes.includes("graph-check") &&
      diagnosticEntrypoint.graphLookupAudit.targetProfilePreserved === true &&
      diagnosticEntrypoint.graphLookupAudit.repairFallback === true &&
      diagnosticEntrypoint.graphLookupAudit.checkReadPolicyAuditable === true &&
      diagnosticEntrypoint.graphLookupAudit.checkFailureFollowupsStructured === true &&
      diagnosticEntrypoint.graphLookupAudit.graphCheckFailureFollowupStructured === true &&
      diagnosticEntrypoint.graphLookupAudit.spanFields.includes("programGraph.nodes[].path") &&
      diagnosticEntrypoint.graphLookupAudit.spanFields.includes("programGraph.nodes[].line") &&
      diagnosticEntrypoint.graphLookupAudit.spanFields.includes("programGraph.nodes[].column") &&
      protocol.entrypointAudit.resultFields.includes("fixes[].graphPatchCandidates[].candidateContract") &&
      protocol.entrypointAudit.resultFields.includes("fixes[].graphPatchCandidates[].patch.text") &&
      protocol.entrypointAudit.resultFields.includes("fixes[].graphPatchCandidates[].patch.operations") &&
      protocol.entrypointAudit.resultFields.includes("fixes[].graphPatchCandidates[].verificationCommands") &&
      protocol.entrypointAudit.resultFields.includes("agentTransaction.proofLedger") &&
      protocol.entrypointAudit.resultFields.includes("agentTransaction.rollback.actions[]") &&
      protocol.entrypointAudit.resultFields.includes("diagnostics[].graphLookup"),
    metrics: {
      entrypointAudit: diagnosticEntrypoint.entrypointAudit,
      graphLookupAudit: diagnosticEntrypoint.graphLookupAudit,
    },
  },
  {
    id: "graph-repair-loop",
    passed: graphLoop.audit.some((item) => item.state === "fixed") &&
      graphLoop.audit.filter((item) => item.repairId).length === 2,
    metrics: { steps: graphLoop.steps, proofAudit: graphLoop.proofAudit },
  },
  {
    id: "repair-plan-semantic-candidate-coverage",
    passed: protocol.repairPlanAudit.candidateOps.includes("set") &&
      protocol.repairPlanAudit.candidateOps.includes("addFunction") &&
      protocol.repairPlanAudit.candidateOps.includes("replaceCallee") &&
      protocol.repairPlanAudit.candidateOps.includes("removeFunction") &&
      protocol.repairPlanAudit.candidateOps.includes("changeReturnType") &&
      protocol.repairPlanAudit.candidateOps.includes("changeParamType") &&
      protocol.repairPlanAudit.candidateOps.includes("changeFieldType") &&
      protocol.repairPlanAudit.candidateOps.includes("changeLocalType") &&
      protocol.repairPlanAudit.repairIds.includes("remove-duplicate-declaration") &&
      protocol.repairPlanAudit.candidateContracts >= 10 &&
      protocol.repairPlanAudit.completeCandidateContracts === protocol.repairPlanAudit.candidateContracts &&
      protocol.repairPlanAudit.completeCandidatePatchText === protocol.repairPlanAudit.candidateContracts &&
      protocol.repairPlanAudit.evidenceBackedCandidateContracts >= 10 &&
      protocol.repairPlanAudit.candidateTargetProfilePreserved === true &&
      protocol.repairPlanAudit.candidateVerificationPurposes.includes("source-check") &&
      protocol.repairPlanAudit.candidateVerificationPurposes.includes("graph-check"),
    metrics: protocol.repairPlanAudit,
  },
  {
    id: "checked-semantic-edit-coverage",
    passed: graphPatch.semanticEditAudit.coveredOperations.includes("replaceCallee") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("renameSymbol") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("renameParam") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("renameLocal") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("renameField") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("addImport") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("removeImport") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("replaceImport") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("renameImportAlias") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("addFunction") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("addParam") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("removeParam") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("removeFunction") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("changeReturnType") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("changeParamType") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("changeFieldType") &&
      graphPatch.semanticEditAudit.coveredOperations.includes("changeLocalType") &&
      graphPatch.semanticEditAudit.categories.includes("call-graph") &&
      graphPatch.semanticEditAudit.categories.includes("imports") &&
      graphPatch.semanticEditAudit.categories.includes("signature") &&
      graphPatch.semanticEditAudit.categories.includes("symbol-rename") &&
      graphPatch.semanticEditAudit.categories.includes("type-propagation") &&
      graphPatch.semanticEditAudit.checkedPreconditions.includes("graph-hash") &&
      graphPatch.semanticEditAudit.checkedPreconditions.includes("expect-value") &&
      graphPatch.semanticEditAudit.checkedPreconditions.includes("name-conflict") &&
      graphPatch.semanticEditAudit.operationSurfaceBacked === true &&
      graphPatch.semanticEditAudit.sourceBackedTransactions >= 10 &&
      graphPatch.semanticEditAudit.conflictRejections >= 8 &&
      graphPatch.semanticEditAudit.checkedSourceRewrite === true,
    metrics: graphPatch.semanticEditAudit,
  },
  {
    id: "operation-level-retry-contract",
    passed: graphInspect.agentQuery.repairLoop.operationRetry === "operations[].retryCommands[]" &&
      protocolManifest.operationRetryContract === true &&
      graphInspect.agentQuery.repairLoop.auditFields.includes("operations[].retryCommands") &&
      graphInspect.agentQuery.checkedEditSurface.transactionContract.resultFields.includes("operations[].retryCommands") &&
      graphInspect.agentQuery.checkedEditSurface.transactionContract.resultFields.includes("operations[].retryCommands[].purpose") &&
      graphInspect.agentQuery.checkedEditSurface.transactionContract.failureClasses.some((item) => item.code === "GPH005" &&
        item.class === "precondition-failed" &&
        item.phase === "patch" &&
        item.retryable === true &&
        item.retryCommand === "operations[].retryCommands[] graph-impact") &&
      graphPatch.proofAudit.purposes.includes("graph-impact") &&
      graphPatch.proofAudit.passedPurposes.includes("graph-impact"),
    metrics: {
      operationRetry: graphInspect.agentQuery.repairLoop.operationRetry,
      manifestOperationRetryContract: protocolManifest.operationRetryContract,
      auditFields: graphInspect.agentQuery.repairLoop.auditFields.filter((field: string) => field.includes("retryCommands")),
      resultFields: graphInspect.agentQuery.checkedEditSurface.transactionContract.resultFields.filter((field: string) => field.includes("operations[].retryCommands")),
      gph005RetryCommand: graphInspect.agentQuery.checkedEditSurface.transactionContract.failureClasses.find((item) => item.code === "GPH005")?.retryCommand ?? null,
      proofPurposes: graphPatch.proofAudit.purposes.filter((purpose: string) => purpose === "graph-impact"),
      passedPurposes: graphPatch.proofAudit.passedPurposes.filter((purpose: string) => purpose === "graph-impact"),
    },
  },
  {
    id: "checked-graph-edit-entrypoint-audit-fields",
    passed: protocolManifest.checkedGraphEditRetryFields.includes("agentTransaction.failure.retryCommands") &&
      protocolManifest.checkedGraphEditEvidenceFields.includes("evidenceBindings[]") &&
      protocolManifest.checkedGraphEditEvidenceFields.includes("evidenceBindings[].sourceFields") &&
      protocolManifest.checkedGraphEditRetryFields.includes("operations[].retryCommands") &&
      protocolManifest.checkedGraphEditRetryFields.includes("operations[].retryCommands[].purpose") &&
      protocolManifest.checkedGraphEditSaveProofFields.includes("saved.graphHash") &&
      protocolManifest.checkedGraphEditSaveProofFields.includes("saveProof.semanticStable") &&
      protocolManifest.checkedGraphEditSaveProofFields.includes("saveProof.comparedGraphHash") &&
      protocolManifest.checkedGraphEditResultFields >= 11,
    metrics: {
      resultFields: protocolManifest.checkedGraphEditResultFields,
      retryFields: protocolManifest.checkedGraphEditRetryFields,
      evidenceFields: protocolManifest.checkedGraphEditEvidenceFields,
      saveProofFields: protocolManifest.checkedGraphEditSaveProofFields,
    },
  },
  {
    id: "graph-patch-evidence-bindings",
    passed: graphPatch.evidenceAudit?.resultFieldsDeclared === true &&
      graphPatch.evidenceAudit?.bindingsMatchOperations === true &&
      graphPatch.evidenceAudit?.compilerFactSourcesDeclared === true &&
      graphPatch.evidenceAudit?.preconditionActualBound === true &&
      graphInspect.agentQuery.checkedEditSurface.transactionContract.resultFields.includes("evidenceBindings[]") &&
      graphInspect.agentQuery.checkedEditSurface.transactionContract.resultFields.includes("evidenceBindings[].sourceFields") &&
      protocolManifest.checkedGraphEditEvidenceFields.includes("evidenceBindings[]"),
    metrics: {
      graphPatchEvidenceAudit: graphPatch.evidenceAudit,
      contractFields: graphInspect.agentQuery.checkedEditSurface.transactionContract.resultFields.filter((field: string) => field.startsWith("evidenceBindings")),
      manifestFields: protocolManifest.checkedGraphEditEvidenceFields,
    },
  },
  {
    id: "graph-patch-verification-result-fields",
    passed: graphPatch.verificationResultFieldAudit?.resultFieldsDeclared === true &&
      graphPatch.verificationResultFieldAudit?.scheduledCommandsDeclared === true &&
      graphPatch.verificationResultFieldAudit?.rollbackCommandsDeclared === true &&
      graphPatch.verificationResultFieldAudit?.purposeSpecificFields === true &&
      graphInspect.agentQuery.checkedEditSurface.transactionContract.resultFields.includes("agentTransaction.verificationCommands[].resultFields") &&
      graphInspect.agentQuery.checkedEditSurface.transactionContract.resultFields.includes("agentTransaction.rollback.verificationCommands[].resultFields") &&
      graphInspect.agentQuery.checkedEditSurface.transactionContract.rollback.sourceVerificationCommands.every((command: any) => Array.isArray(command.resultFields)) &&
      graphInspect.agentQuery.checkedEditSurface.transactionContract.rollback.artifactVerificationCommands.every((command: any) => Array.isArray(command.resultFields)) &&
      protocolManifest.checkedGraphEditVerificationResultFields.includes("agentTransaction.verificationCommands[].resultFields"),
    metrics: {
      verificationResultFieldAudit: graphPatch.verificationResultFieldAudit,
      contractFields: graphInspect.agentQuery.checkedEditSurface.transactionContract.resultFields.filter((field: string) => field.includes("verificationCommands[].resultFields")),
      manifestFields: protocolManifest.checkedGraphEditVerificationResultFields,
    },
  },
  {
    id: "repair-loop-proof-ledger-audit-fields",
    passed: graphInspect.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger") &&
      graphInspect.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger.phases[].status") &&
      protocolManifest.repairLoopProofLedgerAuditable === true &&
      protocol.proofLedgerAudit?.phaseOrderStructured === true &&
      graphPatch.proofLedgerAudit?.phaseOrderStructured === true,
    metrics: {
      auditFields: graphInspect.agentQuery.repairLoop.auditFields.filter((field: string) => field.includes("proofLedger")),
      manifestRepairLoopProofLedgerAuditable: protocolManifest.repairLoopProofLedgerAuditable,
      repairLedgerStructured: protocol.proofLedgerAudit?.phaseOrderStructured === true,
      graphPatchLedgerStructured: graphPatch.proofLedgerAudit?.phaseOrderStructured === true,
    },
  },
  {
    id: "repair-loop-target-profile-preserved",
    passed: Object.values(graphInspect.agentQuery.repairLoop.commandContracts).every((contract: any) =>
      contract.argv.includes("--target") &&
      contract.argv.includes("<same-target>") &&
      contract.argv.includes("--profile") &&
      contract.argv.includes("<same-profile>")) &&
      protocolManifest.repairLoopTargetProfilePreserved === true,
    metrics: {
      commandContracts: graphInspect.agentQuery.repairLoop.commandContracts,
      manifestRepairLoopTargetProfilePreserved: protocolManifest.repairLoopTargetProfilePreserved,
    },
  },
  {
    id: "transaction-failure-matrix",
    passed: graphPatch.failureAudit?.observedCodes?.includes("BLD002") &&
      ["GPH001", "GPH002", "GPH003", "GPH004", "GPH005"].every((code) => graphPatch.failureAudit.observedCodes.includes(code)) &&
      graphPatch.failureAudit.declaredCodes.includes("GPH006") &&
      graphPatch.failureAudit.nonRetryableCodes.includes("GPH006") &&
      graphPatch.failureAudit.retryPurposes.includes("graph-dump") &&
      graphPatch.failureAudit.retryPurposes.includes("graph-inspect") &&
      graphPatch.failureAudit.operationRetryPurposes.includes("graph-impact") &&
      graphPatch.failureAudit.targetProfileRetryPreserved === true &&
      graphPatch.failureAudit.inlinePatchCommandArgsPreserved === true &&
      graphPatch.failureAudit.noVerifyOnFailure === true &&
      ["source-unavailable", "unsupported-repair", "edit-not-found", "write-failed"].every((failureClass) =>
        protocol.failureAudit?.declaredClasses?.includes(failureClass)) &&
      ["source-unavailable", "unsupported-repair", "edit-not-found"].every((failureClass) =>
        protocol.failureAudit?.observedClasses?.includes(failureClass)) &&
      protocol.failureAudit?.retryPurposes?.includes("repair-plan") &&
      protocol.failureAudit?.retryPurposes?.includes("source-check") &&
      protocol.failureAudit?.targetProfileRetryPreserved === true &&
      protocol.failureAudit?.noVerifyOnFailure === true,
    metrics: {
      graphPatch: graphPatch.failureAudit,
      protocol: protocol.failureAudit,
    },
  },
  {
    id: "protocol-proof-coverage",
    passed: protocol.proofAudit.required >= 30 &&
      protocol.proofAudit.failed > 0 &&
      protocol.proofAudit.passedPurposes.includes("source-check") &&
      protocol.proofAudit.passedPurposes.includes("graph-check") &&
      protocol.proofAudit.passedPurposes.includes("artifact-validate"),
    metrics: protocol.proofAudit,
  },
  {
    id: "graph-patch-proof-ledger-structured",
    passed: graphPatch.proofLedgerAudit?.phaseOrderStructured === true &&
      graphPatch.proofLedgerAudit?.successLedgerEvidenceFields === true &&
      graphPatch.proofLedgerAudit?.verificationPurposesMatched === true &&
      graphPatch.proofLedgerAudit?.rollbackPurposesMatched === true &&
      graphPatch.proofLedgerAudit?.rollbackActionsStructured === true &&
      graphPatch.proofLedgerAudit?.failureLedgerClassified === true,
    metrics: graphPatch.proofLedgerAudit,
  },
  {
    id: "repair-transaction-proof-ledger-structured",
    passed: protocol.proofLedgerAudit?.phaseOrderStructured === true &&
      protocol.proofLedgerAudit?.resultFieldsDeclared === true &&
      protocol.proofLedgerAudit?.successLedgerEvidenceFields === true &&
      protocol.proofLedgerAudit?.verificationPurposesMatched === true &&
      protocol.proofLedgerAudit?.rollbackPurposesMatched === true &&
      protocol.proofLedgerAudit?.rollbackActionsStructured === true &&
      protocol.proofLedgerAudit?.failureLedgerClassified === true,
    metrics: protocol.proofLedgerAudit,
  },
  {
    id: "verifiable-build-command",
    passed: buildCommand.artifactBytes > 0 &&
      buildCommand.artifactHash?.algorithm === "fnv1a64" &&
      buildCommand.artifactHash?.value === buildCommand.repeatArtifactHash?.value &&
      buildCommand.proofAudit.required === 3 &&
      buildCommand.proofAudit.passedPurposes.includes("artifact-validate") &&
      buildCommand.proofAudit.passedPurposes.includes("source-check") &&
      buildCommand.proofAudit.passedPurposes.includes("size-analysis") &&
      buildCommand.graph?.artifactBytes > 0 &&
      buildCommand.graph?.artifactHash?.algorithm === "fnv1a64" &&
      buildCommand.graph?.artifactHash?.value === buildCommand.graph?.repeatArtifactHash?.value &&
      buildCommand.graph?.proofAudit?.required === 3 &&
      buildCommand.graph?.proofAudit?.passedPurposes.includes("artifact-validate") &&
      buildCommand.graph?.proofAudit?.passedPurposes.includes("graph-check") &&
      buildCommand.graph?.proofAudit?.passedPurposes.includes("graph-size") &&
      buildCommand.commandAudit?.replayable === true &&
      buildCommand.commandAudit?.artifactEvidenceMatched === true &&
      buildCommand.commandAudit?.artifactWritePolicyAuditable === true &&
      buildCommand.commandAudit?.artifactRollbackActionAuditable === true &&
      buildCommand.commandAudit?.buildTestFollowupStructured === true &&
      buildCommand.commandAudit?.buildFailureRecoveryStructured === true &&
      buildCommand.commandAudit?.graphBuildFailureRecoveryStructured === true &&
      buildCommand.commandAudit?.graphArtifactCommandRerouteStructured === true &&
      buildCommand.commandAudit?.graphBuildSizeFollowupStructured === true &&
      buildCommand.commandAudit?.graphBuildTestFollowupStructured === true &&
      buildCommand.commandAudit?.runJsonUnsupportedFollowupStructured === true &&
      buildCommand.commandAudit?.targetProfilePreserved === true &&
      buildCommand.safetyAudit?.sourceSafetyStructured === true &&
      buildCommand.safetyAudit?.graphSafetyStructured === true &&
      buildCommand.safetyAudit?.productionGateStructured === true &&
      buildCommand.safetyAudit?.blockingRisksAuditable === true &&
      buildCommand.safetyAudit?.uncheckedSurfacesAuditable === true &&
      buildCommand.cacheAudit?.source?.packageCacheAdvertised === true &&
      buildCommand.cacheAudit?.source?.packageCacheOk === true &&
      buildCommand.cacheAudit?.source?.compilerCachesOk === true &&
      buildCommand.cacheAudit?.source?.incrementalOk === true &&
      buildCommand.cacheAudit?.graph?.packageCacheAdvertised === true &&
      buildCommand.cacheAudit?.graph?.compilerCachesOk === true &&
      buildCommand.cacheAudit?.graph?.incrementalOk === true,
    metrics: {
      artifactBytes: buildCommand.artifactBytes,
      artifactHash: buildCommand.artifactHash,
      commandAudit: buildCommand.commandAudit,
      safetyAudit: buildCommand.safetyAudit,
      cacheAudit: buildCommand.cacheAudit,
      proofAudit: buildCommand.proofAudit,
      graph: buildCommand.graph,
    },
  },
  {
    id: "verifiable-size-command",
    passed: sizeCommand.artifactBytes > 0 &&
      sizeCommand.artifactHash?.algorithm === "fnv1a64" &&
      sizeCommand.artifactHash?.value === sizeCommand.repeatArtifactHash?.value &&
      sizeCommand.proofAudit.required === 2 &&
      sizeCommand.proofAudit.passedPurposes.includes("source-check") &&
      sizeCommand.proofAudit.passedPurposes.includes("size-analysis") &&
      sizeCommand.graph?.artifactBytes > 0 &&
      sizeCommand.graph?.artifactHash?.algorithm === "fnv1a64" &&
      sizeCommand.graph?.artifactHash?.value === sizeCommand.graph?.repeatArtifactHash?.value &&
      sizeCommand.graph?.proofAudit?.required === 2 &&
      sizeCommand.graph?.proofAudit?.passedPurposes.includes("graph-check") &&
      sizeCommand.graph?.proofAudit?.passedPurposes.includes("graph-size") &&
      sizeCommand.commandAudit?.replayable === true &&
      sizeCommand.commandAudit?.artifactEvidenceMatched === true &&
      sizeCommand.commandAudit?.artifactWritePolicyAuditable === true &&
      sizeCommand.commandAudit?.artifactRollbackActionAuditable === true &&
      sizeCommand.commandAudit?.sizeBuildFollowupStructured === true &&
      sizeCommand.commandAudit?.targetProfilePreserved === true &&
      sizeCommand.safetyAudit?.sourceSafetyStructured === true &&
      sizeCommand.safetyAudit?.graphSafetyStructured === true &&
      sizeCommand.safetyAudit?.productionGateStructured === true &&
      sizeCommand.safetyAudit?.blockingRisksAuditable === true &&
      sizeCommand.safetyAudit?.uncheckedSurfacesAuditable === true &&
      sizeCommand.cacheAudit?.source?.packageCacheAdvertised === true &&
      sizeCommand.cacheAudit?.source?.packageCacheOk === true &&
      sizeCommand.cacheAudit?.source?.compilerCachesOk === true &&
      sizeCommand.cacheAudit?.source?.incrementalOk === true &&
      sizeCommand.cacheAudit?.graph?.packageCacheAdvertised === true &&
      sizeCommand.cacheAudit?.graph?.compilerCachesOk === true &&
      sizeCommand.cacheAudit?.graph?.incrementalOk === true,
    metrics: {
      artifactBytes: sizeCommand.artifactBytes,
      artifactHash: sizeCommand.artifactHash,
      commandAudit: sizeCommand.commandAudit,
      safetyAudit: sizeCommand.safetyAudit,
      cacheAudit: sizeCommand.cacheAudit,
      proofAudit: sizeCommand.proofAudit,
      graph: sizeCommand.graph,
    },
  },
  {
    id: "verifiable-ship-command",
    passed: shipCommand.artifactBytes > 0 &&
      shipCommand.checksum?.algorithm === "fnv1a64" &&
      shipCommand.checksum?.value === shipCommand.repeatChecksum?.value &&
      shipCommand.artifactKinds.includes("binary") &&
      shipCommand.artifactKinds.includes("checksum") &&
      shipCommand.artifactKinds.includes("size-report") &&
      shipCommand.proofAudit.required === 3 &&
      shipCommand.proofAudit.passedPurposes.includes("artifact-validate") &&
      shipCommand.proofAudit.passedPurposes.includes("source-check") &&
      shipCommand.proofAudit.passedPurposes.includes("size-analysis") &&
      shipCommand.graph?.artifactBytes > 0 &&
      shipCommand.graph?.checksum?.algorithm === "fnv1a64" &&
      shipCommand.graph?.checksum?.value === shipCommand.graph?.repeatChecksum?.value &&
      shipCommand.graph?.proofAudit?.required === 3 &&
      shipCommand.graph?.proofAudit?.passedPurposes.includes("artifact-validate") &&
      shipCommand.graph?.proofAudit?.passedPurposes.includes("graph-check") &&
      shipCommand.graph?.proofAudit?.passedPurposes.includes("graph-size") &&
      shipCommand.commandAudit?.replayable === true &&
      shipCommand.commandAudit?.artifactEvidenceMatched === true &&
      shipCommand.commandAudit?.artifactWritePolicyAuditable === true &&
      shipCommand.commandAudit?.artifactRollbackActionAuditable === true &&
      shipCommand.commandAudit?.graphShipSizeFollowupStructured === true &&
      shipCommand.commandAudit?.graphShipTestFollowupStructured === true &&
      shipCommand.commandAudit?.sourceShipSizeFollowupStructured === true &&
      shipCommand.commandAudit?.sourceShipTestFollowupStructured === true &&
      shipCommand.commandAudit?.targetProfilePreserved === true &&
      shipCommand.safetyAudit?.sourceSafetyStructured === true &&
      shipCommand.safetyAudit?.graphSafetyStructured === true &&
      shipCommand.safetyAudit?.productionGateStructured === true &&
      shipCommand.safetyAudit?.blockingRisksAuditable === true &&
      shipCommand.safetyAudit?.uncheckedSurfacesAuditable === true &&
      shipCommand.cacheAudit?.source?.packageCacheAdvertised === true &&
      shipCommand.cacheAudit?.source?.packageCacheOk === true &&
      shipCommand.cacheAudit?.source?.compilerCachesOk === true &&
      shipCommand.cacheAudit?.source?.incrementalOk === true &&
      shipCommand.cacheAudit?.graph?.packageCacheAdvertised === true &&
      shipCommand.cacheAudit?.graph?.compilerCachesOk === true &&
      shipCommand.cacheAudit?.graph?.incrementalOk === true,
    metrics: {
      artifactBytes: shipCommand.artifactBytes,
      checksum: shipCommand.checksum,
      artifactKinds: shipCommand.artifactKinds,
      commandAudit: shipCommand.commandAudit,
      safetyAudit: shipCommand.safetyAudit,
      cacheAudit: shipCommand.cacheAudit,
      proofAudit: shipCommand.proofAudit,
      graph: shipCommand.graph,
    },
  },
  {
    id: "verifiable-test-command",
    passed: testCommand.pass?.status === "passed" &&
      testCommand.fail?.status === "unexpected-pass" &&
      testCommand.noTests?.selectedTests === 0 &&
      testCommand.fail?.unexpectedPasses === 1 &&
      testCommand.proofAudit.required === 6 &&
      testCommand.proofAudit.passedPurposes.includes("source-check") &&
      testCommand.proofAudit.passedPurposes.includes("test-run") &&
      testCommand.proofAudit.failedPurposes.includes("test-run") &&
      testCommand.graph?.pass?.status === "passed" &&
      testCommand.graph?.fail?.status === "unexpected-pass" &&
      testCommand.graph?.fail?.unexpectedPasses === 1 &&
      testCommand.graph?.proofAudit?.required === 4 &&
      testCommand.graph?.proofAudit?.passedPurposes.includes("graph-check") &&
      testCommand.graph?.proofAudit?.passedPurposes.includes("graph-test") &&
      testCommand.graph?.proofAudit?.failedPurposes.includes("graph-test") &&
      testCommand.commandAudit?.replayable === true &&
      testCommand.commandAudit?.failureEvidenceMatched === true &&
      testCommand.commandAudit?.failureInspectFollowupStructured === true &&
      testCommand.commandAudit?.noTestsDocAuditFollowupStructured === true &&
      testCommand.testEvidenceAudit?.sourceFailureFieldsStructured === true &&
      testCommand.testEvidenceAudit?.graphFailureFieldsStructured === true &&
      testCommand.testEvidenceAudit?.sourceDiscoveryStructured === true &&
      testCommand.testEvidenceAudit?.graphDiscoveryStructured === true &&
      testCommand.testEvidenceAudit?.graphHashBound === true &&
      testCommand.testEvidenceAudit?.expectedFailureRegressionAudited === true &&
      testCommand.testEvidenceAudit?.noTestsAudited === true,
    metrics: {
      pass: testCommand.pass,
      fail: testCommand.fail,
      commandAudit: testCommand.commandAudit,
      testEvidenceAudit: testCommand.testEvidenceAudit,
      proofAudit: testCommand.proofAudit,
      graph: testCommand.graph,
    },
  },
  {
    id: "verifiable-target-selection-command",
    passed: targetsCommand.targets > 0 &&
      targetsCommand.proofAudit.required === 1 &&
      targetsCommand.proofAudit.passedPurposes.includes("target-selection") &&
      targetsCommand.targetReadinessAudit?.commandReplayable === true &&
      targetsCommand.targetReadinessAudit?.selectionStructured === true &&
      targetsCommand.targetReadinessAudit?.capabilityFactsStructured === true &&
      targetsCommand.targetReadinessAudit?.toolchainFactsStructured === true &&
      targetsCommand.targetReadinessAudit?.doctorFollowupStructured === true &&
      targetsCommand.targetReadinessAudit?.hostTargetPresent === true &&
      targetsCommand.targetReadinessAudit?.jsonReplayable === true &&
      targetsCommand.invalidTargetRecoveryStructured === true &&
      targetsCommand.invalidTargetCommandArgsPreserved === true &&
      protocol.proofAudit.passedPurposes.includes("target-selection"),
    metrics: {
      host: targetsCommand.host,
      targets: targetsCommand.targets,
      targetReadinessAudit: targetsCommand.targetReadinessAudit,
      invalidTargetRecoveryStructured: targetsCommand.invalidTargetRecoveryStructured,
      invalidTargetCommandArgsPreserved: targetsCommand.invalidTargetCommandArgsPreserved,
      proofAudit: targetsCommand.proofAudit,
    },
  },
  {
    id: "verifiable-clean-command",
    passed: cleanCommand.commandAudit?.replayable === true &&
      cleanCommand.commandAudit?.destructiveCleanPolicyAuditable === true &&
      cleanCommand.commandAudit?.failureRetryStructured === true &&
      cleanCommand.proofAudit.required === 1 &&
      cleanCommand.proofAudit.passedPurposes.includes("clean-audit"),
    metrics: cleanCommand,
  },
  {
    id: "verifiable-new-command",
    passed: newCommand.commandAudit?.replayable === true &&
      newCommand.commandAudit?.projectWritePolicyAuditable === true &&
      newCommand.commandAudit?.projectFilesAuditable === true &&
      newCommand.commandAudit?.pathExistsFailureAuditable === true &&
      newCommand.commandAudit?.failureRetryStructured === true &&
      newCommand.proofAudit.required === 2 &&
      newCommand.proofAudit.passedPurposes.includes("source-check") &&
      newCommand.proofAudit.passedPurposes.includes("test-run"),
    metrics: newCommand,
  },
  {
    id: "verifiable-source-understanding-command",
    passed: sourceCommand.tokens > 0 &&
      sourceCommand.functions === 1 &&
      sourceCommand.formattedBytes > 0 &&
      sourceCommand.sourceUnderstandingAudit.tokenSpanFields.includes("tokens[].offset") &&
      sourceCommand.sourceUnderstandingAudit.tokenSpanFields.includes("tokens[].length") &&
      sourceCommand.sourceUnderstandingAudit.declarationFields.includes("functions[].name") &&
      sourceCommand.sourceUnderstandingAudit.declarationFields.includes("functions[].bodyKinds") &&
      sourceCommand.sourceUnderstandingAudit.tokenParseFollowupStructured === true &&
      sourceCommand.sourceUnderstandingAudit.parseGraphInspectFollowupStructured === true &&
      sourceCommand.sourceUnderstandingAudit.formatFailureFollowupStructured === true &&
      sourceCommand.sourceUnderstandingAudit.formattedStable === true &&
      sourceCommand.proofAudit.required === 3 &&
      sourceCommand.proofAudit.passedPurposes.includes("tokens") &&
      sourceCommand.proofAudit.passedPurposes.includes("parse") &&
      sourceCommand.proofAudit.passedPurposes.includes("format") &&
      protocol.proofAudit.passedPurposes.includes("tokens") &&
      protocol.proofAudit.passedPurposes.includes("parse") &&
      protocol.proofAudit.passedPurposes.includes("format"),
    metrics: {
      tokens: sourceCommand.tokens,
      functions: sourceCommand.functions,
      formattedBytes: sourceCommand.formattedBytes,
      sourceUnderstandingAudit: sourceCommand.sourceUnderstandingAudit,
      proofAudit: sourceCommand.proofAudit,
    },
  },
  {
    id: "verifiable-graph-inspect-command",
    passed: graphInspectCommand.graphHash?.startsWith("graph:") &&
      graphInspectCommand.nodes >= 1 &&
      graphInspectCommand.readFullSourceRequired === false &&
      graphInspectCommand.commandAudit?.lookupCommandContractsTargetProfilePreserved === true &&
      graphInspectCommand.proofAudit.required === 1 &&
      graphInspectCommand.proofAudit.passedPurposes.includes("graph-inspect") &&
      protocol.proofAudit.passedPurposes.includes("graph-inspect"),
    metrics: {
      graphHash: graphInspectCommand.graphHash,
      nodes: graphInspectCommand.nodes,
      edges: graphInspectCommand.edges,
      readFullSourceRequired: graphInspectCommand.readFullSourceRequired,
      commandAudit: graphInspectCommand.commandAudit,
      proofAudit: graphInspectCommand.proofAudit,
    },
  },
  {
    id: "verifiable-graph-dump-command",
    passed: graphDumpCommand.graphHash?.startsWith("graph:") &&
      graphDumpCommand.nodes >= 1 &&
      graphDumpCommand.validationOk === true &&
      graphDumpCommand.proofAudit.required === 1 &&
      graphDumpCommand.proofAudit.passedPurposes.includes("graph-dump") &&
      protocol.proofAudit.passedPurposes.includes("graph-dump") &&
      graphDumpCommand.commandAudit?.viewFollowupStructured === true &&
      graphDumpCommand.commandAudit?.unsupportedOutRetryStructured === true,
    metrics: {
      graphHash: graphDumpCommand.graphHash,
      nodes: graphDumpCommand.nodes,
      edges: graphDumpCommand.edges,
      validationOk: graphDumpCommand.validationOk,
      commandAudit: graphDumpCommand.commandAudit,
      proofAudit: graphDumpCommand.proofAudit,
    },
  },
  {
    id: "verifiable-graph-find-command",
    passed: graphFindCommand.proofAudit.required >= 2 &&
      graphFindCommand.proofAudit.passed === graphFindCommand.proofAudit.required &&
      graphFindCommand.proofAudit.purposes.includes("graph-find") &&
      graphFindCommand.commandAudit.replayable === true &&
      graphFindCommand.commandAudit.targetProfilePreserved === true &&
      graphFindCommand.commandAudit.readPolicyAuditable === true &&
      graphFindCommand.identityAudit?.exactSymbolUnique === true &&
      graphFindCommand.identityAudit?.ambiguousNameStructured === true &&
      graphFindCommand.identityAudit?.ambiguousSliceFollowupStructured === true &&
      graphFindCommand.identityAudit?.notFoundInspectFollowupStructured === true &&
      graphFindCommand.identityAudit?.followupSliceBound === true &&
      graphFindCommand.identityAudit?.contractDeclaresResolution === true &&
      graphFindCommand.identityAudit?.contractDeclaresFollowupSliceCommand === true &&
      graphFindCommand.commandAudit?.followupCommandStructured === true &&
      graphFindCommand.findAudit.matches < graphFindCommand.findAudit.fullNodes &&
      graphFindCommand.findAudit.followupSliceNodes < graphFindCommand.findAudit.fullNodes,
    metrics: graphFindCommand,
  },
  {
    id: "verifiable-graph-impact-command",
    passed: graphImpactCommand.proofAudit.required >= 2 &&
      graphImpactCommand.proofAudit.passed === graphImpactCommand.proofAudit.required &&
      graphImpactCommand.proofAudit.purposes.includes("graph-impact") &&
      graphImpactCommand.commandAudit.replayable === true &&
      graphImpactCommand.commandAudit.targetProfilePreserved === true &&
      graphImpactCommand.commandAudit.readPolicyAuditable === true &&
      graphImpactCommand.commandAudit.graphPatchFollowupStructured === true &&
      graphImpactCommand.commandAudit.missingNodeInspectFollowupStructured === true &&
      graphImpactCommand.impactAudit.directCallSites >= 1 &&
      graphImpactCommand.impactAudit.editGuards.includes("removeFunction-blocked-by-direct-call-sites"),
    metrics: graphImpactCommand,
  },
  {
    id: "verifiable-graph-slice-command",
    passed: graphSliceCommand.sliceAudit.sliceNodes < graphSliceCommand.sliceAudit.fullNodes &&
      graphSliceCommand.proofAudit.required >= 2 &&
      graphSliceCommand.proofAudit.passed === graphSliceCommand.proofAudit.required &&
      graphSliceCommand.commandAudit.replayable === true &&
      graphSliceCommand.commandAudit.targetProfilePreserved === true &&
      graphSliceCommand.commandAudit.readPolicyAuditable === true &&
      graphSliceCommand.commandAudit.graphImpactFollowupStructured === true &&
      graphSliceCommand.commandAudit.graphPatchFollowupStructured === true &&
      graphSliceCommand.commandAudit.missingNodeInspectFollowupStructured === true &&
      graphSliceCommand.proofAudit.passedPurposes.includes("graph-slice") &&
      protocol.proofAudit.passedPurposes.includes("graph-slice"),
    metrics: {
      sliceAudit: graphSliceCommand.sliceAudit,
      commandAudit: graphSliceCommand.commandAudit,
      proofAudit: graphSliceCommand.proofAudit,
    },
  },
  {
    id: "verifiable-graph-compare-command",
    passed: graphCompareCommand.semanticStable === true &&
      graphCompareCommand.graphHash?.startsWith("graph:") &&
      graphCompareCommand.proofAudit.required === 1 &&
      graphCompareCommand.proofAudit.passedPurposes.includes("graph-compare") &&
      graphCompareCommand.commandAudit?.graphViewFollowupStructured === true &&
      graphCompareCommand.commandAudit?.unstableGraphViewFollowupStructured === true &&
      graphCompareCommand.commandFailureAudit?.commandFailureRetryStructured === true &&
      graphCompareCommand.commandFailureAudit?.graphUsageRetryArgsBound === true &&
      graphCompareCommand.commandFailureAudit?.missingGraphArtifactRecoveryStructured === true &&
      protocol.proofAudit.passedPurposes.includes("graph-compare"),
    metrics: {
      graphHash: graphCompareCommand.graphHash,
      semanticStable: graphCompareCommand.semanticStable,
      leftSourceKind: graphCompareCommand.leftSourceKind,
      rightSourceKind: graphCompareCommand.rightSourceKind,
      commandAudit: graphCompareCommand.commandAudit,
      proofAudit: graphCompareCommand.proofAudit,
      commandFailureAudit: graphCompareCommand.commandFailureAudit,
    },
  },
  {
    id: "verifiable-graph-artifact-lifecycle-command",
    passed: graphArtifactCommand.semanticStable === true &&
      graphArtifactCommand.graphHash?.startsWith("graph:") &&
      graphArtifactCommand.proofAudit.required === 5 &&
      graphArtifactCommand.proofAudit.passedPurposes.includes("artifact-validate") &&
      graphArtifactCommand.proofAudit.passedPurposes.includes("graph-import") &&
      graphArtifactCommand.proofAudit.passedPurposes.includes("graph-view") &&
      graphArtifactCommand.proofAudit.passedPurposes.includes("graph-roundtrip") &&
      graphArtifactCommand.commandAudit?.artifactWritePolicyAuditable === true &&
      graphArtifactCommand.commandAudit?.importFollowupsStructured === true &&
      graphArtifactCommand.commandAudit?.roundtripFollowupStructured === true &&
      graphArtifactCommand.commandAudit?.validateFollowupStructured === true &&
      graphArtifactCommand.commandAudit?.viewCheckFollowupStructured === true &&
      graphArtifactCommand.commandAudit?.graphCheckSizeFollowupStructured === true &&
      protocol.proofAudit.passedPurposes.includes("graph-import") &&
      protocol.proofAudit.passedPurposes.includes("graph-view") &&
      protocol.proofAudit.passedPurposes.includes("graph-roundtrip"),
    metrics: {
      graphHash: graphArtifactCommand.graphHash,
      moduleIdentity: graphArtifactCommand.moduleIdentity,
      semanticStable: graphArtifactCommand.semanticStable,
      commandAudit: graphArtifactCommand.commandAudit,
      proofAudit: graphArtifactCommand.proofAudit,
    },
  },
  {
    id: "verifiable-diagnostic-explain-command",
    passed: explainCommand.codes.length >= 3 &&
      explainCommand.codes.some((item) => item.repairId === "match-binding-annotation") &&
      explainCommand.proofAudit.required === 3 &&
      explainCommand.proofAudit.passedPurposes.includes("explain") &&
      explainCommand.guidanceAudit?.requiredSourceCheck === true &&
      explainCommand.guidanceAudit?.optionalRepairPlan === true &&
      explainCommand.guidanceAudit?.optionalGraphInspect === true &&
      explainCommand.guidanceAudit?.repairPlanResultContract === true &&
      explainCommand.guidanceAudit?.repairPlanRollbackFields === true &&
      explainCommand.guidanceAudit?.commandReplayable === true &&
      protocol.proofAudit.passedPurposes.includes("explain"),
    metrics: {
      codes: explainCommand.codes,
      guidanceAudit: explainCommand.guidanceAudit,
      proofAudit: explainCommand.proofAudit,
    },
  },
  {
    id: "verifiable-runtime-audit-command",
    passed: runtimeAuditCommand.abi.diagnostics === 0 &&
      runtimeAuditCommand.memory.mirValid === true &&
      runtimeAuditCommand.memory.hiddenHeapAllocation === false &&
      runtimeAuditCommand.time.partialDiagnosticsStable === true &&
      runtimeAuditCommand.proofAudit.required === 6 &&
      runtimeAuditCommand.proofAudit.passedPurposes.includes("abi-audit") &&
      runtimeAuditCommand.proofAudit.passedPurposes.includes("memory-audit") &&
      runtimeAuditCommand.proofAudit.passedPurposes.includes("time-audit") &&
      runtimeAuditCommand.proofAudit.passedPurposes.includes("source-check") &&
      runtimeAuditCommand.stateAudit?.abiCommandReplayable === true &&
      runtimeAuditCommand.stateAudit?.memoryCommandReplayable === true &&
      runtimeAuditCommand.stateAudit?.timeCommandReplayable === true &&
      runtimeAuditCommand.stateAudit?.abiStateStructured === true &&
      runtimeAuditCommand.stateAudit?.memoryStateStructured === true &&
      runtimeAuditCommand.stateAudit?.memoryGraphInspectFollowupStructured === true &&
      runtimeAuditCommand.stateAudit?.timeStateStructured === true &&
      protocol.proofAudit.passedPurposes.includes("abi-audit") &&
      protocol.proofAudit.passedPurposes.includes("memory-audit") &&
      protocol.proofAudit.passedPurposes.includes("time-audit"),
    metrics: {
      target: runtimeAuditCommand.target,
      abi: runtimeAuditCommand.abi,
      memory: runtimeAuditCommand.memory,
      time: runtimeAuditCommand.time,
      stateAudit: runtimeAuditCommand.stateAudit,
      proofAudit: runtimeAuditCommand.proofAudit,
    },
  },
  {
    id: "verifiable-dev-doc-command",
    passed: devDocCommand.doc.symbols >= 1 &&
      devDocCommand.doc.missingExampleCount >= 1 &&
      devDocCommand.dev.mode === "watch-plan" &&
      devDocCommand.dev.affectedModules === 1 &&
      devDocCommand.dev.partialDiagnosticsStable === true &&
      devDocCommand.proofAudit.required === 4 &&
      devDocCommand.proofAudit.passedPurposes.includes("doc-audit") &&
      devDocCommand.proofAudit.passedPurposes.includes("dev-plan") &&
      devDocCommand.proofAudit.passedPurposes.includes("source-check") &&
      devDocCommand.workflowAudit?.docCommandReplayable === true &&
      devDocCommand.workflowAudit?.devCommandReplayable === true &&
      devDocCommand.workflowAudit?.publicationGateStructured === true &&
      devDocCommand.workflowAudit?.docSymbolGraphFindFollowupStructured === true &&
      devDocCommand.workflowAudit?.devPlanStructured === true &&
      protocol.proofAudit.passedPurposes.includes("doc-audit") &&
      protocol.proofAudit.passedPurposes.includes("dev-plan"),
    metrics: {
      doc: devDocCommand.doc,
      dev: devDocCommand.dev,
      workflowAudit: devDocCommand.workflowAudit,
      proofAudit: devDocCommand.proofAudit,
    },
  },
  {
    id: "verifiable-doctor-command",
    passed: ["ok", "warning", "error"].includes(doctorCommand.status) &&
      doctorCommand.checks >= 5 &&
      doctorCommand.targetToolchains >= 1 &&
      doctorCommand.proofAudit.required === 1 &&
      doctorCommand.proofAudit.passedPurposes.includes("doctor") &&
      doctorCommand.doctorReadinessAudit?.commandReplayable === true &&
      doctorCommand.doctorReadinessAudit?.checksStructured === true &&
      doctorCommand.doctorReadinessAudit?.targetToolchainsStructured === true &&
      doctorCommand.doctorReadinessAudit?.targetSelectionFollowupStructured === true &&
      doctorCommand.doctorReadinessAudit?.hostTargetReadinessStructured === true &&
      protocol.proofAudit.passedPurposes.includes("doctor"),
    metrics: {
      status: doctorCommand.status,
      commandSucceeded: doctorCommand.commandSucceeded,
      host: doctorCommand.host,
      checks: doctorCommand.checks,
      targetToolchains: doctorCommand.targetToolchains,
      doctorReadinessAudit: doctorCommand.doctorReadinessAudit,
      proofAudit: doctorCommand.proofAudit,
    },
  },
  {
    id: "verifiable-skills-command",
    passed: skillsCommand.skills >= 5 &&
      skillsCommand.commandAudit?.listReplayable === true &&
      skillsCommand.commandAudit?.getReplayable === true &&
      skillsCommand.commandAudit?.readPolicyAuditable === true &&
      skillsCommand.commandAudit?.failureContractAuditable === true &&
      skillsCommand.proofAudit.required === 2 &&
      skillsCommand.proofAudit.passedPurposes.includes("skills-read") &&
      protocol.proofAudit.passedPurposes.includes("skills-read"),
    metrics: skillsCommand,
  },
  {
    id: "verifiable-version-command",
    passed: versionCommand.commandAudit?.replayable === true &&
      versionCommand.commandAudit?.readPolicyAuditable === true &&
      versionCommand.commandAudit?.protocolFollowupStructured === true &&
      versionCommand.proofAudit.required === 1 &&
      versionCommand.proofAudit.passedPurposes.includes("version-read") &&
      protocol.proofAudit.passedPurposes.includes("version-read"),
    metrics: versionCommand,
  },
  {
    id: "rollback-proof-coverage",
    passed: protocol.proofAudit.purposes.includes("rollback-source-check") &&
      protocol.proofAudit.purposes.includes("rollback-graph-check") &&
      protocol.proofAudit.purposes.includes("rollback-artifact-validate") &&
      protocol.proofAudit.passedPurposes.includes("rollback-source-check") &&
      protocol.proofAudit.passedPurposes.includes("rollback-graph-check") &&
      protocol.proofAudit.passedPurposes.includes("rollback-artifact-validate"),
    metrics: {
      rollbackPurposes: protocol.proofAudit.purposes.filter((purpose: string) => purpose.startsWith("rollback-")),
      passedRollbackPurposes: protocol.proofAudit.passedPurposes.filter((purpose: string) => purpose.startsWith("rollback-")),
    },
  },
  {
    id: "token-saving-graph-query-contract",
    passed: graphInspect.agentQuery.tokenStrategy.readFullSourceRequired === false &&
      graphInspect.agentQuery.tokenStrategy.preferNodeNeighborhood === true &&
      graphInspect.agentQuery.lookupSurfaces.nodeNeighborhood.recommendedRadius === 1 &&
      graphInspectSliceFollowupStructured === true &&
      packageInspect.agentQuery.tokenStrategy.readFullSourceRequired === false &&
      tokenSlice.nodes.length < packageInspect.programGraph.nodes.length &&
      tokenSliceBytes < packageSourceBytes &&
      tokenSliceBytes < packageInspectBytes / 100,
    metrics: {
      readFullSourceRequired: graphInspect.agentQuery.tokenStrategy.readFullSourceRequired,
      preferNodeNeighborhood: graphInspect.agentQuery.tokenStrategy.preferNodeNeighborhood,
      recommendedRadius: graphInspect.agentQuery.lookupSurfaces.nodeNeighborhood.recommendedRadius,
      sample: `${tokenSample}::__zero_std_codec_hex_decoded_len`,
      sourceBytes: packageSourceBytes,
      fullInspectBytes: packageInspectBytes,
      fullGraphNodes: packageInspect.programGraph.nodes.length,
      neighborhoodNodes: tokenSlice.nodes.length,
      neighborhoodBytes: tokenSliceBytes,
      neighborhoodToSourceRatio: Number((tokenSliceBytes / packageSourceBytes).toFixed(3)),
      neighborhoodToFullInspectRatio: Number((tokenSliceBytes / packageInspectBytes).toFixed(4)),
    },
  },
];

const agentNativeAdvantage = {
  reliableEdits: proofReceipt.declaredFieldsMatched === true &&
    proofReceipt.argvBound === true &&
    proofReceipt.timingRecorded === true &&
    authoringReplay.audit?.checkedGraphEdit === true &&
    authoringReplay.audit?.sourceRewritten === true &&
    authoringReplay.audit?.rollbackAvailable === true &&
    rollbackReplay.audit?.rollbackActionStructured === true &&
    rollbackReplay.audit?.restoreApplied === true &&
    rollbackReplay.audit?.rollbackVerificationCommandsReplayed === true &&
    rollbackReplay.audit?.semanticIdentityRestored === true &&
    graphPatch.semanticEditAudit.checkedSourceRewrite === true &&
    graphPatch.semanticEditAudit.sourceBackedTransactions >= 18 &&
    graphPatch.semanticEditAudit.conflictRejections >= 22 &&
    graphPatch.proofLedgerAudit?.phaseOrderStructured === true &&
    graphPatch.proofLedgerAudit?.rollbackActionsStructured === true,
  tokenEfficient: graphInspect.agentQuery.tokenStrategy.readFullSourceRequired === false &&
    packageInspect.agentQuery.tokenStrategy.readFullSourceRequired === false &&
    tokenSliceBytes < packageSourceBytes &&
    tokenSliceBytes < packageInspectBytes / 100,
  hallucinationResistant: protocolManifest.proofResultFieldsCatalogAuditable === true &&
    protocolManifest.proofReceiptPolicyAuditable === true &&
    protocolManifest.commandAudit?.replayable === true &&
    protocolManifest.commandAudit?.localGateCommandAuditable === true &&
    graphFindCommand.identityAudit?.exactSymbolUnique === true &&
    graphFindCommand.identityAudit?.ambiguousNameStructured === true &&
    graphFindCommand.identityAudit?.notFoundInspectFollowupStructured === true &&
    [graphFindCommand, graphImpactCommand, graphSliceCommand].every((item) =>
      item.commandAudit?.readPolicyAuditable === true &&
      item.commandAudit?.targetProfilePreserved === true),
  autoRepairable: protocol.repairPlanAudit.candidateContracts >= 17 &&
    autoRepairBuildReplay.audit?.diagnosticEntrypointStructured === true &&
    autoRepairBuildReplay.audit?.repairPlanProofLedger === true &&
    autoRepairBuildReplay.audit?.graphPatchCandidateStructured === true &&
    autoRepairBuildReplay.audit?.checkedGraphPatchApplied === true &&
    autoRepairBuildReplay.audit?.postRepairCheckVerified === true &&
    protocol.repairPlanAudit.evidenceBackedCandidateContracts >= 17 &&
    protocol.failureAudit?.retryCommandContracts === true &&
    protocol.proofLedgerAudit?.failureLedgerClassified === true &&
    graphInspect.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger") &&
    graphLoop.steps >= 3,
  verifiableBuildSystem: [buildCommand, sizeCommand, shipCommand, testCommand].every((item) =>
    item.commandAudit?.replayable === true) &&
    authoringReplay.audit?.behaviorVerified === true &&
    authoringReplay.audit?.artifactVerified === true &&
    authoringReplay.audit?.compilerVerificationCommandsReplayed === true &&
    rollbackReplay.audit?.sourceCheckRestored === true &&
    rollbackReplay.audit?.graphCheckHashRestored === true &&
    autoRepairBuildReplay.audit?.artifactVerified === true &&
    autoRepairBuildReplay.audit?.compilerVerificationCommandsReplayed === true &&
    [buildCommand, sizeCommand, shipCommand].every((item) =>
      item.commandAudit?.artifactWritePolicyAuditable === true &&
      item.commandAudit?.artifactRollbackActionAuditable === true &&
      item.cacheAudit?.source?.compilerCachesOk === true &&
      item.cacheAudit?.graph?.compilerCachesOk === true) &&
    testCommand.testEvidenceAudit?.sourceFailureFieldsStructured === true &&
    testCommand.testEvidenceAudit?.graphHashBound === true &&
    proofReceipt.artifactWorkflowReceipts === true &&
    proofReceipt.graphArtifactWorkflowReceipts === true &&
    proofReceipt.testWorkflowReceipts === true &&
    proofReceipt.graphTestWorkflowReceipts === true,
};
const agentNativeAdvantageScore = Object.values(agentNativeAdvantage).filter(Boolean).length * 20;
cases.push({
  id: "agent-native-advantage-evidence",
  passed: agentNativeAdvantageScore === 100,
  metrics: {
    ...agentNativeAdvantage,
    score: agentNativeAdvantageScore,
    dimensions: Object.keys(agentNativeAdvantage),
  },
});

const agentObjectiveAudit = {
  humanReadableSource: sourceCommand.sourceUnderstandingAudit?.formattedStable === true &&
    sourceCommand.proofAudit?.passedPurposes?.includes("tokens") &&
    sourceCommand.proofAudit?.passedPurposes?.includes("parse") &&
    sourceCommand.proofAudit?.passedPurposes?.includes("format"),
  semanticGraphUnderstanding: protocolManifest.semanticGraphLookupCommandContracts === true &&
    graphFindCommand.ok === true &&
    graphImpactCommand.ok === true &&
    graphSliceCommand.ok === true &&
    graphInspect.agentQuery.tokenStrategy.readFullSourceRequired === false,
  compilerMediatedModification: authoringReplay.audit?.checkedGraphEdit === true &&
    authoringReplay.audit?.sourceRewritten === true &&
    graphPatch.semanticEditAudit?.checkedSourceRewrite === true,
  compilerMediatedRepair: autoRepairBuildReplay.audit?.diagnosticEntrypointStructured === true &&
    autoRepairBuildReplay.audit?.graphPatchCandidateStructured === true &&
    autoRepairBuildReplay.audit?.postRepairCheckVerified === true,
  refactoringSurface: graphPatch.semanticEditAudit?.coveredOperations?.length >= 17 &&
    graphPatch.semanticEditAudit?.categories?.length >= 5 &&
    ["graph-hash", "expect-value", "name-conflict"].every((item) =>
      graphPatch.semanticEditAudit?.checkedPreconditions?.includes(item)),
  verifiableBuildAndTest: authoringReplay.audit?.behaviorVerified === true &&
    authoringReplay.audit?.artifactVerified === true &&
    [buildCommand, testCommand, sizeCommand, shipCommand].every((item) => item.commandAudit?.replayable === true),
  auditableProofReceipts: proofReceipt.declaredFieldsMatched === true &&
    proofReceipt.argvBound === true &&
    proofReceipt.timingRecorded === true &&
    proofReceipt.receiptPurposes?.includes("protocol-read"),
  rollbackProven: rollbackReplay.audit?.restoreApplied === true &&
    rollbackReplay.audit?.rollbackVerificationCommandsReplayed === true &&
    rollbackReplay.audit?.semanticIdentityRestored === true,
  tokenEfficientGraphProtocol: tokenSliceBytes < packageSourceBytes &&
    tokenSliceBytes < packageInspectBytes / 100 &&
    graphFindCommand.commandAudit?.readPolicyAuditable === true,
  hallucinationResistance: protocolManifest.proofResultFieldsCatalogAuditable === true &&
    protocolManifest.commandAudit?.localGateCommandAuditable === true &&
    graphFindCommand.identityAudit?.exactSymbolUnique === true &&
    graphFindCommand.identityAudit?.notFoundInspectFollowupStructured === true,
  autoRepairToBuild: autoRepairBuildReplay.audit?.checkedGraphPatchApplied === true &&
    autoRepairBuildReplay.audit?.artifactVerified === true &&
    autoRepairBuildReplay.audit?.compilerVerificationCommandsReplayed === true,
  protocolNotJustLanguage: protocolManifest.entrypoints >= 11 &&
    protocolManifest.proofPurposes >= 35 &&
    protocolManifest.objectiveContractAuditable === true &&
    protocolManifest.localGates.includes("agent-contracts") &&
    protocolManifest.permissionBoundaryStructured === true,
};
const agentObjectiveEvidenceItems = Object.values(agentObjectiveAudit).filter(Boolean).length;
cases.push({
  id: "agent-objective-audit",
  passed: agentObjectiveEvidenceItems === Object.keys(agentObjectiveAudit).length,
  metrics: {
    ...agentObjectiveAudit,
    evidenceItems: agentObjectiveEvidenceItems,
    requirements: Object.keys(agentObjectiveAudit),
  },
});

const passed = cases.filter((item) => item.passed).length;
const operationLevelRetryCase = cases.find((item) => item.id === "operation-level-retry-contract");
const checkedGraphEditEntrypointCase = cases.find((item) => item.id === "checked-graph-edit-entrypoint-audit-fields");
const graphPatchEvidenceBindingsCase = cases.find((item) => item.id === "graph-patch-evidence-bindings");
const graphPatchVerificationResultFieldsCase = cases.find((item) => item.id === "graph-patch-verification-result-fields");
const transactionFailureMatrixCase = cases.find((item) => item.id === "transaction-failure-matrix");
const rollbackProofCoverageCase = cases.find((item) => item.id === "rollback-proof-coverage");
const graphPatchRollbackProofs = ["rollback-artifact-validate", "rollback-source-check", "rollback-graph-check"];
const report = {
  schemaVersion: 1,
  kind: "agent-protocol-eval",
  ok: passed === cases.length,
  summary: {
    cases: cases.length,
    passed,
    failed: cases.length - passed,
    successRate: passed / cases.length,
    protocolManifestEntrypoints: protocolManifest.entrypoints,
    protocolManifestCommandReplayable: protocolManifest.commandAudit?.replayable === true &&
      protocolManifest.commandAudit?.readPolicyAuditable === true &&
      protocolManifest.commandAudit?.localGateCommandAuditable === true,
    proofResultFieldsCatalogAuditable: protocolManifest.proofResultFieldsCatalogAuditable === true,
    proofReceiptPolicyAuditable: protocolManifest.proofReceiptPolicyAuditable === true,
    objectiveContractAuditable: protocolManifest.objectiveContractAuditable === true,
    proofReceiptReplayOperational: proofReceipt.declaredFieldsMatched === true &&
      proofReceipt.argvBound === true &&
      proofReceipt.timingRecorded === true &&
      proofReceipt.sourceWorkflowReceipts === true &&
      proofReceipt.artifactWorkflowReceipts === true &&
      proofReceipt.graphArtifactWorkflowReceipts === true &&
      proofReceipt.artifactShipWorkflowReceipts === true &&
      proofReceipt.graphArtifactShipWorkflowReceipts === true &&
      proofReceipt.testWorkflowReceipts === true &&
      proofReceipt.graphTestWorkflowReceipts === true &&
      proofReceipt.receiptPurposes.includes("protocol-read") &&
      protocol.proofReceiptAudit?.declaredFieldsMatched === true &&
      protocol.proofReceiptAudit?.artifactWorkflowReceipts === true &&
      protocol.proofReceiptAudit?.graphArtifactWorkflowReceipts === true &&
      protocol.proofReceiptAudit?.artifactShipWorkflowReceipts === true &&
      protocol.proofReceiptAudit?.graphArtifactShipWorkflowReceipts === true &&
      protocol.proofReceiptAudit?.testWorkflowReceipts === true &&
      protocol.proofReceiptAudit?.graphTestWorkflowReceipts === true,
    protocolManifestProofCommands: protocolManifest.proofAudit?.required ?? 0,
    proofCommandsRequired: protocol.proofAudit.required,
    authoringReplayOperational: authoringReplay.ok === true && Object.values(authoringReplay.audit ?? {}).every(Boolean),
    authoringReplayProofCommands: authoringReplay.proofAudit?.required ?? 0,
    autoRepairBuildReplayOperational: autoRepairBuildReplay.ok === true && Object.values(autoRepairBuildReplay.audit ?? {}).every(Boolean),
    autoRepairBuildReplayProofCommands: autoRepairBuildReplay.proofAudit?.required ?? 0,
    rollbackReplayOperational: rollbackReplay.ok === true && Object.values(rollbackReplay.audit ?? {}).every(Boolean),
    rollbackReplayProofCommands: rollbackReplay.proofAudit?.required ?? 0,
    objectiveAuditOperational: agentObjectiveEvidenceItems === Object.keys(agentObjectiveAudit).length,
    objectiveAuditEvidenceItems: agentObjectiveEvidenceItems,
    diagnosticEntrypointCommands: diagnosticEntrypoint.entrypointAudit.commands,
    diagnosticEntrypointResultFields: diagnosticEntrypoint.entrypointAudit.resultFields.length,
    diagnosticEntrypointRollbackActionFields: diagnosticEntrypoint.entrypointAudit.rollbackActionFields === true &&
      protocol.entrypointAudit.resultFields.includes("agentTransaction.rollback.actions[]"),
    diagnosticGraphLookupCommands: diagnosticEntrypoint.graphLookupAudit.commands,
    diagnosticGraphLookupTargetProfilePreserved: diagnosticEntrypoint.graphLookupAudit.targetProfilePreserved === true,
    diagnosticGraphLookupRepairFallback: diagnosticEntrypoint.graphLookupAudit.repairFallback === true,
    diagnosticCheckReadPolicyAuditable: diagnosticEntrypoint.graphLookupAudit.checkReadPolicyAuditable === true,
    checkFailureRepairFollowupsStructured: diagnosticEntrypoint.graphLookupAudit.checkFailureFollowupsStructured === true &&
      diagnosticEntrypoint.graphLookupAudit.graphCheckFailureFollowupStructured === true,
    protocolEntrypointResultFields: protocol.entrypointAudit.resultFields.length,
    repairPlanCandidateOperations: protocol.repairPlanAudit.candidateOps.length,
    repairPlanCandidateContracts: protocol.repairPlanAudit.candidateContracts,
    repairPlanCompleteCandidateContracts: protocol.repairPlanAudit.completeCandidateContracts === protocol.repairPlanAudit.candidateContracts,
    repairPlanCandidateEvidenceBacked: protocol.repairPlanAudit.evidenceBackedCandidateContracts,
    repairPlanCandidateTargetProfilePreserved: protocol.repairPlanAudit.candidateTargetProfilePreserved === true,
    rollbackProofPurposes: protocol.proofAudit.purposes.filter((purpose: string) => purpose.startsWith("rollback-")).length,
    rollbackProofCoverage: rollbackProofCoverageCase?.passed === true,
    operationLevelRetryContract: operationLevelRetryCase?.passed === true,
    checkedGraphEditEntrypointAudit: checkedGraphEditEntrypointCase?.passed === true,
    graphPatchFailureMatrixComplete: ["BLD002", "GPH001", "GPH002", "GPH003", "GPH004", "GPH005"].every((code) =>
      graphPatch.failureAudit?.observedCodes?.includes(code)) &&
      graphPatch.failureAudit?.nonRetryableCodes?.includes("GPH006"),
    graphPatchFailureRetryCommandContracts: graphPatch.failureAudit?.failureRetryCommandContracts === true,
    graphPatchInvalidPatchInputRetryStructured: graphPatch.failureAudit?.invalidPatchInputRetryStructured === true,
    graphPatchInlineCommandArgsPreserved: graphPatch.failureAudit?.inlinePatchCommandArgsPreserved === true,
    repairTransactionFailureMatrixComplete: ["source-unavailable", "unsupported-repair", "edit-not-found", "write-failed"].every((failureClass) =>
      protocol.failureAudit?.declaredClasses?.includes(failureClass)) &&
      ["source-unavailable", "unsupported-repair", "edit-not-found"].every((failureClass) =>
        protocol.failureAudit?.observedClasses?.includes(failureClass)),
    repairTransactionRetryCommandContracts: protocol.failureAudit?.retryCommandContracts === true,
    repairTransactionProofLedgerStructured: protocol.proofLedgerAudit?.phaseOrderStructured === true &&
      protocol.proofLedgerAudit?.resultFieldsDeclared === true &&
      protocol.proofLedgerAudit?.successLedgerEvidenceFields === true &&
      protocol.proofLedgerAudit?.verificationPurposesMatched === true &&
      protocol.proofLedgerAudit?.rollbackPurposesMatched === true &&
      protocol.proofLedgerAudit?.failureLedgerClassified === true,
    repairRollbackActionAuditable: protocol.proofLedgerAudit?.rollbackActionsStructured === true &&
      protocol.failureAudit?.rollbackActions?.includes("restore-line:patches[].path:patches[].old"),
    repairLoopProofLedgerAuditable: graphInspect.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger") &&
      graphInspect.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger.phases[].status") &&
      protocolManifest.repairLoopProofLedgerAuditable === true &&
      protocol.proofLedgerAudit?.phaseOrderStructured === true &&
      graphPatch.proofLedgerAudit?.phaseOrderStructured === true,
    repairLoopTargetProfilePreserved: Object.values(graphInspect.agentQuery.repairLoop.commandContracts).every((contract: any) =>
      contract.argv.includes("--target") &&
      contract.argv.includes("<same-target>") &&
      contract.argv.includes("--profile") &&
      contract.argv.includes("<same-profile>")) &&
      protocolManifest.repairLoopTargetProfilePreserved === true,
    transactionFailureRetryTargetProfilePreserved: transactionFailureMatrixCase?.passed === true &&
      graphPatch.failureAudit?.targetProfileRetryPreserved === true &&
      protocol.failureAudit?.targetProfileRetryPreserved === true,
    graphPatchProofCommandsReplayed: graphPatch.proofAudit.required,
    graphPatchProofReplayClassified: graphPatch.proofAudit.required >= 10 &&
      graphPatch.proofAudit.required === graphPatch.proofAudit.passed + graphPatch.proofAudit.failed &&
      graphPatch.proofAudit.passed >= 10,
    graphPatchProofLedgerStructured: graphPatch.proofLedgerAudit?.phaseOrderStructured === true &&
      graphPatch.proofLedgerAudit?.successLedgerEvidenceFields === true &&
      graphPatch.proofLedgerAudit?.verificationPurposesMatched === true &&
      graphPatch.proofLedgerAudit?.rollbackPurposesMatched === true &&
      graphPatch.proofLedgerAudit?.rollbackActionsStructured === true &&
      graphPatch.proofLedgerAudit?.failureLedgerClassified === true,
    graphPatchRollbackActionAuditable: graphPatch.proofLedgerAudit?.rollbackActionsStructured === true,
    graphPatchRollbackProofReplayPassed: graphPatchRollbackProofs.every((purpose) =>
      graphPatch.proofAudit.passedPurposes.includes(purpose)),
    graphPatchEvidenceBindingsAuditable: graphPatchEvidenceBindingsCase?.passed === true,
    graphPatchVerificationResultFieldsAuditable: graphPatchVerificationResultFieldsCase?.passed === true,
    semanticEditOperations: graphPatch.semanticEditAudit.coveredOperations.length,
    semanticEditCategories: graphPatch.semanticEditAudit.categories.length,
    semanticEditCheckedPreconditions: graphPatch.semanticEditAudit.checkedPreconditions.length,
    semanticEditRequiredPreconditions: ["graph-hash", "expect-value", "name-conflict"].every((item) =>
      graphPatch.semanticEditAudit.checkedPreconditions.includes(item)),
    semanticEditOperationSurfaceBacked: graphPatch.semanticEditAudit.operationSurfaceBacked === true,
    semanticEditSourceBackedTransactions: graphPatch.semanticEditAudit.sourceBackedTransactions,
    semanticEditConflictRejections: graphPatch.semanticEditAudit.conflictRejections,
    semanticEditCheckedSourceRewrite: graphPatch.semanticEditAudit.checkedSourceRewrite === true,
    buildProofCommands: buildCommand.proofAudit.required,
    graphBuildProofCommands: buildCommand.graph?.proofAudit?.required ?? 0,
    sizeProofCommands: sizeCommand.proofAudit.required,
    graphSizeProofCommands: sizeCommand.graph?.proofAudit?.required ?? 0,
    shipProofCommands: shipCommand.proofAudit.required,
    graphShipProofCommands: shipCommand.graph?.proofAudit?.required ?? 0,
    testProofCommands: testCommand.proofAudit.required,
    graphTestProofCommands: testCommand.graph?.proofAudit?.required ?? 0,
    buildSystemCommandReplayable: [buildCommand, sizeCommand, shipCommand, testCommand].every((item) =>
      item.commandAudit?.replayable === true),
    artifactWritePolicyAuditable: [buildCommand, sizeCommand, shipCommand].every((item) =>
      item.commandAudit?.artifactWritePolicyAuditable === true),
    artifactRollbackActionAuditable: [buildCommand, sizeCommand, shipCommand].every((item) =>
      item.commandAudit?.artifactRollbackActionAuditable === true),
    buildTestFollowupStructured: buildCommand.commandAudit?.buildTestFollowupStructured === true,
    buildFailureRecoveryStructured: buildCommand.commandAudit?.buildFailureRecoveryStructured === true,
    graphBuildFailureRecoveryStructured: buildCommand.commandAudit?.graphBuildFailureRecoveryStructured === true,
    graphArtifactCommandRerouteStructured: buildCommand.commandAudit?.graphArtifactCommandRerouteStructured === true,
    graphBuildSizeFollowupStructured: buildCommand.commandAudit?.graphBuildSizeFollowupStructured === true,
    graphBuildTestFollowupStructured: buildCommand.commandAudit?.graphBuildTestFollowupStructured === true,
    sizeBuildFollowupStructured: sizeCommand.commandAudit?.sizeBuildFollowupStructured === true,
    runJsonUnsupportedFollowupStructured: buildCommand.commandAudit?.runJsonUnsupportedFollowupStructured === true,
    graphShipSizeFollowupStructured: shipCommand.commandAudit?.graphShipSizeFollowupStructured === true,
    graphShipTestFollowupStructured: shipCommand.commandAudit?.graphShipTestFollowupStructured === true,
    sourceShipSizeFollowupStructured: shipCommand.commandAudit?.sourceShipSizeFollowupStructured === true,
    sourceShipTestFollowupStructured: shipCommand.commandAudit?.sourceShipTestFollowupStructured === true,
    buildSystemArtifactEvidenceMatched: [buildCommand, sizeCommand, shipCommand].every((item) =>
      item.commandAudit?.artifactEvidenceMatched === true) &&
      testCommand.commandAudit?.failureEvidenceMatched === true,
    testFailureInspectFollowupStructured: testCommand.commandAudit?.failureInspectFollowupStructured === true,
    testNoTestsDocAuditFollowupStructured: testCommand.commandAudit?.noTestsDocAuditFollowupStructured === true,
    testSystemFailureEvidenceStructured: testCommand.testEvidenceAudit?.sourceFailureFieldsStructured === true &&
      testCommand.testEvidenceAudit?.graphFailureFieldsStructured === true &&
      testCommand.testEvidenceAudit?.expectedFailureRegressionAudited === true,
    testSystemGraphEvidenceBound: testCommand.testEvidenceAudit?.sourceDiscoveryStructured === true &&
      testCommand.testEvidenceAudit?.graphDiscoveryStructured === true &&
      testCommand.testEvidenceAudit?.graphHashBound === true,
    buildSystemTargetProfilePreserved: [buildCommand, sizeCommand, shipCommand].every((item) =>
      item.commandAudit?.targetProfilePreserved === true),
    buildSystemSafetyFactsStructured: [buildCommand, sizeCommand, shipCommand].every((item) =>
      item.safetyAudit?.sourceSafetyStructured === true &&
      item.safetyAudit?.graphSafetyStructured === true),
    buildSystemProductionReadinessAuditable: [buildCommand, sizeCommand, shipCommand].every((item) =>
      item.safetyAudit?.productionGateStructured === true &&
      item.safetyAudit?.blockingRisksAuditable === true &&
      item.safetyAudit?.uncheckedSurfacesAuditable === true),
    buildSystemCacheAuditStructured: [buildCommand, sizeCommand, shipCommand].every((item) =>
      item.cacheAudit?.source?.packageCacheAdvertised === true &&
      item.cacheAudit?.source?.compilerCacheAdvertised === true &&
      item.cacheAudit?.source?.incrementalAdvertised === true &&
      item.cacheAudit?.source?.packageCacheOk === true &&
      item.cacheAudit?.source?.compilerCachesOk === true &&
      item.cacheAudit?.source?.incrementalOk === true),
    buildSystemProgramGraphCacheBound: [buildCommand, sizeCommand, shipCommand].every((item) =>
      item.cacheAudit?.graph?.packageCacheAdvertised === true &&
      item.cacheAudit?.graph?.compilerCacheAdvertised === true &&
      item.cacheAudit?.graph?.incrementalAdvertised === true &&
      item.cacheAudit?.graph?.packageCacheOk === true &&
      item.cacheAudit?.graph?.compilerCachesOk === true &&
      item.cacheAudit?.graph?.incrementalOk === true),
    agentPermissionBoundaryStructured: protocolManifest.permissionBoundaryStructured === true,
    targetSelectionProofCommands: targetsCommand.proofAudit.required,
    hostTargetReadinessStructured: targetsCommand.targetReadinessAudit?.selectionStructured === true &&
      targetsCommand.targetReadinessAudit?.capabilityFactsStructured === true &&
      targetsCommand.targetReadinessAudit?.toolchainFactsStructured === true &&
      targetsCommand.targetReadinessAudit?.hostTargetPresent === true &&
      doctorCommand.doctorReadinessAudit?.checksStructured === true &&
      doctorCommand.doctorReadinessAudit?.targetToolchainsStructured === true &&
      doctorCommand.doctorReadinessAudit?.hostTargetReadinessStructured === true,
    hostTargetCommandReplayable: targetsCommand.targetReadinessAudit?.commandReplayable === true &&
      targetsCommand.targetReadinessAudit?.jsonReplayable === true &&
      doctorCommand.doctorReadinessAudit?.commandReplayable === true,
    targetSelectionJsonReplayable: targetsCommand.targetReadinessAudit?.commandReplayable === true &&
      targetsCommand.targetReadinessAudit?.jsonReplayable === true,
    targetSelectionInvalidTargetRecoveryStructured: targetsCommand.invalidTargetRecoveryStructured === true,
    targetSelectionInvalidTargetCommandArgsPreserved: targetsCommand.invalidTargetCommandArgsPreserved === true,
    targetSelectionDoctorFollowupStructured: targetsCommand.targetReadinessAudit?.doctorFollowupStructured === true &&
      doctorCommand.doctorReadinessAudit?.targetToolchainsStructured === true &&
      doctorCommand.proofAudit.passedPurposes.includes("doctor"),
    doctorTargetSelectionFollowupStructured: doctorCommand.doctorReadinessAudit?.targetSelectionFollowupStructured === true &&
      targetsCommand.targetReadinessAudit?.jsonReplayable === true,
    cleanProofCommands: cleanCommand.proofAudit.required,
    destructiveCleanPolicyAuditable: cleanCommand.commandAudit?.destructiveCleanPolicyAuditable === true,
    destructiveCleanFailureRetryStructured: cleanCommand.commandAudit?.failureRetryStructured === true,
    newProjectProofCommands: newCommand.proofAudit.required,
    newProjectWritePolicyAuditable: newCommand.commandAudit?.projectWritePolicyAuditable === true,
    newProjectFailureRetryStructured: newCommand.commandAudit?.failureRetryStructured === true,
    newProjectVerificationAuditable: newCommand.proofAudit.passedPurposes.includes("source-check") &&
      newCommand.proofAudit.passedPurposes.includes("test-run"),
    sourceUnderstandingProofCommands: sourceCommand.proofAudit.required,
    sourceUnderstandingStructuredFields: sourceCommand.sourceUnderstandingAudit.tokenSpanFields.includes("tokens[].offset") &&
      sourceCommand.sourceUnderstandingAudit.tokenSpanFields.includes("tokens[].length") &&
      sourceCommand.sourceUnderstandingAudit.declarationFields.includes("functions[].name") &&
      sourceCommand.sourceUnderstandingAudit.declarationFields.includes("functions[].bodyKinds"),
    sourceUnderstandingTokenParseFollowupStructured: sourceCommand.sourceUnderstandingAudit.tokenParseFollowupStructured === true,
    sourceUnderstandingGraphFollowupStructured: sourceCommand.sourceUnderstandingAudit.parseGraphInspectFollowupStructured === true,
    sourceUnderstandingFormatFailureFollowupStructured: sourceCommand.sourceUnderstandingAudit.formatFailureFollowupStructured === true,
    sourceUnderstandingFormattedStable: sourceCommand.sourceUnderstandingAudit.formattedStable === true,
    semanticGraphLookupCommandContracts: graphInspectCommand.commandAudit?.lookupCommandContractsTargetProfilePreserved === true &&
      protocolManifest.semanticGraphLookupCommandContracts === true,
    graphInspectProofCommands: graphInspectCommand.proofAudit.required,
    graphInspectReadPolicyAuditable: graphInspectCommand.commandAudit?.readPolicyAuditable === true,
    graphDumpProofCommands: graphDumpCommand.proofAudit.required,
    graphDumpWritePolicyAuditable: graphDumpCommand.commandAudit?.previewWritePolicyAuditable === true &&
      graphDumpCommand.commandAudit?.artifactWritePolicyAuditable === true,
    graphDumpViewFollowupStructured: graphDumpCommand.commandAudit?.viewFollowupStructured === true,
    graphUnsupportedOutRetryStructured: graphDumpCommand.commandAudit?.unsupportedOutRetryStructured === true,
    graphFindProofCommands: graphFindCommand.proofAudit.required,
    graphImpactProofCommands: graphImpactCommand.proofAudit.required,
    graphImpactPatchFollowupStructured: graphImpactCommand.commandAudit?.graphPatchFollowupStructured === true &&
      graphImpactCommand.commandAudit?.graphPatchFollowupTargetProfilePreserved === true &&
      graphImpactCommand.impactCommandAudit?.editGuardsStructured === true,
    graphImpactMissingNodeInspectFollowupStructured: graphImpactCommand.commandAudit?.missingNodeInspectFollowupStructured === true &&
      graphImpactCommand.impactCommandAudit?.missingNodeInspectFollowupStructured === true,
    graphSliceProofCommands: graphSliceCommand.proofAudit.required,
    graphSliceImpactFollowupStructured: graphSliceCommand.commandAudit?.graphImpactFollowupStructured === true,
    graphSlicePatchFollowupStructured: graphSliceCommand.commandAudit?.graphPatchFollowupStructured === true &&
      graphSliceCommand.commandAudit?.graphPatchFollowupTargetProfilePreserved === true,
    graphSliceMissingNodeInspectFollowupStructured: graphSliceCommand.commandAudit?.missingNodeInspectFollowupStructured === true,
    semanticGraphQueryReplayable: [graphFindCommand, graphImpactCommand, graphSliceCommand].every((item) =>
      item.commandAudit?.replayable === true),
    semanticGraphQueryTargetProfilePreserved: [graphFindCommand, graphImpactCommand, graphSliceCommand].every((item) =>
      item.commandAudit?.targetProfilePreserved === true),
    semanticGraphQueryReadPolicyAuditable: [graphFindCommand, graphImpactCommand, graphSliceCommand].every((item) =>
      item.commandAudit?.readPolicyAuditable === true),
    semanticGraphLookupIdentityBound: graphFindCommand.identityAudit?.exactSymbolUnique === true &&
      graphFindCommand.identityAudit?.ambiguousNameStructured === true &&
      graphFindCommand.identityAudit?.ambiguousSliceFollowupStructured === true &&
      graphFindCommand.identityAudit?.notFoundInspectFollowupStructured === true &&
      graphFindCommand.identityAudit?.followupSliceBound === true &&
      graphFindCommand.identityAudit?.contractDeclaresResolution === true &&
      graphFindCommand.identityAudit?.contractDeclaresFollowupSliceCommand === true,
    graphFindFollowupSliceCommandStructured: graphFindCommand.identityAudit?.contractDeclaresFollowupSliceCommand === true &&
      graphFindCommand.commandAudit?.followupCommandStructured === true,
    graphFindAmbiguousSliceFollowupStructured: graphFindCommand.identityAudit?.ambiguousSliceFollowupStructured === true,
    graphFindNotFoundInspectFollowupStructured: graphFindCommand.identityAudit?.notFoundInspectFollowupStructured === true,
    graphInspectSliceFollowupStructured,
    graphCompareProofCommands: graphCompareCommand.proofAudit.required,
    graphCompareViewFollowupStructured: graphCompareCommand.commandAudit?.graphViewFollowupStructured === true,
    graphCompareUnstableViewFollowupStructured: graphCompareCommand.commandAudit?.unstableGraphViewFollowupStructured === true,
    commandFailureRetryStructured: graphCompareCommand.commandFailureAudit?.commandFailureRetryStructured === true &&
      graphCompareCommand.commandFailureAudit?.graphUsageRetryArgsBound === true,
    missingGraphArtifactRecoveryStructured: graphCompareCommand.commandFailureAudit?.missingGraphArtifactRecoveryStructured === true,
    graphArtifactProofCommands: graphArtifactCommand.proofAudit.required,
    graphArtifactWritePolicyAuditable: graphArtifactCommand.commandAudit?.artifactWritePolicyAuditable === true,
    graphArtifactImportFollowupsStructured: graphArtifactCommand.commandAudit?.importFollowupsStructured === true,
    graphArtifactRoundtripFollowupStructured: graphArtifactCommand.commandAudit?.roundtripFollowupStructured === true,
    graphArtifactValidateViewFollowupStructured: graphArtifactCommand.commandAudit?.validateFollowupStructured === true,
    graphArtifactViewCheckFollowupStructured: graphArtifactCommand.commandAudit?.viewCheckFollowupStructured === true,
    graphArtifactCheckSizeFollowupStructured: graphArtifactCommand.commandAudit?.graphCheckSizeFollowupStructured === true,
    explainProofCommands: explainCommand.proofAudit.required,
    runtimeAuditProofCommands: runtimeAuditCommand.proofAudit.required,
    devDocProofCommands: devDocCommand.proofAudit.required,
    auxiliaryAgentGuidanceStructured: explainCommand.guidanceAudit?.requiredSourceCheck === true &&
      explainCommand.guidanceAudit?.optionalRepairPlan === true &&
      explainCommand.guidanceAudit?.optionalGraphInspect === true &&
      explainCommand.guidanceAudit?.repairPlanResultContract === true &&
      explainCommand.guidanceAudit?.repairPlanRollbackFields === true &&
      devDocCommand.workflowAudit?.publicationGateStructured === true &&
      devDocCommand.workflowAudit?.docSymbolGraphFindFollowupStructured === true &&
      devDocCommand.workflowAudit?.devPlanStructured === true &&
      runtimeAuditCommand.stateAudit?.abiStateStructured === true &&
      runtimeAuditCommand.stateAudit?.memoryStateStructured === true &&
      runtimeAuditCommand.stateAudit?.timeStateStructured === true,
    runtimeAuditGraphFollowupStructured: runtimeAuditCommand.stateAudit?.memoryGraphInspectFollowupStructured === true,
    explainRepairPlanResultContractAuditable: explainCommand.guidanceAudit?.repairPlanResultContract === true &&
      explainCommand.guidanceAudit?.repairPlanRollbackFields === true,
    docSymbolGraphFindFollowupStructured: devDocCommand.workflowAudit?.docSymbolGraphFindFollowupStructured === true,
    auxiliaryAgentCommandReplayable: explainCommand.guidanceAudit?.commandReplayable === true &&
      devDocCommand.workflowAudit?.docCommandReplayable === true &&
      devDocCommand.workflowAudit?.devCommandReplayable === true &&
      runtimeAuditCommand.stateAudit?.abiCommandReplayable === true &&
      runtimeAuditCommand.stateAudit?.memoryCommandReplayable === true &&
      runtimeAuditCommand.stateAudit?.timeCommandReplayable === true,
    skillsGuidanceCommandReplayable: skillsCommand.commandAudit?.listReplayable === true &&
      skillsCommand.commandAudit?.getReplayable === true &&
      skillsCommand.commandAudit?.readPolicyAuditable === true,
    versionProofCommands: versionCommand.proofAudit.required,
    versionProvenanceCommandReplayable: versionCommand.commandAudit?.replayable === true &&
      versionCommand.commandAudit?.readPolicyAuditable === true &&
      versionCommand.commandAudit?.protocolFollowupStructured === true,
    doctorProofCommands: doctorCommand.proofAudit.required,
    skillsProofCommands: skillsCommand.proofAudit.required,
    graphRepairSteps: graphLoop.steps,
    fullSourceReadRequiredForGraphLookup: graphInspect.agentQuery.tokenStrategy.readFullSourceRequired,
    tokenSavingSampleRatio: Number((tokenSliceBytes / packageSourceBytes).toFixed(3)),
    tokenSavingFullInspectRatio: Number((tokenSliceBytes / packageInspectBytes).toFixed(4)),
    agentNativeAdvantageScore,
    agentNativeReliableEdits: agentNativeAdvantage.reliableEdits,
    agentNativeTokenEfficient: agentNativeAdvantage.tokenEfficient,
    agentNativeHallucinationResistant: agentNativeAdvantage.hallucinationResistant,
    agentNativeAutoRepairable: agentNativeAdvantage.autoRepairable,
    agentNativeVerifiableBuildSystem: agentNativeAdvantage.verifiableBuildSystem,
  },
  cases,
};

console.log(JSON.stringify(report, null, 2));
if (!report.ok) process.exitCode = 1;
