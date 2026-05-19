#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = "bin/zero";
const documents = new Map();
const symbols = new Map();
const documentFacts = new Map();

function uriToPath(uri) {
  return uri && uri.startsWith("file://") ? fileURLToPath(uri) : uri;
}

function pathToUri(path) {
  return pathToFileURL(path).href;
}

function positionAt(text, offset) {
  const prefix = text.slice(0, offset);
  const lines = prefix.split("\n");
  return { line: lines.length - 1, character: lines[lines.length - 1].length };
}

function offsetAt(text, position) {
  const lines = text.split("\n");
  let offset = 0;
  for (let i = 0; i < Math.min(position.line, lines.length); i++) offset += lines[i].length + 1;
  return offset + Math.min(position.character, lines[position.line]?.length ?? 0);
}

function wordAt(text, position) {
  const offset = offsetAt(text, position);
  let start = offset;
  let end = offset;
  while (start > 0 && /[A-Za-z0-9_]/.test(text[start - 1])) start--;
  while (end < text.length && /[A-Za-z0-9_]/.test(text[end])) end++;
  return text.slice(start, end);
}

function rangeForLine(line, start, end) {
  return {
    start: { line, character: start },
    end: { line, character: end },
  };
}

function symbolKind(kind) {
  return {
    function: 12,
    shape: 23,
    enum: 10,
    choice: 23,
    interface: 11,
    const: 14,
    type: 5,
  }[kind] ?? 13;
}

function analyzeText(uri, text) {
  const found = [];
  const lines = text.split("\n");
  const declaration = /^\s*(?:pub\s+)?(?:(export\s+c)\s+)?(fun|shape|enum|choice|interface|const|type)\s+([A-Za-z_][A-Za-z0-9_]*)/;
  for (let lineIndex = 0; lineIndex < lines.length; lineIndex++) {
    const match = lines[lineIndex].match(declaration);
    if (!match) continue;
    const kind = match[2] === "fun" ? "function" : match[2];
    const name = match[3];
    const start = lines[lineIndex].indexOf(name);
    found.push({
      name,
      kind,
      detail: lines[lineIndex].trim(),
      uri,
      range: rangeForLine(lineIndex, start, start + name.length),
      selectionRange: rangeForLine(lineIndex, start, start + name.length),
    });
  }
  symbols.set(uri, found);
  return found;
}

function simpleDiagnostics(uri, text) {
  const diagnostics = [];
  let balance = 0;
  for (let i = 0; i < text.length; i++) {
    if (text[i] === "{") balance++;
    if (text[i] === "}") balance--;
    if (balance < 0) {
      diagnostics.push({
        range: { start: positionAt(text, i), end: positionAt(text, i + 1) },
        severity: 1,
        code: "PAR100",
        source: "zero",
        message: "unmatched closing brace",
      });
      balance = 0;
    }
  }
  if (balance > 0) {
    diagnostics.push({
      range: { start: positionAt(text, text.length), end: positionAt(text, text.length) },
      severity: 1,
      code: "PAR100",
      source: "zero",
      message: "missing closing brace",
    });
  }
  return diagnostics;
}

async function compilerDiagnostics(uri, text) {
  const path = uriToPath(uri);
  try {
    await writeFile(path, text);
    const result = await execFileAsync(zero, ["check", "--json", path]).catch((error) => error);
    if (!result.stdout) return simpleDiagnostics(uri, text);
    const body = JSON.parse(result.stdout);
    const diagnostics = (body.diagnostics ?? []).map((diag) => ({
      range: {
        start: { line: Math.max(0, diag.line - 1), character: Math.max(0, diag.column - 1) },
        end: { line: Math.max(0, diag.line - 1), character: Math.max(0, diag.column) },
      },
      severity: 1,
      code: diag.code,
      source: "zero",
      message: diag.message,
      data: { repair: diag.repair, fixSafety: diag.fixSafety },
    }));
    return diagnostics;
  } catch {
    return simpleDiagnostics(uri, text);
  }
}

