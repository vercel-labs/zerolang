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
    assert.ok(snippets.if);
    assert.ok(snippets.while);
    assert.ok(snippets["GET route"]);
  });

  it("declares block comment support in language config", async () => {
    const config = toml.parse(await readFile("languages/zero/config.toml", "utf8"));

    assert.deepEqual(config.block_comment, ["/*", "*/"]);
  });

  it("parses representative Zero files without tree-sitter ERROR nodes", async () => {
    const grammarDir = join(extensionDir, "tree-sitter-zero");
    const sampleFiles = [
      "examples/std-http-headers.0",
      "examples/std-http-json.0",
      "examples/std-http-request.0",
      "examples/std-json-bytes.0",
      "conformance/native/pass/array-repeat-literal.0",
      "conformance/native/pass/array-repeat-record-field.0",
      "conformance/native/pass/generic-static-array-specialization.0",
      "conformance/native/pass/generic-static-forwarded-array-specialization.0",
      "conformance/native/pass/std-http-fetch.0",
      "examples/static-interface.0",
      "examples/direct-enum-match.0",
    ];

    for (const relativePath of sampleFiles) {
      let output = "";
      try {
        const { stdout, stderr } = await execFileAsync(
          "npx",
          ["tree-sitter", "parse", join(repoRoot, relativePath)],
          { cwd: grammarDir, windowsHide: true },
        );
        output = `${stdout}\n${stderr}`;
      } catch (error) {
        output = `${error.stdout ?? ""}\n${error.stderr ?? ""}`;
      }
      assert.doesNotMatch(output, /\(ERROR /, `expected ${relativePath} to parse without ERROR nodes`);
    }
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
