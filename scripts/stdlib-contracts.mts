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

type StdSkillSignature = {
  returnType: string;
  argTypes: string[];
  errorNames: string[];
};

type AllocationCategory =
  | "borrowed-view"
  | "caller-storage"
  | "explicit-allocator"
  | "host-resource"
  | "no-allocation"
  | "owned-resource";

type ReturnViewCategory =
  | "borrowed-view"
  | "caller-storage-view"
  | "explicit-allocator-view"
  | "static-view";

type ResourceLifetimeCategory =
  | "borrowed-resource"
  | "owned-resource"
  | "resource-capability"
  | "resource-value";

type StreamingBehaviorCategory =
  | "bounded-input"
  | "buffer-copy"
  | "chunked-read"
  | "line-scan"
  | "stream-scan";

const knownCapabilities = new Set([
  "alloc",
  "args",
  "codec",
  "env",
  "fs",
  "memory",
  "net",
  "none",
  "parse",
  "path",
  "proc",
  "rand",
  "time",
]);
const hostOnlyCapabilities = new Set(["args", "env", "fs", "proc"]);
const runtimeCapabilities = new Set(["memory", "net"]);
const targetSupportValues = new Set(["target-neutral", "host", "host-runtime"]);
const errorNamePattern = /^[A-Z][A-Za-z0-9]*$/;
const helperKindPattern = /^Z_STD_HELPER_KIND_[A-Z_]+$/;
const helperNamePattern = /^std\.[a-z][A-Za-z0-9]*\.[A-Za-z][A-Za-z0-9]*$/;
const moduleNamePattern = /^std\.[a-z][A-Za-z0-9]*$/;
const vagueAllocationBehaviors = new Set(["directory traversal"]);
const knownResourceTypes = new Set([
  "BufferedReader",
  "BufferedWriter",
  "ByteBuf",
  "File",
  "FixedBufAlloc",
  "FixedReader",
  "FixedWriter",
  "RandSource",
  "Vec",
]);
const countedResourceTypes = new Set([
  "Address",
  "Alloc",
  "BufferedReader",
  "BufferedWriter",
  "ByteBuf",
  "File",
  "FixedBufAlloc",
  "FixedReader",
  "FixedWriter",
  "Fs",
  "GeneralAlloc",
  "HttpClient",
  "HttpError",
  "HttpHeaderValue",
  "HttpMethod",
  "HttpResult",
  "HttpServer",
  "JsonDoc",
  "Net",
  "NullAlloc",
  "PageAlloc",
  "ProcChild",
  "ProcStatus",
  "RandSource",
  "Vec",
]);
const publicModuleDocsDir = "docs/articles/modules";
const skillPath = "skill-data/stdlib.md";
const stdSigPath = "native/zero-c/src/std_sig.c";
const stdSourcePath = "native/zero-c/src/std_source.c";
const embeddedStdlibGraphPath = "native/zero-c/src/embedded_stdlib_graph.inc";
const embeddedStdlibGraphDir = "native/zero-c/src/embedded_stdlib_graph";
const fixtureRoots = ["examples", "conformance", "benchmarks/rosetta"];
const generatedFixtureFiles = ["conformance/run.mjs"];

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
  const values: Array<string | null> = [];
  const tokenPattern = /"((?:\\.|[^"\\])*)"|NULL/g;
  for (const match of raw.matchAll(tokenPattern)) {
    values.push(match[0] === "NULL" ? null : match[1].replace(/\\"/g, "\"").replace(/\\\\/g, "\\"));
  }
  return values;
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

function parseEmbeddedStdlibGraphBytes(text: string, graphPath: string): Buffer | null {
  const ident = `zero_embedded_stdlib_graph_${cIdent(graphPath)}_bytes`;
  const block = cBlock(text, `static const unsigned char ${ident}[] =`);
  if (block.length === 0) return null;
  const bytes = [...block.matchAll(/0x([0-9a-fA-F]{2})/g)].map((match) => Number.parseInt(match[1], 16));
  return Buffer.from(bytes);
}

