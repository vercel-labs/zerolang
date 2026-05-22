import { readFile } from "node:fs/promises";

const sourceFiles = [
  "native/zero-c/src/checker.c",
  "native/zero-c/src/main.c",
  "native/zero-c/src/ir.c",
  "native/zero-c/src/row_syntax.c",
  "native/zero-c/src/ast.c",
  "native/zero-c/src/emit_macho64.c",
  "native/zero-c/src/emit_elf64.c",
  "native/zero-c/src/emit_coff.c",
  "native/zero-c/src/target.c",
  "native/zero-c/include/zero.h",
];

function countMatches(text, pattern) {
  return [...text.matchAll(pattern)].length;
}

function lineCount(text) {
  if (text.length === 0) return 0;
  return text.endsWith("\n") ? text.split("\n").length - 1 : text.split("\n").length;
}

function updateBraceDepth(line, depth) {
  for (const ch of line) {
    if (ch === "{") depth++;
    else if (ch === "}") depth--;
  }
  return depth;
}

function largeFunctions(path, text) {
  const lines = text.split("\n");
  const results = [];
  let depth = 0;
  let current = null;
  const functionStart = /^([A-Za-z_][A-Za-z0-9_]*|static)[A-Za-z0-9_ \t*]+[A-Za-z_][A-Za-z0-9_]*\([^;]*\)[ \t]*\{/;
  for (let index = 0; index < lines.length; index++) {
    const line = lines[index];
    if (!current && depth === 0 && functionStart.test(line)) {
      current = { path, line: index + 1, signature: line.trim() };
    }
    depth = updateBraceDepth(line, depth);
    if (current && depth === 0) {
      const size = index + 1 - current.line + 1;
      if (size >= 80) results.push({ ...current, lines: size });
      current = null;
    }
  }
  return results;
}

function namesFromRegex(text, pattern) {
  return [...text.matchAll(pattern)].map((match) => match[1]).sort();
}

function duplicates(items) {
  const counts = new Map();
  for (const item of items) counts.set(item, (counts.get(item) ?? 0) + 1);
  return [...counts.entries()]
    .filter(([, count]) => count > 1)
    .map(([name, count]) => ({ name, count }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

function missingFrom(left, right) {
  const rightSet = new Set(right);
  return [...new Set(left)].filter((item) => !rightSet.has(item)).sort();
}

const texts = new Map();
for (const path of sourceFiles) {
  texts.set(path, await readFile(path, "utf8"));
}

const files = Object.fromEntries([...texts.entries()].map(([path, text]) => [path, {
  lines: lineCount(text),
  strcmpCalls: countMatches(text, /strcmp\(/g),
  unsupportedMarkers: countMatches(text, /Unknown|unsupported|currently|MVP|direct backend/g),
}]));

const checker = texts.get("native/zero-c/src/checker.c") ?? "";
const main = texts.get("native/zero-c/src/main.c") ?? "";
const ir = texts.get("native/zero-c/src/ir.c") ?? "";

const checkerKnownStdNames = namesFromRegex(checker, /"(std\.[^"]+)"/g);
const checkerReturnNames = namesFromRegex(checker, /strcmp\(name\.data,\s+"(std\.[^"]+)"/g);
const checkerArgCountNames = namesFromRegex(checker, /strcmp\(name,\s+"(std\.[^"]+)"/g);
const checkerArgTypeNames = namesFromRegex(checker, /strcmp\(name,\s+"(std\.[^"]+)"/g);
const mainHelperNames = namesFromRegex(main, /\{\s*"(std\.[^"]+)"/g);
const irStdNames = namesFromRegex(ir, /strcmp\(callee_name,\s+"(std\.[^"]+)"/g);

const report = {
  schema: 1,
  files,
  largeFunctions: [...texts.entries()]
    .flatMap(([path, text]) => largeFunctions(path, text))
    .sort((a, b) => b.lines - a.lines)
    .slice(0, 25),
  stdlib: {
    checkerReturnCount: new Set(checkerReturnNames).size,
    checkerKnownStdNameCount: new Set(checkerKnownStdNames).size,
    checkerArgCountCount: new Set(checkerArgCountNames).size,
    checkerArgTypeCount: new Set(checkerArgTypeNames).size,
    mainHelperCount: new Set(mainHelperNames).size,
    irDirectStdCallCount: new Set(irStdNames).size,
    duplicateMainHelpers: duplicates(mainHelperNames),
    returnNamesMissingFromMainHelpers: missingFrom(checkerReturnNames, mainHelperNames),
    mainHelpersMissingFromCheckerKnownNames: missingFrom(mainHelperNames, checkerKnownStdNames),
  },
};

console.log(JSON.stringify(report, null, 2));
