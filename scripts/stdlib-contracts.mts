#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { existsSync } from "node:fs";
import { readdir, readFile } from "node:fs/promises";
import { join } from "node:path";

type StdHelper = {
  name: string;
  module: string;
  returnType: string;
  argCount: number;
  argTypes: Array<string | null>;
  errorNames: string[];
  capability: string;
  targetSupport: string;
  allocationBehavior: string;
  emitsRuntimeHelper: boolean;
  kind: string;
};

type StdSourceModule = {
  module: string;
  path: string;
  graphPath: string;
};

type StdSourceCall = {
  publicName: string;
  targetName: string;
  module: string;
};

const targetSupportValues = new Set(["target-neutral", "host", "host-runtime"]);
const helperKindPattern = /^Z_STD_HELPER_KIND_[A-Z_]+$/;
const helperNamePattern = /^std\.[a-z][A-Za-z0-9]*\.[A-Za-z][A-Za-z0-9]*$/;
const moduleNamePattern = /^std\.[a-z][A-Za-z0-9]*$/;
const publicModuleDocsDir = "docs/articles/modules";
const skillPath = "skill-data/stdlib.md";
const stdSigPath = "native/zero-c/src/std_sig.c";
const stdSourcePath = "native/zero-c/src/std_source.c";
const embeddedStdlibPath = "native/zero-c/src/embedded_stdlib.inc";
const embeddedStdlibGraphPath = "native/zero-c/src/embedded_stdlib_graph.inc";
const fixtureRoots = ["examples", "conformance", "benchmarks/rosetta"];

function cBlock(text: string, marker: string): string {
  const start = text.indexOf(marker);
  if (start < 0) return "";
  const open = text.indexOf("{", start);
  if (open < 0) return "";
  let depth = 0;
  for (let index = open; index < text.length; index++) {
    const ch = text[index];
    if (ch === "{") depth++;
    else if (ch === "}") {
      depth--;
      if (depth === 0) return text.slice(open + 1, index);
    }
  }
  return "";
}

function parseCStringArray(raw: string): Array<string | null> {
  return raw
    .split(",")
    .map((value) => value.trim())
    .filter((value) => value.length > 0)
    .map((value) => value === "NULL" ? null : value.replace(/^"|"$/g, ""));
}

function duplicateValues(values: string[]): string[] {
  const seen = new Set<string>();
  const duplicates = new Set<string>();
  for (const value of values) {
    if (seen.has(value)) duplicates.add(value);
    seen.add(value);
  }
  return [...duplicates].sort((a, b) => a.localeCompare(b));
}

function cIdent(text: string): string {
  return text.replace(/[^A-Za-z0-9_]/g, "_");
}

function parseEmbeddedStdlibSource(text: string, sourcePath: string): string | null {
  const ident = `zero_embedded_stdlib_${cIdent(sourcePath)}_chunks`;
  const block = cBlock(text, `static const char *const ${ident}[] =`);
  if (block.length === 0) return null;
  let source = "";
  for (const match of block.matchAll(/"((?:\\.|[^"\\])*)"/g)) {
    source += JSON.parse(match[0]);
  }
  return source;
}

function parseEmbeddedStdlibGraph(text: string, graphPath: string): string | null {
  const ident = `zero_embedded_stdlib_graph_${cIdent(graphPath)}_chunks`;
  const block = cBlock(text, `static const char *const ${ident}[] =`);
  if (block.length === 0) return null;
  let source = "";
  for (const match of block.matchAll(/"((?:\\.|[^"\\])*)"/g)) {
    source += JSON.parse(match[0]);
  }
  return source;
}

function parseStdHelpers(text: string): StdHelper[] {
  const block = cBlock(text, "const ZStdHelperInfo z_std_helpers[] =");
  const helpers: StdHelper[] = [];
  const rowPattern = /\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(-?\d+)\s*,\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(true|false)\s*,\s*(Z_STD_HELPER_KIND_[A-Z_]+)\s*\}/g;
  for (const match of block.matchAll(rowPattern)) {
    const name = match[1];
    const parts = name.split(".");
    helpers.push({
      name,
      module: `${parts[0]}.${parts[1]}`,
      returnType: match[2],
      argCount: Number(match[3]),
      argTypes: parseCStringArray(match[4]),
      errorNames: parseCStringArray(match[5]).filter((value): value is string => value !== null),
      capability: match[6],
      targetSupport: match[7],
      allocationBehavior: match[8],
      emitsRuntimeHelper: match[9] === "true",
      kind: match[10],
    });
  }
  return helpers.sort((a, b) => a.name.localeCompare(b.name));
}

