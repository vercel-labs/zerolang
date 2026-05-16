import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import toml from "toml";

const execFileAsync = promisify(execFile);
const testDir = dirname(fileURLToPath(import.meta.url));
const extensionDir = resolve(testDir, "..");
const repoRoot = resolve(extensionDir, "..", "..");

describe("Zed extension manifest", () => {
  it("declares Zero language metadata and grammar", async () => {
    const manifest = toml.parse(await readFile("extension.toml", "utf8"));

    assert.equal(manifest.id, "zero");
    assert.equal(manifest.name, "Zero");
    assert.equal(manifest.schema_version, 1);
    assert.equal(manifest.grammars.zero.repository, "file://tree-sitter-zero");
  });

  it("recognizes .0 files as Zero", async () => {
    const config = toml.parse(await readFile("languages/zero/config.toml", "utf8"));

    assert.equal(config.name, "Zero");
    assert.equal(config.grammar, "zero");
    assert.deepEqual(config.path_suffixes, ["0"]);
    assert.deepEqual(config.line_comments, ["// "]);
  });

  it("ships Tree-sitter queries for core editor features", async () => {
    const highlights = await readFile("languages/zero/highlights.scm", "utf8");
    const outline = await readFile("languages/zero/outline.scm", "utf8");

    assert.match(highlights, /function_declaration/);
    assert.match(highlights, /primitive_type/);
    assert.match(highlights, /raises/);
    assert.match(outline, /shape_declaration/);
  });

  it("ships snippets matching the VS Code extension surface", async () => {
    const snippets = JSON.parse(await readFile("snippets/zero.json", "utf8"));

    assert.ok(snippets.main);
    assert.ok(snippets.shape);
    assert.ok(snippets.function);
    assert.ok(snippets.test);
    assert.ok(snippets["GET route"]);
  });

  it("prepares a dev extension with a fetchable local grammar repository", async () => {
    await execFileAsync(process.execPath, ["scripts/prepare-dev-extension.mjs"], {
      cwd: extensionDir,
      windowsHide: true,
    });

    const manifest = toml.parse(
      await readFile(join(repoRoot, ".zero", "zed", "zero", "extension.toml"), "utf8"),
    );
    const grammar = manifest.grammars.zero;
    assert.match(grammar.repository, /^file:\/\//);
    assert.equal(grammar.rev, "main");

    const { stdout } = await execFileAsync("git", ["ls-remote", grammar.repository, grammar.rev], {
      windowsHide: true,
    });
    assert.match(stdout, /refs\/heads\/main/);
  });
});
