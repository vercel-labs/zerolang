import { existsSync } from "node:fs";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const stripTypeArgs = ["--experimental-strip-types", "--disable-warning=ExperimentalWarning"];
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zeroBin = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
if (!zeroBin) throw new Error(`agent protocol smoke requires ZERO_BIN or ${defaultZeroBin}`);
const smokeScripts = [
  "scripts/agent-protocol-manifest-smoke.mts",
  "scripts/agent-proof-receipt-smoke.mts",
  "scripts/agent-repair-transaction-smoke.mts",
  "scripts/agent-graph-repair-loop-smoke.mts",
  "scripts/agent-diagnostic-graph-lookup-smoke.mts",
  "scripts/agent-recoverable-graph-smoke.mts",
  "scripts/agent-graph-identity-smoke.mts",
  "scripts/agent-graph-find-smoke.mts",
  "scripts/agent-graph-impact-smoke.mts",
  "scripts/agent-graph-slice-smoke.mts",
  "scripts/agent-graph-patch-transaction-smoke.mts",
  "scripts/agent-repair-demo.mts",
  "scripts/agent-build-command-smoke.mts",
  "scripts/agent-size-command-smoke.mts",
  "scripts/agent-ship-command-smoke.mts",
  "scripts/agent-test-command-smoke.mts",
  "scripts/agent-targets-command-smoke.mts",
  "scripts/agent-clean-command-smoke.mts",
  "scripts/agent-new-command-smoke.mts",
  "scripts/agent-version-command-smoke.mts",
  "scripts/agent-source-command-smoke.mts",
  "scripts/agent-graph-inspect-command-smoke.mts",
  "scripts/agent-graph-dump-command-smoke.mts",
  "scripts/agent-graph-compare-command-smoke.mts",
  "scripts/agent-graph-artifact-command-smoke.mts",
  "scripts/agent-explain-command-smoke.mts",
  "scripts/agent-runtime-audit-command-smoke.mts",
  "scripts/agent-dev-doc-command-smoke.mts",
  "scripts/agent-doctor-command-smoke.mts",
  "scripts/agent-skills-command-smoke.mts",
];

function parseJsonReport(stdout: string) {
  const start = stdout.indexOf("{");
  const end = stdout.lastIndexOf("}");
  if (start < 0 || end < start) return null;
  try {
    return JSON.parse(stdout.slice(start, end + 1));
  } catch {
    return null;
  }
}

const reports = [];

for (const script of smokeScripts) {
  try {
    const { stdout } = await execFileAsync(process.execPath, [...stripTypeArgs, script], {
      env: { ...process.env, ZERO_BIN: zeroBin },
      maxBuffer: 8 * 1024 * 1024,
    });
    process.stdout.write(stdout);
    const report = parseJsonReport(stdout);
    if (report) reports.push({ script, report });
  } catch (error) {
    process.stderr.write(`agent protocol smoke failed: ${script}\n`);
    if (error.stdout) process.stderr.write(error.stdout.toString());
    if (error.stderr) process.stderr.write(error.stderr.toString());
    process.exitCode = error.code && Number.isInteger(error.code) ? error.code : 1;
    break;
  }
}

