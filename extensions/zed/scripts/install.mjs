#!/usr/bin/env node
import { execFileSync } from "node:child_process";
import { existsSync } from "node:fs";
import { readFile, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const extensionDir = resolve(here, "..");
const grammarPath = resolve(extensionDir, "tree-sitter-zero");
const manifestPath = resolve(extensionDir, "extension.toml");

const placeholder = "file://ZERO_GRAMMAR_PATH";
const resolved = `file://${grammarPath}`;

// Zed compiles the grammar by running `git fetch origin <rev>` against the
// configured repository URL, so the vendored tree-sitter-zero directory needs
// to be a git repository even though it is committed inside zerolang as plain
// files. Initialise a local-only `.git/` here on first install. It is
// gitignored in extensions/zed/.gitignore and never pushed.
if (!existsSync(join(grammarPath, ".git"))) {
  console.log("Initialising local git repository in tree-sitter-zero/...");
  const run = (args) => {
    execFileSync("git", args, { cwd: grammarPath, stdio: "inherit" });
  };
  run(["init", "--quiet", "--initial-branch=main"]);
  run(["add", "-A"]);
  run([
    "-c",
    "user.email=zed-extension@zerolang.local",
    "-c",
    "user.name=Zero Zed extension installer",
    "commit",
    "--quiet",
    "-m",
    "Bootstrap vendored grammar for Zed dev install",
  ]);
}

const raw = await readFile(manifestPath, "utf8");
const repositoryLine = /^(\s*repository\s*=\s*)"file:\/\/[^"]*"/m;
if (!repositoryLine.test(raw)) {
  console.error(
    "extension.toml does not contain a `repository = \"file://...\"` line " +
      "inside [grammars.zero]. Restore it with `git checkout -- " +
      "extension.toml` and try again."
  );
  process.exit(1);
}

const next = raw.replace(repositoryLine, `$1"${resolved}"`);
await writeFile(manifestPath, next);

console.log("Substituted grammar path in extension.toml:");
console.log(`  ${resolved}`);
console.log();
console.log("To install in Zed:");
console.log("  1. Open Zed and run the command palette");
console.log("     (cmd-shift-p on macOS / ctrl-shift-p on Linux)");
console.log("  2. Run \"zed: install dev extension\"");
console.log(`  3. Select this directory: ${extensionDir}`);
console.log();
console.log("When done, restore the placeholder so the manifest stays portable:");
console.log("  git checkout -- extension.toml");
