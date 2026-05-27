import { existsSync } from "node:fs";
import { readdir, rm } from "node:fs/promises";
import { execFile } from "node:child_process";
import path from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const cc = process.env.CC ?? "cc";
const out = `/tmp/zero-canonical-text-smoke-${process.pid}`;

async function fixtureArgs(root: string) {
  const canonicalDir = path.join(root, "fixtures/canonical");
  const rejectDir = path.join(root, "fixtures/reject");
  const args: string[] = [];
  if (existsSync(canonicalDir)) {
    for (const name of (await readdir(canonicalDir)).filter((item) => item.endsWith(".canonical.0")).sort()) {
      args.push("--accept", path.join(canonicalDir, name));
    }
  }
  if (existsSync(rejectDir)) {
    for (const name of (await readdir(rejectDir)).filter((item) => item.endsWith(".0")).sort()) {
      args.push("--reject", path.join(rejectDir, name));
    }
  }
  return args;
}

try {
  await execFileAsync(cc, [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-I",
    "native/zero-c/include",
    "-I",
    "native/zero-c/src",
    "native/zero-c/src/ast.c",
    "native/zero-c/src/canonical_text.c",
    "native/zero-c/src/canonical_text_format.c",
    "native/zero-c/tests/canonical_text_smoke.c",
    "-o",
    out,
  ]);
  const args = process.env.ZERO_CANONICAL_TEXT_FIXTURES ? await fixtureArgs(process.env.ZERO_CANONICAL_TEXT_FIXTURES) : [];
  const result = await execFileAsync(out, args, { maxBuffer: 1024 * 1024 * 8 });
  if (!result.stdout.includes("canonical text smoke ok")) {
    throw new Error(`unexpected canonical text smoke output: ${result.stdout}`);
  }
} finally {
  await rm(out, { force: true });
}
