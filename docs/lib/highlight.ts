type TokenName =
  | "comment"
  | "string"
  | "char"
  | "keyword"
  | "type"
  | "function"
  | "number"
  | "variable"
  | "id"
  | "key"
  | "boolean"
  | "punctuation"
  | "operator";

type Grammar = ReadonlyArray<readonly [TokenName, RegExp]>;

function escapeHtml(value: string): string {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

const GRAMMARS: Record<string, Grammar> = {
  bash: [
    ["string", /'(?:[^'\\]|\\.)*'|"(?:[^"\\]|\\.)*"/],
    ["comment", /#.*/],
    ["variable", /\$\{?[A-Za-z_][A-Za-z0-9_]*\}?/],
    ["key", /--?[A-Za-z0-9][A-Za-z0-9-]*(?:=[^\s\\]+)?/],
    ["keyword", /\b(?:if|then|else|elif|fi|for|while|do|done|case|esac|function|export|local|readonly|return|exit|set|unset|cd|zero|pnpm|npm|git|curl|make)\b/],
    ["number", /\b\d+\b/],
    ["operator", /&&|\|\||\\|[|<>;&=]/],
    ["punctuation", /[{}()[\]]/],
  ],
  zero: [
    ["comment", /\/\/.*/],
    ["string", /"(?:[^"\\]|\\.)*"/],
    ["char", /'(?:[^'\\\n]|\\(?:[nrt0'"\\]|x[0-9A-Fa-f]{2}))'/],
    ["keyword", /\b(?:pub|fn|let|var|return|raises|if|else|while|for|in|match|check|rescue|raise|use|interface|enum|choice|type|alias|const|as|break|continue|defer|export|extern|packed|static|meta|mut|test|and|or|not|true|false|null)\b/],
    ["type", /\b(?:Void|World|Self|Bool|bool|char|u4|u8|u16|u32|u64|i8|i16|i32|i64|usize|isize|f16|f32|f64|Span|MutSpan|Maybe|Alloc|Arena|FixedBufAlloc|GeneralAlloc|NullAlloc|PageAlloc|ref|mutref|owned|Slice|Array|String|Error|Io|Fs|Net|Env|Args|Clock|Rand|Proc|Sync|Cancel|Reader|Writer|File|Path|Conn|Listener|Address|Config|Type|Field|c_int|c_long|c_size|cstr|[A-Z][A-Za-z0-9_]*)\b/],
    ["function", /\b[a-z_]\w*(?=\s*\()/],
    ["number", /\b(?:\d+\.\d+(?:[eE][+-]?\d+)?|0x[0-9a-fA-F_]+|0b[01_]+|0o[0-7_]+|\d[\d_]*(?:_[A-Za-z][A-Za-z0-9_]*)?)\b/],
    ["variable", /\b[a-z_]\w*(?=\.)/],
    ["punctuation", /[{}()\[\];,.<>]/],
    ["operator", /:|[-+*/%=!&|^~@?]+/],
  ],
  "zero-graph": [
    ["comment", /\/\/.*/],
    ["string", /"(?:[^"\\]|\\.)*"/],
    ["id", /#[A-Za-z_][A-Za-z0-9_]*/],
    ["keyword", /\b(?:zero-graph|zero-program-graph-patch|origin|module|hash|node|edge|expect|graphHash|set|insert|insertEdge|replace|delete|rename|addFunction|addMain|addParam|addReturnBinary|addLetLiteral|addLetBinary|addReturnValue|addCheckWrite|addCheckWriteValue|addTest|replaceFunctionBody|replaceBlockBody|end|let|var|return|if|else|while|for|in|match|check|rescue|raise|use|test|and|or|not)\b/],
    ["type", /\b(?:Function|Param|Block|Identifier|Literal|MethodCall|Call|FieldAccess|Let|Var|Return|Check|If|While|For|Match|TypeRef|ArrayLiteral|Unary|Binary|Void|World|Self|Bool|bool|char|u4|u8|u16|u32|u64|i8|i16|i32|i64|usize|isize|f16|f32|f64|Span|MutSpan|Maybe|Alloc|Arena|FixedBufAlloc|GeneralAlloc|NullAlloc|PageAlloc|ref|mutref|owned|Slice|Array|String|Error|[A-Z][A-Za-z0-9_]*)\b/],
    ["boolean", /\b(?:true|false|null)\b/],
    ["key", /\b[A-Za-z_][A-Za-z0-9_-]*(?=\s*(?::|=))/],
    ["function", /\b(?:std|[a-z_][A-Za-z0-9_]*)\.[A-Za-z_][A-Za-z0-9_.]*(?=\s|$)/],
    ["number", /\b(?:v\d+|graph:[0-9a-fA-F]+|nodehash:[0-9a-fA-F]+|0x[0-9a-fA-F_]+|0b[01_]+|0o[0-7_]+|\d[\d_]*)\b/],
    ["variable", /\b[a-z_][A-Za-z0-9_]*(?=\.)/],
    ["punctuation", /[{}()\[\];,.<>]/],
    ["operator", /:|=|[-+*/%!&|^~@?]+/],
  ],
};

const LANGUAGE_ALIASES: Record<string, string> = {
  sh: "bash",
  shell: "bash",
  zsh: "bash",
  graph: "zero-graph",
  "program-graph": "zero-graph",
  "zero-program-graph": "zero-graph",
  "graph-patch": "zero-graph",
  "zero-graph-patch": "zero-graph",
  "program-graph-patch": "zero-graph",
  "zero-program-graph-patch": "zero-graph",
};

function normalizeLanguage(language: string): string {
  const key = language.trim().toLowerCase();
  return LANGUAGE_ALIASES[key] ?? key;
}

function looksLikeGraph(code: string): boolean {
  const text = code.trimStart();
  return (
    /^zero-graph\s+v\d+\b/.test(text) ||
    /^zero-program-graph-patch\s+v\d+\b/.test(text) ||
    /^(?:node|edge)\s+#[A-Za-z_][A-Za-z0-9_]*\b/m.test(text) ||
    /^(?:expect\s+graphHash|set\s+node=|insert\s+node=|insertEdge\s+|replace\s+node=|rename\s+node=|delete\s+node=|replaceFunctionBody\s+|replaceBlockBody\s+)/m.test(text)
  );
}

export function highlightLanguage(language: string, code: string): string | null {
  const normalized = normalizeLanguage(language);
  if (GRAMMARS[normalized]) return normalized;
  if ((normalized === "" || normalized === "text" || normalized === "txt") && looksLikeGraph(code)) {
    return "zero-graph";
  }
  return null;
}

export function highlight(code: string, language: string): string {
  const normalized = highlightLanguage(language, code);
  const grammar = normalized ? GRAMMARS[normalized] : null;
  if (!grammar) return escapeHtml(code);

  const combined = new RegExp(
    grammar.map(([, pattern]) => `(${pattern.source})`).join("|"),
    "gm",
  );

  const names = grammar.map(([name]) => name);
  let result = "";
  let lastIndex = 0;

  for (const match of code.matchAll(combined)) {
    const matchIndex = match.index ?? 0;
    if (matchIndex > lastIndex) {
      result += escapeHtml(code.slice(lastIndex, matchIndex));
    }
    const groupIndex = match.slice(1).findIndex((g) => g !== undefined);
    if (groupIndex === -1) continue;
    const tokenType = names[groupIndex];
    const tokenText = match[0];
    result += `<span class="hl-${tokenType}">${escapeHtml(tokenText)}</span>`;
    lastIndex = matchIndex + tokenText.length;
  }

  if (lastIndex < code.length) {
    result += escapeHtml(code.slice(lastIndex));
  }
  return result;
}
