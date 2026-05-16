import { cp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const extensionDir = resolve(scriptDir, "..");
const repoRoot = resolve(extensionDir, "..", "..");
const outDir = join(repoRoot, ".zero", "zed", "zero");
const grammarDir = join(extensionDir, "tree-sitter-zero");

await rm(outDir, { force: true, recursive: true });
await mkdir(outDir, { recursive: true });

for (const path of ["languages", "snippets", "README.md"]) {
  await cp(join(extensionDir, path), join(outDir, path), { recursive: true });
}

let manifest = await readFile(join(extensionDir, "extension.toml"), "utf8");
manifest = manifest.replace(
  'repository = "file://tree-sitter-zero"',
  `repository = "${pathToFileURL(grammarDir).href}"`,
);
await writeFile(join(outDir, "extension.toml"), manifest);

console.log(`Prepared Zed dev extension at ${relative(repoRoot, outDir)}`);