function parseStdSourceModules(text: string): StdSourceModule[] {
  const block = cBlock(text, "static const ZStdSourceModule std_source_modules[] =");
  return [...block.matchAll(/\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*zero_embedded_stdlib_[A-Za-z0-9_]+_chunks\s*,\s*zero_embedded_stdlib_graph_[A-Za-z0-9_]+_chunks\s*\}/g)]
    .map((match) => ({ module: match[1], path: match[2], graphPath: match[2].replace(/\.0$/, ".graph") }))
    .sort((a, b) => a.module.localeCompare(b.module));
}

function parseStdSourceCalls(text: string): StdSourceCall[] {
  const block = cBlock(text, "static const ZStdSourceCall std_source_calls[] =");
  return [...block.matchAll(/\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}/g)]
    .map((match) => ({ publicName: match[1], targetName: match[2], module: match[3] }))
    .sort((a, b) => a.publicName.localeCompare(b.publicName));
}

async function readOptional(path: string): Promise<string | null> {
  if (!existsSync(path)) return null;
  return readFile(path, "utf8");
}

async function sourceFilesUnder(root: string): Promise<string[]> {
  if (!existsSync(root)) return [];
  const entries = await readdir(root, { withFileTypes: true });
  const groups = await Promise.all(entries.map(async (entry) => {
    const path = join(root, entry.name);
    if (entry.isDirectory()) return sourceFilesUnder(path);
    if (entry.isFile() && entry.name.endsWith(".0")) return [path];
    return [];
  }));
  return groups.flat();
}

function pushIf(condition: boolean, failures: string[], message: string) {
  if (condition) failures.push(message);
}

const [stdSig, stdSource, embeddedStdlib, embeddedStdlibGraph, skill] = await Promise.all([
  readFile(stdSigPath, "utf8"),
  readFile(stdSourcePath, "utf8"),
  readFile(embeddedStdlibPath, "utf8"),
  readFile(embeddedStdlibGraphPath, "utf8"),
  readFile(skillPath, "utf8"),
]);

const stdEntries = await readdir("std", { withFileTypes: true });
const stdProjectionFiles = stdEntries
  .filter((entry) => entry.isFile() && entry.name.endsWith(".0"))
  .map((entry) => `std/${entry.name}`)
  .sort((a, b) => a.localeCompare(b));
const stdGraphFiles = stdEntries
  .filter((entry) => entry.isFile() && entry.name.endsWith(".graph"))
  .map((entry) => `std/${entry.name}`)
  .sort((a, b) => a.localeCompare(b));
const helpers = parseStdHelpers(stdSig);
const modules = [...new Set(helpers.map((helper) => helper.module))].sort((a, b) => a.localeCompare(b));
const sourceModules = parseStdSourceModules(stdSource);
const sourceCalls = parseStdSourceCalls(stdSource);
const fixtureFiles = (await Promise.all(fixtureRoots.map((root) => sourceFilesUnder(root))))
  .flat()
  .sort((a, b) => a.localeCompare(b));
const fixtureTexts = await Promise.all(fixtureFiles.map(async (path) => ({
  path,
  text: await readFile(path, "utf8"),
})));
const helpersByName = new Map(helpers.map((helper) => [helper.name, helper]));
const sourceModulesByName = new Map(sourceModules.map((module) => [module.module, module]));
const sourceModulePaths = new Set(sourceModules.map((module) => module.path));
const sourceModuleGraphPaths = new Set(sourceModules.map((module) => module.graphPath));
const sourceCallsByPublicName = new Map(sourceCalls.map((call) => [call.publicName, call]));
const sourceImplementedHelperModules = new Set(sourceCalls.map((call) => call.module));
const partiallyGraphBackedModules = new Set(["std.args", "std.cli", "std.codec", "std.env", "std.fs", "std.http", "std.io", "std.json", "std.math", "std.mem", "std.net", "std.parse", "std.path", "std.proc", "std.search", "std.time"]);
const docsEntries = await readdir(publicModuleDocsDir, { withFileTypes: true });
const publicModuleDocs = new Set(
  docsEntries
    .filter((entry) => entry.isFile() && entry.name.endsWith(".md"))
    .map((entry) => `std.${entry.name.replace(/\.md$/, "")}`),
);
const docsByModule = new Map<string, string>();
await Promise.all(modules.map(async (module) => {
  const path = `${publicModuleDocsDir}/${module.slice("std.".length)}.md`;
  const content = await readOptional(path);
  if (content !== null) docsByModule.set(module, content);
}));

