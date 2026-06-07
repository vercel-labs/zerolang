import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const outDir = join(".zero", "agent-graph-repair-loop-smoke", String(process.pid));

async function zeroJson(args: string[], options: { allowFailure?: boolean } = {}) {
  const result = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 }).catch((error) => error);
  if (!options.allowFailure && result.code) throw result;
  return JSON.parse(result.stdout);
}

type AgentCommand = { purpose: string; required: boolean; argv: string[] };

async function runVerificationCommands(commands: AgentCommand[]) {
  const results = [];
  for (const command of commands) {
    assert.equal(typeof command.purpose, "string");
    assert(command.purpose.length > 0);
    assert.equal(typeof command.required, "boolean");
    if (!command.required) continue;
    assert.equal(command.argv[0], "zero");
    const result = await zeroJson(command.argv.slice(1), { allowFailure: true });
    results.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: result.ok, diagnostics: result.diagnostics ?? [] });
  }
  return results;
}

await rm(outDir, { recursive: true, force: true });
await mkdir(outDir, { recursive: true });

const workFile = join(outDir, "main.0");
await writeFile(workFile, await readFile("examples/agent-repair-demo/broken.0", "utf8"));

const audit = [];
for (let step = 1; step <= 4; step++) {
  const check = await zeroJson(["check", "--json", workFile], { allowFailure: true });
  if (check.ok) {
    audit.push({ step, state: "fixed", diagnostics: 0 });
    break;
  }

  const diagnostic = check.diagnostics[0];
  const plan = await zeroJson(["fix", "--plan", "--json", workFile]);
  const candidate = plan.fixes[0]?.graphPatchCandidates?.[0];
  assert(candidate, `missing graph patch candidate for ${diagnostic.code}`);
  assert.equal(candidate.kind, "checked-graph-patch-candidate");
  assert.equal(candidate.diagnosticCode, diagnostic.code);
  assert.equal(candidate.command.argv[0], "zero");
  assert.equal(candidate.command.argv[1], "graph");
  assert.equal(candidate.command.argv[2], "patch");
  assert.equal(candidate.command.argv[3], "--json");
  assert.equal(candidate.command.argv[4], workFile);
  assert.equal(candidate.candidateContract.kind, "checked-graph-patch-candidate-contract");
  assert.equal(candidate.candidateContract.commandField, "command.argv");
  assert.equal(candidate.candidateContract.patchTextField, "patch.text");
  assert.equal(candidate.candidateContract.requiresPatchFile, true);
  assert(candidate.candidateContract.stateFields.includes("patch.graphHash"));
  assert(candidate.candidateContract.stateFields.includes("patch.operations[].expect"));
  assert(candidate.candidateContract.auditFields.includes("evidence"));
  assert.equal(candidate.candidateContract.resultContract, "agentQuery.checkedEditSurface.transactionContract");
  assert.match(candidate.patch.graphHash, /^graph:/);
  assert.match(candidate.patch.operations[0].node, /^#/);
  assert(candidate.preconditions.some((item: string) => item.includes(diagnostic.code)));

  const patchFile = join(outDir, `step-${step}.program-graph.patch`);
  const patchText = candidate.candidateContract.patchTextField === "patch.text" ? candidate.patch.text : [];
  await writeFile(patchFile, `${patchText.join("\n")}\n`);
  const patchArgv = [...candidate.command.argv];
  patchArgv[5] = patchFile;
  const patch = await zeroJson(patchArgv.slice(1));
  assert.equal(patch.ok, true);
  assert.equal(patch.agentTransaction.kind, "compiler-mediated-graph-patch-transaction");
  assert.equal(candidate.candidateContract.resultContract, "agentQuery.checkedEditSurface.transactionContract");
  assert.equal(patch.agentTransaction.applied, true);
  assert.equal(patch.agentTransaction.rollback.savedKind, "source");
  assert.equal(patch.agentTransaction.rollback.savedPath, workFile);
  assert.equal(patch.agentTransaction.originalGraphHash, candidate.patch.graphHash);
  assert.equal(patch.operations[0].op, candidate.patch.operations[0].op);
  assert.equal(patch.operations[0].node, candidate.patch.operations[0].node);
  assert.equal(patch.operations[0].expected, candidate.patch.operations[0].expect);
  assert.equal(patch.operations[0].value, candidate.patch.operations[0].value);

  const verification = await runVerificationCommands(patch.agentTransaction.verificationCommands);
  assert.deepEqual(verification.map((item) => item.purpose), ["source-check", "graph-check"]);
  audit.push({
    step,
    diagnosticCode: diagnostic.code,
    repairId: candidate.repairId,
    graphHash: candidate.patch.graphHash,
    patchedGraphHash: patch.patchedGraphHash,
    verification,
  });
}

const fixed = await zeroJson(["check", "--json", workFile]);
assert.equal(fixed.ok, true);
assert.equal(fixed.diagnostics.length, 0);
assert.equal(audit.filter((item) => item.repairId).length, 2);
assert.equal(audit[0].diagnosticCode, "TYP009");
assert.equal(audit[0].repairId, "make-binding-mutable");
assert.equal(audit[1].diagnosticCode, "TYP002");
assert.equal(audit[1].repairId, "match-binding-annotation");
assert.equal(audit[1].verification[0].ok, true);
const verificationProofs = audit.flatMap((item) => item.verification ?? []);
const proofAudit = {
  required: verificationProofs.length,
  passed: verificationProofs.filter((item) => item.ok).length,
  failed: verificationProofs.filter((item) => !item.ok).length,
  purposes: [...new Set(verificationProofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(verificationProofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};
assert.deepEqual(proofAudit.purposes, ["graph-check", "source-check"]);
assert.deepEqual(proofAudit.passedPurposes, ["graph-check", "source-check"]);
assert.equal(proofAudit.required, 4);

console.log(JSON.stringify({ ok: true, kind: "agent-graph-repair-loop-smoke", steps: audit.length, proofAudit, audit }, null, 2));
