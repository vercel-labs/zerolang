#!/usr/bin/env node
// Verify the grammar against the Zero compiler corpus.
// Parses every .0 file under zero/examples and zero/conformance/native/pass,
// counts ERROR / MISSING nodes, and exits non-zero if any file fails.
//
// Use this as the safety net: any drift between the grammar and the language
// surface that the compiler actually parses surfaces here as failing files.
//
// Usage (from this grammar directory):
//   node scripts/check-against-upstream.mjs            # parse, report, exit
//   ZERO_REPO=/path/to/zero node scripts/...           # override repo path
//
// Default ZERO_REPO is the zerolang root (../../..) because this grammar is
// vendored inside that monorepo.
//
// Output is grouped per source directory; failures list the first error
// position per file so you can jump to it.

import { execFileSync } from "node:child_process";
import { readdir } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const grammarDir = resolve(here, "..");
const zeroRepo = process.env.ZERO_REPO
  ? resolve(process.env.ZERO_REPO)
  : resolve(grammarDir, "..", "..", "..");

const scanDirs = [
  ["examples", join(zeroRepo, "examples")],
  ["conformance", join(zeroRepo, "conformance", "native", "pass")],
];

async function findZeroFiles(dir) {
  const out = [];
  async function walk(d) {
    let entries;
    try { entries = await readdir(d, { withFileTypes: true }); }
    catch { return; }
    for (const e of entries) {
      const p = join(d, e.name);
      if (e.isDirectory()) await walk(p);
      else if (e.name.endsWith(".0")) out.push(p);
    }
  }
  await walk(dir);
  out.sort();
  return out;
}

function parseFile(path) {
  let output = "";
  try {
    output = execFileSync("tree-sitter", ["parse", path], {
      cwd: grammarDir,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    });
  } catch (e) {
    output = e.stdout?.toString() ?? "";
  }
  const m = output.match(/\((ERROR|MISSING)\s+\[(\d+),\s*(\d+)\]/);
  if (!m) return { ok: true };
  return { ok: false, kind: m[1], row: +m[2], col: +m[3] };
}

let totalPass = 0;
let totalFail = 0;
const sections = [];

for (const [label, dir] of scanDirs) {
  const files = await findZeroFiles(dir);
  const failures = [];
  for (const file of files) {
    const r = parseFile(file);
    if (r.ok) totalPass++;
    else { totalFail++; failures.push({ file, ...r }); }
  }
  sections.push({ label, total: files.length, failures });
}

process.stdout.write("Grammar coverage against Zero corpus:\n\n");
for (const { label, total, failures } of sections) {
  const pass = total - failures.length;
  const pct = total === 0 ? "—" : `${((pass / total) * 100).toFixed(0)}%`;
  process.stdout.write(`  ${label.padEnd(12)} ${pass}/${total} (${pct})\n`);
}
process.stdout.write(`\n  ${"total".padEnd(12)} ${totalPass}/${totalPass + totalFail}\n`);

if (totalFail > 0) {
  process.stdout.write("\nFailing files (first error per file):\n");
  for (const { label, failures } of sections) {
    if (!failures.length) continue;
    process.stdout.write(`\n  ${label}/\n`);
    for (const { file, kind, row, col } of failures) {
      const rel = file.replace(zeroRepo + "/", "");
      process.stdout.write(`    ${rel}:${row + 1}:${col + 1}  ${kind}\n`);
    }
  }
}

process.exit(totalFail > 0 ? 1 : 0);
