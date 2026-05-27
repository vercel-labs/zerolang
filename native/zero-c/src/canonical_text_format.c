#include "canonical_text.h"

#include <stdlib.h>

typedef struct {
  ZBuf *out;
  const ZCanonicalToken *previous;
  const ZCanonicalToken *line_first;
  size_t indent;
  bool line_start;
  bool line_has_assignment;
  bool multiline_braces[256];
  bool list_braces[256];
  size_t brace_depth;
} CanonFormat;

static bool fmt_text_eq(const char *left, const char *right) {
  if (!left || !right) return left == right;
  while (*left && *right && *left == *right) {
    left++;
    right++;
  }
  return *left == 0 && *right == 0;
}

static bool fmt_is_symbol(const ZCanonicalToken *token, const char *text) {
  return token && token->kind == Z_CANON_TOKEN_SYMBOL && fmt_text_eq(token->text, text);
}

static bool fmt_is_word(const ZCanonicalToken *token, const char *text) {
  return token && token->kind == Z_CANON_TOKEN_WORD && fmt_text_eq(token->text, text);
}

static bool fmt_connected(const ZCanonicalToken *left, const ZCanonicalToken *right) {
  return left && right && left->offset + left->length == right->offset;
}

static void fmt_append_char(ZBuf *out, char ch) {
  size_t required = out->len + 2;
  if (required > out->cap) {
    out->cap = z_grow_capacity(out->cap, required, 64);
    out->data = z_checked_reallocarray(out->data, out->cap, sizeof(char));
  }
  out->data[out->len++] = ch;
  out->data[out->len] = 0;
}

static void fmt_append(ZBuf *out, const char *text) {
  while (text && *text) fmt_append_char(out, *text++);
}

static void fmt_newline(CanonFormat *fmt) {
  if (fmt->out->len > 0 && fmt->out->data[fmt->out->len - 1] != '\n') fmt_append_char(fmt->out, '\n');
  fmt->line_start = true;
  fmt->line_first = NULL;
  fmt->line_has_assignment = false;
}

static void fmt_blank_line(CanonFormat *fmt) {
  fmt_newline(fmt);
  if (fmt->out->len < 2 || fmt->out->data[fmt->out->len - 2] != '\n') fmt_append_char(fmt->out, '\n');
}

static void fmt_indent(CanonFormat *fmt) {
  if (!fmt->line_start) return;
  for (size_t i = 0; i < fmt->indent; i++) fmt_append(fmt->out, "    ");
  fmt->line_start = false;
}

static const ZCanonicalToken *fmt_next_significant(const ZCanonicalTokenVec *tokens, size_t index) {
  for (size_t i = index + 1; i < tokens->len; i++) {
    const ZCanonicalToken *token = &tokens->items[i];
    if (token->kind != Z_CANON_TOKEN_NEWLINE && token->kind != Z_CANON_TOKEN_COMMENT) return token;
  }
  return NULL;
}

static bool fmt_starts_declaration(const ZCanonicalToken *token) {
  return fmt_is_word(token, "use") || fmt_is_word(token, "extern") || fmt_is_word(token, "const") ||
    fmt_is_word(token, "alias") || fmt_is_word(token, "type") || fmt_is_word(token, "packed") ||
    fmt_is_word(token, "enum") || fmt_is_word(token, "choice") || fmt_is_word(token, "interface") ||
    fmt_is_word(token, "fn") || fmt_is_word(token, "pub") || fmt_is_word(token, "export") ||
    fmt_is_word(token, "test");
}

static bool fmt_line_opens_multiline_brace(const CanonFormat *fmt) {
  if (fmt->line_has_assignment) return false;
  const ZCanonicalToken *first = fmt->line_first;
  return fmt_is_word(first, "type") || fmt_is_word(first, "packed") || fmt_is_word(first, "enum") ||
    fmt_is_word(first, "choice") || fmt_is_word(first, "interface") || fmt_is_word(first, "extern") || fmt_is_word(first, "fn") ||
    fmt_is_word(first, "pub") || fmt_is_word(first, "export") || fmt_is_word(first, "test") ||
    fmt_is_word(first, "if") || fmt_is_word(first, "else") || fmt_is_word(first, "while") ||
    fmt_is_word(first, "for") || fmt_is_word(first, "match") || fmt_is_word(first, "defer") ||
    fmt_is_symbol(first, ".");
}

