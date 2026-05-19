import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { docs, findDocBySlug } from "./docs";
import type { Article, Heading } from "./types";

const ARTICLES_ROOT = join(process.cwd(), "articles");

export async function readArticleBySlug(slug: string): Promise<string | null> {
  const doc = findDocBySlug(slug);
  if (!doc) return null;
  const relative = doc.sourcePath.replace(/^\/articles\//, "");
  const filePath = join(ARTICLES_ROOT, relative);
  return readFile(filePath, "utf8");
}

export async function readArticleByPath(routePath: string): Promise<Article | null> {
  const doc = docs.find((d) => d.path === routePath);
  if (!doc) return null;
  const source = await readArticleBySlug(doc.slug);
  if (source === null) return null;
  return { doc, source };
}

export function extractHeadings(markdown: string): Heading[] {
  const lines = markdown.replace(/\r\n/g, "\n").split("\n");
  const headings: Heading[] = [];
  let inCodeBlock = false;

  for (const line of lines) {
    if (line.trim().startsWith("```")) {
      inCodeBlock = !inCodeBlock;
      continue;
    }
    if (inCodeBlock) continue;

    const match = /^(#{2,4})\s+(.+)$/.exec(line.trim());
    if (match) {
      const text = match[2].replace(/`([^`]+)`/g, "$1");
      headings.push({
        level: match[1].length as Heading["level"],
        text,
        id: slugify(match[2]),
      });
    }
  }
  return headings;
}

function slugify(value: string): string {
  return value
    .toLowerCase()
    .replace(/`([^`]+)`/g, "$1")
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "");
}
