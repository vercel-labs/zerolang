import { readFile, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const canonicalPath = join(repoRoot, "extensions/shared/snippets/zero.json");
const targets = [
  join(repoRoot, "extensions/vscode/snippets/zero.json"),
  join(repoRoot, "extensions/zed/snippets/zero.json"),
];

const canonical = await readFile(canonicalPath, "utf8");
const formatted = `${JSON.stringify(JSON.parse(canonical), null, 2)}\n`;

for (const target of targets) {
  await writeFile(target, formatted);
}

console.log(`Synced snippets to ${targets.length} extension paths`);