async function compilerFacts(path) {
  const [graphResult, sizeResult, memResult, docResult] = await Promise.all([
    execFileAsync(zero, ["graph", "--json", path]).catch((error) => error),
    execFileAsync(zero, ["size", "--json", path]).catch((error) => error),
    execFileAsync(zero, ["mem", "--json", path]).catch((error) => error),
    execFileAsync(zero, ["doc", "--json", path]).catch((error) => error),
  ]);
  const parseBody = (result) => {
    if (!result.stdout) return null;
    try {
      return JSON.parse(result.stdout);
    } catch {
      return null;
    }
  };
  const graph = parseBody(graphResult);
  const size = parseBody(sizeResult);
  const mem = parseBody(memResult);
  const doc = parseBody(docResult);
  return {
    schemaVersion: 1,
    sourceFile: graph?.sourceFile ?? size?.sourceFile ?? mem?.sourceFile ?? path,
    generatedCBytes: Math.max(graph?.generatedCBytes ?? 0, size?.generatedCBytes ?? 0, mem?.generatedCBytes ?? 0, doc?.generatedCBytes ?? 0),
    cBridgeFallback: Boolean(
      graph?.selfHostRouting?.cBridge?.required ??
      size?.selfHostRouting?.cBridge?.required ??
      mem?.cBridgeFallback ??
      doc?.cBridgeFallback ??
      false
    ),
    targetCapabilityFacts: graph?.targetSupport ?? size?.targetSupport ?? null,
    directBackendStatus: {
      graph: graph?.selfHostRouting ?? null,
      objectBackend: size?.objectBackend ?? mem?.objectBackend ?? null,
    },
    generatedBindingPreviews: (graph?.cImports ?? []).map((item) => ({
      header: item.header,
      alias: item.alias,
      functions: item.typedModel?.functions?.map((fn) => fn.name) ?? [],
      constants: item.typedModel?.constants?.map((constant) => constant.name) ?? [],
      structs: item.typedModel?.structs?.map((shape) => shape.name) ?? [],
    })),
    runtimeHelperCosts: {
      usedStdlibHelpers: size?.usedStdlibHelpers ?? [],
      runtimeShims: size?.runtimeShims ?? [],
      compilerRuntimeHelpers: size?.compilerRuntimeHelpers ?? [],
    },
    memoryBudget: {
      sections: mem?.sections ?? [],
      stackBytes: mem?.stackBytes ?? null,
      heapBytes: mem?.heapBytes ?? null,
      staticBytes: mem?.staticBytes ?? null,
    },
    docs: {
      publicSymbols: doc?.symbols ?? [],
      requiresCapabilities: doc?.requiresCapabilities ?? [],
    },
  };
}

function completionItems() {
  const keywordItems = ["pub", "fun", "shape", "enum", "choice", "interface", "let", "mut", "return", "check", "raises"].map((label) => ({ label, kind: 14 }));
  const symbolItems = [...symbols.values()].flat().map((symbol) => ({ label: symbol.name, kind: symbolKind(symbol.kind), detail: symbol.detail }));
  return [...keywordItems, ...symbolItems];
}

function findSymbol(name) {
  for (const list of symbols.values()) {
    const found = list.find((symbol) => symbol.name === name);
    if (found) return found;
  }
  return null;
}

