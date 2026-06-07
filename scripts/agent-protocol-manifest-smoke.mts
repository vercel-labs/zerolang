import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent protocol manifest smoke requires ZERO_BIN or ${defaultZeroBin}`);

const { stdout } = await execFileAsync(zero, ["agent", "protocol", "--json"], { maxBuffer: 8 * 1024 * 1024 });
const manifest = JSON.parse(stdout);

async function zeroJson(args: string[]) {
  const result = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 }).catch((error) => error);
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const body = await zeroJson(command.argv.slice(1));
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: body.kind === "agent-protocol-manifest" });
  }
  return proofs;
}

const requiredEntrypoints = ["semantic-inspect", "semantic-find", "semantic-impact", "semantic-slice", "semantic-compare", "diagnose", "repair-plan", "checked-graph-edit", "build", "test", "ship"];
const requiredOperations = [
  "renameSymbol",
  "renameParam",
  "renameLocal",
  "renameField",
  "replaceCallee",
  "addImport",
  "removeImport",
  "replaceImport",
  "renameImportAlias",
  "addFunction",
  "addParam",
  "removeParam",
  "removeFunction",
  "changeReturnType",
  "changeParamType",
  "changeFieldType",
  "changeLocalType",
];
const requiredProofPurposes = [
  "source-check",
  "protocol-read",
  "version-read",
  "skills-read",
  "abi-audit",
  "clean-audit",
  "dev-plan",
  "doc-audit",
  "doctor",
  "graph-check",
  "graph-find",
  "graph-impact",
  "graph-slice",
  "graph-compare",
  "graph-import",
  "graph-roundtrip",
  "graph-size",
  "graph-view",
  "artifact-validate",
  "explain",
  "memory-audit",
  "tokens",
  "parse",
  "format",
  "repair-plan",
  "graph-test",
  "target-selection",
  "time-audit",
  "rollback-source-check",
  "rollback-graph-check",
  "rollback-artifact-validate",
];
const requiredGateSummaryFields = [
  "proofCommandsRequired",
  "proofPurposes",
  "proofResultFieldsCatalogAuditable",
  "proofReceiptPolicyAuditable",
  "objectiveContractAuditable",
  "proofReceiptReplayOperational",
  "diagnosticEntrypointResultFields",
  "diagnosticEntrypointRollbackActionFields",
  "diagnosticGraphLookupCommands",
  "diagnosticGraphLookupTargetProfilePreserved",
  "diagnosticGraphLookupRepairFallback",
  "diagnosticCheckReadPolicyAuditable",
  "checkFailureRepairFollowupsStructured",
  "evalCases",
  "evalSuccessRate",
  "operationLevelRetryContract",
  "checkedGraphEditEntrypointAudit",
  "graphPatchFailureMatrixComplete",
  "graphPatchFailureRetryCommandContracts",
  "graphPatchInvalidPatchInputRetryStructured",
  "graphPatchInlineCommandArgsPreserved",
  "repairTransactionFailureMatrixComplete",
  "repairTransactionRetryCommandContracts",
  "repairTransactionProofLedgerStructured",
  "repairRollbackActionAuditable",
  "repairLoopProofLedgerAuditable",
  "repairLoopTargetProfilePreserved",
  "transactionFailureRetryTargetProfilePreserved",
  "rollbackProofCoverage",
  "graphPatchProofCommandsReplayed",
  "graphPatchProofReplayClassified",
  "graphPatchProofLedgerStructured",
  "graphPatchRollbackActionAuditable",
  "graphPatchRollbackProofReplayPassed",
  "graphPatchEvidenceBindingsAuditable",
  "graphPatchVerificationResultFieldsAuditable",
  "repairPlanCandidateOperations",
  "repairPlanCandidateContracts",
  "repairPlanCompleteCandidateContracts",
  "repairPlanCandidateEvidenceBacked",
  "repairPlanCandidateTargetProfilePreserved",
  "semanticEditOperations",
  "semanticEditCategories",
  "semanticEditCheckedPreconditions",
  "semanticEditRequiredPreconditions",
  "semanticEditOperationSurfaceBacked",
  "semanticEditSourceBackedTransactions",
  "semanticEditConflictRejections",
  "semanticEditCheckedSourceRewrite",
  "sourceUnderstandingProofCommands",
  "sourceUnderstandingStructuredFields",
  "sourceUnderstandingTokenParseFollowupStructured",
  "sourceUnderstandingGraphFollowupStructured",
  "sourceUnderstandingFormatFailureFollowupStructured",
  "sourceUnderstandingFormattedStable",
  "semanticGraphLookupCommandContracts",
  "semanticGraphQueryReplayable",
  "semanticGraphQueryTargetProfilePreserved",
  "semanticGraphQueryReadPolicyAuditable",
  "semanticGraphLookupIdentityBound",
  "graphFindFollowupSliceCommandStructured",
  "graphFindAmbiguousSliceFollowupStructured",
  "graphFindNotFoundInspectFollowupStructured",
  "graphInspectSliceFollowupStructured",
  "graphInspectReadPolicyAuditable",
  "graphImpactPatchFollowupStructured",
  "graphImpactMissingNodeInspectFollowupStructured",
  "graphSliceImpactFollowupStructured",
  "graphSlicePatchFollowupStructured",
  "graphSliceMissingNodeInspectFollowupStructured",
  "graphCompareViewFollowupStructured",
  "graphCompareUnstableViewFollowupStructured",
  "commandFailureRetryStructured",
  "missingGraphArtifactRecoveryStructured",
  "graphDumpWritePolicyAuditable",
  "graphDumpViewFollowupStructured",
  "graphUnsupportedOutRetryStructured",
  "graphArtifactWritePolicyAuditable",
  "graphArtifactImportFollowupsStructured",
  "graphArtifactRoundtripFollowupStructured",
  "graphArtifactValidateViewFollowupStructured",
  "graphArtifactViewCheckFollowupStructured",
  "graphArtifactCheckSizeFollowupStructured",
  "buildSystemCommandReplayable",
  "artifactWritePolicyAuditable",
  "artifactRollbackActionAuditable",
  "buildTestFollowupStructured",
  "buildFailureRecoveryStructured",
  "graphBuildFailureRecoveryStructured",
  "graphArtifactCommandRerouteStructured",
  "graphBuildSizeFollowupStructured",
  "graphBuildTestFollowupStructured",
  "sizeBuildFollowupStructured",
  "runJsonUnsupportedFollowupStructured",
  "graphShipSizeFollowupStructured",
  "graphShipTestFollowupStructured",
  "sourceShipSizeFollowupStructured",
  "sourceShipTestFollowupStructured",
  "buildSystemArtifactEvidenceMatched",
  "testFailureInspectFollowupStructured",
  "testNoTestsDocAuditFollowupStructured",
  "testSystemFailureEvidenceStructured",
  "testSystemGraphEvidenceBound",
  "buildSystemTargetProfilePreserved",
  "buildSystemSafetyFactsStructured",
  "buildSystemProductionReadinessAuditable",
  "buildSystemCacheAuditStructured",
  "buildSystemProgramGraphCacheBound",
  "agentPermissionBoundaryStructured",
  "targetSelectionJsonReplayable",
  "targetSelectionDoctorFollowupStructured",
  "doctorTargetSelectionFollowupStructured",
  "destructiveCleanPolicyAuditable",
  "destructiveCleanFailureRetryStructured",
  "newProjectWritePolicyAuditable",
  "newProjectFailureRetryStructured",
  "newProjectVerificationAuditable",
  "hostTargetReadinessStructured",
  "hostTargetCommandReplayable",
  "auxiliaryAgentGuidanceStructured",
  "runtimeAuditGraphFollowupStructured",
  "explainRepairPlanResultContractAuditable",
  "docSymbolGraphFindFollowupStructured",
  "auxiliaryAgentCommandReplayable",
  "skillsGuidanceCommandReplayable",
  "protocolManifestCommandReplayable",
  "authoringReplayOperational",
  "authoringReplayProofCommands",
  "autoRepairBuildReplayOperational",
  "autoRepairBuildReplayProofCommands",
  "rollbackReplayOperational",
  "rollbackReplayProofCommands",
  "objectiveAuditOperational",
  "objectiveAuditEvidenceItems",
  "versionProvenanceCommandReplayable",
  "fullSourceReadRequiredForGraphLookup",
  "tokenSavingSampleRatio",
  "tokenSavingFullInspectRatio",
  "agentNativeAdvantageScore",
  "agentNativeReliableEdits",
  "agentNativeTokenEfficient",
  "agentNativeHallucinationResistant",
  "agentNativeAutoRepairable",
  "agentNativeVerifiableBuildSystem",
  "metricsBudgetOk",
  "metricsViolations",
];
const requiredObjectiveRequirements = [
  "humanReadableSource",
  "semanticGraphUnderstanding",
  "compilerMediatedModification",
  "compilerMediatedRepair",
  "refactoringSurface",
  "verifiableBuildAndTest",
  "auditableProofReceipts",
  "rollbackProven",
  "tokenEfficientGraphProtocol",
  "hallucinationResistance",
  "autoRepairToBuild",
  "protocolNotJustLanguage",
];
const requiredCheckedGraphEditFields = [
  "agentTransaction.failure.retryCommands",
  "evidenceBindings[]",
  "evidenceBindings[].sourceFields",
  "agentTransaction.rollback.verificationCommands[].resultFields",
  "agentTransaction.verificationCommands[].resultFields",
  "operations[].retryCommands",
  "operations[].retryCommands[].purpose",
  "agentTransaction.proofLedger",
  "agentTransaction.proofLedger.phases[].phase",
  "agentTransaction.proofLedger.phases[].status",
  "agentTransaction.proofLedger.phases[].evidenceFields",
  "agentTransaction.proofLedger.phases[].requiredPurposes",
  "saved.graphHash",
  "saveProof.semanticStable",
  "saveProof.comparedGraphHash",
];

assert.equal(manifest.schemaVersion, 1);
assert.equal(manifest.kind, "agent-protocol-manifest");
assert.equal(manifest.protocolVersion, "agent-programming-protocol-v1");
assert.equal(manifest.agentCommand.schemaVersion, 1);
assert.equal(manifest.agentCommand.kind, "agent-protocol-command-contract");
assert.deepEqual(manifest.agentCommand.command.argv, ["zero", "agent", "protocol", "--json"]);
assert.equal(manifest.agentCommand.readPolicy.name, "compiler-owned-agent-protocol");
assert.equal(manifest.agentCommand.readPolicy.writesSource, false);
assert.equal(manifest.agentCommand.readPolicy.writesArtifacts, false);
assert.equal(manifest.agentCommand.readPolicy.versionMatchedToCompiler, true);
assert(manifest.agentCommand.auditFields.includes("protocolVersion"));
assert(manifest.agentCommand.auditFields.includes("proofPurposes"));
assert(manifest.agentCommand.auditFields.includes("proofReceiptPolicy"));
assert(manifest.agentCommand.auditFields.includes("objectiveContract"));
assert(manifest.agentCommand.auditFields.includes("localGates[].command.argv"));
assert(manifest.agentCommand.auditFields.includes("agentCommand.readPolicy"));
assert.equal(manifest.agentCommand.verificationCommands[0].purpose, "protocol-read");
assert.deepEqual(manifest.agentCommand.verificationCommands[0].argv, ["zero", "agent", "protocol", "--json"]);
assert.equal(manifest.positioning.sourceOfTruth, "compiler");
assert.equal(manifest.objectiveContract.schemaVersion, 1);
assert.equal(manifest.objectiveContract.kind, "agent-native-build-system-objective-contract");
assert.equal(manifest.objectiveContract.summaryField, "objectiveAuditOperational");
assert.equal(manifest.objectiveContract.evidenceCountField, "objectiveAuditEvidenceItems");
assert.equal(manifest.objectiveContract.localGate, "agent-contracts");
assert.deepEqual(manifest.objectiveContract.requirements.map((item) => item.id), requiredObjectiveRequirements);
for (const requirement of manifest.objectiveContract.requirements) {
  assert(Array.isArray(requirement.evidenceFields), `objective requirement ${requirement.id} missing evidenceFields`);
  assert(requirement.evidenceFields.length > 0, `objective requirement ${requirement.id} has empty evidenceFields`);
}
assert(manifest.objectiveContract.requirements.find((item) => item.id === "semanticGraphUnderstanding").evidenceFields.includes("semanticGraphLookupCommandContracts"));
assert(manifest.objectiveContract.requirements.find((item) => item.id === "rollbackProven").evidenceFields.includes("rollbackReplayOperational"));
assert(manifest.objectiveContract.requirements.find((item) => item.id === "protocolNotJustLanguage").evidenceFields.includes("protocolManifestCommandReplayable"));
assert.equal(manifest.agentQuery.tokenStrategy.readFullSourceRequired, false);
assert.equal(manifest.agentQuery.repairLoop.operationRetry, "operations[].retryCommands[]");
assert(manifest.agentQuery.repairLoop.auditFields.includes("operations[].retryCommands"));
assert(manifest.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger"));
assert(manifest.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger.phases[].status"));
assert.equal(manifest.agentQuery.checkedEditSurface.transactionContract.kind, "compiler-mediated-graph-patch-transaction");
assert(manifest.agentQuery.checkedEditSurface.transactionContract.failureClasses.some((item) => item.code === "GPH005" && item.retryCommand === "operations[].retryCommands[] graph-impact"));
assert.equal(manifest.riskModel.productionReady, false);
assert.match(manifest.riskModel.agentPermissionBoundary, /external agents/);
assert.equal(manifest.permissionModel.schemaVersion, 1);
assert.equal(manifest.permissionModel.boundary, "compiler-described-agent-enforced");
assert.equal(manifest.permissionModel.commandAllowlist.owner, "external-agent");
assert(manifest.permissionModel.commandAllowlist.requiredFields.includes("agentCommand.command.argv"));
assert(manifest.permissionModel.commandAllowlist.requiredFields.includes("agentTransaction.command.argv"));
assert(manifest.permissionModel.writePolicies.some((item) => item.name === "preview-only" && item.writesSource === false && item.writesArtifacts === false));
assert(manifest.permissionModel.writePolicies.some((item) => item.name === "writes-source" && item.writesSource === true && item.requiresRollback === true));
assert(manifest.permissionModel.writePolicies.some((item) => item.name === "writes-source-tree" && item.writesSource === true && item.createsRootField === "project.path"));
assert(manifest.permissionModel.writePolicies.some((item) => item.name === "writes-artifact" && item.writesArtifacts === true && item.requiresVerification === true));
assert(manifest.permissionModel.writePolicies.some((item) => item.name === "destructive-clean" && item.deletesArtifacts === true && item.rollbackAvailable === false));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero graph patch --json" && item.policyField === "agentTransaction.rollback.savedKind"));
assert(manifest.localGates[0].summaryFields.includes("graphPatchRollbackActionAuditable"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero new --json" && item.policyField === "agentCommand.writePolicy"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero new --json" && item.rollbackField === "project.path"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero build/size/ship --json --out" && item.policyField === "agentCommand.writePolicy"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero build/size/ship --json --out" && item.rollbackField === "artifactPath"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero graph import/view/roundtrip/validate --json --out" && item.policyField === "agentCommand.writePolicy"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero graph import/view/roundtrip/validate --json --out" && item.rollbackField === "saved.path"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero graph dump --json --out" && item.policyField === "agentCommand.writePolicy"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero graph dump --json --out" && item.rollbackField === "saved.path"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero clean --json" && item.policyField === "agentCommand.writePolicy"));
assert(manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero clean --json" && item.rollbackField === null));
assert(manifest.permissionModel.externalResponsibilities.includes("sandbox process, filesystem, and network access outside compiler contracts"));
assert(manifest.localGates.some((gate) => gate.name === "agent-contracts" && gate.command.argv.includes("--summary-only")));
const agentContractsGate = manifest.localGates.find((gate) => gate.name === "agent-contracts");
assert(agentContractsGate);
for (const field of requiredGateSummaryFields) {
  assert(agentContractsGate.summaryFields.includes(field), `agent-contracts gate summaryFields missing ${field}`);
}

for (const name of requiredEntrypoints) {
  assert(manifest.entrypoints.some((entrypoint) => entrypoint.name === name), `missing entrypoint ${name}`);
}
const checkedGraphEditEntrypoint = manifest.entrypoints.find((entrypoint) => entrypoint.name === "checked-graph-edit");
assert(checkedGraphEditEntrypoint);
const semanticFindEntrypoint = manifest.entrypoints.find((entrypoint) => entrypoint.name === "semantic-find");
assert(semanticFindEntrypoint);
assert(semanticFindEntrypoint.resultFields.includes("resolution.status"));
assert(semanticFindEntrypoint.resultFields.includes("resolution.ambiguous"));
assert(manifest.agentQuery.lookupSurfaces.symbol.fields.includes("resolution.status"));
assert(manifest.agentQuery.lookupSurfaces.symbol.fields.includes("resolution.exactSymbolMatches"));
for (const field of requiredCheckedGraphEditFields) {
  assert(checkedGraphEditEntrypoint.resultFields.includes(field), `checked-graph-edit entrypoint missing result field ${field}`);
}
for (const operation of requiredOperations) {
  assert(manifest.supportedSemanticOperations.includes(operation), `missing semantic operation ${operation}`);
  assert(manifest.agentQuery.checkedEditSurface.supportedOperations.includes(operation), `agentQuery missing operation ${operation}`);
}
for (const purpose of requiredProofPurposes) {
  assert(manifest.proofPurposes.includes(purpose), `missing proof purpose ${purpose}`);
  assert(Array.isArray(manifest.proofResultFields?.[purpose]), `missing proofResultFields for ${purpose}`);
  assert(manifest.proofResultFields[purpose].length > 0, `empty proofResultFields for ${purpose}`);
}
assert(manifest.proofResultFields["source-check"].includes("diagnostics"));
assert(manifest.proofResultFields["graph-check"].includes("graphHash"));
assert(manifest.proofResultFields["artifact-validate"].some((field) => field === "artifactHash" || field === "graphHash" || field === "validation.ok"));
assert(manifest.proofResultFields["test-run"].includes("results[]"));
assert(manifest.proofResultFields["rollback-source-check"].includes("agentCommand.verificationCommands"));
assert.equal(manifest.proofReceiptPolicy.owner, "external-agent");
assert.equal(manifest.proofReceiptPolicy.source, "compiler-declared-verificationCommands");
for (const field of ["purpose", "command.argv", "exitCode", "resultFields", "observedFields"]) {
  assert(manifest.proofReceiptPolicy.requiredFields.includes(field), `proof receipt requiredFields missing ${field}`);
}
assert.equal(manifest.proofReceiptPolicy.resultFieldSource, "proofResultFields[purpose] unless verificationCommands[].resultFields overrides it");
assert(manifest.proofReceiptPolicy.bindsTo.includes("graphHash"));
assert(manifest.proofReceiptPolicy.rejectIf.includes("required command not replayed"));
for (const invariant of ["expect value when replacing existing facts", "verificationCommands replay after save"]) {
  assert(manifest.transactionInvariants.checkedEditsRequire.includes(invariant), `missing invariant ${invariant}`);
}

const proofs = await runRequiredCommands(manifest.agentCommand.verificationCommands);
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
assert.deepEqual(proofAudit.purposes, ["protocol-read"]);
assert.deepEqual(proofAudit.passedPurposes, ["protocol-read"]);

const commandAudit = {
  replayable: JSON.stringify(manifest.agentCommand.command.argv) === JSON.stringify(["zero", "agent", "protocol", "--json"]),
  readPolicyAuditable: manifest.agentCommand.readPolicy.name === "compiler-owned-agent-protocol" &&
    manifest.agentCommand.readPolicy.versionMatchedToCompiler === true &&
    manifest.agentCommand.readPolicy.writesSource === false &&
    manifest.agentCommand.readPolicy.writesArtifacts === false,
  localGateCommandAuditable: manifest.agentCommand.auditFields.includes("localGates[].command.argv") &&
    manifest.localGates.some((gate) => gate.command.argv.includes("scripts/agent-contracts-gate.mts")),
};

const report = {
  ok: true,
  kind: "agent-protocol-manifest-smoke",
  entrypoints: manifest.entrypoints.length,
  semanticOperations: manifest.supportedSemanticOperations.length,
  proofPurposes: manifest.proofPurposes.length,
  proofResultFieldPurposes: Object.keys(manifest.proofResultFields ?? {}).length,
  commandAudit,
  proofAudit,
  operationRetryContract: manifest.agentQuery.repairLoop.operationRetry === "operations[].retryCommands[]" &&
    manifest.agentQuery.checkedEditSurface.transactionContract.failureClasses.some((item) => item.code === "GPH005" && item.retryCommand === "operations[].retryCommands[] graph-impact"),
  proofResultFieldsCatalogAuditable: requiredProofPurposes.every((purpose) =>
    manifest.proofPurposes.includes(purpose) &&
    Array.isArray(manifest.proofResultFields?.[purpose]) &&
    manifest.proofResultFields[purpose].length > 0) &&
    manifest.proofResultFields["source-check"].includes("diagnostics") &&
    manifest.proofResultFields["graph-check"].includes("graphHash") &&
    manifest.proofResultFields["test-run"].includes("results[]"),
  proofReceiptPolicyAuditable: manifest.agentCommand.auditFields.includes("proofReceiptPolicy") &&
    manifest.proofReceiptPolicy?.owner === "external-agent" &&
    manifest.proofReceiptPolicy?.source === "compiler-declared-verificationCommands" &&
    manifest.proofReceiptPolicy?.requiredFields?.includes("command.argv") &&
    manifest.proofReceiptPolicy?.requiredFields?.includes("observedFields") &&
    manifest.proofReceiptPolicy?.bindsTo?.includes("graphHash") &&
    manifest.proofReceiptPolicy?.rejectIf?.includes("required command not replayed"),
  objectiveContractAuditable: manifest.objectiveContract?.schemaVersion === 1 &&
    manifest.objectiveContract?.summaryField === "objectiveAuditOperational" &&
    manifest.objectiveContract?.evidenceCountField === "objectiveAuditEvidenceItems" &&
    manifest.objectiveContract?.localGate === "agent-contracts" &&
    requiredObjectiveRequirements.every((id) =>
      manifest.objectiveContract.requirements.some((item) => item.id === id && Array.isArray(item.evidenceFields) && item.evidenceFields.length > 0)) &&
    manifest.objectiveContract.requirements.find((item) => item.id === "rollbackProven")?.evidenceFields?.includes("rollbackReplayOperational") &&
    manifest.objectiveContract.requirements.find((item) => item.id === "tokenEfficientGraphProtocol")?.evidenceFields?.includes("tokenSavingSampleRatio") &&
    manifest.objectiveContract.requirements.find((item) => item.id === "protocolNotJustLanguage")?.evidenceFields?.includes("protocolManifestCommandReplayable"),
  repairLoopProofLedgerAuditable: manifest.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger") &&
    manifest.agentQuery.repairLoop.auditFields.includes("agentTransaction.proofLedger.phases[].status"),
  repairLoopTargetProfilePreserved: Object.values(manifest.agentQuery.repairLoop.commandContracts).every((contract: any) =>
    contract.argv.includes("--target") &&
    contract.argv.includes("<same-target>") &&
    contract.argv.includes("--profile") &&
    contract.argv.includes("<same-profile>")),
  semanticGraphLookupCommandContracts: Object.values(manifest.agentQuery.lookupCommandContracts).every((contract: any) =>
    contract.argv.includes("--target") &&
    contract.argv.includes("<same-target>") &&
    contract.argv.includes("--profile") &&
    contract.argv.includes("<same-profile>")),
  checkedGraphEditResultFields: checkedGraphEditEntrypoint.resultFields.length,
  checkedGraphEditRetryFields: checkedGraphEditEntrypoint.resultFields.filter((field) => field.includes("retryCommands")),
  checkedGraphEditVerificationResultFields: checkedGraphEditEntrypoint.resultFields.filter((field) => field.includes("verificationCommands[].resultFields")),
  checkedGraphEditEvidenceFields: checkedGraphEditEntrypoint.resultFields.filter((field) => field.startsWith("evidenceBindings")),
  checkedGraphEditSaveProofFields: checkedGraphEditEntrypoint.resultFields.filter((field) => field.startsWith("saveProof.") || field === "saved.graphHash"),
  permissionBoundaryStructured: manifest.permissionModel.schemaVersion === 1 &&
    manifest.permissionModel.commandAllowlist.requiredFields.includes("agentCommand.command.argv") &&
    manifest.permissionModel.commandAllowlist.requiredFields.includes("agentTransaction.command.argv") &&
    manifest.permissionModel.writePolicies.some((item) => item.name === "preview-only" && item.writesSource === false) &&
    manifest.permissionModel.writePolicies.some((item) => item.name === "writes-source" && item.requiresRollback === true) &&
    manifest.permissionModel.writePolicies.some((item) => item.name === "writes-source-tree" && item.createsRootField === "project.path") &&
    manifest.permissionModel.writePolicies.some((item) => item.name === "writes-artifact" && item.requiresVerification === true) &&
    manifest.permissionModel.writePolicies.some((item) => item.name === "destructive-clean" && item.rollbackAvailable === false) &&
    manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero new --json" && item.policyField === "agentCommand.writePolicy" && item.rollbackField === "project.path") &&
    manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero build/size/ship --json --out" && item.policyField === "agentCommand.writePolicy" && item.rollbackField === "artifactPath") &&
    manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero graph dump --json --out" && item.policyField === "agentCommand.writePolicy" && item.rollbackField === "saved.path") &&
    manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero clean --json" && item.policyField === "agentCommand.writePolicy" && item.rollbackField === null) &&
    manifest.permissionModel.writeSurfaces.some((item) => item.command === "zero graph import/view/roundtrip/validate --json --out" && item.policyField === "agentCommand.writePolicy" && item.rollbackField === "saved.path"),
  permissionBoundaryFields: [
    "permissionModel.commandAllowlist.requiredFields",
    "permissionModel.writePolicies",
    "permissionModel.writeSurfaces",
    "permissionModel.externalResponsibilities",
  ],
  localGateSummaryFields: agentContractsGate.summaryFields.length,
  localGates: manifest.localGates.map((gate) => gate.name),
};

console.log(JSON.stringify(report, null, 2));