function parseEmbeddedStdlibGraphPart(text: string, graphPath: string): string | null {
  const partName = `${cIdent(graphPath)}.inc`;
  const pattern = new RegExp(`#include\\s+"embedded_stdlib_graph/${partName.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}"`);
  return pattern.test(text) ? join(embeddedStdlibGraphDir, partName) : null;
}

async function readEmbeddedStdlibGraphBytes(text: string | null, graphPath: string): Promise<Buffer | null> {
  if (text !== null) {
    const embeddedGraph = parseEmbeddedStdlibGraphBytes(text, graphPath);
    if (embeddedGraph !== null) return embeddedGraph;
    const partPath = parseEmbeddedStdlibGraphPart(text, graphPath);
    if (partPath !== null && existsSync(partPath)) {
      const partText = await readFile(partPath, "utf8");
      return parseEmbeddedStdlibGraphBytes(partText, graphPath);
    }
  }
  return existsSync(graphPath) ? readFile(graphPath) : null;
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

function parseSkillSignatures(text: string): Map<string, StdSkillSignature> {
  const start = text.indexOf("## Function Signatures");
  if (start < 0) return new Map();
  const end = text.indexOf("## Maybe Pattern", start);
  const catalog = text.slice(start, end < 0 ? undefined : end);
  const signatures = new Map<string, StdSkillSignature>();
  const modulePattern = /### (std\.[A-Za-z0-9]+)\s+```text\n([\s\S]*?)\n```/g;
  for (const moduleMatch of catalog.matchAll(modulePattern)) {
    const module = moduleMatch[1];
    for (const rawLine of moduleMatch[2].split(/\r?\n/)) {
      const line = rawLine.trim();
      if (line.length === 0) continue;
      const signatureMatch = line.match(/^([A-Za-z][A-Za-z0-9]*)\((.*)\) -> (.+)$/);
      if (!signatureMatch) continue;
      const argText = signatureMatch[2].trim();
      const argTypes = argText.length === 0
        ? []
        : splitTopLevelCommaList(argText).map((arg) => {
            const colon = arg.indexOf(":");
            return (colon >= 0 ? arg.slice(colon + 1) : arg).trim();
          });
      const returnAndErrors = signatureMatch[3].trim();
      const errorMatch = returnAndErrors.match(/\s+raises\s+\[([^\]]*)\]\s*$/);
      const errorNames = errorMatch
        ? errorMatch[1].split(/\s*,\s*/).map((name) => name.trim()).filter((name) => name.length > 0)
        : [];
      const returnType = returnAndErrors.replace(/\s+raises\s+\[[^\]]*\]\s*$/, "").trim();
      signatures.set(`${module}.${signatureMatch[1]}`, { returnType, argTypes, errorNames });
    }
  }
  return signatures;
}

function splitTopLevelCommaList(text: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let start = 0;
  for (let index = 0; index < text.length; index++) {
    const ch = text[index];
    if (ch === "<" || ch === "(" || ch === "[") {
      depth++;
    } else if (ch === ">" || ch === ")" || ch === "]") {
      if (depth > 0) depth--;
    } else if (ch === "," && depth === 0) {
      const part = text.slice(start, index).trim();
      if (part.length > 0) parts.push(part);
      start = index + 1;
    }
  }
  const tail = text.slice(start).trim();
  if (tail.length > 0) parts.push(tail);
  return parts;
}

function parseStdSourceModules(text: string): StdSourceModule[] {
  const block = cBlock(text, "static const ZStdSourceModule std_source_modules[] =");
  return [...block.matchAll(/\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*zero_embedded_stdlib_graph_[A-Za-z0-9_]+_bytes\s*,\s*sizeof\s*\(\s*zero_embedded_stdlib_graph_[A-Za-z0-9_]+_bytes\s*\)\s*\}/g)]
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

function incrementCount(map: Map<string, number>, key: string) {
  map.set(key, (map.get(key) ?? 0) + 1);
}