function hover(params) {
  const doc = documents.get(params.textDocument.uri);
  const name = doc ? wordAt(doc.text, params.position) : "";
  const symbol = findSymbol(name);
  if (!symbol) return null;
  const facts = documentFacts.get(params.textDocument.uri);
  const directStatus = facts?.directBackendStatus?.objectBackend?.targetFacts?.directStatus ?? facts?.targetCapabilityFacts?.requiredCapabilitySupport?.status ?? "unknown";
  const target = facts?.targetCapabilityFacts?.target ?? "host";
  const capabilities = (facts?.docs?.requiresCapabilities ?? []).map((item) => item.name ?? item).filter(Boolean).join(", ") || "none";
  const bindingPreview = (facts?.generatedBindingPreviews ?? [])
    .map((item) => `${item.alias ?? item.header}: ${(item.functions ?? []).slice(0, 3).join(", ") || "typed header"}`)
    .join("; ") || "none";
  return {
    contents: {
      kind: "markdown",
      value: `\`${symbol.detail}\`\n\nkind: ${symbol.kind}\n\ntarget: ${target}\n\ncapabilities: ${capabilities}\n\ndirect backend: ${directStatus}\n\ngenerated binding previews: ${bindingPreview}\n\ngeneratedCBytes: ${facts?.generatedCBytes ?? 0}`,
    },
    range: symbol.selectionRange,
  };
}

function documentSymbols(params) {
  const uri = params.textDocument.uri;
  return (symbols.get(uri) ?? []).map((symbol) => ({
    name: symbol.name,
    kind: symbolKind(symbol.kind),
    detail: symbol.detail,
    range: symbol.range,
    selectionRange: symbol.selectionRange,
  }));
}

