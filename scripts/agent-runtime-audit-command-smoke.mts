import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent runtime audit command smoke requires ZERO_BIN or ${defaultZeroBin}`);

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

const abi = await zeroJson(["abi", "--json", input]);
assert.equal(abi.ok, true);
assert.equal(abi.agentCommand.kind, "agent-abi-command-contract");
assert.deepEqual(abi.agentCommand.verificationCommands.map((command) => command.purpose), ["abi-audit", "source-check"]);
assert(abi.agentCommand.auditFields.includes("primitiveLayouts"));
assert(abi.agentCommand.stateFields.includes("callingConvention"));
assert.equal(abi.diagnostics.length, 0);

const mem = await zeroJson(["mem", "--json", input]);
assert.equal(mem.agentCommand.kind, "agent-mem-command-contract");
assert.deepEqual(mem.agentCommand.verificationCommands.map((command) => command.purpose), ["memory-audit", "source-check"]);
assert(mem.agentCommand.auditFields.includes("memoryBudgets"));
assert(mem.agentCommand.stateFields.includes("memory.linearMemory"));
assert.equal(mem.agentCommand.recommendedNextCommands[0].purpose, "graph-inspect");
assert.equal(mem.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(mem.agentCommand.recommendedNextCommands[0].when, "memory, allocator, or capability facts need semantic source attribution");
assert.equal(mem.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(mem.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "inspect", "--json", "--target", "<same-target>", "--profile", "<same-profile>", "<input>"]);
assert(mem.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.graphHash"));
assert(mem.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.nodes[].id"));
assert(mem.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.nodes[].nodeHash"));
assert(mem.agentCommand.recommendedNextCommands[0].resultFields.includes("agentLookupIndexes.capabilities[]"));
assert(mem.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands"));
assert(mem.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.equal(mem.mir.valid, true);
assert.equal(mem.memory.hiddenHeapAllocation, false);
assert.equal(mem.memoryBudgets.hiddenHeapAllocation, false);

const time = await zeroJson(["time", "--json", input]);
assert.equal(time.agentCommand.kind, "agent-time-command-contract");
assert.deepEqual(time.agentCommand.verificationCommands.map((command) => command.purpose), ["time-audit", "source-check"]);
assert(time.agentCommand.auditFields.includes("compilerPhases"));
assert(time.agentCommand.stateFields.includes("interfaceFingerprints.modules[].publicInterfaceHash"));
assert(time.compilerPhases.length > 0);
assert.equal(time.incrementalInvalidation.partialDiagnosticsStable, true);

const proofs = [
  ...await runRequiredCommands(abi.agentCommand.verificationCommands),
  ...await runRequiredCommands(mem.agentCommand.verificationCommands),
  ...await runRequiredCommands(time.agentCommand.verificationCommands),
];
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 6);
assert.deepEqual(proofAudit.purposes, ["abi-audit", "memory-audit", "source-check", "time-audit"]);
assert.deepEqual(proofAudit.passedPurposes, ["abi-audit", "memory-audit", "source-check", "time-audit"]);

const stateAudit = {
  abiCommandReplayable: JSON.stringify(abi.agentCommand.command.argv) ===
    JSON.stringify(abi.agentCommand.verificationCommands.find((command) => command.purpose === "abi-audit")?.argv),
  memoryCommandReplayable: JSON.stringify(mem.agentCommand.command.argv) ===
    JSON.stringify(mem.agentCommand.verificationCommands.find((command) => command.purpose === "memory-audit")?.argv),
  timeCommandReplayable: JSON.stringify(time.agentCommand.command.argv) ===
    JSON.stringify(time.agentCommand.verificationCommands.find((command) => command.purpose === "time-audit")?.argv),
  abiStateStructured: abi.agentCommand.auditFields.includes("primitiveLayouts") &&
    abi.agentCommand.stateFields.includes("callingConvention") &&
    Array.isArray(abi.diagnostics),
  memoryStateStructured: mem.agentCommand.auditFields.includes("memoryBudgets") &&
    mem.agentCommand.stateFields.includes("memory.linearMemory") &&
    mem.mir.valid === true &&
    mem.memoryBudgets.hiddenHeapAllocation === false,
  memoryGraphInspectFollowupStructured: mem.agentCommand.recommendedNextCommands[0].purpose === "graph-inspect" &&
    mem.agentCommand.recommendedNextCommands[0].argv.includes("<same-target>") &&
    mem.agentCommand.recommendedNextCommands[0].argv.includes("<same-profile>") &&
    mem.agentCommand.recommendedNextCommands[0].resultFields.includes("agentLookupIndexes.capabilities[]") &&
    mem.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands"),
  timeStateStructured: time.agentCommand.auditFields.includes("compilerPhases") &&
    time.agentCommand.stateFields.includes("interfaceFingerprints.modules[].publicInterfaceHash") &&
    time.compilerPhases.length > 0 &&
    time.incrementalInvalidation.partialDiagnosticsStable === true,
};
assert.equal(stateAudit.abiCommandReplayable, true);
assert.equal(stateAudit.memoryCommandReplayable, true);
assert.equal(stateAudit.timeCommandReplayable, true);
assert.equal(stateAudit.abiStateStructured, true);
assert.equal(stateAudit.memoryStateStructured, true);
assert.equal(stateAudit.memoryGraphInspectFollowupStructured, true);
assert.equal(stateAudit.timeStateStructured, true);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-runtime-audit-command-smoke",
  target: abi.target,
  abi: { diagnostics: abi.diagnostics.length, callingConvention: abi.callingConvention ?? null },
  memory: { hiddenHeapAllocation: mem.memory.hiddenHeapAllocation, mirValid: mem.mir.valid },
  time: { phases: time.compilerPhases.length, partialDiagnosticsStable: time.incrementalInvalidation.partialDiagnosticsStable },
  stateAudit,
  proofAudit,
}, null, 2));
