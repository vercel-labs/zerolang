import { cp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { execFile } from "node:child_process";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { promisify } from "node:util";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const extensionDir = resolve(scriptDir, "..");
const repoRoot = resolve(extensionDir, "..", "..");
const outDir = join(repoRoot, ".zero", "zed", "zero");
const grammarDir = join(extensionDir, "tree-sitter-zero");
const grammarRepoDir = join(repoRoot, ".zero", "zed", "tree-sitter-zero");
const execFileAsync = promisify(execFile);

async function git(args, options = {}) {
  return execFileAsync("git", args, {
    cwd: options.cwd,
    windowsHide: true,
  });
}

await rm(outDir, { force: true, recursive: true });
await rm(grammarRepoDir, { force: true, recursive: true });
await mkdir(outDir, { recursive: true });
await mkdir(grammarRepoDir, { recursive: true });

for (const path of ["languages", "snippets", "README.md"]) {
  await cp(join(extensionDir, path), join(outDir, path), { recursive: true });
}

for (const path of ["grammar.js", "package.json", "tree-sitter.json", "src", "test"]) {
  await cp(join(grammarDir, path), join(grammarRepoDir, path), { recursive: true });
}
await git(["init", "--initial-branch=main"], { cwd: grammarRepoDir });
await git(["add", "."], { cwd: grammarRepoDir });
await git(
  [
    "-c",
    "user.name=Zero Zed Dev",
    "-c",
    "user.email=zero-zed-dev@example.invalid",
    "commit",
    "--quiet",
    "-m",
    "Prepare Zero tree-sitter grammar",
  ],
  { cwd: grammarRepoDir },
);

let manifest = await readFile(join(extensionDir, "extension.toml"), "utf8");
manifest = manifest.replace(
  'repository = "file://tree-sitter-zero"',
  `repository = "${pathToFileURL(grammarRepoDir).href}"`,
);
await writeFile(join(outDir, "extension.toml"), manifest);

console.log(`Prepared Zed dev extension at ${relative(repoRoot, outDir)}`);
console.log(`Prepared local grammar repo at ${relative(repoRoot, grammarRepoDir)}`);