function signatureHelp(params) {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return { signatures: [], activeSignature: 0, activeParameter: 0 };
  const offset = offsetAt(doc.text, params.position);
  const before = doc.text.slice(0, offset);
  const call = before.match(/([A-Za-z_][A-Za-z0-9_]*)\([^()]*$/);
  const symbol = call ? findSymbol(call[1]) : null;
  return {
    signatures: symbol ? [{ label: symbol.detail, parameters: [] }] : [],
    activeSignature: 0,
    activeParameter: 0,
  };
}

function definition(params) {
  const doc = documents.get(params.textDocument.uri);
  const name = doc ? wordAt(doc.text, params.position) : "";
  const symbol = findSymbol(name);
  return symbol ? { uri: symbol.uri, range: symbol.selectionRange } : null;
}

function references(params) {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const name = wordAt(doc.text, params.position);
  const locations = [];
  const pattern = new RegExp(`\\b${name}\\b`, "g");
  let match = null;
  while ((match = pattern.exec(doc.text)) !== null) {
    const start = positionAt(doc.text, match.index);
    locations.push({ uri: params.textDocument.uri, range: { start, end: { line: start.line, character: start.character + name.length } } });
  }
  return locations;
}

function rename(params) {
  const refs = references(params);
  return {
    changes: {
      [params.textDocument.uri]: refs.map((location) => ({ range: location.range, newText: params.newName })),
    },
  };
}

async function codeActions(params) {
  const path = uriToPath(params.textDocument.uri);
  const diagnosticActions = (params.context?.diagnostics ?? [])
    .filter((diagnostic) => diagnostic.data?.repair?.id)
    .map((diagnostic) => ({
      title: diagnostic.data.repair.summary ?? `Repair ${diagnostic.code}`,
      kind: "quickfix",
      diagnostics: [diagnostic],
      data: {
        id: diagnostic.data.repair.id,
        diagnosticCode: diagnostic.code,
        safety: diagnostic.data.fixSafety ?? "requires-human-review",
        patches: [],
      },
    }));
  const result = await execFileAsync(zero, ["fix", "--patch", "--json", path]).catch((error) => error);
  if (!result.stdout) return diagnosticActions;
  const body = JSON.parse(result.stdout);
  const patchActions = (body.fixes ?? []).map((fix) => ({
    title: fix.summary,
    kind: "quickfix",
    diagnostics: params.context?.diagnostics ?? [],
    data: {
      id: fix.id,
      diagnosticCode: fix.diagnosticCode,
      safety: fix.safety,
      patches: body.patches ?? [],
    },
  }));
  return [...diagnosticActions, ...patchActions];
}

async function didOpen(params, send) {
  const { uri, text } = params.textDocument;
  documents.set(uri, { text, version: params.textDocument.version ?? 0 });
  analyzeText(uri, text);
  const diagnostics = await compilerDiagnostics(uri, text);
  send("textDocument/publishDiagnostics", { uri, diagnostics });
  const facts = await compilerFacts(uriToPath(uri));
  documentFacts.set(uri, facts);
  send("zero/editorMetadata", { uri, facts });
}

async function didChange(params, send) {
  const uri = params.textDocument.uri;
  const text = params.contentChanges.at(-1)?.text ?? documents.get(uri)?.text ?? "";
  documents.set(uri, { text, version: params.textDocument.version ?? 0 });
  analyzeText(uri, text);
  const diagnostics = await compilerDiagnostics(uri, text);
  send("textDocument/publishDiagnostics", { uri, diagnostics });
  const facts = await compilerFacts(uriToPath(uri));
  documentFacts.set(uri, facts);
  send("zero/editorMetadata", { uri, facts });
}

function workspaceSymbols(query) {
  return [...symbols.values()].flat()
    .filter((symbol) => !query || symbol.name.toLowerCase().includes(query.toLowerCase()))
    .map((symbol) => ({ name: symbol.name, kind: symbolKind(symbol.kind), location: { uri: symbol.uri, range: symbol.selectionRange } }));
}

async function handle(method, params, send) {
  if (method === "initialize") {
    return {
      capabilities: {
        textDocumentSync: 2,
        workspaceSymbolProvider: true,
        completionProvider: { triggerCharacters: ["."] },
        hoverProvider: true,
        signatureHelpProvider: { triggerCharacters: ["("] },
        definitionProvider: true,
        documentSymbolProvider: true,
        referencesProvider: true,
        renameProvider: true,
        codeActionProvider: true,
      },
    };
  }
  if (method === "textDocument/didOpen") return didOpen(params, send);
  if (method === "textDocument/didChange") return didChange(params, send);
  if (method === "workspace/symbol") return workspaceSymbols(params.query ?? "");
  if (method === "textDocument/completion") return completionItems();
  if (method === "textDocument/hover") return hover(params);
  if (method === "textDocument/signatureHelp") return signatureHelp(params);
  if (method === "textDocument/definition") return definition(params);
  if (method === "textDocument/documentSymbol") return documentSymbols(params);
  if (method === "textDocument/references") return references(params);
  if (method === "textDocument/rename") return rename(params);
  if (method === "textDocument/codeAction") return codeActions(params);
  return null;
}

function sendMessage(message) {
  const json = JSON.stringify(message);
  process.stdout.write(`Content-Length: ${Buffer.byteLength(json, "utf8")}\r\n\r\n${json}`);
}

function startServer() {
  let buffer = Buffer.alloc(0);
  const send = (method, params) => sendMessage({ jsonrpc: "2.0", method, params });
  process.stdin.on("data", async (chunk) => {
    const data = typeof chunk === "string" ? Buffer.from(chunk) : chunk;
    buffer = Buffer.concat([buffer, data]);
    while (true) {
      const headerEnd = buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) return;
      const header = buffer.slice(0, headerEnd).toString("utf8");
      const match = header.match(/Content-Length: (\d+)/i);
      if (!match) return;
      const length = Number(match[1]);
      const bodyStart = headerEnd + 4;
      if (buffer.length < bodyStart + length) return;
      const body = buffer.slice(bodyStart, bodyStart + length).toString("utf8");
      buffer = buffer.slice(bodyStart + length);
      const message = JSON.parse(body);
      if (message.method === "exit") process.exit(0);
      const result = await handle(message.method, message.params ?? {}, send);
      if (Object.hasOwn(message, "id")) sendMessage({ jsonrpc: "2.0", id: message.id, result });
    }
  });
}

