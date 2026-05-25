#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const stdRoot = path.join(repoRoot, "std");
const outPath = path.join(repoRoot, "native/zero-c/src/embedded_stdlib.inc");

function chunkText(text) {
  const chunks = [];
  let current = "";
  for (const line of text.match(/[^\n]*\n|[^\n]+$/g) || []) {
    if (current && JSON.stringify(current + line).length > 3000) {
      chunks.push(current);
      current = "";
    }
    if (JSON.stringify(line).length <= 3000) {
      current += line;
      continue;
    }
    for (let index = 0; index < line.length; index += 1400) {
      if (current) {
        chunks.push(current);
        current = "";
      }
      chunks.push(line.slice(index, index + 1400));
    }
  }
  if (current) chunks.push(current);
  return chunks;
}

function cIdent(text) {
  return text.replace(/[^A-Za-z0-9_]/g, "_");
}

const inputs = fs.existsSync(stdRoot)
  ? fs.readdirSync(stdRoot)
      .filter((name) => name.endsWith(".0"))
      .sort((a, b) => a.localeCompare(b))
      .map((name) => `std/${name}`)
  : [];

const out = [];
out.push("/* Generated from Zero standard-library sources. Run node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/embed-stdlib-sources.mts to refresh. */");
out.push("#ifndef ZERO_EMBEDDED_STDLIB_INC");
out.push("#define ZERO_EMBEDDED_STDLIB_INC");
out.push("");

for (const relativePath of inputs) {
  const ident = `zero_embedded_stdlib_${cIdent(relativePath)}_chunks`;
  const text = fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
  out.push(`static const char *const ${ident}[] = {`);
  for (const chunk of chunkText(text)) out.push(`  ${JSON.stringify(chunk)},`);
  out.push("  NULL");
  out.push("};");
  out.push("");
}

out.push("#endif");
out.push("");

fs.writeFileSync(outPath, out.join("\n"));
