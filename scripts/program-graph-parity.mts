#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const execMaxBuffer = 16 * 1024 * 1024;
const zero = "bin/zero";
const outDir = `/tmp/zero-program-graph-parity-${process.pid}`;
const requireStableNodeIds = process.argv.includes("--require-stable-node-ids");

function firstDiagnosticCode(diagnostics) {
  return Array.isArray(diagnostics) && diagnostics.length > 0 ? diagnostics[0].code ?? null : null;
}

function targetReadinessSummary(readiness) {
  if (!readiness) return null;
  return {
    languageOk: readiness.languageOk,
    buildable: readiness.buildable,
    stage: readiness.stage,
    code: firstDiagnosticCode(readiness.diagnostics),
  };
}

async function zeroJson(args) {
  const result = await execFileAsync(zero, args, { maxBuffer: execMaxBuffer });
  return JSON.parse(result.stdout);
}

async function zeroText(args) {
  const result = await execFileAsync(zero, args, { maxBuffer: execMaxBuffer });
  return result.stdout;
}

async function dumpGraphArtifact(fixture, name) {
  const artifact = `${outDir}/${name}.program-graph`;
  await zeroText(["graph", "dump", "--out", artifact, fixture]);
  return artifact;
}

function buildSummary(result) {
  return {
    emit: result.emit,
    target: result.target,
    profile: result.profile,
    compiler: result.compiler,
    generatedCBytes: result.generatedCBytes,
    cBridgeFallback: result.cBridgeFallback,
    objectEmission: result.objectBackend?.objectEmission?.path ?? null,
    linkFormat: result.objectBackend?.linking?.objectFormat ?? null,
    directFacts: result.objectBackend?.directFacts ?? null,
  };
}

function testSummary(result) {
  return {
    ok: result.ok,
    target: result.target,
    testBackend: result.testBackend,
    selectedTests: result.selectedTests,
    discoveredTests: result.discoveredTests,
    passedTests: result.passedTests,
    failedTests: result.failedTests,
    expectedFailures: result.expectedFailures,
    unexpectedPasses: result.unexpectedPasses,
    stdout: result.stdout,
  };
}

async function assertCheckParity(fixture) {
  const source = await zeroJson(["check", "--json", fixture]);
  const graph = await zeroJson(["graph", "check", "--json", fixture]);

  assert.equal(graph.canonicalSource, true, `${fixture}: graph check should report source input`);
  assert.equal(graph.check.phase, "typecheck", `${fixture}: graph check phase`);
  assert.equal(graph.check.lowering, "direct-program-graph", `${fixture}: graph lowering`);
  assert.equal(graph.check.ok, source.ok, `${fixture}: source and graph typecheck should agree`);
  assert.equal(graph.check.target, source.targetReadiness?.target ?? graph.check.target, `${fixture}: target should agree`);
  assert.deepEqual(targetReadinessSummary(graph.targetReadiness), targetReadinessSummary(source.targetReadiness), `${fixture}: target readiness should agree`);
}

async function assertRoundtripStable(fixture) {
  const roundtrip = await zeroJson(["graph", "roundtrip", "--json", fixture]);
  assert.equal(roundtrip.ok, true, `${fixture}: graph roundtrip ok`);
  assert.equal(roundtrip.semanticStable, true, `${fixture}: graph roundtrip semantic stability`);
  assert.equal(roundtrip.lowering, "direct-program-graph", `${fixture}: graph roundtrip lowering`);
  assert.equal(roundtrip.roundtripModuleIdentity, roundtrip.moduleIdentity, `${fixture}: module identity`);
  assert.equal(roundtrip.comparison.ok, true, `${fixture}: graph semantic comparison`);
  assert.deepEqual(roundtrip.semanticCounts.original, roundtrip.semanticCounts.roundtrip, `${fixture}: semantic counts`);
}

