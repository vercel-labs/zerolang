import { execFile } from "node:child_process";
import { readdir, rm } from "node:fs/promises";
import path from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const cc = process.env.CC ?? "cc";
const out = `/tmp/zero-row-syntax-smoke-${process.pid}`;
const checkOut = `/tmp/zero-row-syntax-check-smoke-${process.pid}`;
const passDir = "native/zero-c/tests/row_syntax/pass";
const passFiles = (await readdir(passDir))
  .filter((name) => name.endsWith(".row"))
  .sort()
  .map((name) => path.join(passDir, name));

try {
  await execFileAsync(cc, [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-I",
    "native/zero-c/include",
    "native/zero-c/src/parser.c",
    "native/zero-c/src/row_syntax.c",
    "native/zero-c/tests/row_syntax_smoke.c",
    "-o",
    out,
  ]);
  const result = await execFileAsync(out);
  if (!result.stdout.includes("row syntax smoke ok")) {
    throw new Error(`unexpected row syntax smoke output: ${result.stdout}`);
  }

  await execFileAsync(cc, [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-I",
    "native/zero-c/include",
    "native/zero-c/src/fs.c",
    "native/zero-c/src/parser.c",
    "native/zero-c/src/row_syntax.c",
    "native/zero-c/src/checker.c",
    "native/zero-c/src/type_core.c",
    "native/zero-c/src/unify.c",
    "native/zero-c/src/call_resolve.c",
    "native/zero-c/src/target.c",
    "native/zero-c/tests/row_syntax_check_smoke.c",
    "-o",
    checkOut,
  ]);
  const checkResult = await execFileAsync(checkOut, passFiles);
  if (!checkResult.stdout.includes("row syntax check smoke ok")) {
    throw new Error(`unexpected row syntax check smoke output: ${checkResult.stdout}`);
  }
} finally {
  await rm(out, { force: true });
  await rm(checkOut, { force: true });
}
