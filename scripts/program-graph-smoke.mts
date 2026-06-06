import { execFile } from "node:child_process";
import { rm } from "node:fs/promises";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const cc = process.env.CC ?? "cc";
const out = `/tmp/zero-program-graph-smoke-${process.pid}`;

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
    "native/zero-c/src/program_graph.c",
    "native/zero-c/src/program_graph_identity.c",
    "native/zero-c/src/program_graph_import.c",
    "native/zero-c/src/program_graph_lower.c",
    "native/zero-c/src/program_graph_node_id.c",
    "native/zero-c/src/program_graph_order.c",
    "native/zero-c/src/program_graph_format.c",
    "native/zero-c/src/program_graph_resolve.c",
    "native/zero-c/src/program_graph_semantics.c",
    "native/zero-c/src/program_graph_source_map.c",
    "native/zero-c/src/program_graph_validate.c",
    "native/zero-c/src/c_import.c",
    "native/zero-c/src/canonical_text.c",
    "native/zero-c/src/std_sig.c",
    "native/zero-c/src/std_source.c",
    "native/zero-c/src/ast.c",
    "native/zero-c/tests/program_graph_smoke.c",
    "-o",
    out,
  ]);
  const result = await execFileAsync(out);
  if (!result.stdout.includes("program graph smoke ok")) {
    throw new Error(`unexpected ProgramGraph smoke output: ${result.stdout}`);
  }
} finally {
  await rm(out, { force: true });
}