if (!process.exitCode) {
  const proofAudits = reports.flatMap((item) => [
    item.report.proofAudit ? { script: item.script, ...item.report.proofAudit } : null,
    item.report.graph?.proofAudit ? { script: `${item.script}#graph`, ...item.report.graph.proofAudit } : null,
  ].filter(Boolean));
  const entrypointAudits = reports
    .filter((item) => item.report.entrypointAudit)
    .map((item) => ({ script: item.script, ...item.report.entrypointAudit }));
  const repairPlanAudits = reports
    .filter((item) => item.report.repairPlanAudit)
    .map((item) => ({ script: item.script, ...item.report.repairPlanAudit }));
  const failureAudits = reports
    .filter((item) => item.report.failureAudit)
    .map((item) => ({ script: item.script, ...item.report.failureAudit }));
  const repairProofLedgerAudit = reports
    .find((item) => item.script === "scripts/agent-repair-transaction-smoke.mts")?.report.proofLedgerAudit;
  const proofReceiptAudit = reports
    .find((item) => item.script === "scripts/agent-proof-receipt-smoke.mts")?.report;
  const protocolProofAudit = {
    reports: proofAudits.length,
    required: proofAudits.reduce((sum, item) => sum + item.required, 0),
    passed: proofAudits.reduce((sum, item) => sum + item.passed, 0),
    failed: proofAudits.reduce((sum, item) => sum + item.failed, 0),
    purposes: [...new Set(proofAudits.flatMap((item) => item.purposes ?? []))].sort(),
    passedPurposes: [...new Set(proofAudits.flatMap((item) => item.passedPurposes ?? []))].sort(),
  };
  const protocolEntrypointAudit = {
    reports: entrypointAudits.length,
    commands: entrypointAudits.reduce((sum, item) => sum + item.commands, 0),
    required: entrypointAudits.reduce((sum, item) => sum + item.required, 0),
    optional: entrypointAudits.reduce((sum, item) => sum + item.optional, 0),
    purposes: [...new Set(entrypointAudits.flatMap((item) => item.purposes ?? []))].sort(),
    resultFields: [...new Set(entrypointAudits.flatMap((item) => item.resultFields ?? []))].sort(),
  };
  if (proofAudits.length < 3) throw new Error("agent protocol proof audit should include repair, loop, and patch reports");
  if (proofReceiptAudit?.declaredFieldsMatched !== true || proofReceiptAudit?.argvBound !== true || proofReceiptAudit?.timingRecorded !== true || proofReceiptAudit?.sourceWorkflowReceipts !== true || proofReceiptAudit?.artifactWorkflowReceipts !== true || proofReceiptAudit?.graphArtifactWorkflowReceipts !== true || proofReceiptAudit?.artifactShipWorkflowReceipts !== true || proofReceiptAudit?.graphArtifactShipWorkflowReceipts !== true || proofReceiptAudit?.testWorkflowReceipts !== true || proofReceiptAudit?.graphTestWorkflowReceipts !== true || !proofReceiptAudit?.receiptPurposes?.includes("protocol-read")) throw new Error("agent protocol proof receipt replay audit failed");
  if (entrypointAudits.length < 1) throw new Error("agent protocol smoke should include diagnostic repair entrypoint reports");
  if (protocolProofAudit.required < 38) throw new Error("agent protocol proof audit lost required proof coverage");
  if (protocolEntrypointAudit.required < 4 || protocolEntrypointAudit.optional < 8) throw new Error("agent protocol lost diagnostic repair entrypoint coverage");
  if (protocolProofAudit.failed <= 0) throw new Error("agent protocol proof audit should cover recoverable failure proofs");
  const protocolRepairPlanAudit = {
    reports: repairPlanAudits.length,
    candidateOps: [...new Set(repairPlanAudits.flatMap((item) => item.candidateOps ?? []))].sort(),
    repairIds: [...new Set(repairPlanAudits.flatMap((item) => item.repairIds ?? []))].sort(),
    candidateContracts: repairPlanAudits.reduce((sum, item) => sum + (item.candidateContractAudit?.candidates ?? 0), 0),
    completeCandidateContracts: repairPlanAudits.reduce((sum, item) => sum + (item.candidateContractAudit?.completeContracts ?? 0), 0),
    completeCandidatePatchText: repairPlanAudits.reduce((sum, item) => sum + (item.candidateContractAudit?.completePatchText ?? 0), 0),
    evidenceBackedCandidateContracts: repairPlanAudits.reduce((sum, item) => sum + (item.candidateContractAudit?.evidenceBacked ?? 0), 0),
    candidateTargetProfilePreserved: repairPlanAudits.some((item) => item.candidateContractAudit?.targetProfilePreserved === true),
    candidateVerificationPurposes: [...new Set(repairPlanAudits.flatMap((item) => item.candidateContractAudit?.verificationPurposes ?? []))].sort(),
  };
  const protocolFailureAudit = {
    reports: failureAudits.length,
    declaredCodes: [...new Set(failureAudits.flatMap((item) => item.declaredCodes ?? []))].sort(),
    declaredClasses: [...new Set(failureAudits.flatMap((item) => item.declaredClasses ?? []))].sort(),
    observedCodes: [...new Set(failureAudits.flatMap((item) => item.observedCodes ?? []))].sort(),
    observedClasses: [...new Set(failureAudits.flatMap((item) => item.observedClasses ?? []))].sort(),
    observedPhases: [...new Set(failureAudits.flatMap((item) => item.observedPhases ?? []))].sort(),
    retryPurposes: [...new Set(failureAudits.flatMap((item) => item.retryPurposes ?? []))].sort(),
    operationRetryPurposes: [...new Set(failureAudits.flatMap((item) => item.operationRetryPurposes ?? []))].sort(),
    nonRetryableCodes: [...new Set(failureAudits.flatMap((item) => item.nonRetryableCodes ?? []))].sort(),
    rollbackActions: [...new Set(failureAudits.flatMap((item) => item.rollbackActions ?? []))].sort(),
    targetProfileRetryPreserved: failureAudits.length >= 2 && failureAudits.every((item) => item.targetProfileRetryPreserved === true),
    retryCommandContracts: failureAudits.length >= 2 && failureAudits.every((item) => item.retryCommandContracts === true || item.failureRetryCommandContracts === true),
    noVerifyOnFailure: failureAudits.length >= 2 && failureAudits.every((item) => item.noVerifyOnFailure === true),
  };
  for (const code of ["BLD002", "GPH001", "GPH002", "GPH003", "GPH004", "GPH005"]) if (!protocolFailureAudit.observedCodes.includes(code)) throw new Error(`agent protocol failure audit missing observed code ${code}`);
  for (const code of ["GPH001", "GPH002", "GPH003", "GPH004", "GPH005", "GPH006"]) if (!protocolFailureAudit.declaredCodes.includes(code)) throw new Error(`agent protocol failure audit missing declared code ${code}`);
  for (const failureClass of ["source-unavailable", "unsupported-repair", "edit-not-found", "write-failed"]) if (!protocolFailureAudit.declaredClasses.includes(failureClass)) throw new Error(`agent protocol failure audit missing declared class ${failureClass}`);
  for (const failureClass of ["source-unavailable", "unsupported-repair", "edit-not-found", "stale-graph", "precondition-failed"]) if (!protocolFailureAudit.observedClasses.includes(failureClass)) throw new Error(`agent protocol failure audit missing observed class ${failureClass}`);
  for (const purpose of ["source-check", "repair-plan", "graph-inspect", "graph-dump"]) if (!protocolFailureAudit.retryPurposes.includes(purpose)) throw new Error(`agent protocol failure audit missing retry purpose ${purpose}`);
  if (!protocolFailureAudit.operationRetryPurposes.includes("graph-impact")) throw new Error("agent protocol failure audit missing operation retry purpose graph-impact");
  if (!protocolFailureAudit.nonRetryableCodes.includes("GPH006")) throw new Error("agent protocol failure audit missing non-retryable GPH006");
  if (!protocolFailureAudit.rollbackActions.includes("restore-line:patches[].path:patches[].old")) throw new Error("agent protocol failure audit missing repair rollback action");
  if (!protocolFailureAudit.targetProfileRetryPreserved) throw new Error("agent protocol failure audit lost target/profile retry preservation");
  if (!protocolFailureAudit.retryCommandContracts) throw new Error("agent protocol failure audit lost retry command contracts");
  if (!protocolFailureAudit.noVerifyOnFailure) throw new Error("agent protocol failure audit should not schedule verify commands on failed transactions");
  if (repairProofLedgerAudit?.phaseOrderStructured !== true || repairProofLedgerAudit?.resultFieldsDeclared !== true || repairProofLedgerAudit?.successLedgerEvidenceFields !== true || repairProofLedgerAudit?.verificationPurposesMatched !== true || repairProofLedgerAudit?.rollbackPurposesMatched !== true || repairProofLedgerAudit?.rollbackActionsStructured !== true || repairProofLedgerAudit?.failureLedgerClassified !== true) throw new Error("agent protocol repair transaction proof ledger audit failed");
  if (!protocolRepairPlanAudit.candidateOps.includes("removeFunction")) throw new Error("agent protocol repair plan audit missing removeFunction candidate");
  if (!protocolRepairPlanAudit.repairIds.includes("remove-duplicate-declaration")) throw new Error("agent protocol repair plan audit missing duplicate declaration repair");
  if (protocolRepairPlanAudit.candidateContracts < 10) throw new Error("agent protocol repair plan audit lost checked graph patch candidates");
  if (protocolRepairPlanAudit.completeCandidateContracts !== protocolRepairPlanAudit.candidateContracts) throw new Error("agent protocol repair plan audit lost complete candidate contracts");
  if (protocolRepairPlanAudit.completeCandidatePatchText !== protocolRepairPlanAudit.candidateContracts) throw new Error("agent protocol repair plan audit lost complete candidate patch text");
  if (protocolRepairPlanAudit.evidenceBackedCandidateContracts < 10) throw new Error("agent protocol repair plan audit lost candidate evidence");
  if (!protocolRepairPlanAudit.candidateTargetProfilePreserved) throw new Error("agent protocol repair plan audit lost candidate target/profile preservation");
  for (const purpose of ["source-check", "graph-check"]) if (!protocolRepairPlanAudit.candidateVerificationPurposes.includes(purpose)) throw new Error(`agent protocol repair plan audit missing candidate verification purpose ${purpose}`);
  for (const purpose of ["source-check", "abi-audit", "clean-audit", "dev-plan", "doc-audit", "doctor", "explain", "graph-check", "graph-find", "graph-impact", "graph-slice", "graph-compare", "graph-import", "graph-roundtrip", "graph-size", "graph-test", "graph-view", "artifact-validate", "format", "graph-dump", "graph-inspect", "memory-audit", "parse", "protocol-read", "repair-plan", "rollback-source-check", "rollback-graph-check", "rollback-artifact-validate", "size-analysis", "skills-read", "target-selection", "test-run", "time-audit", "tokens", "version-read"]) {
    if (!protocolProofAudit.purposes.includes(purpose)) throw new Error(`agent protocol proof audit missing purpose ${purpose}`);
  }
  for (const purpose of ["source-check", "abi-audit", "clean-audit", "dev-plan", "doc-audit", "doctor", "explain", "graph-check", "graph-find", "graph-impact", "graph-slice", "graph-compare", "graph-dump", "graph-import", "graph-inspect", "graph-roundtrip", "graph-size", "graph-test", "graph-view", "artifact-validate", "format", "memory-audit", "parse", "protocol-read", "rollback-source-check", "rollback-graph-check", "rollback-artifact-validate", "size-analysis", "skills-read", "target-selection", "test-run", "time-audit", "tokens", "version-read"]) {
    if (!protocolProofAudit.passedPurposes.includes(purpose)) throw new Error(`agent protocol proof audit missing passed purpose ${purpose}`);
  }
  for (const purpose of ["repair-plan", "repair-patch", "repair-apply"]) {
    if (!protocolEntrypointAudit.purposes.includes(purpose)) throw new Error(`agent protocol entrypoint audit missing purpose ${purpose}`);
  }
  for (const field of ["agentTransaction.proofLedger", "agentTransaction.rollback.actions[]", "agentTransaction.rollback.verificationCommands[].purpose", "fixes[].graphPatchCandidates[].candidateContract", "fixes[].graphPatchCandidates[].patch.text", "fixes[].graphPatchCandidates[].patch.operations", "fixes[].graphPatchCandidates[].verificationCommands", "diagnostics[].graphLookup"]) {
    if (!protocolEntrypointAudit.resultFields.includes(field)) throw new Error(`agent protocol entrypoint audit missing result field ${field}`);
  }
  console.log(JSON.stringify({ ok: true, kind: "agent-protocol-smoke", scripts: smokeScripts.length, reports: reports.length, proofAudit: protocolProofAudit, proofReceiptAudit, proofLedgerAudit: repairProofLedgerAudit, entrypointAudit: protocolEntrypointAudit, repairPlanAudit: protocolRepairPlanAudit, failureAudit: protocolFailureAudit }, null, 2));
  console.log("agent protocol smoke ok");
}
