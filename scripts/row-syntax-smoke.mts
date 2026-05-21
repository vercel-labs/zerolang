import { execFile } from "node:child_process";
import { rm } from "node:fs/promises";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const cc = process.env.CC ?? "cc";
const out = `/tmp/zero-row-syntax-smoke-${process.pid}`;

try {
  await execFileAsync(cc, [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-I",
    "native/zero-c/include",
    "native/zero-c/src/row_syntax.c",
    "native/zero-c/tests/row_syntax_smoke.c",
    "-o",
    out,
  ]);
  const result = await execFileAsync(out);
  if (!result.stdout.includes("row syntax smoke ok")) {
    throw new Error(`unexpected row syntax smoke output: ${result.stdout}`);
  }
} finally {
  await rm(out, { force: true });
}
