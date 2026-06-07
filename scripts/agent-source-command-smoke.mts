import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const defaultZeroBin = process.platform === "win32" ? ".zero\\bin\\zero.exe" : ".zero/bin/zero";
const fallbackZeroBin = process.platform === "win32" ? "bin\\zero.exe" : "bin/zero";
const zero = process.env.ZERO_BIN ?? (existsSync(defaultZeroBin) ? defaultZeroBin : existsSync(fallbackZeroBin) ? fallbackZeroBin : "");
assert(zero, `agent source command smoke requires ZERO_BIN or ${defaultZeroBin}`);

const outDir = join(".zero", "agent-source-command-smoke", String(process.pid));
const fixture = join(outDir, "hello.0");
const unformattedFixture = join(outDir, "unformatted.0");
rmSync(outDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });
writeFileSync(fixture, "pub fn main(world: World) -> Void raises {\n    check world.out.write(\"hello from zero\\n\")\n}\n");
writeFileSync(unformattedFixture, "pub fn main(world: World)->Void raises{check world.out.write(\"hello from zero\\n\")}\n");

async function zeroJson(args: string[], options: { allowFailure?: boolean } = {}) {
  const result = await execFileAsync(zero, args, { maxBuffer: 8 * 1024 * 1024 }).catch((error) => error);
  if (!options.allowFailure && result.code) throw result;
  return JSON.parse(result.stdout);
}

async function runRequiredCommands(commands: Array<{ purpose: string; required: boolean; argv: string[] }>) {
  const proofs = [];
  for (const command of commands.filter((item) => item.required)) {
    assert.equal(command.argv[0], "zero");
    const result = await execFileAsync(zero, command.argv.slice(1), { maxBuffer: 8 * 1024 * 1024 }).catch((error) => error);
    proofs.push({ purpose: command.purpose, required: command.required, argv: command.argv, ok: !result.code });
  }
  return proofs;
}

const tokens = await zeroJson(["tokens", "--json", fixture]);
const parsed = await zeroJson(["parse", "--json", fixture]);
const formatted = await zeroJson(["fmt", "--json", "--check", fixture]);
const formatFailure = await zeroJson(["fmt", "--json", "--check", unformattedFixture], { allowFailure: true });