function sortedObjectFromCounts(counts: Map<string, number>): Record<string, number> {
  return Object.fromEntries([...counts.entries()].sort((a, b) => a[0].localeCompare(b[0])));
}

function allocationCategory(helper: StdHelper): AllocationCategory | null {
  const behavior = helper.allocationBehavior.toLowerCase();
  if (
    behavior.includes("no allocation") ||
    behavior.includes("never allocates") ||
    behavior.includes("bounds-checked") ||
    behavior.includes("checks ") ||
    behavior.includes("counts ") ||
    behavior.includes("decodes a bounded") ||
    behavior.includes("deterministic test source") ||
    behavior.includes("inspects ") ||
    behavior.includes("little-endian byte read") ||
    behavior.includes("little-endian byte write primitive") ||
    behavior.includes("matches ") ||
    behavior.includes("reads body start offset") ||
    behavior.includes("reads header-value") ||
    behavior.includes("reads http status metadata") ||
    behavior.includes("reads response") ||
    behavior.includes("reads transport error metadata") ||
    behavior.includes("returns structured validation status") ||
    behavior.includes("structured validation status code") ||
    behavior.includes("streaming token count") ||
    behavior.includes("typed decode boundary metadata")
  ) return "no-allocation";
  if (
    behavior.includes("caller") ||
    behavior.includes("buffer") ||
    behavior.includes("storage") ||
    behavior.includes("writes ") ||
    behavior.includes("fills ") ||
    behavior.includes("sorts ")
  ) return "caller-storage";
  if (
    behavior.includes("borrows") ||
    behavior.includes("borrowed") ||
    behavior.includes("raw query parameter") ||
    behavior.includes("raw top-level") ||
    behavior.includes("field value slice")
  ) return "borrowed-view";
  if (behavior.includes("owned") || behavior.includes("handle") || behavior.includes("closes ")) return "owned-resource";
  if (behavior.includes("alloc") || behavior.includes("allocator") || behavior.includes("arena")) return "explicit-allocator";
  if (
    behavior.includes("directory") ||
    behavior.includes("entropy") ||
    behavior.includes("file") ||
    behavior.includes("http listener") ||
    behavior.includes("network") ||
    behavior.includes("process") ||
    behavior.includes("tls boundary")
  ) return "host-resource";
  return null;
}

function resourceWrapperUses(type: string): Array<{ wrapper: string; typeName: string }> {
  return [...type.matchAll(/\b(owned|mutref|ref)<([A-Za-z][A-Za-z0-9]*)>/g)]
    .map((match) => ({ wrapper: match[1], typeName: match[2] }));
}

function resourceTypeUses(type: string): string[] {
  const wrappers = resourceWrapperUses(type).map((use) => use.typeName);
  const exact = type.match(/^[A-Za-z][A-Za-z0-9]*$/) && countedResourceTypes.has(type) ? [type] : [];
  return [...wrappers, ...exact];
}

function unwrapMaybe(type: string): string {
  const match = type.match(/^Maybe<(.+)>$/);
  return match ? match[1] : type;
}

function returnsBorrowedViewType(type: string): boolean {
  const unwrapped = unwrapMaybe(type);
  return unwrapped === "String" || unwrapped.includes("Span<");
}

function hasMutableSpanArg(helper: StdHelper): boolean {
  return helper.argTypes.some((type) => type?.startsWith("MutSpan<"));
}

function describesStaticView(behavior: string): boolean {
  return behavior.includes("boundary metadata") ||
    behavior.includes("status text") ||
    behavior.includes("tls boundary") ||
    behavior.includes("borrows static") ||
    (behavior.includes("static") && !behavior.includes("borrows/static"));
}

