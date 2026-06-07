#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const stdRoot = path.join(repoRoot, "std");
const outPath = path.join(repoRoot, "native/zero-c/src/embedded_stdlib_graph.inc");

function cIdent(text: string): string {
  return text.replace(/[^A-Za-z0-9_]/g, "_");
}

function appendByteArray(out: string[], ident: string, bytes: Buffer) {
  out.push(`static const unsigned char ${ident}[] = {`);
  const rowWidth = 64;
  for (let offset = 0; offset < bytes.length; offset += rowWidth) {
    const row = [...bytes.subarray(offset, Math.min(offset + rowWidth, bytes.length))]
      .map((byte) => `0x${byte.toString(16).padStart(2, "0")}`)
      .join(", ");
    out.push(`  ${row},`);
  }
  out.push("};");
}

const inputs = fs.existsSync(stdRoot)
  ? fs.readdirSync(stdRoot)
      .filter((name) => name.endsWith(".graph"))
      .sort((a, b) => a.localeCompare(b))
      .map((name) => `std/${name}`)
  : [];

const out: string[] = [];
out.push("/* Generated from Zero standard-library binary ProgramGraphs. Run node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/embed-stdlib-graphs.mts to refresh. */");
out.push("#ifndef ZERO_EMBEDDED_STDLIB_GRAPH_INC");
out.push("#define ZERO_EMBEDDED_STDLIB_GRAPH_INC");
out.push("");

for (const relativePath of inputs) {
  const ident = `zero_embedded_stdlib_graph_${cIdent(relativePath)}_bytes`;
  const bytes = fs.readFileSync(path.join(repoRoot, relativePath));
  appendByteArray(out, ident, bytes);
  out.push("");
}

out.push("#endif");
out.push("");

fs.writeFileSync(outPath, out.join("\n"));
