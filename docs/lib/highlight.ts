type TokenName =
  | "comment"
  | "string"
  | "char"
  | "keyword"
  | "type"
  | "function"
  | "number"
  | "variable"
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
  zero: [
    ["comment", /\/\/.*/],
    ["string", /"(?:[^"\\]|\\.)*"/],
    ["char", /'(?:[^'\\\n]|\\(?:[nrt0'"\\]|x[0-9A-Fa-f]{2}))'/],
    ["keyword", /\b(?:pub|fun|let|mut|var|return|if|else|while|for|in|match|check|rescue|raise|raises|use|import|shape|interface|enum|choice|type|const|as|break|continue|defer|export|extern|packed|static|meta|test|and|or|not|true|false|null)\b/],
    ["type", /\b(?:Void|World|Self|Bool|bool|char|u4|u8|u16|u32|u64|i8|i16|i32|i64|usize|isize|f16|f32|f64|Span|MutSpan|Maybe|Alloc|Arena|FixedBufAlloc|GeneralAlloc|NullAlloc|PageAlloc|ref|mutref|owned|Slice|Array|String|Error|Io|Fs|Net|Env|Args|Clock|Rand|Proc|Sync|Cancel|Reader|Writer|File|Path|Conn|Listener|Address|Config|Type|Field|c_int|c_long|c_size|cstr|[A-Z][A-Za-z0-9_]*)\b/],
    ["function", /\b[a-z_]\w*(?=\s*\()/],
    ["number", /\b(?:\d+\.\d+(?:[eE][+-]?\d+)?|0x[0-9a-fA-F_]+|0b[01_]+|0o[0-7_]+|\d[\d_]*(?:_[A-Za-z][A-Za-z0-9_]*)?)\b/],
    ["variable", /\b[a-z_]\w*(?=\.)/],
    ["punctuation", /[{}()\[\];,.<>]/],
    ["operator", /->|:|[-+*/%=!&|^~@?]+/],
  ],
};

export function highlight(code: string, language: string): string {
  const grammar = GRAMMARS[language];
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