static bool fmt_line_opens_list_brace(const CanonFormat *fmt) {
  const ZCanonicalToken *first = fmt->line_first;
  return fmt_is_word(first, "type") || fmt_is_word(first, "packed") || fmt_is_word(first, "extern") ||
    fmt_is_word(first, "enum") || fmt_is_word(first, "choice");
}

static bool fmt_operator(const ZCanonicalToken *token) {
  return fmt_is_symbol(token, "=") || fmt_is_symbol(token, "->") || fmt_is_symbol(token, "=>") ||
    fmt_is_symbol(token, "+") || fmt_is_symbol(token, "-") || fmt_is_symbol(token, "*") ||
    fmt_is_symbol(token, "/") || fmt_is_symbol(token, "%") || fmt_is_symbol(token, "==") ||
    fmt_is_symbol(token, "!=") || fmt_is_symbol(token, "<=") || fmt_is_symbol(token, ">=") ||
    fmt_is_symbol(token, "&&") || fmt_is_symbol(token, "||") || fmt_is_symbol(token, "+%") ||
    fmt_is_symbol(token, "+|");
}

static bool fmt_compact_angle(const ZCanonicalToken *left, const ZCanonicalToken *right) {
  return (fmt_is_symbol(left, "<") || fmt_is_symbol(right, "<") || fmt_is_symbol(left, ">") || fmt_is_symbol(right, ">")) &&
    fmt_connected(left, right);
}

static bool fmt_space_before(const CanonFormat *fmt, const ZCanonicalToken *token) {
  const ZCanonicalToken *prev = fmt->previous;
  if (!prev || fmt->line_start) return false;
  if (fmt_is_symbol(token, ",") || fmt_is_symbol(token, ":") || fmt_is_symbol(token, ";") || fmt_is_symbol(token, ")") || fmt_is_symbol(token, "]")) return false;
  if (fmt_is_symbol(token, "}")) return !fmt_is_symbol(prev, "{");
  if (fmt_is_symbol(token, ".") || fmt_is_symbol(prev, ".")) return false;
  if ((fmt_is_symbol(token, "..") || fmt_is_symbol(prev, "..")) && fmt_connected(prev, token)) return false;
  if (fmt_is_symbol(prev, "&") && fmt_connected(prev, token)) return false;
  if (fmt_connected(prev, token) && fmt_is_symbol(prev, "]")) return false;
  if (fmt_is_symbol(token, "(")) {
    if (fmt_is_symbol(prev, ">") && fmt_connected(prev, token)) return false;
    if (prev->kind == Z_CANON_TOKEN_WORD && !fmt_is_word(prev, "if") && !fmt_is_word(prev, "while") &&
        !fmt_is_word(prev, "for") && !fmt_is_word(prev, "match") && !fmt_is_word(prev, "return") &&
        !fmt_is_word(prev, "check") && !fmt_is_word(prev, "expect") && !fmt_is_word(prev, "rescue")) return false;
    return !(fmt_is_symbol(prev, "(") || fmt_is_symbol(prev, "["));
  }
  if (fmt_is_symbol(token, "[")) {
    if (fmt_is_word(prev, "raises")) return true;
    if (prev->kind == Z_CANON_TOKEN_WORD || fmt_is_symbol(prev, ")") || fmt_is_symbol(prev, "]")) return false;
    return !(fmt_is_symbol(prev, "(") || fmt_is_symbol(prev, "[") || fmt_is_symbol(prev, "<"));
  }
  if (fmt_compact_angle(prev, token)) return false;
  if (fmt_is_symbol(prev, "(") || fmt_is_symbol(prev, "[") || fmt_is_symbol(prev, "<")) return false;
  if (fmt_is_symbol(token, "{")) return true;
  if (fmt_is_symbol(prev, "{")) return true;
  if (fmt_operator(prev) || fmt_operator(token) || fmt_is_symbol(token, "<") || fmt_is_symbol(token, ">") || fmt_is_symbol(prev, ":") || fmt_is_symbol(prev, ",") || fmt_is_symbol(prev, ";")) return true;
  return true;
}

static void fmt_emit_token(CanonFormat *fmt, const ZCanonicalToken *token) {
  if (fmt_space_before(fmt, token)) fmt_append_char(fmt->out, ' ');
  fmt_indent(fmt);
  fmt_append(fmt->out, token->text);
  if (!fmt->line_first && (token->kind != Z_CANON_TOKEN_SYMBOL || fmt_is_symbol(token, "."))) fmt->line_first = token;
  if (fmt_is_symbol(token, "=")) fmt->line_has_assignment = true;
  fmt->previous = token;
}

