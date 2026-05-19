#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const outPath = path.join(repoRoot, "native/zero-c/src/embedded_skills.inc");
const inputs = [
  "skills/zero/SKILL.md",
  ...fs.readdirSync(path.join(repoRoot, "skill-data"))
    .filter((name) => name.endsWith(".md"))
    .sort((a, b) => a.localeCompare(b))
    .map((name) => `skill-data/${name}`),
];

type EmbeddedSkill = {
  name: string;
  description: string;
  hidden: boolean;
  relativePath: string;
  text: string;
  ident?: string;
};

function parseFrontmatter(text, relativePath): Omit<EmbeddedSkill, "relativePath" | "text" | "ident"> {
  const match = text.trimStart().match(/^---\r?\n([\s\S]*?)\r?\n---/);
  if (!match) throw new Error(`${relativePath}: missing skill frontmatter`);

  const lines = match[1].split(/\r?\n/);
  let name = null;
  let description = "";
  let hidden = false;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line.startsWith("name:")) {
      name = line.slice("name:".length).trim();
    } else if (line.startsWith("description:")) {
      const parts = [line.slice("description:".length).trim()];
      while (i + 1 < lines.length && /^(  |\t)/.test(lines[i + 1])) {
        parts.push(lines[++i].trim());
      }
      description = parts.join(" ");
    } else if (line.startsWith("hidden:")) {
      hidden = ["true", "yes"].includes(line.slice("hidden:".length).trim());
    }
  }

  if (!name) throw new Error(`${relativePath}: missing skill name`);
  return { name, description, hidden };
}

function chunkText(text) {
  const chunks = [];
  let current = "";
  for (const line of text.match(/[^\n]*\n|[^\n]+$/g) || []) {
    if (current && JSON.stringify(current + line).length > 3000) {
      chunks.push(current);
      current = "";
    }
    if (JSON.stringify(line).length <= 3000) {
      current += line;
      continue;
    }
    for (let index = 0; index < line.length; index += 1400) {
      if (current) {
        chunks.push(current);
        current = "";
      }
      chunks.push(line.slice(index, index + 1400));
    }
  }
  if (current) chunks.push(current);
  return chunks;
}

function cIdent(text) {
  return text.replace(/[^A-Za-z0-9_]/g, "_");
}

const skills: EmbeddedSkill[] = inputs.map((relativePath) => {
  const text = fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
  return { ...parseFrontmatter(text, relativePath), relativePath, text };
}).sort((a, b) => a.name.localeCompare(b.name));

const out = [];
out.push("/* Generated from Zero skill data. Run node --experimental-strip-types --disable-warning=ExperimentalWarning scripts/embed-skill-data.mts to refresh. */");
out.push("#ifndef ZERO_EMBEDDED_SKILLS_INC");
out.push("#define ZERO_EMBEDDED_SKILLS_INC");
out.push("");
out.push("typedef struct {");
out.push("  const char *name;");
out.push("  const char *description;");
out.push("  bool hidden;");
out.push("  const char *const *content;");
out.push("} ZeroEmbeddedSkill;");
out.push("");

for (const skill of skills) {
  const ident = `zero_embedded_skill_${cIdent(skill.name)}_chunks`;
  skill.ident = ident;
  out.push(`static const char *const ${ident}[] = {`);
  for (const chunk of chunkText(skill.text)) {
    out.push(`  ${JSON.stringify(chunk)},`);
  }
  out.push("  NULL");
  out.push("};");
  out.push("");
}

out.push("static const ZeroEmbeddedSkill zero_embedded_skills[] = {");
for (const skill of skills) {
  out.push(`  {${JSON.stringify(skill.name)}, ${JSON.stringify(skill.description)}, ${skill.hidden ? "true" : "false"}, ${skill.ident}},`);
}
out.push("};");
out.push("static const size_t zero_embedded_skill_count = sizeof(zero_embedded_skills) / sizeof(zero_embedded_skills[0]);");
out.push("");
out.push("#endif");
out.push("");

fs.writeFileSync(outPath, out.join("\n"));
