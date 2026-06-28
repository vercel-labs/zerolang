#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const stdRoot = path.join(repoRoot, "std");
const outPath = path.join(repoRoot, "native/zero-c/src/embedded_stdlib_graph.inc");
const outDir = path.join(repoRoot, "native/zero-c/src/embedded_stdlib_graph");

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

const fnvPrime = 1099511628211n;
const fnvMask = (1n << 64n) - 1n;

function fnvMul(hash: bigint): bigint {
  return (hash * fnvPrime) & fnvMask;
}

function hashText(hash: bigint, text: string): bigint {
  for (const byte of Buffer.from(text)) {
    hash ^= BigInt(byte);
    hash = fnvMul(hash);
  }
  hash ^= 0xffn;
  return fnvMul(hash);
}

function hashBytes(hash: bigint, bytes: Buffer): bigint {
  hash ^= BigInt(bytes.length);
  hash = fnvMul(hash);
  for (const byte of bytes) {
    hash ^= BigInt(byte);
    hash = fnvMul(hash);
  }
  return hash;
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

fs.rmSync(outDir, { recursive: true, force: true });
fs.mkdirSync(outDir, { recursive: true });

let fingerprint = hashText(1469598103934665603n, "zero-embedded-stdlib-graph-v1");

for (const relativePath of inputs) {
  const ident = `zero_embedded_stdlib_graph_${cIdent(relativePath)}_bytes`;
  const partName = `${cIdent(relativePath)}.inc`;
  const part: string[] = [];
  const bytes = fs.readFileSync(path.join(repoRoot, relativePath));
  const moduleName = relativePath.slice(0, -".graph".length).replace("/", ".");
  const modulePath = relativePath.slice(0, -".graph".length) + ".0";
  fingerprint = hashText(fingerprint, moduleName);
  fingerprint = hashText(fingerprint, modulePath);
  fingerprint = hashBytes(fingerprint, bytes);
  part.push(`/* Generated from ${relativePath}. */`);
  appendByteArray(part, ident, bytes);
  part.push("");
  fs.writeFileSync(path.join(outDir, partName), part.join("\n"));
  out.push(`#include "embedded_stdlib_graph/${partName}"`);
}

out.push("");
out.push(`#define ZERO_EMBEDDED_STDLIB_GRAPH_FINGERPRINT 0x${fingerprint.toString(16).padStart(16, "0")}ull`);
out.push("");
out.push("#endif");
out.push("");

fs.writeFileSync(outPath, out.join("\n"));