bool z_canonical_text_format(const ZCanonicalTokenVec *tokens, ZBuf *out, ZDiag *diag) {
  if (!tokens || !out) return false;
  CanonFormat fmt = {.out = out, .line_start = true};
  for (size_t i = 0; i < tokens->len; i++) {
    const ZCanonicalToken *token = &tokens->items[i];
    if (token->kind == Z_CANON_TOKEN_EOF) break;
    if (token->kind == Z_CANON_TOKEN_NEWLINE) {
      const ZCanonicalToken *next = fmt_next_significant(tokens, i);
      if (next && fmt.indent == 0 && fmt_starts_declaration(next) && fmt.out->len > 0) fmt_blank_line(&fmt);
      else fmt_newline(&fmt);
      continue;
    }
    if (token->kind == Z_CANON_TOKEN_COMMENT) {
      if (!fmt.line_start) fmt_append_char(out, ' ');
      fmt_indent(&fmt);
      fmt_append(out, token->text);
      fmt_newline(&fmt);
      fmt.previous = token;
      continue;
    }
    if (fmt_is_symbol(token, "{")) {
      const ZCanonicalToken *next = fmt_next_significant(tokens, i);
      bool multiline = next && !fmt_is_symbol(next, "}") && fmt_line_opens_multiline_brace(&fmt);
      if (fmt.brace_depth < sizeof(fmt.multiline_braces) / sizeof(fmt.multiline_braces[0])) {
        fmt.brace_depth++;
        fmt.multiline_braces[fmt.brace_depth] = multiline;
        fmt.list_braces[fmt.brace_depth] = multiline && fmt_line_opens_list_brace(&fmt);
      }
      fmt_emit_token(&fmt, token);
      if (multiline) {
        fmt.indent++;
        fmt_newline(&fmt);
      }
      continue;
    }
    if (fmt_is_symbol(token, "}")) {
      bool multiline = fmt.brace_depth > 0 && fmt.multiline_braces[fmt.brace_depth];
      if (multiline) {
        if (!fmt.line_start) fmt_newline(&fmt);
        if (fmt.indent > 0) fmt.indent--;
      }
      if (fmt.brace_depth > 0) fmt.brace_depth--;
      fmt_emit_token(&fmt, token);
      const ZCanonicalToken *next = fmt_next_significant(tokens, i);
      if (multiline && next && !fmt_is_word(next, "else") && !fmt_is_symbol(next, ",") && !fmt_is_symbol(next, ")") && !fmt_is_symbol(next, "]")) {
        if (fmt.indent == 0 && fmt_starts_declaration(next)) fmt_blank_line(&fmt);
        else fmt_newline(&fmt);
      }
      continue;
    }
    if (fmt_is_symbol(token, ",") && fmt.brace_depth > 0 && fmt.list_braces[fmt.brace_depth]) {
      fmt_emit_token(&fmt, token);
      const ZCanonicalToken *next = fmt_next_significant(tokens, i);
      if (next && !fmt_is_symbol(next, "}")) fmt_newline(&fmt);
      continue;
    }
    fmt_emit_token(&fmt, token);
  }
  fmt_newline(&fmt);
  (void)diag;
  return true;
}

bool z_canonical_text_format_source(const char *source, ZBuf *out, ZDiag *diag) {
  ZCanonicalTokenVec tokens = z_canonical_text_tokenize(source, diag);
  if (diag && diag->code != 0) {
    z_free_canonical_text_tokens(&tokens);
    return false;
  }
  ZCanonicalTree tree = {0};
  ZCanonicalFacts facts = {0};
  if (!z_canonical_text_parse(&tokens, &tree, &facts, diag)) {
    z_free_canonical_text_tree(&tree);
    z_free_canonical_text_tokens(&tokens);
    return false;
  }
  bool ok = z_canonical_text_format(&tokens, out, diag);
  z_free_canonical_text_tree(&tree);
  z_free_canonical_text_tokens(&tokens);
  if (!ok) return false;
  ZCanonicalTokenVec formatted_tokens = z_canonical_text_tokenize(out->data ? out->data : "", diag);
  if (diag && diag->code != 0) {
    z_free_canonical_text_tokens(&formatted_tokens);
    return false;
  }
  ZCanonicalTree formatted_tree = {0};
  ZCanonicalFacts formatted_facts = {0};
  ok = z_canonical_text_parse(&formatted_tokens, &formatted_tree, &formatted_facts, diag);
  z_free_canonical_text_tree(&formatted_tree);
  z_free_canonical_text_tokens(&formatted_tokens);
  return ok;
}
