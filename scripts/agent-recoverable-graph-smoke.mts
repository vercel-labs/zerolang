import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const fixture = "examples/agent-repair-demo/broken.0";

type GraphNode = {
  kind: string;
  name: string;
  path: string;
  line: number;
  column: number;
};

async function zeroJson(args: string[]) {
  try {
    const { stdout } = await execFileAsync(zero, args, { maxBuffer: 16 * 1024 * 1024 });
    return { exitCode: 0, json: JSON.parse(stdout) };
  } catch (error) {
    const stdout = error.stdout?.toString() ?? "";
    return { exitCode: error.code, json: JSON.parse(stdout) };
  }
}

function assertRecoverableGraph(payload: any, expectedCheck = ["zero", "check", "--json", fixture], expectedGraphCheck = ["zero", "graph", "check", "--json", fixture]) {
  assert.equal(payload.ok, false);
  assert.equal(payload.diagnostics[0].code, "TYP009");
  assert.equal(payload.agentRecovery.kind, "recoverable-program-graph");
  assert.equal(payload.agentRecovery.available, true);
  assert.equal(payload.agentRecovery.graphCompleteness, "parse-level");
  assert(payload.agentRecovery.safeUses.some((item: string) => item.includes("recoverableProgramGraph.nodes[]")));
  assert.equal(payload.agentRecovery.verificationCommands[0].purpose, "source-check");
  assert.equal(payload.agentRecovery.verificationCommands[0].required, true);
  assert.deepEqual(payload.agentRecovery.verificationCommands[0].argv, expectedCheck);
  assert.equal(payload.agentRecovery.verificationCommands[1].purpose, "graph-check");
  assert.equal(payload.agentRecovery.verificationCommands[1].required, true);
  assert.deepEqual(payload.agentRecovery.verificationCommands[1].argv, expectedGraphCheck);
  assert.equal(payload.agentQuery.kind, "agent-program-graph-query-contract");
  assert.equal(payload.recoverableProgramGraph.validation.ok, true);
  assert.match(payload.recoverableProgramGraph.graphHash, /^graph:/);
  const diagnostic = payload.diagnostics[0];
  const matched = payload.recoverableProgramGraph.nodes.find((node: GraphNode) =>
    node.path === diagnostic.path &&
    node.line === diagnostic.graphLookup.spanMatch.line &&
    node.column === diagnostic.graphLookup.spanMatch.column
  );
  assert(matched, "diagnostic span should match a recoverable ProgramGraph node");
  assert.equal(matched.kind, "Identifier");
  assert.equal(matched.name, "dst");
}

const inspect = await zeroJson(["graph", "inspect", "--json", fixture]);
assert.equal(inspect.exitCode, 1);
assertRecoverableGraph(inspect.json);

const dump = await zeroJson(["graph", "dump", "--json", fixture]);
assert.equal(dump.exitCode, 1);
assertRecoverableGraph(dump.json);
const targetInspect = await zeroJson(["graph", "inspect", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture]);
assert.equal(targetInspect.exitCode, 1);
assertRecoverableGraph(
  targetInspect.json,
  ["zero", "check", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture],
  ["zero", "graph", "check", "--json", "--target", "linux-musl-x64", "--profile", "dev", fixture],
);

console.log("agent recoverable graph smoke ok");
