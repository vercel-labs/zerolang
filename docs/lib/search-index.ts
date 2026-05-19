import { readArticleBySlug } from "./articles";
import { docs } from "./docs";
import type { SearchIndexEntry } from "./types";

let cached: SearchIndexEntry[] | null = null;

function stripMarkdown(md: string): string {
  return md
    .replace(/```[\s\S]*?```/g, "")
    .replace(/`[^`]+`/g, "")
    .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
    .replace(/^#{1,6}\s+/gm, "")
    .replace(/\*{1,3}([^*]+)\*{1,3}/g, "$1")
    .replace(/<[^>]+>/g, "")
    .replace(/\n{3,}/g, "\n\n")
    .trim();
}

export async function getSearchIndex(): Promise<SearchIndexEntry[]> {
  if (cached) return cached;

  const entries: SearchIndexEntry[] = [];
  for (const doc of docs) {
    try {
      const raw = await readArticleBySlug(doc.slug);
      const content = raw ? stripMarkdown(raw) : "";
      entries.push({
        title: doc.title,
        href: doc.path,
        section: doc.section ?? "",
        content,
      });
    } catch {
      entries.push({
        title: doc.title,
        href: doc.path,
        section: doc.section ?? "",
        content: "",
      });
    }
  }

  cached = entries;
  return entries;
}
