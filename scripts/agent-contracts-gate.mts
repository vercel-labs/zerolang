import { existsSync } from "node:fs";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const nodeMajor = Number(process.versions.node.split(".")[0]);
if (nodeMajor < 24) {
  console.log(JSON.stringify({
    schemaVersion: 1,
    kind: "agent-contracts-gate",
    ok: false,
    failure: {
      code: "AGENT_ENV_NODE_VERSION",
      expected: "Node >=24",
      actual: process.versions.node,
      retry: "run with the repository Node 24 toolchain or put Node >=24 first on PATH",
    },
  }, null, 2));
  process.exit(1);
}
const stripTypeArgs = ["--experimental-strip-types", "--disable-warning=ExperimentalWarning"];
const summaryOnly = process.argv.includes("--summary-only") || process.env.AGENT_CONTRACTS_SUMMARY_ONLY === "1";
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zeroBin = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
if (!zeroBin) {
  console.log(JSON.stringify({
    schemaVersion: 1,
    kind: "agent-contracts-gate",
    ok: false,
    failure: {
      code: "AGENT_ENV_ZERO_BIN",
      expected: defaultZeroBin,
      actual: null,
      retry: "build the native compiler first with `make -C native/zero-c` or set ZERO_BIN",
    },
  }, null, 2));
  process.exit(1);
}
const gates = [
  { name: "agent-protocol", script: "scripts/agent-protocol-smoke.mts" },
  { name: "agent-eval", script: "scripts/agent-protocol-eval.mts" },
  { name: "command-contracts", script: "scripts/snapshot-command-contracts.mts", env: { ZERO_NATIVE_TEST_ALLOW_LOCAL: "1" } },
  { name: "compiler-metrics", script: "scripts/compiler-metrics.mts" },
];

const results = [];
const reports = [];

function jsonObjects(stdout: string) {
  const out = [];
  let start = -1, depth = 0, inString = false, escaped = false;
  for (let i = 0; i < stdout.length; i++) {
    const ch = stdout[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch === "\\") escaped = true;
      else if (ch === "\"") inString = false;
      continue;
    }
    if (ch === "\"") inString = true;
    else if (ch === "{") { if (depth === 0) start = i; depth++; }
    else if (ch === "}" && depth > 0) {
      depth--;
      if (depth === 0 && start >= 0) {
        try { out.push(JSON.parse(stdout.slice(start, i + 1))); } catch {}
      }
    }
  }
  return out;
}

for (const gate of gates) {
  const env = { ...process.env, ZERO_BIN: zeroBin, ...gate.env };
  const started = Date.now();
  try {
    const { stdout, stderr } = await execFileAsync(process.execPath, [...stripTypeArgs, gate.script], { env, maxBuffer: 32 * 1024 * 1024 });
    if (!summaryOnly) {
      process.stdout.write(stdout);
      if (stderr) process.stderr.write(stderr);
    }
    reports.push(...jsonObjects(stdout).map((report) => ({ gate: gate.name, report })));
    results.push({ name: gate.name, ok: true, durationMs: Date.now() - started });
  } catch (error) {
    if (!summaryOnly) {
      if (error.stdout) process.stdout.write(error.stdout.toString());
      if (error.stderr) process.stderr.write(error.stderr.toString());
    }
    if (error.stdout) reports.push(...jsonObjects(error.stdout.toString()).map((report) => ({ gate: gate.name, report })));
    results.push({ name: gate.name, ok: false, durationMs: Date.now() - started, code: error.code ?? 1 });
    process.exitCode = Number.isInteger(error.code) ? error.code : 1;
    break;
  }
}