const failures: string[] = [];

pushIf(helpers.length === 0, failures, "no stdlib helpers were parsed from std_sig.c");
for (const duplicate of duplicateValues(helpers.map((helper) => helper.name))) {
  failures.push(`duplicate stdlib helper contract: ${duplicate}`);
}
for (const helper of helpers) {
  pushIf(!helperNamePattern.test(helper.name), failures, `${helper.name}: helper name must be std.<module>.<name>`);
  pushIf(!moduleNamePattern.test(helper.module), failures, `${helper.name}: helper module must be std.<module>`);
  pushIf(helper.returnType.length === 0, failures, `${helper.name}: return type is empty`);
  pushIf(helper.argCount < 0 || helper.argCount > 4, failures, `${helper.name}: arg count ${helper.argCount} exceeds contract bounds`);
  pushIf(helper.argTypes.length > 4, failures, `${helper.name}: arg type list exceeds contract bounds`);
  pushIf(helper.errorNames.length > 4, failures, `${helper.name}: error list exceeds contract bounds`);
  pushIf(!targetSupportValues.has(helper.targetSupport), failures, `${helper.name}: unknown target support '${helper.targetSupport}'`);
  pushIf(helper.capability.length === 0, failures, `${helper.name}: capability is empty`);
  pushIf(helper.allocationBehavior.length === 0, failures, `${helper.name}: allocation behavior is empty`);
  pushIf(!helperKindPattern.test(helper.kind), failures, `${helper.name}: helper kind '${helper.kind}' is not a std helper kind`);
}

for (const module of modules) {
  const docsPath = `${publicModuleDocsDir}/${module.slice("std.".length)}.md`;
  pushIf(!publicModuleDocs.has(module), failures, `${module}: missing public module docs at ${docsPath}`);
  pushIf(!skill.includes(module), failures, `${module}: skill-data/stdlib.md does not mention module`);
}

for (const helper of helpers) {
  const docs = docsByModule.get(helper.module);
  if (docs) {
    pushIf(!docs.includes(helper.name), failures, `${helper.name}: public module docs do not mention helper`);
  }
  pushIf(!fixtureTexts.some((fixture) => fixture.text.includes(helper.name)), failures, `${helper.name}: no example, conformance fixture, or Rosetta task exercises helper`);
}

for (const projectionPath of stdProjectionFiles) {
  const graphPath = projectionPath.replace(/\.0$/, ".graph");
  pushIf(!stdGraphFiles.includes(graphPath), failures, `${projectionPath}: missing sibling graph store ${graphPath}`);
  pushIf(!sourceModulePaths.has(projectionPath), failures, `${projectionPath}: std projection is not registered as an embedded graph-backed module`);
}
for (const graphPath of stdGraphFiles) {
  const projectionPath = graphPath.replace(/\.graph$/, ".0");
  pushIf(!stdProjectionFiles.includes(projectionPath), failures, `${graphPath}: graph store has no sibling projection ${projectionPath}`);
  pushIf(!sourceModuleGraphPaths.has(graphPath), failures, `${graphPath}: std graph store is not registered for embedding`);
}