assert.equal(tokens.agentCommand.kind, "agent-tokens-command-contract");
assert.deepEqual(tokens.agentCommand.verificationCommands.map((command) => command.purpose), ["tokens"]);
assert(tokens.agentCommand.spanFields.includes("tokens[].offset"));
assert(tokens.tokens.length > 0);
assert.equal(tokens.agentCommand.recommendedNextCommands[0].purpose, "parse");
assert.equal(tokens.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(tokens.agentCommand.recommendedNextCommands[0].when, "token spans are insufficient for declaration or semantic edit planning");
assert.equal(tokens.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(tokens.agentCommand.recommendedNextCommands[0].argv, ["zero", "parse", "--json", "<input>"]);
assert(tokens.agentCommand.recommendedNextCommands[0].resultFields.includes("root"));
assert(tokens.agentCommand.recommendedNextCommands[0].resultFields.includes("functions[]"));
assert(tokens.agentCommand.recommendedNextCommands[0].resultFields.includes("functions[].name"));
assert(tokens.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands"));
assert(tokens.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));

assert.equal(parsed.agentCommand.kind, "agent-parse-command-contract");
assert.deepEqual(parsed.agentCommand.verificationCommands.map((command) => command.purpose), ["parse"]);
assert(parsed.agentCommand.declarationFields.includes("functions[].bodyKinds"));
assert.equal(parsed.agentCommand.recommendedNextCommands[0].purpose, "graph-inspect");
assert.equal(parsed.agentCommand.recommendedNextCommands[0].required, false);
assert.equal(parsed.agentCommand.recommendedNextCommands[0].when, "declaration summary is insufficient for semantic edit planning");
assert.equal(parsed.agentCommand.recommendedNextCommands[0].inputField, "command.argv input");
assert.deepEqual(parsed.agentCommand.recommendedNextCommands[0].argv, ["zero", "graph", "inspect", "--json", "<input>"]);
assert(parsed.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.graphHash"));
assert(parsed.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.nodes[].id"));
assert(parsed.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.nodes[].nodeHash"));
assert(parsed.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.nodes[].symbolId"));
assert(parsed.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands"));
assert(parsed.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.verificationCommands"));
assert.equal(parsed.root.functionCount, 1);
assert.equal(parsed.functions[0].name, "main");

assert.equal(formatted.agentCommand.kind, "agent-fmt-command-contract");
assert.deepEqual(formatted.agentCommand.verificationCommands.map((command) => command.purpose), ["format"]);
assert.equal(formatted.check, true);
assert.equal(formatted.matches, true);
assert.equal(formatted.formattedBytes, formatted.sourceBytes);
assert.equal(formatFailure.ok, false);
assert.equal(formatFailure.matches, false);
assert.equal(formatFailure.diagnostics[0].code, "FMT001");
assert.equal(formatFailure.failure.class, "format-diff");
assert.equal(formatFailure.failure.retryCommands[0].purpose, "format-preview");
assert.deepEqual(formatFailure.failure.retryCommands[0].argv, ["zero", "fmt", "--json", unformattedFixture]);
assert.equal(formatFailure.failure.retryCommands[1].purpose, "source-check");
assert.deepEqual(formatFailure.recommendedNextCommands[0].argv, formatFailure.failure.retryCommands[0].argv);

const proofs = [
  ...await runRequiredCommands(tokens.agentCommand.verificationCommands),
  ...await runRequiredCommands(parsed.agentCommand.verificationCommands),
  ...await runRequiredCommands(formatted.agentCommand.verificationCommands),
];
const proofAudit = {
  required: proofs.length,
  passed: proofs.filter((item) => item.ok).length,
  failed: proofs.filter((item) => !item.ok).length,
  purposes: [...new Set(proofs.map((item) => item.purpose))].sort(),
  passedPurposes: [...new Set(proofs.filter((item) => item.ok).map((item) => item.purpose))].sort(),
};

assert.equal(proofAudit.required, 3);
assert.deepEqual(proofAudit.purposes, ["format", "parse", "tokens"]);
assert.deepEqual(proofAudit.passedPurposes, ["format", "parse", "tokens"]);

const sourceUnderstandingAudit = {
  tokenSpanFields: tokens.agentCommand.spanFields,
  declarationFields: parsed.agentCommand.declarationFields,
  formattedStable: formatted.check === true &&
    formatted.matches === true &&
    formatted.formattedBytes === formatted.sourceBytes,
  formatFailureFollowupStructured: formatFailure.failure.retryCommands[0].purpose === "format-preview" &&
    JSON.stringify(formatFailure.recommendedNextCommands[0].argv) === JSON.stringify(formatFailure.failure.retryCommands[0].argv) &&
    formatFailure.failure.retryCommands[1].purpose === "source-check",
  tokenParseFollowupStructured: tokens.agentCommand.recommendedNextCommands[0].purpose === "parse" &&
    tokens.agentCommand.recommendedNextCommands[0].argv.includes("<input>") &&
    tokens.agentCommand.recommendedNextCommands[0].resultFields.includes("functions[].name") &&
    tokens.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands"),
  parseGraphInspectFollowupStructured: parsed.agentCommand.recommendedNextCommands[0].purpose === "graph-inspect" &&
    parsed.agentCommand.recommendedNextCommands[0].argv.includes("<input>") &&
    parsed.agentCommand.recommendedNextCommands[0].resultFields.includes("programGraph.nodes[].nodeHash") &&
    parsed.agentCommand.recommendedNextCommands[0].resultFields.includes("agentCommand.recommendedNextCommands"),
  commandKinds: [
    tokens.agentCommand.kind,
    parsed.agentCommand.kind,
    formatted.agentCommand.kind,
  ].sort(),
};
assert(sourceUnderstandingAudit.tokenSpanFields.includes("tokens[].offset"));
assert(sourceUnderstandingAudit.tokenSpanFields.includes("tokens[].length"));
assert(sourceUnderstandingAudit.declarationFields.includes("functions[].name"));
assert(sourceUnderstandingAudit.declarationFields.includes("functions[].bodyKinds"));
assert.equal(sourceUnderstandingAudit.formattedStable, true);
assert.equal(sourceUnderstandingAudit.formatFailureFollowupStructured, true);
assert.equal(sourceUnderstandingAudit.tokenParseFollowupStructured, true);
assert.equal(sourceUnderstandingAudit.parseGraphInspectFollowupStructured, true);
assert.deepEqual(sourceUnderstandingAudit.commandKinds, [
  "agent-fmt-command-contract",
  "agent-parse-command-contract",
  "agent-tokens-command-contract",
]);

console.log(JSON.stringify({
  ok: true,
  kind: "agent-source-command-smoke",
  tokens: tokens.tokens.length,
  functions: parsed.root.functionCount,
  formattedBytes: formatted.formattedBytes,
  proofAudit,
  sourceUnderstandingAudit,
}, null, 2));