async function assertCommandStateContracts() {
  const sourceDump = await zeroJson(["graph", "dump", "--json", "examples/hello.0"]);
  assert.equal(sourceDump.canonicalSource, true, "graph dump from source should report canonical source input");
  assert.equal(sourceDump.validation.state, "shape-valid", "graph dump should produce a shape-valid graph");
  assert.equal(sourceDump.validation.ok, true, "graph dump validation should pass");

  const artifact = await dumpGraphArtifact("examples/hello.0", "state-contracts");
  const validate = await zeroJson(["graph", "validate", "--json", artifact]);
  assert.equal(validate.ok, true, "graph validate should accept the artifact");
  assert.equal(validate.canonicalSource, false, "graph validate should report artifact input");
  assert.equal(validate.validation.state, "shape-valid", "graph validate should promise shape-valid state");
  assert.equal(validate.validation.ok, true, "graph validate state should be ok");

  const emptyLiteralArtifact = await dumpGraphArtifact("benchmarks/rosetta/empty-string.0", "empty-string-literal");
  const emptyLiteralDump = await readFile(emptyLiteralArtifact, "utf8");
  assert.match(emptyLiteralDump, /node #[^ ]+ Literal[^\n]* value:""/, "graph dump should preserve empty literal values");
  const emptyLiteralValidate = await zeroJson(["graph", "validate", "--json", emptyLiteralArtifact]);
  assert.equal(emptyLiteralValidate.ok, true, "graph validate should accept stored empty string literal artifacts");

  const view = await zeroJson(["graph", "view", "--json", artifact]);
  assert.equal(view.ok, true, "graph view should render a valid source projection");
  assert.equal(view.canonicalSource, false, "graph view should report artifact input");
  assert.match(view.view, /pub fn main/, "graph view should include canonical source text");

  const check = await zeroJson(["graph", "check", "--json", artifact]);
  assert.equal(check.ok, true, "graph check should typecheck the artifact");
  assert.equal(check.canonicalSource, false, "graph check should report artifact input");
  assert.equal(check.check.phase, "typecheck", "graph check should promise typecheck state");
  assert.equal(check.check.lowering, "direct-program-graph", "graph check should lower through ProgramGraph");
  assert.equal(check.targetReadiness.languageOk, true, "graph check should include language readiness");

  const size = await zeroJson(["graph", "size", "--json", "--target", "linux-musl-x64", artifact]);
  assert.equal(size.graph.canonicalSource, false, "graph size should report artifact input");
  assert.equal(size.graph.lowering, "direct-program-graph", "graph size should lower through ProgramGraph");
  assert.equal(size.generatedCBytes, 0, "graph size should stay on the direct backend");
  assert.equal(size.cBridgeFallback, false, "graph size should not use C bridge fallback");

  const roundtrip = await zeroJson(["graph", "roundtrip", "--json", artifact]);
  assert.equal(roundtrip.ok, true, "graph roundtrip should accept the artifact");
  assert.equal(roundtrip.canonicalSource, false, "graph roundtrip should report artifact input");
  assert.equal(roundtrip.semanticStable, true, "graph roundtrip should promise semantic stability");
  assert.equal(roundtrip.lowering, "direct-program-graph", "graph roundtrip should lower through ProgramGraph");
}

async function assertUnconstrainedGenericTypeParams() {
  const fixture = `${outDir}/generic-no-constraint.0`;
  await writeFile(fixture, [
    "type Box<T> {",
    "    value: T,",
    "}",
    "",
    "fn id<T>(value: T) -> T {",
    "    return value",
    "}",
    "",
    "pub fn main(world: World) -> Void raises {",
    "    let box: Box<i32> = Box { value: id<i32>(1) }",
    "    if box.value == 1 {",
    "        check world.out.write(\"generic no constraint ok\\n\")",
    "    }",
    "}",
    "",
  ].join("\n"));
  const check = await zeroJson(["check", "--json", fixture]);
  assert.equal(check.ok, true, "source check should accept unconstrained generic parameters");
  const dump = await zeroJson(["graph", "dump", "--json", fixture]);
  assert.equal(dump.validation.ok, true, "graph dump should validate unconstrained generic parameters");
  assert(dump.nodes.some((node) => node.kind === "Param" && node.name === "T" && node.type === ""), "graph dump should preserve an unconstrained type parameter");
  const artifact = await dumpGraphArtifact(fixture, "generic-no-constraint");
  const validate = await zeroJson(["graph", "validate", "--json", artifact]);
  assert.equal(validate.ok, true, "graph validate should accept unconstrained generic parameter artifacts");
}

async function assertBuildParity(fixture, name) {
  const artifact = await dumpGraphArtifact(fixture, `${name}-build-input`);
  const sourceOut = `${outDir}/${name}.source-build`;
  const graphOut = `${outDir}/${name}.graph-build`;
  const source = await zeroJson(["build", "--json", "--target", "linux-musl-x64", "--out", sourceOut, fixture]);
  const graph = await zeroJson(["graph", "build", "--json", "--target", "linux-musl-x64", "--out", graphOut, artifact]);

  assert.equal(graph.graph.artifact, artifact, `${fixture}: graph build artifact`);
  assert.equal(graph.graph.canonicalSource, false, `${fixture}: graph build should use artifact input`);
  assert.equal(graph.graph.lowering, "direct-program-graph", `${fixture}: graph build lowering`);
  assert.deepEqual(buildSummary(graph), buildSummary(source), `${fixture}: source and graph build summaries should agree`);
  assert(source.artifactBytes > 0, `${fixture}: source build should write an artifact`);
  assert(graph.artifactBytes > 0, `${fixture}: graph build should write an artifact`);
}

async function assertRunParity(fixture, name, args = []) {
  const artifact = await dumpGraphArtifact(fixture, `${name}-run-input`);
  const sourceOut = `${outDir}/${name}.source-run`;
  const graphOut = `${outDir}/${name}.graph-run`;
  const source = await zeroText(["run", "--out", sourceOut, fixture, "--", ...args]);
  const graph = await zeroText(["graph", "run", "--out", graphOut, artifact, "--", ...args]);

  assert.equal(graph, source, `${fixture}: source and graph run output should agree`);
}

async function assertTestParity(fixture, name) {
  const artifact = await dumpGraphArtifact(fixture, `${name}-test-input`);
  const source = await zeroJson(["test", "--json", fixture]);
  const graph = await zeroJson(["graph", "test", "--json", artifact]);

  assert.equal(graph.graph.artifact, artifact, `${fixture}: graph test artifact`);
  assert.equal(graph.graph.canonicalSource, false, `${fixture}: graph test should use artifact input`);
  assert.equal(graph.graph.lowering, "direct-program-graph", `${fixture}: graph test lowering`);
  assert.deepEqual(testSummary(graph), testSummary(source), `${fixture}: source and graph test summaries should agree`);
}

async function assertSourceBackedPatchParity() {
  const fixture = `${outDir}/source-backed-patch.0`;
  const original = await readFile("examples/hello.0", "utf8");
  await writeFile(fixture, original);

  const beforeGraph = await zeroJson(["graph", "dump", "--json", fixture]);
  assert.equal(beforeGraph.canonicalSource, true, "source-backed patch input should import from source");
  assert.equal(beforeGraph.validation.state, "shape-valid", "source-backed patch input should be shape-valid");
  const literal = findStringLiteral(beforeGraph, "hello from zero\n");

  const patch = await zeroJson([
    "graph",
    "patch",
    "--json",
    fixture,
    "--expect-graph-hash",
    beforeGraph.graphHash,
    "--op",
    `set node="${literal.id}" field="value" expect="hello from zero\\n" value="hello source-backed\\n"`,
  ]);
  assert.equal(patch.ok, true, "source-backed graph patch should succeed");
  assert.equal(patch.canonicalSource, true, "source-backed graph patch should report source input");
  assert.equal(patch.saved.path, fixture, "source-backed graph patch should save to the source file");
  assert.equal(patch.originalGraphHash, beforeGraph.graphHash, "source-backed graph patch should check the expected graph hash");
  assert.match(patch.patchedGraphHash, /^graph:[0-9a-f]{16}$/, "source-backed graph patch should report a graph hash");
  assert.notEqual(patch.patchedGraphHash, beforeGraph.graphHash, "source-backed graph patch should change graph hash");
  assert.equal(patch.operationCount, 1, "source-backed graph patch should report one operation");
  assert.equal(patch.operations[0].ok, true, "source-backed graph patch operation should pass");
  assert.equal(patch.operations[0].node, literal.id, "source-backed graph patch should target the requested node");

  const patchedSource = await readFile(fixture, "utf8");
  assert.match(patchedSource, /hello source-backed\\n/, "source-backed graph patch should rewrite source text");
  assert.equal(await zeroText(["check", fixture]), "ok\n", "patched source should check through source command");
  assert.equal(await zeroText(["graph", "check", fixture]), "program graph check ok\n", "patched source should check through graph command");

  const artifact = await dumpGraphArtifact(fixture, "source-backed-patch-run");
  const sourceOut = `${outDir}/source-backed-patch.source-run`;
  const graphOut = `${outDir}/source-backed-patch.graph-run`;
  const source = await zeroText(["run", "--out", sourceOut, fixture]);
  const graph = await zeroText(["graph", "run", "--out", graphOut, artifact]);
  assert.equal(source, "hello source-backed\n", "patched source run output");
  assert.equal(graph, source, "patched graph artifact run output should match source");
}

async function assertGraphPatchPreservesNodeIds() {
  const artifact = await dumpGraphArtifact("examples/hello.0", "patch-preserves-id");
  const patchedArtifact = `${outDir}/patch-preserves-id.patched.program-graph`;
  const beforeGraph = await zeroJson(["graph", "dump", "--json", "examples/hello.0"]);
  const beforeLiteral = findStringLiteral(beforeGraph, "hello from zero\n");
  const beforeMain = beforeGraph.nodes.find((node) => node.kind === "Function" && node.name === "main");
  assert(beforeMain, "missing main function");

  const patch = await zeroJson([
    "graph",
    "patch",
    "--json",
    "--out",
    patchedArtifact,
    artifact,
    "--expect-graph-hash",
    beforeGraph.graphHash,
    "--op",
    `replace node="${beforeLiteral.id}" expect="${beforeLiteral.nodeHash}" kind="Literal" type="String" value="hello preserved\\n"`,
  ]);
  assert.equal(patch.ok, true, "graph artifact patch should succeed");
  assert.equal(patch.operations[0].node, beforeLiteral.id, "graph patch should target the inspected literal ID");
  assert.notEqual(patch.patchedGraphHash, beforeGraph.graphHash, "graph patch should update graphHash");

  const patchedText = await readFile(patchedArtifact, "utf8");
  assert.match(patchedText, new RegExp(`node ${beforeLiteral.id.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")} Literal[^\\n]*value:"hello preserved\\\\n"`), "graph patch should preserve literal node ID");
  assert.match(patchedText, new RegExp(`node ${beforeMain.id.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")} Function[^\\n]*name:"main"`), "graph patch should not churn unrelated function ID");
  assert.equal(await zeroText(["graph", "check", patchedArtifact]), "program graph check ok\n", "patched graph artifact should check");
}

function findStringLiteral(graph, value) {
  const literal = graph.nodes.find((node) => node.kind === "Literal" && node.type === "String" && node.value === value);
  assert(literal, `missing string literal ${JSON.stringify(value)}`);
  return literal;
}

function findNodeById(graph, id) {
  const node = graph.nodes.find((item) => item.id === id);
  assert(node, `missing graph node ${id}`);
  return node;
}

function assertMissingNodeId(graph, id, message) {
  assert(!graph.nodes.some((item) => item.id === id), message);
}

function findOwnerNode(graph, nodeId, edgeKind) {
  const edge = graph.edges.find((item) => item.target === "node" && item.to === nodeId && item.kind === edgeKind);
  assert(edge, `missing ${edgeKind} owner edge for ${nodeId}`);
  return findNodeById(graph, edge.from);
}

function findCheckForStringLiteral(graph, value) {
  const literal = findStringLiteral(graph, value);
  const call = findOwnerNode(graph, literal.id, "arg");
  const check = findOwnerNode(graph, call.id, "expr");
  assert.equal(check.kind, "Check", `expected check owner for ${JSON.stringify(value)}`);
  return { check, literal };
}

function findNodeByKindAndName(graph, kind, name) {
  const node = graph.nodes.find((item) => item.kind === kind && item.name === name);
  assert(node, `missing ${kind} node ${name}`);
  return node;
}

async function assertDeclarationSiblingIdentity() {
  const declarationFixture = `${outDir}/identity-declarations.0`;
  const declarations = [
    "type Point {",
    "    x: i32,",
    "}",
    "",
    "type Other {",
    "    y: i32,",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let point: Point = Point { x: 1 }",
    "    expect point.x == 1",
    "}",
    "",
  ].join("\n");
  await writeFile(declarationFixture, declarations);
  const beforeDeclarations = await zeroJson(["graph", "dump", "--json", declarationFixture]);
  const beforePoint = findNodeByKindAndName(beforeDeclarations, "Shape", "Point");
  const beforeOther = findNodeByKindAndName(beforeDeclarations, "Shape", "Other");

  await writeFile(declarationFixture, declarations.replace("type Point", "type Added {\n    z: i32,\n}\n\ntype Point"));
  const prependedDeclarations = await zeroJson(["graph", "dump", "--json", declarationFixture]);
  assert.equal(findNodeByKindAndName(prependedDeclarations, "Shape", "Point").id, beforePoint.id, "prepending a declaration should not churn existing shape IDs");
  assert.equal(findNodeByKindAndName(prependedDeclarations, "Shape", "Other").id, beforeOther.id, "prepending a declaration should not churn later shape IDs");

  const methodFixture = `${outDir}/identity-methods.0`;
  const methods = [
    "type Counter {",
    "    value: i32,",
    "    fn read(self: ref<Self>) -> i32 {",
    "        return self.value",
    "    }",
    "    fn done(self: ref<Self>) -> Bool {",
    "        return true",
    "    }",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let counter: Counter = Counter { value: 42 }",
    "    expect Counter.read(&counter) == 42",
    "}",
    "",
  ].join("\n");
  await writeFile(methodFixture, methods);
  const beforeMethods = await zeroJson(["graph", "dump", "--json", methodFixture]);
  const beforeRead = findNodeByKindAndName(beforeMethods, "Function", "read");
  const beforeDone = findNodeByKindAndName(beforeMethods, "Function", "done");

  await writeFile(methodFixture, methods.replace("    fn read", "    fn zero(self: ref<Self>) -> u32 {\n        return 0_u32\n    }\n    fn read"));
  const prependedMethods = await zeroJson(["graph", "dump", "--json", methodFixture]);
  assert.equal(findNodeByKindAndName(prependedMethods, "Function", "read").id, beforeRead.id, "prepending a distinct method should not churn existing method IDs");
  assert.equal(findNodeByKindAndName(prependedMethods, "Function", "done").id, beforeDone.id, "prepending a distinct method should not churn later method IDs");

  await writeFile(methodFixture, methods.replace("    fn read", "    fn count(self: ref<Self>) -> i32 {\n        return 1\n    }\n    fn read"));
  const sameShapeMethods = await zeroJson(["graph", "dump", "--json", methodFixture]);
  const sameShapeRead = findNodeByKindAndName(sameShapeMethods, "Function", "read");
  const countMethod = findNodeByKindAndName(sameShapeMethods, "Function", "count");
  assert.notEqual(sameShapeRead.id, beforeRead.id, "same-shape method collision should retire the old ambiguous method ID");
  assert.notEqual(countMethod.id, beforeRead.id, "prepended same-shape method should not steal the old method ID");
  assertMissingNodeId(sameShapeMethods, beforeRead.id, "old ambiguous method ID should not target any same-shape method");
}

async function assertSourceEditIdentityBaseline() {
  const fixture = `${outDir}/identity-edit.0`;
  const original = [
    "fn helper() -> i32 {",
    "    return 1",
    "}",
    "",
    "pub fn main(world: World) -> Void raises {",
    "    check world.out.write(\"hello from zero\\n\")",
    "}",
    "",
  ].join("\n");
  await writeFile(fixture, original);

  const beforeGraph = await zeroJson(["graph", "dump", "--json", fixture]);
  const before = findStringLiteral(beforeGraph, "hello from zero\n");
  const beforeHelper = beforeGraph.nodes.find((node) => node.kind === "Function" && node.name === "helper");
  const beforeCheck = beforeGraph.nodes.find((node) => node.kind === "Check");
  assert(beforeHelper, "missing helper function before rename");
  assert(beforeCheck, "missing check statement before insertion");

  await writeFile(fixture, original.replace("hello from zero\\n", "hello from graph\\n"));
  const afterGraph = await zeroJson(["graph", "dump", "--json", fixture]);
  const after = findStringLiteral(afterGraph, "hello from graph\n");

  assert.notEqual(after.nodeHash, before.nodeHash, "editing literal content should change nodeHash");
  assert.notEqual(afterGraph.graphHash, beforeGraph.graphHash, "editing literal content should change graphHash");
  assert.equal(after.id, before.id, "source-imported node id should survive a local content edit");

  await writeFile(fixture, original.replace("fn helper", "fn renamedHelper"));
  const renamedGraph = await zeroJson(["graph", "dump", "--json", fixture]);
  const renamedHelper = renamedGraph.nodes.find((node) => node.kind === "Function" && node.name === "renamedHelper");
  assert(renamedHelper, "missing renamed function");
  assert.equal(renamedHelper.id, beforeHelper.id, "source-imported declaration id should survive a rename");
  assert.notEqual(renamedHelper.nodeHash, beforeHelper.nodeHash, "renaming a declaration should change nodeHash");
  assert.notEqual(renamedHelper.symbolId, beforeHelper.symbolId, "renaming a declaration should change symbolId");
  if (requireStableNodeIds) assert.equal(renamedHelper.id, beforeHelper.id, "strict stable node id check");

  await writeFile(fixture, original.replace("\npub fn main", "\nfn appendedHelper() -> i32 {\n    return 2\n}\n\npub fn main"));
  const sameShapeSiblingGraph = await zeroJson(["graph", "dump", "--json", fixture]);
  const sameShapeHelper = sameShapeSiblingGraph.nodes.find((node) => node.kind === "Function" && node.name === "helper");
  const insertedHelper = sameShapeSiblingGraph.nodes.find((node) => node.kind === "Function" && node.name === "appendedHelper");
  assert(sameShapeHelper, "missing helper after same-shape sibling insertion");
  assert(insertedHelper, "missing inserted same-shape sibling");
  assert.notEqual(sameShapeHelper.id, beforeHelper.id, "same-shape sibling collision should retire the old ambiguous declaration ID");
  assert.notEqual(insertedHelper.id, beforeHelper.id, "same-shape sibling should not steal the old declaration ID");
  assertMissingNodeId(sameShapeSiblingGraph, beforeHelper.id, "old ambiguous declaration ID should not target any same-shape sibling");

  await writeFile(fixture, original.replace("fn helper", "fn prependedHelper() -> i32 {\n    return 2\n}\n\nfn helper"));
  const prependedSiblingGraph = await zeroJson(["graph", "dump", "--json", fixture]);
  const prependedExistingHelper = prependedSiblingGraph.nodes.find((node) => node.kind === "Function" && node.name === "helper");
  const prependedHelper = prependedSiblingGraph.nodes.find((node) => node.kind === "Function" && node.name === "prependedHelper");
  assert(prependedExistingHelper, "missing helper after prepended same-shape sibling");
  assert(prependedHelper, "missing prepended same-shape sibling");
  assert.notEqual(prependedExistingHelper.id, beforeHelper.id, "prepending a same-shape sibling should retire the old ambiguous declaration ID");
  assert.notEqual(prependedHelper.id, beforeHelper.id, "prepended same-shape sibling should not steal the old declaration ID");
  assertMissingNodeId(prependedSiblingGraph, beforeHelper.id, "old ambiguous declaration ID should not target any prepended sibling");

  await writeFile(fixture, original.replace("    check world.out.write", "    let marker: i32 = 1\n    check world.out.write"));
  const insertedGraph = await zeroJson(["graph", "dump", "--json", fixture]);
  const insertedCheck = insertedGraph.nodes.find((node) => node.kind === "Check");
  const insertedLiteral = findStringLiteral(insertedGraph, "hello from zero\n");
  assert.equal(insertedCheck?.id, beforeCheck.id, "inserting before a statement should not churn the existing statement ID");
  assert.equal(insertedLiteral.id, before.id, "inserting before a statement should not churn nested expression IDs");

  await writeFile(fixture, original.replace("    check world.out.write(\"hello from zero\\n\")", "    check world.out.write(\"hello from zero\\n\")\n    check world.out.write(\"inserted\\n\")"));
  const sameKindStatementGraph = await zeroJson(["graph", "dump", "--json", fixture]);
  const sameKindExisting = findCheckForStringLiteral(sameKindStatementGraph, "hello from zero\n");
  const sameKindInserted = findCheckForStringLiteral(sameKindStatementGraph, "inserted\n");
  assert.notEqual(sameKindExisting.check.id, beforeCheck.id, "same-kind statement collision should retire the old ambiguous statement ID");
  assert.notEqual(sameKindExisting.literal.id, before.id, "same-kind statement collision should retire nested ambiguous expression IDs");
  assert.notEqual(sameKindInserted.check.id, beforeCheck.id, "appended same-kind statement should not steal the old statement ID");
  assert.notEqual(sameKindInserted.literal.id, before.id, "appended same-kind expression should not steal the old expression ID");
  assertMissingNodeId(sameKindStatementGraph, beforeCheck.id, "old ambiguous statement ID should not target any same-kind statement");
  assertMissingNodeId(sameKindStatementGraph, before.id, "old ambiguous expression ID should not target any same-kind expression");

  await writeFile(fixture, original.replace("    check world.out.write", "    check world.out.write(\"inserted\\n\")\n    check world.out.write"));
  const prependedStatementGraph = await zeroJson(["graph", "dump", "--json", fixture]);
  const prependedExisting = findCheckForStringLiteral(prependedStatementGraph, "hello from zero\n");
  const prependedInserted = findCheckForStringLiteral(prependedStatementGraph, "inserted\n");
  assert.notEqual(prependedExisting.check.id, beforeCheck.id, "prepending a same-kind statement should retire the old ambiguous statement ID");
  assert.notEqual(prependedExisting.literal.id, before.id, "prepending a same-kind statement should retire nested ambiguous expression IDs");
  assert.notEqual(prependedInserted.check.id, beforeCheck.id, "prepended same-kind statement should not steal the old statement ID");
  assert.notEqual(prependedInserted.literal.id, before.id, "prepended same-kind expression should not steal the old expression ID");
  assertMissingNodeId(prependedStatementGraph, beforeCheck.id, "old ambiguous statement ID should not target any prepended statement");
  assertMissingNodeId(prependedStatementGraph, before.id, "old ambiguous expression ID should not target any prepended expression");
}

try {
  await mkdir(outDir, { recursive: true });

  for (const fixture of [
    "examples/hello.0",
    "examples/type-alias.0",
    "examples/static-interface.0",
    "examples/direct-rescue-basic.0",
    "examples/std-math.0",
    "examples/systems-package",
    "examples/readall-cli",
    "examples/direct-package-arrays/src/main.0",
    "conformance/check/pass/c-header-import.0",
    "conformance/native/pass/borrow-field-independent-assignment.0",
    "conformance/native/pass/open-ended-slices.0",
    "conformance/native/pass/allocator-primitives.0",
    "conformance/native/pass/std-fs-fallible.0",
    "benchmarks/rosetta/fibonacci-sequence.0",
  ]) {
    await assertCheckParity(fixture);
  }

  for (const fixture of [
    "examples/hello.0",
    "examples/type-alias.0",
    "examples/static-interface.0",
    "examples/direct-rescue-basic.0",
    "examples/std-math.0",
    "examples/systems-package",
    "examples/readall-cli",
    "conformance/check/pass/c-header-import.0",
    "conformance/native/pass/borrow-field-independent-assignment.0",
    "conformance/native/pass/open-ended-slices.0",
    "conformance/native/pass/allocator-primitives.0",
    "conformance/native/pass/std-fs-fallible.0",
    "benchmarks/rosetta/fibonacci-sequence.0",
  ]) {
    await assertRoundtripStable(fixture);
  }

  await assertCommandStateContracts();
  await assertUnconstrainedGenericTypeParams();
  await assertBuildParity("examples/hello.0", "hello");
  await assertRunParity("examples/hello.0", "hello");
  await assertRunParity("conformance/native/pass/std-args.0", "std-args", ["alpha", "beta"]);
  await assertTestParity("conformance/native/pass/test-blocks.0", "test-blocks");
  await assertSourceBackedPatchParity();
  await assertGraphPatchPreservesNodeIds();

  await assertSourceEditIdentityBaseline();
  await assertDeclarationSiblingIdentity();
  console.log("program graph parity ok");
} finally {
  await rm(outDir, { force: true, recursive: true });
}