const protocol = reports.find((item) => item.report.kind === "agent-protocol-smoke")?.report;
const evalReport = reports.find((item) => item.report.kind === "agent-protocol-eval")?.report;
const metrics = reports.find((item) => item.report.schema === 1 && item.report.budget)?.report;
const summary = {
  proofCommandsRequired: protocol?.proofAudit?.required ?? null,
  proofPurposes: protocol?.proofAudit?.purposes ?? [],
  proofResultFieldsCatalogAuditable: evalReport?.summary?.proofResultFieldsCatalogAuditable ?? null,
  proofReceiptPolicyAuditable: evalReport?.summary?.proofReceiptPolicyAuditable ?? null,
  objectiveContractAuditable: evalReport?.summary?.objectiveContractAuditable ?? null,
  proofReceiptReplayOperational: evalReport?.summary?.proofReceiptReplayOperational ?? null,
  diagnosticEntrypointResultFields: protocol?.entrypointAudit?.resultFields?.length ?? null,
  diagnosticEntrypointRollbackActionFields: evalReport?.summary?.diagnosticEntrypointRollbackActionFields ?? null,
  evalCases: evalReport?.summary?.cases ?? null,
  evalSuccessRate: evalReport?.summary?.successRate ?? null,
  protocolManifestCommandReplayable: evalReport?.summary?.protocolManifestCommandReplayable ?? null,
  authoringReplayOperational: evalReport?.summary?.authoringReplayOperational ?? null,
  authoringReplayProofCommands: evalReport?.summary?.authoringReplayProofCommands ?? null,
  autoRepairBuildReplayOperational: evalReport?.summary?.autoRepairBuildReplayOperational ?? null,
  autoRepairBuildReplayProofCommands: evalReport?.summary?.autoRepairBuildReplayProofCommands ?? null,
  rollbackReplayOperational: evalReport?.summary?.rollbackReplayOperational ?? null,
  rollbackReplayProofCommands: evalReport?.summary?.rollbackReplayProofCommands ?? null,
  objectiveAuditOperational: evalReport?.summary?.objectiveAuditOperational ?? null,
  objectiveAuditEvidenceItems: evalReport?.summary?.objectiveAuditEvidenceItems ?? null,
  diagnosticGraphLookupCommands: evalReport?.summary?.diagnosticGraphLookupCommands ?? null,
  diagnosticGraphLookupTargetProfilePreserved: evalReport?.summary?.diagnosticGraphLookupTargetProfilePreserved ?? null,
  diagnosticGraphLookupRepairFallback: evalReport?.summary?.diagnosticGraphLookupRepairFallback ?? null,
  diagnosticCheckReadPolicyAuditable: evalReport?.summary?.diagnosticCheckReadPolicyAuditable ?? null,
  checkFailureRepairFollowupsStructured: evalReport?.summary?.checkFailureRepairFollowupsStructured ?? null,
  operationLevelRetryContract: evalReport?.summary?.operationLevelRetryContract ?? null,
  checkedGraphEditEntrypointAudit: evalReport?.summary?.checkedGraphEditEntrypointAudit ?? null,
  graphPatchFailureMatrixComplete: evalReport?.summary?.graphPatchFailureMatrixComplete ?? null,
  graphPatchFailureRetryCommandContracts: evalReport?.summary?.graphPatchFailureRetryCommandContracts ?? null,
  graphPatchInvalidPatchInputRetryStructured: evalReport?.summary?.graphPatchInvalidPatchInputRetryStructured ?? null,
  graphPatchInlineCommandArgsPreserved: evalReport?.summary?.graphPatchInlineCommandArgsPreserved ?? null,
  repairTransactionFailureMatrixComplete: evalReport?.summary?.repairTransactionFailureMatrixComplete ?? null,
  repairTransactionRetryCommandContracts: evalReport?.summary?.repairTransactionRetryCommandContracts ?? null,
  repairTransactionProofLedgerStructured: evalReport?.summary?.repairTransactionProofLedgerStructured ?? null,
  repairRollbackActionAuditable: evalReport?.summary?.repairRollbackActionAuditable ?? null,
  repairLoopProofLedgerAuditable: evalReport?.summary?.repairLoopProofLedgerAuditable ?? null,
  repairLoopTargetProfilePreserved: evalReport?.summary?.repairLoopTargetProfilePreserved ?? null,
  transactionFailureRetryTargetProfilePreserved: evalReport?.summary?.transactionFailureRetryTargetProfilePreserved ?? null,
  rollbackProofCoverage: evalReport?.summary?.rollbackProofCoverage ?? null,
  graphPatchProofCommandsReplayed: evalReport?.summary?.graphPatchProofCommandsReplayed ?? null,
  graphPatchProofReplayClassified: evalReport?.summary?.graphPatchProofReplayClassified ?? null,
  graphPatchProofLedgerStructured: evalReport?.summary?.graphPatchProofLedgerStructured ?? null,
  graphPatchRollbackActionAuditable: evalReport?.summary?.graphPatchRollbackActionAuditable ?? null,
  graphPatchRollbackProofReplayPassed: evalReport?.summary?.graphPatchRollbackProofReplayPassed ?? null,
  graphPatchEvidenceBindingsAuditable: evalReport?.summary?.graphPatchEvidenceBindingsAuditable ?? null,
  graphPatchVerificationResultFieldsAuditable: evalReport?.summary?.graphPatchVerificationResultFieldsAuditable ?? null,
  semanticGraphLookupCommandContracts: evalReport?.summary?.semanticGraphLookupCommandContracts ?? null,
  repairPlanCandidateOperations: evalReport?.summary?.repairPlanCandidateOperations ?? null,
  repairPlanCandidateContracts: evalReport?.summary?.repairPlanCandidateContracts ?? null,
  repairPlanCompleteCandidateContracts: evalReport?.summary?.repairPlanCompleteCandidateContracts ?? null,
  repairPlanCandidateEvidenceBacked: evalReport?.summary?.repairPlanCandidateEvidenceBacked ?? null,
  repairPlanCandidateTargetProfilePreserved: evalReport?.summary?.repairPlanCandidateTargetProfilePreserved ?? null,
  semanticEditOperations: evalReport?.summary?.semanticEditOperations ?? null,
  semanticEditCategories: evalReport?.summary?.semanticEditCategories ?? null,
  semanticEditCheckedPreconditions: evalReport?.summary?.semanticEditCheckedPreconditions ?? null,
  semanticEditRequiredPreconditions: evalReport?.summary?.semanticEditRequiredPreconditions ?? null,
  semanticEditOperationSurfaceBacked: evalReport?.summary?.semanticEditOperationSurfaceBacked ?? null,
  semanticEditSourceBackedTransactions: evalReport?.summary?.semanticEditSourceBackedTransactions ?? null,
  semanticEditConflictRejections: evalReport?.summary?.semanticEditConflictRejections ?? null,
  semanticEditCheckedSourceRewrite: evalReport?.summary?.semanticEditCheckedSourceRewrite ?? null,
  sourceUnderstandingProofCommands: evalReport?.summary?.sourceUnderstandingProofCommands ?? null,
  sourceUnderstandingStructuredFields: evalReport?.summary?.sourceUnderstandingStructuredFields ?? null,
  sourceUnderstandingTokenParseFollowupStructured: evalReport?.summary?.sourceUnderstandingTokenParseFollowupStructured ?? null,
  sourceUnderstandingGraphFollowupStructured: evalReport?.summary?.sourceUnderstandingGraphFollowupStructured ?? null,
  sourceUnderstandingFormatFailureFollowupStructured: evalReport?.summary?.sourceUnderstandingFormatFailureFollowupStructured ?? null,
  sourceUnderstandingFormattedStable: evalReport?.summary?.sourceUnderstandingFormattedStable ?? null,
  semanticGraphQueryReplayable: evalReport?.summary?.semanticGraphQueryReplayable ?? null,
  semanticGraphQueryTargetProfilePreserved: evalReport?.summary?.semanticGraphQueryTargetProfilePreserved ?? null,
  semanticGraphQueryReadPolicyAuditable: evalReport?.summary?.semanticGraphQueryReadPolicyAuditable ?? null,
  semanticGraphLookupIdentityBound: evalReport?.summary?.semanticGraphLookupIdentityBound ?? null,
  graphFindFollowupSliceCommandStructured: evalReport?.summary?.graphFindFollowupSliceCommandStructured ?? null,
  graphFindAmbiguousSliceFollowupStructured: evalReport?.summary?.graphFindAmbiguousSliceFollowupStructured ?? null,
  graphFindNotFoundInspectFollowupStructured: evalReport?.summary?.graphFindNotFoundInspectFollowupStructured ?? null,
  graphInspectSliceFollowupStructured: evalReport?.summary?.graphInspectSliceFollowupStructured ?? null,
  graphInspectReadPolicyAuditable: evalReport?.summary?.graphInspectReadPolicyAuditable ?? null,
  graphImpactPatchFollowupStructured: evalReport?.summary?.graphImpactPatchFollowupStructured ?? null,
  graphImpactMissingNodeInspectFollowupStructured: evalReport?.summary?.graphImpactMissingNodeInspectFollowupStructured ?? null,
  graphSliceImpactFollowupStructured: evalReport?.summary?.graphSliceImpactFollowupStructured ?? null,
  graphSlicePatchFollowupStructured: evalReport?.summary?.graphSlicePatchFollowupStructured ?? null,
  graphSliceMissingNodeInspectFollowupStructured: evalReport?.summary?.graphSliceMissingNodeInspectFollowupStructured ?? null,
  graphCompareViewFollowupStructured: evalReport?.summary?.graphCompareViewFollowupStructured ?? null,
  graphCompareUnstableViewFollowupStructured: evalReport?.summary?.graphCompareUnstableViewFollowupStructured ?? null,
  commandFailureRetryStructured: evalReport?.summary?.commandFailureRetryStructured ?? null,
  missingGraphArtifactRecoveryStructured: evalReport?.summary?.missingGraphArtifactRecoveryStructured ?? null,
  graphDumpWritePolicyAuditable: evalReport?.summary?.graphDumpWritePolicyAuditable ?? null,
  graphDumpViewFollowupStructured: evalReport?.summary?.graphDumpViewFollowupStructured ?? null,
  graphUnsupportedOutRetryStructured: evalReport?.summary?.graphUnsupportedOutRetryStructured ?? null,
  graphArtifactWritePolicyAuditable: evalReport?.summary?.graphArtifactWritePolicyAuditable ?? null,
  graphArtifactImportFollowupsStructured: evalReport?.summary?.graphArtifactImportFollowupsStructured ?? null,
  graphArtifactRoundtripFollowupStructured: evalReport?.summary?.graphArtifactRoundtripFollowupStructured ?? null,
  graphArtifactValidateViewFollowupStructured: evalReport?.summary?.graphArtifactValidateViewFollowupStructured ?? null,
  graphArtifactViewCheckFollowupStructured: evalReport?.summary?.graphArtifactViewCheckFollowupStructured ?? null,
  graphArtifactCheckSizeFollowupStructured: evalReport?.summary?.graphArtifactCheckSizeFollowupStructured ?? null,
  buildSystemCommandReplayable: evalReport?.summary?.buildSystemCommandReplayable ?? null,
  artifactWritePolicyAuditable: evalReport?.summary?.artifactWritePolicyAuditable ?? null,
  artifactRollbackActionAuditable: evalReport?.summary?.artifactRollbackActionAuditable ?? null,
  buildTestFollowupStructured: evalReport?.summary?.buildTestFollowupStructured ?? null,
  buildFailureRecoveryStructured: evalReport?.summary?.buildFailureRecoveryStructured ?? null,
  graphBuildFailureRecoveryStructured: evalReport?.summary?.graphBuildFailureRecoveryStructured ?? null,
  graphArtifactCommandRerouteStructured: evalReport?.summary?.graphArtifactCommandRerouteStructured ?? null,
  graphBuildSizeFollowupStructured: evalReport?.summary?.graphBuildSizeFollowupStructured ?? null,
  graphBuildTestFollowupStructured: evalReport?.summary?.graphBuildTestFollowupStructured ?? null,
  sizeBuildFollowupStructured: evalReport?.summary?.sizeBuildFollowupStructured ?? null,
  runJsonUnsupportedFollowupStructured: evalReport?.summary?.runJsonUnsupportedFollowupStructured ?? null,
  graphShipSizeFollowupStructured: evalReport?.summary?.graphShipSizeFollowupStructured ?? null,
  graphShipTestFollowupStructured: evalReport?.summary?.graphShipTestFollowupStructured ?? null,
  sourceShipSizeFollowupStructured: evalReport?.summary?.sourceShipSizeFollowupStructured ?? null,
  sourceShipTestFollowupStructured: evalReport?.summary?.sourceShipTestFollowupStructured ?? null,
  buildSystemArtifactEvidenceMatched: evalReport?.summary?.buildSystemArtifactEvidenceMatched ?? null,
  testFailureInspectFollowupStructured: evalReport?.summary?.testFailureInspectFollowupStructured ?? null,
  testNoTestsDocAuditFollowupStructured: evalReport?.summary?.testNoTestsDocAuditFollowupStructured ?? null,
  testSystemFailureEvidenceStructured: evalReport?.summary?.testSystemFailureEvidenceStructured ?? null,
  testSystemGraphEvidenceBound: evalReport?.summary?.testSystemGraphEvidenceBound ?? null,
  buildSystemTargetProfilePreserved: evalReport?.summary?.buildSystemTargetProfilePreserved ?? null,
  buildSystemSafetyFactsStructured: evalReport?.summary?.buildSystemSafetyFactsStructured ?? null,
  buildSystemProductionReadinessAuditable: evalReport?.summary?.buildSystemProductionReadinessAuditable ?? null,
  buildSystemCacheAuditStructured: evalReport?.summary?.buildSystemCacheAuditStructured ?? null,
  buildSystemProgramGraphCacheBound: evalReport?.summary?.buildSystemProgramGraphCacheBound ?? null,
  agentPermissionBoundaryStructured: evalReport?.summary?.agentPermissionBoundaryStructured ?? null,
  targetSelectionJsonReplayable: evalReport?.summary?.targetSelectionJsonReplayable ?? null,
  targetSelectionInvalidTargetRecoveryStructured: evalReport?.summary?.targetSelectionInvalidTargetRecoveryStructured ?? null,
  targetSelectionInvalidTargetCommandArgsPreserved: evalReport?.summary?.targetSelectionInvalidTargetCommandArgsPreserved ?? null,
  targetSelectionDoctorFollowupStructured: evalReport?.summary?.targetSelectionDoctorFollowupStructured ?? null,
  doctorTargetSelectionFollowupStructured: evalReport?.summary?.doctorTargetSelectionFollowupStructured ?? null,
  destructiveCleanPolicyAuditable: evalReport?.summary?.destructiveCleanPolicyAuditable ?? null,
  destructiveCleanFailureRetryStructured: evalReport?.summary?.destructiveCleanFailureRetryStructured ?? null,
  newProjectWritePolicyAuditable: evalReport?.summary?.newProjectWritePolicyAuditable ?? null,
  newProjectFailureRetryStructured: evalReport?.summary?.newProjectFailureRetryStructured ?? null,
  newProjectVerificationAuditable: evalReport?.summary?.newProjectVerificationAuditable ?? null,
  hostTargetReadinessStructured: evalReport?.summary?.hostTargetReadinessStructured ?? null,
  hostTargetCommandReplayable: evalReport?.summary?.hostTargetCommandReplayable ?? null,
  auxiliaryAgentGuidanceStructured: evalReport?.summary?.auxiliaryAgentGuidanceStructured ?? null,
  runtimeAuditGraphFollowupStructured: evalReport?.summary?.runtimeAuditGraphFollowupStructured ?? null,
  explainRepairPlanResultContractAuditable: evalReport?.summary?.explainRepairPlanResultContractAuditable ?? null,
  docSymbolGraphFindFollowupStructured: evalReport?.summary?.docSymbolGraphFindFollowupStructured ?? null,
  auxiliaryAgentCommandReplayable: evalReport?.summary?.auxiliaryAgentCommandReplayable ?? null,
  skillsGuidanceCommandReplayable: evalReport?.summary?.skillsGuidanceCommandReplayable ?? null,
  versionProvenanceCommandReplayable: evalReport?.summary?.versionProvenanceCommandReplayable ?? null,
  fullSourceReadRequiredForGraphLookup: evalReport?.summary?.fullSourceReadRequiredForGraphLookup ?? null,
  tokenSavingSampleRatio: evalReport?.summary?.tokenSavingSampleRatio ?? null,
  tokenSavingFullInspectRatio: evalReport?.summary?.tokenSavingFullInspectRatio ?? null,
  agentNativeAdvantageScore: evalReport?.summary?.agentNativeAdvantageScore ?? null,
  agentNativeReliableEdits: evalReport?.summary?.agentNativeReliableEdits ?? null,
  agentNativeTokenEfficient: evalReport?.summary?.agentNativeTokenEfficient ?? null,
  agentNativeHallucinationResistant: evalReport?.summary?.agentNativeHallucinationResistant ?? null,
  agentNativeAutoRepairable: evalReport?.summary?.agentNativeAutoRepairable ?? null,
  agentNativeVerifiableBuildSystem: evalReport?.summary?.agentNativeVerifiableBuildSystem ?? null,
  metricsBudgetOk: metrics?.budget?.ok ?? null,
  metricsViolations: metrics?.budget?.violations?.length ?? null,
};
const requiredProofPurposes = ["abi-audit", "artifact-validate", "clean-audit", "dev-plan", "doc-audit", "doctor", "explain", "format", "graph-check", "graph-find", "graph-impact", "graph-slice", "graph-compare", "graph-dump", "graph-import", "graph-inspect", "graph-roundtrip", "graph-size", "graph-test", "graph-view", "memory-audit", "parse", "protocol-read", "repair-plan", "rollback-artifact-validate", "rollback-graph-check", "rollback-source-check", "size-analysis", "skills-read", "source-check", "target-selection", "test-run", "time-audit", "tokens", "version-read"];
const gateFailures = [
  summary.proofCommandsRequired >= 102 ? null : { field: "proofCommandsRequired", expected: ">=102", actual: summary.proofCommandsRequired },
  summary.diagnosticEntrypointResultFields >= 12 ? null : { field: "diagnosticEntrypointResultFields", expected: ">=12", actual: summary.diagnosticEntrypointResultFields },
  summary.diagnosticEntrypointRollbackActionFields === true ? null : { field: "diagnosticEntrypointRollbackActionFields", expected: true, actual: summary.diagnosticEntrypointRollbackActionFields },
  summary.evalCases >= 44 ? null : { field: "evalCases", expected: ">=44", actual: summary.evalCases },
  summary.evalSuccessRate === 1 ? null : { field: "evalSuccessRate", expected: 1, actual: summary.evalSuccessRate },
  summary.proofResultFieldsCatalogAuditable === true ? null : { field: "proofResultFieldsCatalogAuditable", expected: true, actual: summary.proofResultFieldsCatalogAuditable },
  summary.proofReceiptPolicyAuditable === true ? null : { field: "proofReceiptPolicyAuditable", expected: true, actual: summary.proofReceiptPolicyAuditable },
  summary.objectiveContractAuditable === true ? null : { field: "objectiveContractAuditable", expected: true, actual: summary.objectiveContractAuditable },
  summary.proofReceiptReplayOperational === true ? null : { field: "proofReceiptReplayOperational", expected: true, actual: summary.proofReceiptReplayOperational },
  summary.protocolManifestCommandReplayable === true ? null : { field: "protocolManifestCommandReplayable", expected: true, actual: summary.protocolManifestCommandReplayable },
  summary.authoringReplayOperational === true ? null : { field: "authoringReplayOperational", expected: true, actual: summary.authoringReplayOperational },
  summary.authoringReplayProofCommands >= 7 ? null : { field: "authoringReplayProofCommands", expected: ">=7", actual: summary.authoringReplayProofCommands },
  summary.autoRepairBuildReplayOperational === true ? null : { field: "autoRepairBuildReplayOperational", expected: true, actual: summary.autoRepairBuildReplayOperational },
  summary.autoRepairBuildReplayProofCommands >= 5 ? null : { field: "autoRepairBuildReplayProofCommands", expected: ">=5", actual: summary.autoRepairBuildReplayProofCommands },
  summary.rollbackReplayOperational === true ? null : { field: "rollbackReplayOperational", expected: true, actual: summary.rollbackReplayOperational },
  summary.rollbackReplayProofCommands >= 2 ? null : { field: "rollbackReplayProofCommands", expected: ">=2", actual: summary.rollbackReplayProofCommands },
  summary.objectiveAuditOperational === true ? null : { field: "objectiveAuditOperational", expected: true, actual: summary.objectiveAuditOperational },
  summary.objectiveAuditEvidenceItems >= 12 ? null : { field: "objectiveAuditEvidenceItems", expected: ">=12", actual: summary.objectiveAuditEvidenceItems },
  summary.diagnosticGraphLookupCommands >= 8 ? null : { field: "diagnosticGraphLookupCommands", expected: ">=8", actual: summary.diagnosticGraphLookupCommands },
  summary.diagnosticGraphLookupTargetProfilePreserved === true ? null : { field: "diagnosticGraphLookupTargetProfilePreserved", expected: true, actual: summary.diagnosticGraphLookupTargetProfilePreserved },
  summary.diagnosticGraphLookupRepairFallback === true ? null : { field: "diagnosticGraphLookupRepairFallback", expected: true, actual: summary.diagnosticGraphLookupRepairFallback },
  summary.diagnosticCheckReadPolicyAuditable === true ? null : { field: "diagnosticCheckReadPolicyAuditable", expected: true, actual: summary.diagnosticCheckReadPolicyAuditable },
  summary.checkFailureRepairFollowupsStructured === true ? null : { field: "checkFailureRepairFollowupsStructured", expected: true, actual: summary.checkFailureRepairFollowupsStructured },
  summary.operationLevelRetryContract === true ? null : { field: "operationLevelRetryContract", expected: true, actual: summary.operationLevelRetryContract },
  summary.checkedGraphEditEntrypointAudit === true ? null : { field: "checkedGraphEditEntrypointAudit", expected: true, actual: summary.checkedGraphEditEntrypointAudit },
  summary.graphPatchFailureMatrixComplete === true ? null : { field: "graphPatchFailureMatrixComplete", expected: true, actual: summary.graphPatchFailureMatrixComplete },
  summary.graphPatchFailureRetryCommandContracts === true ? null : { field: "graphPatchFailureRetryCommandContracts", expected: true, actual: summary.graphPatchFailureRetryCommandContracts },
  summary.graphPatchInvalidPatchInputRetryStructured === true ? null : { field: "graphPatchInvalidPatchInputRetryStructured", expected: true, actual: summary.graphPatchInvalidPatchInputRetryStructured },
  summary.graphPatchInlineCommandArgsPreserved === true ? null : { field: "graphPatchInlineCommandArgsPreserved", expected: true, actual: summary.graphPatchInlineCommandArgsPreserved },
  summary.repairTransactionFailureMatrixComplete === true ? null : { field: "repairTransactionFailureMatrixComplete", expected: true, actual: summary.repairTransactionFailureMatrixComplete },
  summary.repairTransactionRetryCommandContracts === true ? null : { field: "repairTransactionRetryCommandContracts", expected: true, actual: summary.repairTransactionRetryCommandContracts },
  summary.repairTransactionProofLedgerStructured === true ? null : { field: "repairTransactionProofLedgerStructured", expected: true, actual: summary.repairTransactionProofLedgerStructured },
  summary.repairRollbackActionAuditable === true ? null : { field: "repairRollbackActionAuditable", expected: true, actual: summary.repairRollbackActionAuditable },
  summary.repairLoopProofLedgerAuditable === true ? null : { field: "repairLoopProofLedgerAuditable", expected: true, actual: summary.repairLoopProofLedgerAuditable },
  summary.repairLoopTargetProfilePreserved === true ? null : { field: "repairLoopTargetProfilePreserved", expected: true, actual: summary.repairLoopTargetProfilePreserved },
  summary.transactionFailureRetryTargetProfilePreserved === true ? null : { field: "transactionFailureRetryTargetProfilePreserved", expected: true, actual: summary.transactionFailureRetryTargetProfilePreserved },
  summary.rollbackProofCoverage === true ? null : { field: "rollbackProofCoverage", expected: true, actual: summary.rollbackProofCoverage },
  summary.graphPatchProofCommandsReplayed >= 16 ? null : { field: "graphPatchProofCommandsReplayed", expected: ">=16", actual: summary.graphPatchProofCommandsReplayed },
  summary.graphPatchProofReplayClassified === true ? null : { field: "graphPatchProofReplayClassified", expected: true, actual: summary.graphPatchProofReplayClassified },
  summary.graphPatchProofLedgerStructured === true ? null : { field: "graphPatchProofLedgerStructured", expected: true, actual: summary.graphPatchProofLedgerStructured },
  summary.graphPatchRollbackActionAuditable === true ? null : { field: "graphPatchRollbackActionAuditable", expected: true, actual: summary.graphPatchRollbackActionAuditable },
  summary.graphPatchRollbackProofReplayPassed === true ? null : { field: "graphPatchRollbackProofReplayPassed", expected: true, actual: summary.graphPatchRollbackProofReplayPassed },
  summary.graphPatchEvidenceBindingsAuditable === true ? null : { field: "graphPatchEvidenceBindingsAuditable", expected: true, actual: summary.graphPatchEvidenceBindingsAuditable },
  summary.graphPatchVerificationResultFieldsAuditable === true ? null : { field: "graphPatchVerificationResultFieldsAuditable", expected: true, actual: summary.graphPatchVerificationResultFieldsAuditable },
  summary.semanticGraphLookupCommandContracts === true ? null : { field: "semanticGraphLookupCommandContracts", expected: true, actual: summary.semanticGraphLookupCommandContracts },
  summary.repairPlanCandidateOperations >= 8 ? null : { field: "repairPlanCandidateOperations", expected: ">=8", actual: summary.repairPlanCandidateOperations },
  summary.repairPlanCandidateContracts >= 10 ? null : { field: "repairPlanCandidateContracts", expected: ">=10", actual: summary.repairPlanCandidateContracts },
  summary.repairPlanCompleteCandidateContracts === true ? null : { field: "repairPlanCompleteCandidateContracts", expected: true, actual: summary.repairPlanCompleteCandidateContracts },
  summary.repairPlanCandidateEvidenceBacked >= 10 ? null : { field: "repairPlanCandidateEvidenceBacked", expected: ">=10", actual: summary.repairPlanCandidateEvidenceBacked },
  summary.repairPlanCandidateTargetProfilePreserved === true ? null : { field: "repairPlanCandidateTargetProfilePreserved", expected: true, actual: summary.repairPlanCandidateTargetProfilePreserved },
  summary.semanticEditOperations >= 17 ? null : { field: "semanticEditOperations", expected: ">=17", actual: summary.semanticEditOperations },
  summary.semanticEditCategories >= 5 ? null : { field: "semanticEditCategories", expected: ">=5", actual: summary.semanticEditCategories },
  summary.semanticEditCheckedPreconditions >= 5 ? null : { field: "semanticEditCheckedPreconditions", expected: ">=5", actual: summary.semanticEditCheckedPreconditions },
  summary.semanticEditRequiredPreconditions === true ? null : { field: "semanticEditRequiredPreconditions", expected: true, actual: summary.semanticEditRequiredPreconditions },
  summary.semanticEditOperationSurfaceBacked === true ? null : { field: "semanticEditOperationSurfaceBacked", expected: true, actual: summary.semanticEditOperationSurfaceBacked },
  summary.semanticEditSourceBackedTransactions >= 18 ? null : { field: "semanticEditSourceBackedTransactions", expected: ">=18", actual: summary.semanticEditSourceBackedTransactions },
  summary.semanticEditConflictRejections >= 22 ? null : { field: "semanticEditConflictRejections", expected: ">=22", actual: summary.semanticEditConflictRejections },
  summary.semanticEditCheckedSourceRewrite === true ? null : { field: "semanticEditCheckedSourceRewrite", expected: true, actual: summary.semanticEditCheckedSourceRewrite },
  summary.sourceUnderstandingProofCommands >= 3 ? null : { field: "sourceUnderstandingProofCommands", expected: ">=3", actual: summary.sourceUnderstandingProofCommands },
  summary.sourceUnderstandingStructuredFields === true ? null : { field: "sourceUnderstandingStructuredFields", expected: true, actual: summary.sourceUnderstandingStructuredFields },
  summary.sourceUnderstandingTokenParseFollowupStructured === true ? null : { field: "sourceUnderstandingTokenParseFollowupStructured", expected: true, actual: summary.sourceUnderstandingTokenParseFollowupStructured },
  summary.sourceUnderstandingGraphFollowupStructured === true ? null : { field: "sourceUnderstandingGraphFollowupStructured", expected: true, actual: summary.sourceUnderstandingGraphFollowupStructured },
  summary.sourceUnderstandingFormatFailureFollowupStructured === true ? null : { field: "sourceUnderstandingFormatFailureFollowupStructured", expected: true, actual: summary.sourceUnderstandingFormatFailureFollowupStructured },
  summary.sourceUnderstandingFormattedStable === true ? null : { field: "sourceUnderstandingFormattedStable", expected: true, actual: summary.sourceUnderstandingFormattedStable },
  summary.semanticGraphQueryReplayable === true ? null : { field: "semanticGraphQueryReplayable", expected: true, actual: summary.semanticGraphQueryReplayable },
  summary.semanticGraphQueryTargetProfilePreserved === true ? null : { field: "semanticGraphQueryTargetProfilePreserved", expected: true, actual: summary.semanticGraphQueryTargetProfilePreserved },
  summary.semanticGraphQueryReadPolicyAuditable === true ? null : { field: "semanticGraphQueryReadPolicyAuditable", expected: true, actual: summary.semanticGraphQueryReadPolicyAuditable },
  summary.semanticGraphLookupIdentityBound === true ? null : { field: "semanticGraphLookupIdentityBound", expected: true, actual: summary.semanticGraphLookupIdentityBound },
  summary.graphFindFollowupSliceCommandStructured === true ? null : { field: "graphFindFollowupSliceCommandStructured", expected: true, actual: summary.graphFindFollowupSliceCommandStructured },
  summary.graphFindAmbiguousSliceFollowupStructured === true ? null : { field: "graphFindAmbiguousSliceFollowupStructured", expected: true, actual: summary.graphFindAmbiguousSliceFollowupStructured },
  summary.graphFindNotFoundInspectFollowupStructured === true ? null : { field: "graphFindNotFoundInspectFollowupStructured", expected: true, actual: summary.graphFindNotFoundInspectFollowupStructured },
  summary.graphInspectSliceFollowupStructured === true ? null : { field: "graphInspectSliceFollowupStructured", expected: true, actual: summary.graphInspectSliceFollowupStructured },
  summary.graphInspectReadPolicyAuditable === true ? null : { field: "graphInspectReadPolicyAuditable", expected: true, actual: summary.graphInspectReadPolicyAuditable },
  summary.graphImpactPatchFollowupStructured === true ? null : { field: "graphImpactPatchFollowupStructured", expected: true, actual: summary.graphImpactPatchFollowupStructured },
  summary.graphImpactMissingNodeInspectFollowupStructured === true ? null : { field: "graphImpactMissingNodeInspectFollowupStructured", expected: true, actual: summary.graphImpactMissingNodeInspectFollowupStructured },
  summary.graphSliceImpactFollowupStructured === true ? null : { field: "graphSliceImpactFollowupStructured", expected: true, actual: summary.graphSliceImpactFollowupStructured },
  summary.graphSlicePatchFollowupStructured === true ? null : { field: "graphSlicePatchFollowupStructured", expected: true, actual: summary.graphSlicePatchFollowupStructured },
  summary.graphSliceMissingNodeInspectFollowupStructured === true ? null : { field: "graphSliceMissingNodeInspectFollowupStructured", expected: true, actual: summary.graphSliceMissingNodeInspectFollowupStructured },
  summary.graphCompareViewFollowupStructured === true ? null : { field: "graphCompareViewFollowupStructured", expected: true, actual: summary.graphCompareViewFollowupStructured },
  summary.graphCompareUnstableViewFollowupStructured === true ? null : { field: "graphCompareUnstableViewFollowupStructured", expected: true, actual: summary.graphCompareUnstableViewFollowupStructured },
  summary.commandFailureRetryStructured === true ? null : { field: "commandFailureRetryStructured", expected: true, actual: summary.commandFailureRetryStructured },
  summary.missingGraphArtifactRecoveryStructured === true ? null : { field: "missingGraphArtifactRecoveryStructured", expected: true, actual: summary.missingGraphArtifactRecoveryStructured },
  summary.graphDumpWritePolicyAuditable === true ? null : { field: "graphDumpWritePolicyAuditable", expected: true, actual: summary.graphDumpWritePolicyAuditable },
  summary.graphDumpViewFollowupStructured === true ? null : { field: "graphDumpViewFollowupStructured", expected: true, actual: summary.graphDumpViewFollowupStructured },
  summary.graphUnsupportedOutRetryStructured === true ? null : { field: "graphUnsupportedOutRetryStructured", expected: true, actual: summary.graphUnsupportedOutRetryStructured },
  summary.graphArtifactWritePolicyAuditable === true ? null : { field: "graphArtifactWritePolicyAuditable", expected: true, actual: summary.graphArtifactWritePolicyAuditable },
  summary.graphArtifactImportFollowupsStructured === true ? null : { field: "graphArtifactImportFollowupsStructured", expected: true, actual: summary.graphArtifactImportFollowupsStructured },
  summary.graphArtifactRoundtripFollowupStructured === true ? null : { field: "graphArtifactRoundtripFollowupStructured", expected: true, actual: summary.graphArtifactRoundtripFollowupStructured },
  summary.graphArtifactValidateViewFollowupStructured === true ? null : { field: "graphArtifactValidateViewFollowupStructured", expected: true, actual: summary.graphArtifactValidateViewFollowupStructured },
  summary.graphArtifactViewCheckFollowupStructured === true ? null : { field: "graphArtifactViewCheckFollowupStructured", expected: true, actual: summary.graphArtifactViewCheckFollowupStructured },
  summary.graphArtifactCheckSizeFollowupStructured === true ? null : { field: "graphArtifactCheckSizeFollowupStructured", expected: true, actual: summary.graphArtifactCheckSizeFollowupStructured },
  summary.buildSystemCommandReplayable === true ? null : { field: "buildSystemCommandReplayable", expected: true, actual: summary.buildSystemCommandReplayable },
  summary.artifactWritePolicyAuditable === true ? null : { field: "artifactWritePolicyAuditable", expected: true, actual: summary.artifactWritePolicyAuditable },
  summary.artifactRollbackActionAuditable === true ? null : { field: "artifactRollbackActionAuditable", expected: true, actual: summary.artifactRollbackActionAuditable },
  summary.buildTestFollowupStructured === true ? null : { field: "buildTestFollowupStructured", expected: true, actual: summary.buildTestFollowupStructured },
  summary.buildFailureRecoveryStructured === true ? null : { field: "buildFailureRecoveryStructured", expected: true, actual: summary.buildFailureRecoveryStructured },
  summary.graphBuildFailureRecoveryStructured === true ? null : { field: "graphBuildFailureRecoveryStructured", expected: true, actual: summary.graphBuildFailureRecoveryStructured },
  summary.graphArtifactCommandRerouteStructured === true ? null : { field: "graphArtifactCommandRerouteStructured", expected: true, actual: summary.graphArtifactCommandRerouteStructured },
  summary.graphBuildSizeFollowupStructured === true ? null : { field: "graphBuildSizeFollowupStructured", expected: true, actual: summary.graphBuildSizeFollowupStructured },
  summary.graphBuildTestFollowupStructured === true ? null : { field: "graphBuildTestFollowupStructured", expected: true, actual: summary.graphBuildTestFollowupStructured },
  summary.sizeBuildFollowupStructured === true ? null : { field: "sizeBuildFollowupStructured", expected: true, actual: summary.sizeBuildFollowupStructured },
  summary.runJsonUnsupportedFollowupStructured === true ? null : { field: "runJsonUnsupportedFollowupStructured", expected: true, actual: summary.runJsonUnsupportedFollowupStructured },
  summary.graphShipSizeFollowupStructured === true ? null : { field: "graphShipSizeFollowupStructured", expected: true, actual: summary.graphShipSizeFollowupStructured },
  summary.graphShipTestFollowupStructured === true ? null : { field: "graphShipTestFollowupStructured", expected: true, actual: summary.graphShipTestFollowupStructured },
  summary.sourceShipSizeFollowupStructured === true ? null : { field: "sourceShipSizeFollowupStructured", expected: true, actual: summary.sourceShipSizeFollowupStructured },
  summary.sourceShipTestFollowupStructured === true ? null : { field: "sourceShipTestFollowupStructured", expected: true, actual: summary.sourceShipTestFollowupStructured },
  summary.buildSystemArtifactEvidenceMatched === true ? null : { field: "buildSystemArtifactEvidenceMatched", expected: true, actual: summary.buildSystemArtifactEvidenceMatched },
  summary.testFailureInspectFollowupStructured === true ? null : { field: "testFailureInspectFollowupStructured", expected: true, actual: summary.testFailureInspectFollowupStructured },
  summary.testNoTestsDocAuditFollowupStructured === true ? null : { field: "testNoTestsDocAuditFollowupStructured", expected: true, actual: summary.testNoTestsDocAuditFollowupStructured },
  summary.testSystemFailureEvidenceStructured === true ? null : { field: "testSystemFailureEvidenceStructured", expected: true, actual: summary.testSystemFailureEvidenceStructured },
  summary.testSystemGraphEvidenceBound === true ? null : { field: "testSystemGraphEvidenceBound", expected: true, actual: summary.testSystemGraphEvidenceBound },
  summary.buildSystemTargetProfilePreserved === true ? null : { field: "buildSystemTargetProfilePreserved", expected: true, actual: summary.buildSystemTargetProfilePreserved },
  summary.buildSystemSafetyFactsStructured === true ? null : { field: "buildSystemSafetyFactsStructured", expected: true, actual: summary.buildSystemSafetyFactsStructured },
  summary.buildSystemProductionReadinessAuditable === true ? null : { field: "buildSystemProductionReadinessAuditable", expected: true, actual: summary.buildSystemProductionReadinessAuditable },
  summary.buildSystemCacheAuditStructured === true ? null : { field: "buildSystemCacheAuditStructured", expected: true, actual: summary.buildSystemCacheAuditStructured },
  summary.buildSystemProgramGraphCacheBound === true ? null : { field: "buildSystemProgramGraphCacheBound", expected: true, actual: summary.buildSystemProgramGraphCacheBound },
  summary.agentPermissionBoundaryStructured === true ? null : { field: "agentPermissionBoundaryStructured", expected: true, actual: summary.agentPermissionBoundaryStructured },
  summary.targetSelectionJsonReplayable === true ? null : { field: "targetSelectionJsonReplayable", expected: true, actual: summary.targetSelectionJsonReplayable },
  summary.targetSelectionInvalidTargetRecoveryStructured === true ? null : { field: "targetSelectionInvalidTargetRecoveryStructured", expected: true, actual: summary.targetSelectionInvalidTargetRecoveryStructured },
  summary.targetSelectionInvalidTargetCommandArgsPreserved === true ? null : { field: "targetSelectionInvalidTargetCommandArgsPreserved", expected: true, actual: summary.targetSelectionInvalidTargetCommandArgsPreserved },
  summary.targetSelectionDoctorFollowupStructured === true ? null : { field: "targetSelectionDoctorFollowupStructured", expected: true, actual: summary.targetSelectionDoctorFollowupStructured },
  summary.doctorTargetSelectionFollowupStructured === true ? null : { field: "doctorTargetSelectionFollowupStructured", expected: true, actual: summary.doctorTargetSelectionFollowupStructured },
  summary.destructiveCleanPolicyAuditable === true ? null : { field: "destructiveCleanPolicyAuditable", expected: true, actual: summary.destructiveCleanPolicyAuditable },
  summary.destructiveCleanFailureRetryStructured === true ? null : { field: "destructiveCleanFailureRetryStructured", expected: true, actual: summary.destructiveCleanFailureRetryStructured },
  summary.newProjectWritePolicyAuditable === true ? null : { field: "newProjectWritePolicyAuditable", expected: true, actual: summary.newProjectWritePolicyAuditable },
  summary.newProjectFailureRetryStructured === true ? null : { field: "newProjectFailureRetryStructured", expected: true, actual: summary.newProjectFailureRetryStructured },
  summary.newProjectVerificationAuditable === true ? null : { field: "newProjectVerificationAuditable", expected: true, actual: summary.newProjectVerificationAuditable },
  summary.hostTargetReadinessStructured === true ? null : { field: "hostTargetReadinessStructured", expected: true, actual: summary.hostTargetReadinessStructured },
  summary.hostTargetCommandReplayable === true ? null : { field: "hostTargetCommandReplayable", expected: true, actual: summary.hostTargetCommandReplayable },
  summary.auxiliaryAgentGuidanceStructured === true ? null : { field: "auxiliaryAgentGuidanceStructured", expected: true, actual: summary.auxiliaryAgentGuidanceStructured },
  summary.runtimeAuditGraphFollowupStructured === true ? null : { field: "runtimeAuditGraphFollowupStructured", expected: true, actual: summary.runtimeAuditGraphFollowupStructured },
  summary.explainRepairPlanResultContractAuditable === true ? null : { field: "explainRepairPlanResultContractAuditable", expected: true, actual: summary.explainRepairPlanResultContractAuditable },
  summary.docSymbolGraphFindFollowupStructured === true ? null : { field: "docSymbolGraphFindFollowupStructured", expected: true, actual: summary.docSymbolGraphFindFollowupStructured },
  summary.auxiliaryAgentCommandReplayable === true ? null : { field: "auxiliaryAgentCommandReplayable", expected: true, actual: summary.auxiliaryAgentCommandReplayable },
  summary.skillsGuidanceCommandReplayable === true ? null : { field: "skillsGuidanceCommandReplayable", expected: true, actual: summary.skillsGuidanceCommandReplayable },
  summary.versionProvenanceCommandReplayable === true ? null : { field: "versionProvenanceCommandReplayable", expected: true, actual: summary.versionProvenanceCommandReplayable },
  summary.fullSourceReadRequiredForGraphLookup === false ? null : { field: "fullSourceReadRequiredForGraphLookup", expected: false, actual: summary.fullSourceReadRequiredForGraphLookup },
  summary.tokenSavingSampleRatio !== null && summary.tokenSavingSampleRatio <= 0.02 ? null : { field: "tokenSavingSampleRatio", expected: "<=0.02", actual: summary.tokenSavingSampleRatio },
  summary.tokenSavingFullInspectRatio !== null && summary.tokenSavingFullInspectRatio <= 0.001 ? null : { field: "tokenSavingFullInspectRatio", expected: "<=0.001", actual: summary.tokenSavingFullInspectRatio },
  summary.agentNativeAdvantageScore === 100 ? null : { field: "agentNativeAdvantageScore", expected: 100, actual: summary.agentNativeAdvantageScore },
  summary.agentNativeReliableEdits === true ? null : { field: "agentNativeReliableEdits", expected: true, actual: summary.agentNativeReliableEdits },
  summary.agentNativeTokenEfficient === true ? null : { field: "agentNativeTokenEfficient", expected: true, actual: summary.agentNativeTokenEfficient },
  summary.agentNativeHallucinationResistant === true ? null : { field: "agentNativeHallucinationResistant", expected: true, actual: summary.agentNativeHallucinationResistant },
  summary.agentNativeAutoRepairable === true ? null : { field: "agentNativeAutoRepairable", expected: true, actual: summary.agentNativeAutoRepairable },
  summary.agentNativeVerifiableBuildSystem === true ? null : { field: "agentNativeVerifiableBuildSystem", expected: true, actual: summary.agentNativeVerifiableBuildSystem },
  summary.metricsBudgetOk === true ? null : { field: "metricsBudgetOk", expected: true, actual: summary.metricsBudgetOk },
  summary.metricsViolations === 0 ? null : { field: "metricsViolations", expected: 0, actual: summary.metricsViolations },
  ...requiredProofPurposes.map((purpose) => summary.proofPurposes.includes(purpose) ? null : { field: "proofPurposes", expected: purpose, actual: summary.proofPurposes }),
].filter(Boolean);
if (gateFailures.length > 0) process.exitCode = 1;

console.log(JSON.stringify({ schemaVersion: 1, kind: "agent-contracts-gate", ok: results.every((item) => item.ok) && gateFailures.length === 0, zeroBin, summary, gateFailures, gates: results }, null, 2));