function returnViewCategory(helper: StdHelper, category: AllocationCategory | null): ReturnViewCategory | null {
  if (!returnsBorrowedViewType(helper.returnType)) return null;
  const behavior = helper.allocationBehavior.toLowerCase();
  if (describesStaticView(behavior)) return "static-view";
  if (behavior.includes("borrows")) return "borrowed-view";
  if (helper.returnType.includes("MutSpan<") && category === "explicit-allocator") return "explicit-allocator-view";
  if (category === "caller-storage" && hasMutableSpanArg(helper)) return "caller-storage-view";
  if (category === "explicit-allocator") return "explicit-allocator-view";
  if (category === "borrowed-view") return "borrowed-view";
  return null;
}

function resourceLifetimeCategories(helper: StdHelper): ResourceLifetimeCategory[] {
  const categories = new Set<ResourceLifetimeCategory>();
  if (helper.returnType.includes("owned<")) categories.add("owned-resource");
  if (helperTypes(helper).some((type) => resourceWrapperUses(type).some((use) => use.wrapper === "ref" || use.wrapper === "mutref"))) {
    categories.add("borrowed-resource");
  }
  const unwrappedReturn = unwrapMaybe(helper.returnType);
  if (countedResourceTypes.has(unwrappedReturn) && !helper.returnType.includes("<")) categories.add("resource-value");
  if (helper.argTypes.some((type) => type !== null && countedResourceTypes.has(type))) categories.add("resource-capability");
  return [...categories].sort((a, b) => a.localeCompare(b));
}

function streamingBehaviorCategory(helper: StdHelper): StreamingBehaviorCategory | null {
  const behavior = helper.allocationBehavior.toLowerCase();
  if (helper.name.endsWith("streamTokens") || helper.name.endsWith("streamTokensBytes") || helper.name.endsWith("readUntilDelimiter") || helper.name.endsWith("readUntilDelimiterStart") || behavior.includes("streaming")) return "stream-scan";
  if (helper.name.endsWith("readBytesAt") || behavior.includes("chunked")) return "chunked-read";
  if (helper.name.includes("Within") || behavior.includes("within size limit")) return "bounded-input";
  if (helper.name.endsWith("nextLine") || helper.name.endsWith("nextLineStart") || helper.name.endsWith("readLine") || helper.name.endsWith("readLineStart") || helper.name.endsWith("countLines")) return "line-scan";
  if (helper.name.endsWith(".copy") || helper.name.endsWith("copyN") || helper.name.endsWith("copyBuffer") || behavior.includes("copies through caller buffer")) return "buffer-copy";
  return null;
}

function capabilityTargetKey(helper: StdHelper): string {
  return `${helper.targetSupport}:${helper.capability}`;
}

function helperTypes(helper: StdHelper): string[] {
  return [
    helper.returnType,
    ...helper.argTypes.filter((type): type is string => type !== null),
  ];
}

