import { highlight, highlightLanguage } from "./highlight";

type HastNode = {
  type?: string;
  tagName?: string;
  value?: string;
  properties?: Record<string, unknown>;
  children?: HastNode[];
};

type NodePredicate = (node: HastNode) => boolean;
type NodeCallback = (node: HastNode) => void;

const ZERO_TOKEN_COLORS: Record<string, { light: string; dark: string }> = {
  comment: { light: "#6A737D", dark: "#6A737D" },
  string: { light: "#032F62", dark: "#9ECBFF" },
  char: { light: "#032F62", dark: "#9ECBFF" },
  keyword: { light: "#D73A49", dark: "#F97583" },
  type: { light: "#005CC5", dark: "#79B8FF" },
  function: { light: "#6F42C1", dark: "#B392F0" },
  number: { light: "#005CC5", dark: "#79B8FF" },
  variable: { light: "#24292E", dark: "#E1E4E8" },
  id: { light: "#6F42C1", dark: "#B392F0" },
  key: { light: "#D73A49", dark: "#F97583" },
  boolean: { light: "#005CC5", dark: "#79B8FF" },
  punctuation: { light: "#24292E", dark: "#E1E4E8" },
  operator: { light: "#D73A49", dark: "#F97583" },
};

function visit(node: HastNode | undefined, predicate: NodePredicate, callback: NodeCallback): void {
  if (!node) return;
  if (predicate(node)) callback(node);
  if (Array.isArray(node.children)) {
    for (const child of node.children) visit(child, predicate, callback);
  }
}

function highlightToHast(code: string, language: string): HastNode[] {
  const parts = highlight(code, language).split("\n");
  const lines = [];
  for (let li = 0; li < parts.length; li++) {
    const lineHtml = parts[li];
    const tokens = [];
    const tokenRegex = /<span class="hl-(\w+)">([\s\S]*?)<\/span>|([^<]+)/g;
    let match: RegExpExecArray | null;
    while ((match = tokenRegex.exec(lineHtml)) !== null) {
      if (match[1]) {
        const color = ZERO_TOKEN_COLORS[match[1]];
        const text = decodeEntities(match[2]);
        tokens.push({
          type: "element",
          tagName: "span",
          properties: color
            ? { style: `--shiki-light:${color.light};--shiki-dark:${color.dark}` }
            : {},
          children: [{ type: "text", value: text }],
        });
      } else if (match[3]) {
        tokens.push({ type: "text", value: decodeEntities(match[3]) });
      }
    }
    lines.push({
      type: "element",
      tagName: "span",
      properties: { "data-line": "" },
      children: tokens.length > 0 ? tokens : [{ type: "text", value: "" }],
    });
  }
  return lines;
}

function decodeEntities(value: string): string {
  return value
    .replaceAll("&amp;", "&")
    .replaceAll("&lt;", "<")
    .replaceAll("&gt;", ">")
    .replaceAll("&quot;", '"')
    .replaceAll("&#039;", "'");
}

function getNodeText(node: HastNode | undefined): string {
  if (!node) return "";
  if (node.type === "text") return node.value ?? "";
  if (!Array.isArray(node.children)) return "";
  return node.children.map(getNodeText).join("");
}

function getCodeLanguage(node: HastNode): string {
  const dataLanguage = node.properties?.dataLanguage ?? node.properties?.["data-language"];
  if (typeof dataLanguage === "string") return dataLanguage;
  const className = node.properties?.className;
  const classes = Array.isArray(className) ? className : typeof className === "string" ? className.split(/\s+/) : [];
  const languageClass = classes.find((value) => typeof value === "string" && value.startsWith("language-"));
  return typeof languageClass === "string" ? languageClass.slice("language-".length) : "";
}

export function rehypeZeroHighlight(): (tree: HastNode) => void {
  return (tree: HastNode) => {
    visit(
      tree,
      (node) =>
        node.type === "element" &&
        node.tagName === "code",
      (codeNode) => {
        const text = getNodeText(codeNode).replace(/\n$/, "");
        const language = highlightLanguage(getCodeLanguage(codeNode), text);
        if (!language) return;
        codeNode.children = highlightToHast(text, language);
        codeNode.properties = {
          ...codeNode.properties,
          style: "display:grid",
        };
      },
    );
  };
}
