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

async function zeroJsonFailure(args) {
  try {
    await execFileAsync(zero, args, { maxBuffer: execMaxBuffer });
  } catch (error) {
    return JSON.parse(error.stdout);
  }
  assert.fail(`${zero} ${args.join(" ")} should fail`);
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

function assertSourceGraphRoute(result, fixture, lowering = "typed-program-graph-mir") {
  assert(result.graph, `${fixture}: source command should report graph compiler input`);
  assert.equal(result.graph.artifact, fixture, `${fixture}: source graph artifact should be the source input`);
  assert.equal(result.graph.canonicalSource, true, `${fixture}: source graph should report canonical source`);
  assert.match(result.graph.graphHash, /^graph:[0-9a-f]{16}$/, `${fixture}: source graph hash`);
  assert.equal(result.graph.lowering, lowering, `${fixture}: source graph lowering`);
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

function findResolutionReference(graph, predicate, message) {
  const reference = graph.resolution?.references?.find(predicate);
  assert(reference, message);
  return reference;
}

function findSemanticCall(graph, predicate, message) {
  const call = graph.semantics?.calls?.find(predicate);
  assert(call, message);
  return call;
}

function assertSemanticCallResolutionMatches(graph, call, message) {
  const reference = graph.resolution?.references?.find((item) => item.kind === "call" && item.node === call.node);
  assert(reference, `${message}: resolver call reference`);
  for (const field of ["qualifiedName", "targetKind", "targetNode", "symbolId"]) {
    assert.equal(call.resolution?.[field], reference[field], `${message}: semantic resolution ${field}`);
  }
}

function resolutionBindings(graph) {
  return graph.resolution?.scopes?.flatMap((scope) => scope.bindings ?? []) ?? [];
}

function hasIncomingGraphEdge(graph, nodeId, kind, order = null) {
  return graph.edges.some((edge) => edge.target === "node" && edge.to === nodeId && edge.kind === kind && (order === null || edge.order === order));
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

async function assertCheckFailureParity(fixture) {
  const source = await zeroJsonFailure(["check", "--json", fixture]);
  const graph = await zeroJsonFailure(["graph", "check", "--json", fixture]);
  assert.equal(graph.canonicalSource, true, `${fixture}: graph check failure should report source input`);
  assert.equal(graph.check.phase, "typecheck", `${fixture}: graph check failure phase`);
  assert.equal(graph.check.lowering, "direct-program-graph", `${fixture}: graph check failure lowering`);
  assert.equal(graph.check.ok, false, `${fixture}: graph check should fail`);
  const sourceDiag = source.diagnostics?.[0];
  const graphDiag = graph.diagnostics?.[0];
  for (const field of ["code", "message", "path", "line", "column", "expected", "actual", "help"]) {
    assert.equal(graphDiag?.[field], sourceDiag?.[field], `${fixture}: diagnostic ${field} should agree`);
  }
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
  assert.equal(sourceDump.resolution.state, "resolved", "graph dump should expose name resolution state");
  assert.equal(sourceDump.resolution.ok, true, "graph dump resolution should pass");
  const worldRef = findResolutionReference(sourceDump, (item) => item.kind === "identifier" && item.name === "world", "graph dump should resolve parameter identifiers");
  assert.equal(worldRef.targetKind, "param", "graph dump should classify parameter references");
  const writeRef = findResolutionReference(sourceDump, (item) => item.kind === "call" && item.qualifiedName === "world.out.write", "graph dump should resolve member calls to their base binding");
  assert.equal(writeRef.targetKind, "member", "graph dump should classify member calls");

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
  assert.equal(size.graph.lowering, "typed-program-graph-mir", "graph size should lower through typed graph MIR");
  assert.equal(size.generatedCBytes, 0, "graph size should stay on the direct backend");
  assert.equal(size.cBridgeFallback, false, "graph size should not use C bridge fallback");

  const stdArgsArtifact = await dumpGraphArtifact("conformance/native/pass/std-args.0", "std-args-mir");
  const stdArgsSize = await zeroJson(["graph", "size", "--json", "--target", "linux-musl-x64", stdArgsArtifact]);
  assert.equal(stdArgsSize.graph.lowering, "typed-program-graph-mir", "graph size should lower std args through typed graph MIR");
  assert.equal(stdArgsSize.generatedCBytes, 0, "graph MIR std args should stay on the direct backend");
  assert.equal(stdArgsSize.objectBackend.directFacts.functionCount, 1, "graph MIR std args should retain the main function");
  const stdArgsRun = await zeroText(["graph", "run", "--out", `${outDir}/std-args-run`, stdArgsArtifact, "one", "two"]);
  assert.equal(stdArgsRun, "one\n", "graph run should execute std args from typed graph MIR");

  const roundtrip = await zeroJson(["graph", "roundtrip", "--json", artifact]);
  assert.equal(roundtrip.ok, true, "graph roundtrip should accept the artifact");
  assert.equal(roundtrip.canonicalSource, false, "graph roundtrip should report artifact input");
  assert.equal(roundtrip.semanticStable, true, "graph roundtrip should promise semantic stability");
  assert.equal(roundtrip.lowering, "direct-program-graph", "graph roundtrip should lower through ProgramGraph");

  const sourceMap = await zeroJson(["graph", "source-map", "--json", "examples/hello.0"]);
  assert.equal(sourceMap.ok, true, "graph source-map should succeed");
  assert.equal(sourceMap.canonicalSource, true, "graph source-map should report canonical source input");
  const mainMapping = sourceMap.mappings.find((mapping) => mapping.kind === "Function" && mapping.name === "main");
  assert(mainMapping && mainMapping.sourceRange.path === "examples/hello.0", "graph source-map should map function nodes to source ranges");
  assert.equal(mainMapping.sourceAvailable, true, "graph source-map should report tokenized source availability");
  assert.deepEqual(mainMapping.sourceRange.start, { line: 1, column: 8 }, "graph source-map should start function ranges at the name token");
  assert.deepEqual(mainMapping.sourceRange.end, { line: 1, column: 12 }, "graph source-map should end function ranges at the name token");
  assert.equal(await zeroText(["graph", "source-map", "examples/hello.0"]), `program graph source map ok: ${sourceMap.counts.mappings} mappings\n`, "graph source-map text output");

  const repeatedTypesFixture = `${outDir}/source-map-repeated-types.0`;
  await writeFile(repeatedTypesFixture, [
    "fn double(value: i32) -> i32 {",
    "    return value + value",
    "}",
    "",
    "pub fn main(world: World) -> Void raises {",
    "    check world.out.write(\"source map repeated types\\n\")",
    "}",
    "",
  ].join("\n"));
  const repeatedTypesMap = await zeroJson(["graph", "source-map", "--json", repeatedTypesFixture]);
  const repeatedTypeRanges = repeatedTypesMap.mappings
    .filter((mapping) => mapping.kind === "TypeRef" && mapping.type === "i32" && mapping.sourceRange.start.line === 1)
    .map((mapping) => mapping.sourceRange.start);
  assert.deepEqual(repeatedTypeRanges, [{ line: 1, column: 18 }, { line: 1, column: 26 }], "graph source-map should disambiguate repeated type tokens");
}

async function assertResolutionFacts() {
  const stdStr = await zeroJson(["graph", "dump", "--json", "examples/std-str.0"]);
  assert.equal(stdStr.resolution.ok, true, "std-str graph resolution");
  const reverse = findResolutionReference(stdStr, (item) => item.kind === "call" && item.qualifiedName === "std.str.reverse", "source-backed std call should resolve");
  assert.equal(reverse.targetKind, "sourceBackedStdlib", "source-backed std call target kind");
  assert.match(reverse.symbolId, /^symbol:std\.str::value\.__zero_std_str_reverse$/, "source-backed std call symbol");
  const memEql = findResolutionReference(stdStr, (item) => item.kind === "call" && item.qualifiedName === "std.mem.eql", "table std helper should resolve");
  assert.equal(memEql.targetKind, "stdlib", "table std helper target kind");
  assert.equal(memEql.symbolId, "stdlib:std.mem.eql", "table std helper symbol");

  const packageGraph = await zeroJson(["graph", "dump", "--json", "examples/direct-package-arrays/src/main.0"]);
  assert.equal(packageGraph.resolution.ok, true, "package graph resolution");
  const record = findResolutionReference(packageGraph, (item) => item.kind === "call" && item.qualifiedName === "record", "package import call should resolve");
  assert.equal(record.targetKind, "function", "package import call target kind");
  assert.equal(record.symbolId, "symbol:arrays::value.record", "package import call target symbol");
  assert.equal(record.viaImport, "symbol:main::import.arrays", "package import call should record import binding");

  const cImport = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/c-import-alias-later-local.0"]);
  assert.equal(cImport.resolution.ok, true, "C import graph resolution");
  const cCall = findResolutionReference(cImport, (item) => item.kind === "call" && item.qualifiedName === "c.zero_c_add", "C import call should resolve");
  assert.equal(cCall.targetKind, "cFunction", "C import call target kind");
  assert.match(cCall.symbolId, /^symbol:c-import-alias-later-local::c-import\.c$/, "C import call symbol");
  const localC = findResolutionReference(cImport, (item) => item.kind === "identifier" && item.name === "c" && item.targetKind === "local", "later local should shadow C import after declaration");
  assert.match(localC.symbolId, /local\.c@/, "local shadow symbol");

  const cImportTypeShadow = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/c-import-type-shadowing.0"]);
  assert.equal(cImportTypeShadow.resolution.ok, true, "C import/type shadow graph resolution");
  const shadowedCounterCall = findResolutionReference(cImportTypeShadow, (item) => item.kind === "call" && item.qualifiedName === "Counter.zero_c_add", "static method should resolve when a C import alias shares the type name");
  assert.equal(shadowedCounterCall.targetKind, "method", "C import/type shadow static method target kind");
  assert.equal(shadowedCounterCall.symbolId, "symbol:c-import-type-shadowing::type.Counter/method.zero_c_add", "C import/type shadow static method symbol");
  assert.equal(shadowedCounterCall.viaImport, "", "same-module static method should not report an import binding");
  const counterIdentifier = findResolutionReference(cImportTypeShadow, (item) => item.kind === "identifier" && item.name === "Counter", "call-chain base should resolve to the type namespace");
  assert.equal(counterIdentifier.targetKind, "shape", "call-chain base should prefer the type binding for static method chains");

  const staticInterface = await zeroJson(["graph", "dump", "--json", "examples/static-interface.0"]);
  assert.equal(staticInterface.resolution.ok, true, "static interface graph resolution");
  const interfaceCall = findResolutionReference(staticInterface, (item) => item.kind === "call" && item.qualifiedName === "T.read", "constrained interface method call should resolve");
  assert.equal(interfaceCall.targetKind, "interfaceMethod", "constrained interface call target kind");
  assert.equal(interfaceCall.symbolId, "symbol:static-interface::type.Readable/method.read", "constrained interface call target symbol");

  const genericFunction = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/generic-function-basic.0"]);
  assert.equal(genericFunction.resolution.ok, true, "generic function graph resolution");
  const identityReturnType = findResolutionReference(genericFunction, (item) => item.kind === "type" && item.name === "T" && item.symbolId === "symbol:generic-function-basic::value.identity/param.T" && hasIncomingGraphEdge(genericFunction, item.node, "returnType"), "generic return type should resolve to its type parameter");
  assert.equal(identityReturnType.targetKind, "type", "generic return type target kind");

  const staticValues = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/static-value-params.0"]);
  assert.equal(staticValues.resolution.ok, true, "static value parameter graph resolution");
  const staticParamBinding = resolutionBindings(staticValues).find((item) => item.name === "N" && item.kind === "staticParam" && item.symbolId === "symbol:static-value-params::value.first/param.N");
  assert(staticParamBinding, "function static parameter should be classified as a staticParam binding");
  const capTypeArg = findResolutionReference(staticValues, (item) => item.kind === "type" && item.name === "cap" && hasIncomingGraphEdge(staticValues, item.node, "typeArg"), "static value type argument should resolve through value lookup");
  assert.equal(capTypeArg.targetKind, "const", "static value type argument target kind");
  assert.equal(capTypeArg.symbolId, "symbol:static-value-params::value.cap", "static value type argument symbol");

  const staticExplicitFixture = `${outDir}/resolution-static-explicit-args.0`;
  await writeFile(staticExplicitFixture, [
    "const Foo: usize = 3",
    "",
    "type Foo {",
    "    value: i32,",
    "}",
    "",
    "type Gate<static enabledFlag: Bool, static selectedMode: Mode> {",
    "    value: i32,",
    "}",
    "",
    "enum Mode: u8 {",
    "    fast,",
    "    tiny,",
    "}",
    "",
    "fn readGate<static enabledFlag: Bool, static selectedMode: Mode>(gate: ref<Gate<enabledFlag, selectedMode>>) -> i32 {",
    "    if enabledFlag {",
    "        return gate.value",
    "    }",
    "    return 0",
    "}",
    "",
    "fn pick<T: Type, static N: usize>(value: T, items: ref<[N]i32>) -> T {",
    "    return value",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let items: [Foo]i32 = [1, 2, 3]",
    "    let value: Foo = pick<Foo, Foo>(Foo { value: 7 }, &items)",
    "    let gate: Gate<true, Mode.fast> = Gate { value: 9 }",
    "    let gated: i32 = readGate<true, Mode.fast>(&gate)",
    "    expect (value.value == 7 && gated == 9)",
    "}",
    "",
  ].join("\n"));
  const staticExplicit = await zeroJson(["graph", "dump", "--json", staticExplicitFixture]);
  assert.equal(staticExplicit.resolution.ok, true, "explicit static generic argument graph resolution");
  const pickTypeArg = findResolutionReference(staticExplicit, (item) => item.kind === "type" && item.name === "Foo" && hasIncomingGraphEdge(staticExplicit, item.node, "typeArg", 0), "type argument in a mixed generic call should resolve as a type");
  assert.equal(pickTypeArg.targetKind, "type", "mixed generic type argument target kind");
  assert.equal(pickTypeArg.symbolId, "symbol:resolution-static-explicit-args::type.Foo", "mixed generic type argument symbol");
  const pickStaticArg = findResolutionReference(staticExplicit, (item) => item.kind === "type" && item.name === "Foo" && hasIncomingGraphEdge(staticExplicit, item.node, "typeArg", 1), "static argument with a type-name collision should resolve as a value");
  assert.equal(pickStaticArg.targetKind, "const", "static argument with a type-name collision target kind");
  assert.equal(pickStaticArg.symbolId, "symbol:resolution-static-explicit-args::value.Foo", "static argument with a type-name collision symbol");
  const boolStaticArg = findResolutionReference(staticExplicit, (item) => item.kind === "type" && item.qualifiedName === "true" && hasIncomingGraphEdge(staticExplicit, item.node, "typeArg", 0), "literal bool static argument should resolve");
  assert.equal(boolStaticArg.targetKind, "staticLiteral", "literal bool static argument target kind");
  assert.equal(boolStaticArg.symbolId, "literal:true", "literal bool static argument symbol");
  const enumStaticArg = findResolutionReference(staticExplicit, (item) => item.kind === "type" && item.qualifiedName === "Mode.fast" && hasIncomingGraphEdge(staticExplicit, item.node, "typeArg", 1), "enum-case static argument should resolve");
  assert.equal(enumStaticArg.targetKind, "variant", "enum-case static argument target kind");
  assert.equal(enumStaticArg.symbolId, "symbol:resolution-static-explicit-args::type.Mode/variant.fast", "enum-case static argument symbol");

  const valueTypeCollisionFixture = `${outDir}/resolution-value-type-collision.0`;
  await writeFile(valueTypeCollisionFixture, [
    "const Foo: usize = 3",
    "",
    "type Foo {",
    "    value: i32,",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let n: usize = Foo",
    "    expect n == 3",
    "}",
    "",
  ].join("\n"));
  const valueTypeCollision = await zeroJson(["graph", "dump", "--json", valueTypeCollisionFixture]);
  assert.equal(valueTypeCollision.resolution.ok, true, "value/type collision graph resolution");
  const valueFoo = findResolutionReference(valueTypeCollision, (item) => item.kind === "identifier" && item.name === "Foo", "ordinary identifiers should prefer value bindings over same-name type bindings");
  assert.equal(valueFoo.targetKind, "const", "value/type collision identifier target kind");
  assert.equal(valueFoo.symbolId, "symbol:resolution-value-type-collision::value.Foo", "value/type collision identifier symbol");

  const builtinShadowFixture = `${outDir}/resolution-builtin-shadow.0`;
  await writeFile(builtinShadowFixture, [
    "type World {",
    "    value: i32,",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let item: World = World { value: 1 }",
    "    expect item.value == 1",
    "}",
    "",
  ].join("\n"));
  const builtinShadow = await zeroJson(["graph", "dump", "--json", builtinShadowFixture]);
  assert.equal(builtinShadow.resolution.ok, true, "builtin type shadow graph resolution");
  const shadowedWorld = findResolutionReference(builtinShadow, (item) => item.kind === "type" && item.name === "World" && hasIncomingGraphEdge(builtinShadow, item.node, "declaredType"), "declared types should shadow builtin type names in resolution facts");
  assert.equal(shadowedWorld.targetKind, "type", "builtin shadow type target kind");
  assert.equal(shadowedWorld.symbolId, "symbol:resolution-builtin-shadow::type.World", "builtin shadow type symbol");

  const forRange = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/for-range.0"]);
  assert.equal(forRange.resolution.ok, true, "for range graph resolution");
  const forIndexBinding = resolutionBindings(forRange).find((item) => item.name === "index" && item.kind === "local");
  assert(forIndexBinding, "for range iterator should create a local binding");
  assert.match(forIndexBinding.symbolId, /local\.index@_stmt_/, "for range iterator symbol should include its node id");
  const forIndexRef = findResolutionReference(forRange, (item) => item.kind === "identifier" && item.name === "index" && item.targetKind === "local", "for range body should resolve iterator references");
  assert.equal(forIndexRef.symbolId, forIndexBinding.symbolId, "for range iterator reference symbol");

  const resultChoice = await zeroJson(["graph", "dump", "--json", "examples/result-choice.0"]);
  assert.equal(resultChoice.resolution.ok, true, "choice constructor graph resolution");
  const choiceConstructor = findResolutionReference(resultChoice, (item) => item.kind === "call" && item.qualifiedName === "Result.ok", "choice constructor should resolve to its case binding");
  assert.equal(choiceConstructor.targetKind, "variant", "choice constructor target kind");
  assert.equal(choiceConstructor.symbolId, "symbol:result-choice::type.Result/variant.ok", "choice constructor target symbol");
  assert.equal(choiceConstructor.viaImport, "", "same-module choice constructor should not report an import binding");
  const patternBinding = resolutionBindings(resultChoice).find((item) => item.name === "value" && item.kind === "pattern");
  assert(patternBinding, "choice match payload should create a pattern binding");
  assert.match(patternBinding.symbolId, /pattern\.value@_stmt_/, "choice match payload symbol should include its node id");

  const testBlocks = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/test-blocks.0"]);
  assert.equal(testBlocks.resolution.ok, true, "test block graph resolution");
  const expectCall = findResolutionReference(testBlocks, (item) => item.kind === "call" && item.qualifiedName === "expect", "test expect call should resolve as a language builtin");
  assert.equal(expectCall.targetKind, "testExpect", "test expect call target kind");
  assert.equal(expectCall.symbolId, "builtin:expect", "test expect call symbol");

  const compileTime = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/compile-time-v1.0"]);
  assert.equal(compileTime.resolution.ok, true, "compile-time facts graph resolution");
  const targetNamespace = findResolutionReference(compileTime, (item) => item.kind === "identifier" && item.name === "target", "target meta namespace should resolve");
  assert.equal(targetNamespace.targetKind, "targetNamespace", "target namespace target kind");
  for (const fact of ["hasField", "fieldCount", "fieldType", "enumCaseCount", "hasEnumCase", "choiceCaseCount", "hasChoiceCase"]) {
    const metaFact = findResolutionReference(compileTime, (item) => item.kind === "call" && item.qualifiedName === fact, `${fact} meta call should resolve`);
    assert.equal(metaFact.targetKind, "metaFact", `${fact} meta call target kind`);
    assert.equal(metaFact.symbolId, `meta:${fact}`, `${fact} meta call symbol`);
  }
  const targetFacts = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/meta-typed-target-type.0"]);
  assert.equal(targetFacts.resolution.ok, true, "target fact graph resolution");
  const hasCapability = findResolutionReference(targetFacts, (item) => item.kind === "call" && item.qualifiedName === "target.hasCapability", "target fact call should resolve");
  assert.equal(hasCapability.targetKind, "targetFact", "target fact call target kind");
  assert.equal(hasCapability.symbolId, "meta:target.hasCapability", "target fact call symbol");

  const fixedVec = await zeroJson(["graph", "dump", "--json", "examples/fixed-vec.0"]);
  assert.equal(fixedVec.resolution.ok, true, "Self graph resolution");
  const selfReturn = findResolutionReference(fixedVec, (item) => item.kind === "type" && item.name === "Self" && hasIncomingGraphEdge(fixedVec, item.node, "returnType"), "Self return type should resolve to the enclosing type");
  assert.equal(selfReturn.targetKind, "type", "Self return type target kind");
  assert.equal(selfReturn.symbolId, "symbol:fixed-vec::type.FixedVec", "Self return type symbol");
  const receiverPush = findResolutionReference(fixedVec, (item) => item.kind === "call" && item.qualifiedName === "vec.push", "receiver method should resolve to the method binding");
  assert.equal(receiverPush.targetKind, "method", "receiver method target kind");
  assert.equal(receiverPush.symbolId, "symbol:fixed-vec::type.FixedVec/method.push", "receiver method target symbol");

  const systemsPackage = await zeroJson(["graph", "dump", "--json", "examples/systems-package"]);
  assert.equal(systemsPackage.resolution.ok, true, "package-local module graph resolution");
  const statusType = findResolutionReference(systemsPackage, (item) => item.kind === "type" && item.name === "Status" && item.symbolId === "symbol:systems-package@0.1.0/types::type.Status", "package-local type should resolve across loaded modules");
  assert.equal(statusType.targetKind, "type", "package-local type target kind");
  const statusVariant = findResolutionReference(systemsPackage, (item) => item.kind === "identifier" && item.name === "Status" && item.symbolId === "symbol:systems-package@0.1.0/types::type.Status", "package-local enum namespace should resolve across loaded modules");
  assert.equal(statusVariant.targetKind, "enum", "package-local enum namespace target kind");

  const hostedTypes = await zeroJson(["graph", "dump", "--json", "examples/readall-cli"]);
  assert.equal(hostedTypes.resolution.ok, true, "hosted std-backed type graph resolution");

  const stdShadowFixture = `${outDir}/std-shadow.0`;
  await writeFile(stdShadowFixture, [
    "pub fn main(world: World) -> Void raises {",
    "    let std: i32 = 1",
    "    if std == 1 {",
    "        check world.out.write(\"std shadow ok\\n\")",
    "    }",
    "}",
    "",
  ].join("\n"));
  const stdShadow = await zeroJson(["graph", "dump", "--json", stdShadowFixture]);
  assert.equal(stdShadow.resolution.ok, true, "local std shadow graph resolution");
  const stdRef = findResolutionReference(stdShadow, (item) => item.kind === "identifier" && item.name === "std", "local std identifier should be present");
  assert.equal(stdRef.targetKind, "local", "local std should shadow the stdlib namespace");
  assert.match(stdRef.symbolId, /local\.std@/, "local std shadow symbol");
}

async function assertSemanticFacts() {
  const hello = await zeroJson(["graph", "dump", "--json", "examples/hello.0"]);
  assert.equal(hello.semantics.state, "typed-facts", "hello graph semantic fact state");
  assert.equal(hello.semantics.ok, true, "hello graph semantic facts");
  assert.equal(hello.semantics.counts.functions, 1, "hello semantic function count");
  assert.equal(hello.semantics.counts.calls, 1, "hello semantic call count");
  assert.equal(hello.semantics.counts.fallibleCalls, 1, "hello semantic fallible call count");
  assert.equal(hello.semantics.counts.resources, hello.semantics.resources.length, "hello semantic resource count");
  assert.equal(hello.semantics.counts.targetRequirements, hello.semantics.targetRequirements.length, "hello semantic target requirement count");
  assert.equal(hello.semantics.counts.repairs, hello.semantics.repairs.length, "hello semantic repair count");
  const helloMain = hello.semantics.functions.find((item) => item.name === "main");
  assert(helloMain, "hello semantic function fact");
  assert.equal(helloMain.returnType, "Void", "hello function return type");
  assert.equal(helloMain.fallible, true, "hello function fallibility");
  assert.deepEqual(helloMain.params.map((item) => [item.name, item.type]), [["world", "World"]], "hello function params");
  assert.equal(helloMain.sourceRange.path, "examples/hello.0", "hello function semantic source range");
  assert.deepEqual(helloMain.sourceRange.start, { line: 1, column: 8 }, "hello function semantic source range start should target the function name");
  assert.deepEqual(helloMain.sourceRange.end, { line: 1, column: 12 }, "hello function semantic source range end should target the function name");
  assert(hello.semantics.ownership.some((item) => item.name === "world" && item.ownership === "resource-handle" && item.resource === true), "world parameter should be represented as a resource handle");
  const write = findSemanticCall(hello, (item) => item.qualifiedName === "world.out.write", "world write semantic call fact");
  assert.equal(write.returnType, "Void", "world write return type");
  assert.equal(write.contract.kind, "worldStreamWrite", "world write contract kind");
  assert.equal(write.contract.capability, "io", "world write capability");
  assert.equal(write.contract.targetSupport, "world-io", "world write target support");
  assert.match(write.contract.targetNode, /^#param_/, "world write contract target node");
  assert.equal(write.contract.symbolId, "symbol:hello::value.main/param.world", "world write contract symbol");
  assertSemanticCallResolutionMatches(hello, write, "world write semantic resolution");
  assert.equal(write.resolution.targetKind, "member", "world write semantic resolution kind");
  assert.equal(write.resolution.symbolId, "symbol:hello::value.main/param.world", "world write semantic resolution symbol");
  assert.equal(write.fallible, true, "world write fallibility");
  assert.equal(write.checked, true, "world write checked state");
  assert.equal(write.contract.requiresCheck, false, "checked world write should not require repair");
  assert.equal(write.contract.repair.id, "check-fallible-call", "world write repair shape");
  assert.equal(write.sourceRange.path, "examples/hello.0", "world write source range");
  assert.deepEqual(write.sourceRange.start, { line: 2, column: 21 }, "world write semantic source range start should target the call member");
  assert.deepEqual(write.sourceRange.end, { line: 2, column: 26 }, "world write semantic source range end should target the call member");
  assert(hello.semantics.resources.some((item) => item.kind === "capabilityUse" && item.resourceKind === "world-io" && item.qualifiedName === "world.out.write"), "world write resource fact");
  assert(hello.semantics.targetRequirements.some((item) => item.qualifiedName === "world.out.write" && item.capability === "io" && item.targetSupport === "world-io"), "world write target requirement fact");
  assert(hello.semantics.repairs.some((item) => item.qualifiedName === "world.out.write" && item.requiresCheck === false && item.repair.id === "check-fallible-call"), "world write top-level repair fact");

  const stdStr = await zeroJson(["graph", "dump", "--json", "examples/std-str.0"]);
  const reverse = findSemanticCall(stdStr, (item) => item.qualifiedName === "std.str.reverse", "std str reverse semantic call fact");
  assert.equal(reverse.contract.kind, "sourceBackedStdlib", "source-backed std contract kind");
  assert.equal(reverse.contract.sourceModule, "std.str", "source-backed std module");
  assert.equal(reverse.contract.returnType, "Maybe<Span<u8>>", "source-backed std return type");
  assert.equal(reverse.contract.capability, "memory", "source-backed std capability");
  assert.equal(reverse.contract.targetSupport, "target-neutral", "source-backed std target support");
  assert.equal(reverse.contract.expectedArgCount, 2, "source-backed std arg count");
  assert.deepEqual(reverse.contract.expectedArgTypes, ["MutSpan<u8>", "Span<u8>"], "source-backed std arg types");
  assertSemanticCallResolutionMatches(stdStr, reverse, "source-backed std semantic resolution");
  assert.equal(reverse.resolution.targetKind, "sourceBackedStdlib", "source-backed std semantic resolution kind");
  assert.equal(reverse.resolution.symbolId, "symbol:std.str::value.__zero_std_str_reverse", "source-backed std semantic symbol");
  assert.deepEqual(reverse.args.map((item) => item.type), ["MutSpan<u8>", "String"], "source-backed std actual arg types");

  const stdFs = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/std-fs-fallible.0"]);
  const stdFsMain = stdFs.semantics.functions.find((item) => item.name === "main");
  assert(stdFsMain, "std fs main semantic function fact");
  assert.deepEqual(stdFsMain.errors, ["NotFound", "TooLarge", "Io"], "named error set facts");
  const readAll = findSemanticCall(stdFs, (item) => item.qualifiedName === "std.fs.readAllOrRaise", "fallible std helper semantic call fact");
  assert.equal(readAll.contract.kind, "stdlib", "fallible std helper contract kind");
  assert.equal(readAll.contract.fallible, true, "fallible std helper contract fallibility");
  assert.equal(readAll.contract.checked, true, "fallible std helper checked state");
  assert.deepEqual(readAll.contract.errors, ["NotFound", "TooLarge", "Io"], "fallible std helper errors");
  assert.equal(readAll.contract.capability, "fs", "fallible std helper capability");
  assert.equal(readAll.contract.targetSupport, "host", "fallible std helper target support");
  assert.equal(readAll.contract.repair.id, "check-fallible-call", "fallible std helper repair shape");
  assertSemanticCallResolutionMatches(stdFs, readAll, "fallible std helper semantic resolution");
  assert.equal(readAll.resolution.targetKind, "stdlib", "fallible std helper semantic resolution kind");
  assert.equal(readAll.resolution.symbolId, "stdlib:std.fs.readAllOrRaise", "fallible std helper semantic symbol");
  assert(stdFs.semantics.ownership.some((item) => item.name === "body" && item.type === "owned<ByteBuf>" && item.ownership === "owned"), "owned ByteBuf ownership fact");
  assert(stdFs.semantics.borrowing.some((item) => item.type === "ref<ByteBuf>" && item.borrowKind === "borrow" && item.target), "borrowed ByteBuf fact");
  assert(stdFs.semantics.resources.some((item) => item.kind === "capabilityUse" && item.qualifiedName === "std.fs.readAllOrRaise" && item.capability === "fs"), "std fs resource-use fact");
  assert(stdFs.semantics.targetRequirements.some((item) => item.qualifiedName === "std.fs.readAllOrRaise" && item.capability === "fs" && item.targetSupport === "host"), "std fs target requirement fact");
  assert(stdFs.semantics.repairs.some((item) => item.qualifiedName === "std.fs.readAllOrRaise" && item.requiresCheck === false), "std fs top-level repair fact");

  const cImport = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/c-import-alias-later-local.0"]);
  const cCall = findSemanticCall(cImport, (item) => item.qualifiedName === "c.zero_c_add", "C import semantic call fact");
  assert.equal(cCall.contract.kind, "cAbi", "C import contract kind");
  assert.equal(cCall.contract.capability, "c-abi", "C import capability");
  assert.equal(cCall.contract.targetSupport, "host-c-abi", "C import target support");
  assert.match(cCall.contract.targetNode, /^#cimp_/, "C import contract target node");
  assert.match(cCall.contract.symbolId, /^symbol:c-import-alias-later-local::c-import\.c$/, "C import contract symbol");
  assertSemanticCallResolutionMatches(cImport, cCall, "C import semantic resolution");
  assert.equal(cCall.resolution.targetKind, "cFunction", "C import semantic resolution kind");
  assert.equal(cCall.returnType, "i32", "C import return type");
  assert.deepEqual(cCall.args.map((item) => item.type), ["i32", "i32"], "C import actual arg types");
  assert(cImport.semantics.targetRequirements.some((item) => item.qualifiedName === "c.zero_c_add" && item.capability === "c-abi" && item.targetSupport === "host-c-abi"), "C import target requirement fact");
  assert(cImport.semantics.resources.some((item) => item.kind === "capabilityUse" && item.qualifiedName === "c.zero_c_add" && item.resourceKind === "c-abi"), "C import resource fact");

  const borrowGraph = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/borrow-return-explicit-ref-field-origin.0"]);
  assert(borrowGraph.semantics.ownership.some((item) => item.name === "current" && item.ownership === "borrow"), "borrowed local ownership fact");
  assert(borrowGraph.semantics.borrowing.some((item) => item.borrowKind === "mut-borrow" && item.mutable === true && item.target), "mutable borrow target fact");
  assert.equal(borrowGraph.semantics.resources.length, 0, "borrow-only fixture should not invent resource facts");

  const userResourceNameFixture = `${outDir}/semantic-user-resource-names.0`;
  await writeFile(userResourceNameFixture, [
    "type File {",
    "    fd: i32,",
    "}",
    "",
    "type UserFileRecord {",
    "    value: i32,",
    "}",
    "",
    "pub fn main() -> Void {",
    "    let record: UserFileRecord = UserFileRecord { value: 1 }",
    "    let file: File = File { fd: record.value }",
    "    let maybe_file: Maybe<File> = null",
    "    expect file.fd == 1",
    "}",
    "",
  ].join("\n"));
  const userResourceNames = await zeroJson(["graph", "dump", "--json", userResourceNameFixture]);
  assert.equal(userResourceNames.semantics.resources.length, 0, "user-defined type names containing resource words should not emit resource facts");
  assert(!userResourceNames.semantics.ownership.some((item) => item.type === "File" || item.type === "UserFileRecord" || item.type === "Maybe<File>"), "user-defined type names containing resource words should not emit ownership resource facts");

  const shadowedStdResourceFixture = `${outDir}/semantic-shadowed-stdlib-resource.0`;
  await writeFile(shadowedStdResourceFixture, [
    "type File {",
    "    fd: i32,",
    "}",
    "",
    "pub fn main() -> Void raises [NotFound, TooLarge, Io] {",
    "    let fs: Fs = std.fs.host()",
    "    let owned_file: owned<File> = check std.fs.createOrRaise(fs, \".zero/semantic-shadowed-stdlib-resource.txt\")",
    "    let user_file: File = File { fd: 1 }",
    "    expect user_file.fd == 1",
    "}",
    "",
  ].join("\n"));
  const shadowedStdResource = await zeroJson(["graph", "dump", "--json", shadowedStdResourceFixture]);
  assert(shadowedStdResource.semantics.ownership.some((item) => item.name === "owned_file" && item.type === "owned<File>" && item.ownership === "owned" && item.resource === true), "stdlib File resource facts should survive a user-defined File type");
  assert(!shadowedStdResource.semantics.ownership.some((item) => item.name === "user_file"), "user-defined File binding should not become an ownership fact");
  assert(shadowedStdResource.semantics.resources.some((item) => item.kind === "binding" && item.type === "owned<File>" && item.resourceKind === "file"), "stdlib File resource binding should survive a user-defined File type");

  const searchSort = await zeroJson(["graph", "dump", "--json", "conformance/native/pass/std-search-sort-widths.0"]);
  assert(!searchSort.semantics.resources.some((item) => item.qualifiedName === "std.search.binaryU32" || item.qualifiedName === "std.search.binaryUsize"), "no-allocation search helpers should not emit resource facts");

  const callResolution = await zeroJson(["graph", "dump", "--json", "conformance/check/pass/call-resolution-inspection.0"]);
  const resolverCallReferences = callResolution.resolution.references.filter((item) => item.kind === "call");
  assert.equal(callResolution.semantics.calls.length, resolverCallReferences.length, "semantic calls should mirror resolver call references and skip operators");
  for (const call of callResolution.semantics.calls) assertSemanticCallResolutionMatches(callResolution, call, `call resolution semantic fact ${call.qualifiedName}`);
  const receiverCall = findSemanticCall(callResolution, (item) => item.qualifiedName === "counter.checkedRead", "receiver method semantic call fact");
  assert.equal(receiverCall.args.length, 0, "receiver method source-visible args");
  assert.equal(receiverCall.contract.expectedArgCount, 0, "receiver method expected arg count should exclude implicit self");
  assert.equal(receiverCall.receiver?.implicit, true, "receiver method should expose implicit receiver");
  assert.equal(receiverCall.receiver?.type, "Counter", "receiver method receiver type");
  const staticRead = findSemanticCall(callResolution, (item) => item.qualifiedName === "Counter.read", "static method call semantic fact");
  assert.equal(staticRead.receiver, null, "static method namespace calls should not report implicit receivers");
  assert.equal(staticRead.args.length, 1, "static method call source-visible args");
  assert.equal(staticRead.contract.expectedArgCount, 1, "static method expected arg count should keep explicit self arg");
  const interfaceRead = findSemanticCall(callResolution, (item) => item.qualifiedName === "T.read", "interface method semantic call fact");
  assert.equal(interfaceRead.resolution.targetKind, "interfaceMethod", "interface method semantic target kind");
  assert.equal(interfaceRead.resolution.symbolId, "symbol:call-resolution-inspection::type.Readable/method.read", "interface method semantic symbol");
  const eventKey = findSemanticCall(callResolution, (item) => item.qualifiedName === "Event.key", "choice variant semantic call fact");
  assert.equal(eventKey.resolution.targetKind, "variant", "choice variant semantic target kind");
  assert.equal(eventKey.resolution.symbolId, "symbol:call-resolution-inspection::type.Event/variant.key", "choice variant semantic symbol");

  await assertCheckFailureParity("conformance/native/fail/unchecked-fallible-call.0");
  await assertCheckFailureParity("conformance/check/fail/wrong-return-type.0");
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
  assert.equal(graph.graph.lowering, "typed-program-graph-mir", `${fixture}: graph build lowering`);
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

async function assertSourceCommandGraphCompilerPath() {
  const helloCheck = await zeroJson(["check", "--json", "examples/hello.0"]);
  assertSourceGraphRoute(helloCheck, "examples/hello.0");
  assert.equal(helloCheck.compilerCaches[0].sourceKind, "program-graph", "source check should use graph cache identity");
  assert.deepEqual(targetReadinessSummary(helloCheck.targetReadiness), {
    languageOk: true,
    buildable: true,
    stage: "ready",
    code: null,
  }, "source check target readiness");

  const helloBuildOut = `${outDir}/source-command-graph-build`;
  const helloBuild = await zeroJson(["build", "--json", "--target", "linux-musl-x64", "--out", helloBuildOut, "examples/hello.0"]);
  assertSourceGraphRoute(helloBuild, "examples/hello.0");
  assert.equal(helloBuild.generatedCBytes, 0, "source build should stay on direct backend");
  assert.equal(helloBuild.compilerCaches[0].sourceKind, "program-graph", "source build should use graph cache identity");
  assert.equal(helloBuild.incrementalInvalidation.sourceKind, "program-graph", "source build invalidation source kind");
  assert.equal(helloBuild.incrementalInvalidation.graphInput.artifact, "examples/hello.0", "source build graph input");
  assert(helloBuild.artifactBytes > 0, "source build should write an artifact");

  const helloSize = await zeroJson(["size", "--json", "--target", "linux-musl-x64", "examples/hello.0"]);
  assertSourceGraphRoute(helloSize, "examples/hello.0");
  assert.equal(helloSize.generatedCBytes, 0, "source size should stay on direct backend");
  assert.equal(helloSize.cBridgeFallback, false, "source size should not use C bridge fallback");
  assert.equal(helloSize.compilerCaches[0].sourceKind, "program-graph", "source size should use graph cache identity");

  const packageCheck = await zeroJson(["check", "--json", "conformance/packages/test-app"]);
  assertSourceGraphRoute(packageCheck, "conformance/packages/test-app/src/main.0");
  assert.equal(packageCheck.graph.moduleIdentity, "package:test-app@0.1.0", "source package graph identity");
  assert.equal(packageCheck.package.name, "test-app", "source package metadata");

  const packageTest = await zeroJson(["test", "--json", "conformance/packages/test-app"]);
  assertSourceGraphRoute(packageTest, "conformance/packages/test-app/src/main.0");
  assert.equal(packageTest.testBackend, "direct-frontend", "source graph test backend");
  assert.equal(packageTest.testDiscovery.mode, "package-graph", "source package tests should report graph discovery");
  assert.equal(packageTest.generatedCBytes, 0, "source graph tests should not use generated C");
  assert.equal(packageTest.cBridgeFallback, false, "source graph tests should not use C bridge fallback");
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

async function assertSourceEditReconcile() {
  const fixture = `${outDir}/identity-reconcile.0`;
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
  const baseArtifact = await dumpGraphArtifact(fixture, "identity-reconcile-base");

  await writeFile(fixture, original.replace("hello from zero\\n", "hello from graph\\n"));
  const edited = await zeroJson(["graph", "reconcile", "--json", baseArtifact, "--source", fixture]);
  assert.equal(edited.ok, true, "reconcile should accept an unambiguous literal edit");
  assert.equal(edited.identity.edited > 0, true, "reconcile should report edited nodes");
  assert.equal(edited.identity.ambiguous, 0, "literal edit should not be ambiguous");
  assert.equal(edited.graphPatch.available, true, "literal edit should produce a graph patch");
  assert.match(edited.graphPatch.text, /set node="#expr_[^"]+" field="value"/, "literal edit patch should target a graph node");
  const literalDecision = edited.decisions.find((decision) => decision.status === "edited" && decision.kind === "Literal");
  assert.deepEqual(literalDecision?.sourceRange.start, { line: 6, column: 27 }, "reconcile should map edited literal decisions to the source token");
  assert.deepEqual(literalDecision?.sourceRange.end, { line: 6, column: 47 }, "reconcile should include the full edited literal token");
  assert.equal(await zeroText(["graph", "reconcile", baseArtifact, "--source", fixture]), "program graph reconcile ok\n", "reconcile text output");

  await writeFile(fixture, original.replace("\npub fn main", "\nfn appendedHelper() -> i32 {\n    return 2\n}\n\npub fn main"));
  const ambiguous = await zeroJsonFailure(["graph", "reconcile", "--json", baseArtifact, "--source", fixture]);
  assert.equal(ambiguous.ok, false, "reconcile should reject ambiguous same-shape declaration edits");
  assert.equal(ambiguous.identity.ambiguous > 0, true, "reconcile should count ambiguous identities");
  assert.equal(ambiguous.diagnostics[0].code, "GRC001", "reconcile should explain ambiguous identity");

  const copiedFixture = `${outDir}/identity-reconcile-copy.0`;
  await writeFile(copiedFixture, original);
  const moduleMismatch = await zeroJsonFailure(["graph", "reconcile", "--json", baseArtifact, "--source", copiedFixture]);
  assert.equal(moduleMismatch.ok, false, "reconcile should reject different module identities");
  assert.equal(moduleMismatch.identity.moduleIdentityChanged, true, "reconcile should flag module identity changes");
  assert.equal(moduleMismatch.diagnostics[0].code, "GRC003", "reconcile should explain module identity mismatch");
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
  await assertResolutionFacts();
  await assertSemanticFacts();
  await assertUnconstrainedGenericTypeParams();
  await assertSourceCommandGraphCompilerPath();
  await assertBuildParity("examples/hello.0", "hello");
  await assertRunParity("examples/hello.0", "hello");
  await assertRunParity("conformance/native/pass/std-args.0", "std-args", ["alpha", "beta"]);
  await assertTestParity("conformance/native/pass/test-blocks.0", "test-blocks");
  await assertSourceBackedPatchParity();
  await assertGraphPatchPreservesNodeIds();

  await assertSourceEditIdentityBaseline();
  await assertSourceEditReconcile();
  await assertDeclarationSiblingIdentity();
  console.log("program graph parity ok");
} finally {
  await rm(outDir, { force: true, recursive: true });
}
