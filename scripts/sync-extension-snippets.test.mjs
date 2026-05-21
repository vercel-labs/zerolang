import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const canonicalPath = join(repoRoot, "extensions/shared/snippets/zero.json");
const targets = [
  join(repoRoot, "extensions/vscode/snippets/zero.json"),
  join(repoRoot, "extensions/zed/snippets/zero.json"),
];

describe("shared extension snippets", () => {
  it("keeps vscode and zed snippets in sync with the canonical source", async () => {
    const canonical = JSON.parse(await readFile(canonicalPath, "utf8"));
    for (const target of targets) {
      const current = JSON.parse(await readFile(target, "utf8"));
      assert.deepEqual(current, canonical, `${target} should match ${canonicalPath}`);
    }
  });
});
