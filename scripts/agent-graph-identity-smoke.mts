import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN ?? "bin/zero";
const outDir = join(".zero", "agent-graph-identity-smoke");

type GraphNode = {
  id: string;
  kind: string;
  name?: string;
  type?: string;
  value?: string;
  symbolId?: string;
  nodeHash?: string;
};

type GraphJson = {
  graphHash: string;
  nodes: GraphNode[];
};

async function graph(path: string): Promise<GraphJson> {
  const { stdout } = await execFileAsync(zero, ["graph", "dump", "--json", path], { maxBuffer: 4 * 1024 * 1024 });
  return JSON.parse(stdout) as GraphJson;
}

function node(graphJson: GraphJson, predicate: (node: GraphNode) => boolean): GraphNode {
  const found = graphJson.nodes.find(predicate);
  assert(found);
  return found;
}

await rm(outDir, { recursive: true, force: true });
await mkdir(outDir, { recursive: true });

const basePath = join(outDir, "base.0");
const baseSource = `pub fn main(world: World) -> Void raises {
    check world.out.write("hello graph identity\\n")
}
`;

await writeFile(basePath, baseSource);
const base = await graph(basePath);
await writeFile(basePath, `fn helper() -> i32 {
    return 1
}

${baseSource}`);
const sibling = await graph(basePath);
await writeFile(basePath, `
pub fn main(world: World) -> Void raises {
        check world.out.write("hello graph identity\\n")
}
`);
const whitespace = await graph(basePath);

const baseMain = node(base, (item) => item.kind === "Function" && item.name === "main");
const baseLiteral = node(base, (item) => item.kind === "Literal" && item.type === "String" && item.value === "hello graph identity\n");
const siblingMain = node(sibling, (item) => item.kind === "Function" && item.name === "main");
const siblingLiteral = node(sibling, (item) => item.kind === "Literal" && item.type === "String" && item.value === "hello graph identity\n");
const whitespaceMain = node(whitespace, (item) => item.kind === "Function" && item.name === "main");
const whitespaceLiteral = node(whitespace, (item) => item.kind === "Literal" && item.type === "String" && item.value === "hello graph identity\n");

assert.equal(siblingMain.id, baseMain.id);
assert.equal(siblingMain.symbolId, baseMain.symbolId);
assert.equal(siblingMain.nodeHash, baseMain.nodeHash);
assert.equal(siblingLiteral.id, baseLiteral.id);
assert.equal(siblingLiteral.nodeHash, baseLiteral.nodeHash);
assert.equal(whitespaceMain.id, baseMain.id);
assert.equal(whitespaceMain.symbolId, baseMain.symbolId);
assert.equal(whitespaceLiteral.id, baseLiteral.id);
assert.notEqual(sibling.graphHash, base.graphHash);
assert.equal(whitespace.graphHash, base.graphHash);

console.log("agent graph identity smoke ok");