const [stdSig, stdSource, embeddedStdlibGraph, skill] = await Promise.all([
  readFile(stdSigPath, "utf8"),
  readFile(stdSourcePath, "utf8"),
  readOptional(embeddedStdlibGraphPath),
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
const skillSignatures = parseSkillSignatures(skill);
const modules = [...new Set(helpers.map((helper) => helper.module))].sort((a, b) => a.localeCompare(b));
const sourceModules = parseStdSourceModules(stdSource);
const sourceCalls = parseStdSourceCalls(stdSource);
const fixtureFiles = [
  ...(await Promise.all(fixtureRoots.map((root) => sourceFilesUnder(root)))).flat(),
  ...generatedFixtureFiles.filter((path) => existsSync(path)),
]
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
const graphImplementedHelperModules = new Set(sourceCalls.map((call) => call.module));
const partiallyGraphBackedModules = new Set(["std.args", "std.cli", "std.codec", "std.crypto", "std.env", "std.fmt", "std.fs", "std.http", "std.io", "std.json", "std.math", "std.mem", "std.net", "std.parse", "std.path", "std.proc", "std.rand", "std.search", "std.str", "std.time", "std.toml"]);
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
const allocationCategoryCounts = new Map<string, number>();
const capabilityCounts = new Map<string, number>();
const capabilityTargetCounts = new Map<string, number>();
const namedErrorCounts = new Map<string, number>();
const resourceTypeCounts = new Map<string, number>();
const resourceLifetimeCounts = new Map<string, number>();
const returnViewCounts = new Map<string, number>();
const targetSupportCounts = new Map<string, number>();
const streamingBehaviorCounts = new Map<string, number>();

pushIf(stdSource.includes("embedded_stdlib.inc"), failures, "std_source.c must not include embedded stdlib source chunks");
pushIf(/zero_embedded_stdlib_[A-Za-z0-9_]+_chunks/.test(stdSource), failures, "std_source.c must not reference embedded stdlib source chunks");
pushIf(/z_std_source_module_copy_source/.test(stdSource), failures, "std_source.c must not expose source-copy stdlib bridges");

pushIf(helpers.length === 0, failures, "no stdlib helpers were parsed from std_sig.c");
for (const duplicate of duplicateValues(helpers.map((helper) => helper.name))) {
  failures.push(`duplicate stdlib helper contract: ${duplicate}`);
}
for (const helper of helpers) {
  pushIf(!helperNamePattern.test(helper.name), failures, `${helper.name}: helper name must be std.<module>.<name>`);
  pushIf(!moduleNamePattern.test(helper.module), failures, `${helper.name}: helper module must be std.<module>`);
  pushIf(helper.returnType.length === 0, failures, `${helper.name}: return type is empty`);
  pushIf(helper.argCount < 0 || helper.argCount > 5, failures, `${helper.name}: arg count ${helper.argCount} exceeds contract bounds`);
  pushIf(helper.argTypes.length > 5, failures, `${helper.name}: arg type list exceeds contract bounds`);
  for (let index = 0; index < helper.argCount; index++) {
    pushIf(helper.argTypes[index] === null || helper.argTypes[index] === undefined, failures, `${helper.name}: arg ${index + 1} is missing from std_sig.c`);
  }
  pushIf(helper.errorNames.length > 4, failures, `${helper.name}: error list exceeds contract bounds`);
  for (const duplicate of duplicateValues(helper.errorNames)) {
    failures.push(`${helper.name}: duplicate named error '${duplicate}'`);
  }
  for (const errorName of helper.errorNames) {
    pushIf(!errorNamePattern.test(errorName), failures, `${helper.name}: invalid named error '${errorName}'`);
    incrementCount(namedErrorCounts, errorName);
  }
  pushIf(helper.name.endsWith("OrRaise") && helper.errorNames.length === 0, failures, `${helper.name}: OrRaise helper must declare named errors`);
  pushIf(helper.errorNames.length > 0 && helper.returnType.startsWith("Maybe<"), failures, `${helper.name}: named-error helpers must not also use Maybe return failure`);
  pushIf(!targetSupportValues.has(helper.targetSupport), failures, `${helper.name}: unknown target support '${helper.targetSupport}'`);
  if (targetSupportValues.has(helper.targetSupport)) incrementCount(targetSupportCounts, helper.targetSupport);
  pushIf(helper.capability.length === 0, failures, `${helper.name}: capability is empty`);
  pushIf(helper.capability.length > 0 && !knownCapabilities.has(helper.capability), failures, `${helper.name}: unknown capability '${helper.capability}'`);
  if (helper.capability.length > 0 && knownCapabilities.has(helper.capability)) incrementCount(capabilityCounts, helper.capability);
  if (targetSupportValues.has(helper.targetSupport) && helper.capability.length > 0 && knownCapabilities.has(helper.capability)) {
    incrementCount(capabilityTargetCounts, capabilityTargetKey(helper));
  }
  pushIf(helper.allocationBehavior.length === 0, failures, `${helper.name}: allocation behavior is empty`);
  pushIf(vagueAllocationBehaviors.has(helper.allocationBehavior), failures, `${helper.name}: allocation behavior '${helper.allocationBehavior}' is too vague`);
  const category = allocationCategory(helper);
  pushIf(category === null, failures, `${helper.name}: allocation behavior '${helper.allocationBehavior}' does not fit a known ownership category`);
  if (category !== null) incrementCount(allocationCategoryCounts, category);
  pushIf(hostOnlyCapabilities.has(helper.capability) && helper.targetSupport !== "host", failures, `${helper.name}: capability '${helper.capability}' must use host target support`);
  pushIf(helper.targetSupport === "host" && helper.capability === "none", failures, `${helper.name}: host helper must declare its host capability`);
  pushIf(helper.targetSupport === "host-runtime" && !runtimeCapabilities.has(helper.capability), failures, `${helper.name}: host-runtime helper must use a runtime-facing capability`);
  pushIf(helper.targetSupport === "target-neutral" && category === "host-resource", failures, `${helper.name}: target-neutral helper must not depend on host resources`);
  const viewCategory = returnViewCategory(helper, category);
  pushIf(returnsBorrowedViewType(helper.returnType) && viewCategory === null, failures, `${helper.name}: return type ${helper.returnType} needs explicit return-view lifetime metadata`);
  if (viewCategory !== null) incrementCount(returnViewCounts, viewCategory);
  for (const lifetimeCategory of resourceLifetimeCategories(helper)) {
    incrementCount(resourceLifetimeCounts, lifetimeCategory);
  }
  const streamingCategory = streamingBehaviorCategory(helper);
  if (streamingCategory !== null) incrementCount(streamingBehaviorCounts, streamingCategory);
  pushIf(helper.name.includes("Within") && streamingCategory !== "bounded-input", failures, `${helper.name}: bounded helper must declare bounded-input streaming behavior`);
  pushIf(helper.name.endsWith("readBytesAt") && streamingCategory !== "chunked-read", failures, `${helper.name}: offset read helper must declare chunked-read behavior`);
  pushIf(helper.name.endsWith("streamTokens") && streamingCategory !== "stream-scan", failures, `${helper.name}: stream helper must declare stream-scan behavior`);
  for (const type of helperTypes(helper)) {
    for (const use of resourceWrapperUses(type)) {
      pushIf(!knownResourceTypes.has(use.typeName), failures, `${helper.name}: ${use.wrapper}<${use.typeName}> uses an unknown resource type`);
    }
    for (const typeName of resourceTypeUses(type)) incrementCount(resourceTypeCounts, typeName);
  }
  if (helper.returnType.includes("owned<")) {
    pushIf(category !== "owned-resource" && category !== "explicit-allocator", failures, `${helper.name}: owned return must declare owned-resource or explicit-allocator allocation behavior`);
  }
  pushIf(!helperKindPattern.test(helper.kind), failures, `${helper.name}: helper kind '${helper.kind}' is not a std helper kind`);
}

for (const module of modules) {
  const docsPath = `${publicModuleDocsDir}/${module.slice("std.".length)}.md`;
  pushIf(!publicModuleDocs.has(module), failures, `${module}: missing public module docs at ${docsPath}`);
  pushIf(!skill.includes(module), failures, `${module}: skill-data/stdlib.md does not mention module`);
}

for (const helper of helpers) {
  const signature = skillSignatures.get(helper.name);
  pushIf(!signature, failures, `${helper.name}: skill-data/stdlib.md is missing a Function Signatures row`);
  if (signature) {
    pushIf(signature.returnType !== helper.returnType, failures, `${helper.name}: skill return type ${signature.returnType} does not match std_sig.c ${helper.returnType}`);
    pushIf(signature.argTypes.length !== helper.argCount, failures, `${helper.name}: skill arg count ${signature.argTypes.length} does not match std_sig.c ${helper.argCount}`);
    pushIf(signature.errorNames.join(",") !== helper.errorNames.join(","), failures, `${helper.name}: skill named errors [${signature.errorNames.join(", ")}] do not match std_sig.c [${helper.errorNames.join(", ")}]`);
    for (let index = 0; index < helper.argCount && index < signature.argTypes.length; index++) {
      const expected = helper.argTypes[index];
      if (expected !== null && expected !== undefined) {
        pushIf(signature.argTypes[index] !== expected, failures, `${helper.name}: skill arg ${index + 1} type ${signature.argTypes[index]} does not match std_sig.c ${expected}`);
      }
    }
  }
  const docs = docsByModule.get(helper.module);
  if (docs) {
    pushIf(!docs.includes(helper.name), failures, `${helper.name}: public module docs do not mention helper`);
  }
  pushIf(!fixtureTexts.some((fixture) => fixture.text.includes(helper.name)), failures, `${helper.name}: no example, conformance fixture, or Rosetta task exercises helper`);
}
for (const name of [...skillSignatures.keys()].sort((a, b) => a.localeCompare(b))) {
  pushIf(!helpersByName.has(name), failures, `${name}: skill-data/stdlib.md Function Signatures row has no std_sig.c helper`);
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
  const embeddedGraph = await readEmbeddedStdlibGraphBytes(embeddedStdlibGraph, sourceModule.graphPath);
  const graph = existsSync(sourceModule.graphPath) ? await readFile(sourceModule.graphPath) : null;
  pushIf(!existsSync(sourceModule.graphPath), failures, `${sourceModule.module}: missing graph-backed std module ${sourceModule.graphPath}`);
  pushIf(embeddedGraph === null, failures, `${sourceModule.module}: embedded stdlib graph is missing for ${sourceModule.graphPath}`);
  pushIf(graph !== null && graph.subarray(0, 8).toString("latin1") !== "ZRGBIN1\0", failures, `${sourceModule.module}: std graph store must be binary ${sourceModule.graphPath}`);
  pushIf(graph !== null && embeddedGraph !== null && Buffer.compare(embeddedGraph, graph) !== 0, failures, `${sourceModule.module}: embedded stdlib graph is stale for ${sourceModule.graphPath}`);
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
  if (!graphImplementedHelperModules.has(sourceModule.module)) continue;
  if (partiallyGraphBackedModules.has(sourceModule.module)) continue;
  for (const helper of helpers.filter((candidate) => candidate.module === sourceModule.module)) {
    pushIf(!sourceCallsByPublicName.has(helper.name), failures, `${helper.name}: graph-backed module helper is missing embedded std graph mapping`);
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
  graphImplementedHelperModuleCount: graphImplementedHelperModules.size,
  graphBackedHelperCount: sourceCalls.length,
  nativeOrTableHelperCount: helpers.length - sourceCalls.length,
  allocationCategoryCounts: sortedObjectFromCounts(allocationCategoryCounts),
  capabilityCounts: sortedObjectFromCounts(capabilityCounts),
  capabilityTargetCounts: sortedObjectFromCounts(capabilityTargetCounts),
  namedErrorCounts: sortedObjectFromCounts(namedErrorCounts),
  returnViewCounts: sortedObjectFromCounts(returnViewCounts),
  resourceLifetimeCounts: sortedObjectFromCounts(resourceLifetimeCounts),
  resourceTypeCounts: sortedObjectFromCounts(resourceTypeCounts),
  streamingBehaviorCounts: sortedObjectFromCounts(streamingBehaviorCounts),
  targetSupportCounts: sortedObjectFromCounts(targetSupportCounts),
  fixtureFileCount: fixtureFiles.length,
  ok: failures.length === 0,
  failures,
};

if (process.argv.includes("--json")) {
  console.log(JSON.stringify(summary, null, 2));
} else if (failures.length === 0) {
  console.log(`stdlib contracts ok (${summary.helperCount} helpers, ${summary.graphStoredModuleCount}/${summary.stdProjectionModuleCount} std modules graph-stored, ${summary.graphBackedHelperCount} graph-backed helper mappings)`);
} else {
  console.error(`stdlib contracts failed (${failures.length} issue${failures.length === 1 ? "" : "s"})`);
  for (const failure of failures.slice(0, 40)) console.error(`- ${failure}`);
  if (failures.length > 40) console.error(`- ... ${failures.length - 40} more`);
}

if (failures.length > 0) process.exitCode = 1;
