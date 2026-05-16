import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { describe, it } from "node:test";
import toml from "toml";

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
});