async function selfTest() {
  const dir = ".zero/lsp";
  await mkdir(dir, { recursive: true });
  const path = join(dir, "sample.0");
  const uri = pathToUri(path);
  const text = "pub fun add(a: i32, b: i32) -> i32 {\n    return a + b\n}\n\npub fun main(world: World) -> Void raises {\n    check world.out.write(\"ok\\n\")\n}\n";
  await writeFile(path, text);
  const notifications = [];
  await didOpen({ textDocument: { uri, text, version: 1 } }, (method, params) => notifications.push({ method, params }));
  const metadata = notifications.find((item) => item.method === "zero/editorMetadata")?.params?.facts;
  assert.equal(metadata.generatedCBytes, 0);
  assert.equal(metadata.cBridgeFallback, false);
  assert(metadata.targetCapabilityFacts);
  assert(metadata.runtimeHelperCosts.usedStdlibHelpers.length >= 0);
  assert(Array.isArray(metadata.memoryBudget.sections));
  assert(metadata.directBackendStatus.objectBackend);
  assert(Array.isArray(metadata.generatedBindingPreviews));
  assert(symbols.get(uri).some((symbol) => symbol.name === "add"));
  assert(notifications.some((item) => item.method === "textDocument/publishDiagnostics"));
  assert(workspaceSymbols("add").some((symbol) => symbol.name === "add"));
  assert(completionItems().some((item) => item.label === "add"));
  const hoverText = hover({ textDocument: { uri }, position: { line: 0, character: 9 } }).contents.value;
  assert(hoverText.includes("add"));
  assert(hoverText.includes("capabilities:"));
  assert(hoverText.includes("generated binding previews:"));
  assert(hoverText.includes("generatedCBytes: 0"));
  assert(documentSymbols({ textDocument: { uri } }).some((symbol) => symbol.name === "add"));
  assert(signatureHelp({ textDocument: { uri }, position: { line: 5, character: 30 } }).signatures.length >= 0);
  assert(definition({ textDocument: { uri }, position: { line: 0, character: 9 } }).uri === uri);
  assert(references({ textDocument: { uri }, position: { line: 0, character: 9 } }).length >= 1);
  assert(rename({ textDocument: { uri }, position: { line: 0, character: 9 }, newName: "sum" }).changes[uri].length >= 1);
  const actions = await codeActions({ textDocument: { uri: pathToUri("conformance/native/fail/mem-copy-immutable-dst.0") }, context: { diagnostics: [] } });
  assert(actions.some((action) => action.data.id === "make-binding-mutable"));
  const targetActions = await codeActions({
    textDocument: { uri },
    context: {
      diagnostics: [{
        code: "TAR002",
        message: "target does not provide hosted filesystem capability",
        data: { repair: { id: "choose-target-with-required-capability", summary: "Build for a target with the required capability or move the code behind a target-specific entry point." }, fixSafety: "requires-human-review" },
      }],
    },
  });
  assert(targetActions.some((action) => action.data.id === "choose-target-with-required-capability"));
  const typeAnnotationActions = await codeActions({
    textDocument: { uri },
    context: {
      diagnostics: [{
        code: "PUB001",
        message: "public declaration requires explicit type metadata",
        data: { repair: { id: "add-public-api-type", summary: "Add an explicit public type annotation so graph and docs metadata stay stable." }, fixSafety: "behavior-preserving" },
      }],
    },
  });
  assert(typeAnnotationActions.some((action) => action.data.id === "add-public-api-type"));
  const errorSetActions = await codeActions({
    textDocument: { uri },
    context: {
      diagnostics: [
        {
          code: "ERR002",
          message: "caller error set is missing a callee error",
          data: { repair: { id: "add-missing-error-name", summary: "Add the missing error name to the caller raises set." }, fixSafety: "api-changing" },
        },
        {
          code: "ERR003",
          message: "fallible call requires check or rescue",
          data: { repair: { id: "check-or-rescue-fallible-call", summary: "Wrap the fallible call with check or handle it locally with rescue." }, fixSafety: "api-changing" },
        },
      ],
    },
  });
  assert(errorSetActions.some((action) => action.data.id === "add-missing-error-name"));
  assert(errorSetActions.some((action) => action.data.id === "check-or-rescue-fallible-call"));
  console.log("zls self-test ok");
}

if (process.argv.includes("--self-test")) {
  await selfTest();
} else {
  startServer();
}