for (const sourceModule of sourceModules) {
  pushIf(!moduleNamePattern.test(sourceModule.module), failures, `${sourceModule.module}: invalid graph-backed std module name`);
  pushIf(!existsSync(sourceModule.path), failures, `${sourceModule.module}: missing std projection ${sourceModule.path}`);
  pushIf(!modules.includes(sourceModule.module), failures, `${sourceModule.module}: graph-backed module has no public helpers`);
  const embedded = parseEmbeddedStdlibSource(embeddedStdlib, sourceModule.path);
  const embeddedGraph = parseEmbeddedStdlibGraph(embeddedStdlibGraph, sourceModule.graphPath);
  const source = existsSync(sourceModule.path) ? await readFile(sourceModule.path, "utf8") : null;
  const graph = existsSync(sourceModule.graphPath) ? await readFile(sourceModule.graphPath, "utf8") : null;
  pushIf(embedded === null, failures, `${sourceModule.module}: embedded stdlib projection is missing for ${sourceModule.path}`);
  pushIf(!existsSync(sourceModule.graphPath), failures, `${sourceModule.module}: missing graph-backed std module ${sourceModule.graphPath}`);
  pushIf(embeddedGraph === null, failures, `${sourceModule.module}: embedded stdlib graph is missing for ${sourceModule.graphPath}`);
  pushIf(source !== null && embedded !== null && embedded !== source, failures, `${sourceModule.module}: embedded stdlib projection is stale for ${sourceModule.path}`);
  pushIf(graph !== null && embeddedGraph !== null && embeddedGraph !== graph, failures, `${sourceModule.module}: embedded stdlib graph is stale for ${sourceModule.graphPath}`);
}
for (const duplicate of duplicateValues(sourceCalls.map((call) => call.publicName))) {
  failures.push(`duplicate graph-backed public helper mapping: ${duplicate}`);
}
for (const duplicate of duplicateValues(sourceCalls.map((call) => call.targetName))) {
  failures.push(`duplicate graph-backed target helper mapping: ${duplicate}`);
}
for (const call of sourceCalls) {
  const helper = helpersByName.get(call.publicName);
  const sourceModule = sourceModulesByName.get(call.module);
  pushIf(!helper, failures, `${call.publicName}: graph-backed mapping has no std_sig contract`);
  pushIf(!sourceModule, failures, `${call.publicName}: graph-backed mapping references unknown module ${call.module}`);
  pushIf(helper !== undefined && helper.module !== call.module, failures, `${call.publicName}: graph-backed mapping module ${call.module} does not match helper module ${helper?.module}`);
  pushIf(helper !== undefined && !helper.emitsRuntimeHelper, failures, `${call.publicName}: graph-backed helper must emit runtime helper code`);
  if (sourceModule && existsSync(sourceModule.path)) {
    const source = await readFile(sourceModule.path, "utf8");
    const targetPattern = new RegExp(`\\bfn\\s+${call.targetName.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}(?:<[^>]+>)?\\s*\\(`);
    pushIf(!targetPattern.test(source), failures, `${call.publicName}: target function ${call.targetName} is missing from ${sourceModule.path}`);
  }
}
for (const sourceModule of sourceModules) {
  if (!sourceImplementedHelperModules.has(sourceModule.module)) continue;
  if (partiallyGraphBackedModules.has(sourceModule.module)) continue;
  for (const helper of helpers.filter((candidate) => candidate.module === sourceModule.module)) {
    pushIf(!sourceCallsByPublicName.has(helper.name), failures, `${helper.name}: graph-backed module helper is missing std_source.c mapping`);
  }
}
const graphStorageComplete = stdProjectionFiles.length === sourceModules.length &&
  stdGraphFiles.length === sourceModules.length &&
  stdProjectionFiles.every((projectionPath) => sourceModulePaths.has(projectionPath)) &&
  stdGraphFiles.every((graphPath) => sourceModuleGraphPaths.has(graphPath));
const summary = {
  schema: 1,
  helperCount: helpers.length,
  moduleCount: modules.length,
  modules,
  stdProjectionModuleCount: stdProjectionFiles.length,
  embeddedStdModuleCount: sourceModules.length,
  graphStoredModuleCount: sourceModules.length,
  graphStorageComplete,
  sourceImplementedHelperModuleCount: sourceImplementedHelperModules.size,
  graphBackedSourceHelperCount: sourceCalls.length,
  nativeOrTableHelperCount: helpers.length - sourceCalls.length,
  fixtureFileCount: fixtureFiles.length,
  ok: failures.length === 0,
  failures,
};

if (process.argv.includes("--json")) {
  console.log(JSON.stringify(summary, null, 2));
} else if (failures.length === 0) {
  console.log(`stdlib contracts ok (${summary.helperCount} helpers, ${summary.graphStoredModuleCount}/${summary.stdProjectionModuleCount} std modules graph-stored, ${summary.graphBackedSourceHelperCount} graph-backed source helper mappings)`);
} else {
  console.error(`stdlib contracts failed (${failures.length} issue${failures.length === 1 ? "" : "s"})`);
  for (const failure of failures.slice(0, 40)) console.error(`- ${failure}`);
  if (failures.length > 40) console.error(`- ... ${failures.length - 40} more`);
}

if (failures.length > 0) process.exitCode = 1;
