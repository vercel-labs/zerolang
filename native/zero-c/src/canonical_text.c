#include "canonical_text.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const ZCanonicalTokenVec *tokens;
  ZCanonicalTree *tree;
  ZCanonicalFacts *facts;
  ZDiag *diag;
  size_t pos;
} CanonParser;

static bool canon_text_eq(const char *left, const char *right) {
  if (!left || !right) return left == right;
  while (*left && *right && *left == *right) {
    left++;
    right++;
  }
  return *left == 0 && *right == 0;
}

static bool canon_is_word_text(const ZCanonicalToken *token, const char *text) {
  return token && token->kind == Z_CANON_TOKEN_WORD && canon_text_eq(token->text, text);
}

static bool canon_is_symbol_text(const ZCanonicalToken *token, const char *text) {
  return token && token->kind == Z_CANON_TOKEN_SYMBOL && canon_text_eq(token->text, text);
}

static bool canon_tokens_connected(const ZCanonicalToken *left, const ZCanonicalToken *right) {
  return left && right && left->offset + left->length == right->offset;
}

static bool canon_fail(ZDiag *diag, const ZCanonicalToken *token, const char *message, const char *expected, const char *actual) {
  if (diag) {
    diag->code = 100;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "");
    snprintf(diag->help, sizeof(diag->help), "use canonical .0 text source");
    diag->line = token ? token->line : 1;
    diag->column = token ? token->column : 1;
    diag->length = token && token->length ? (int)token->length : 1;
  }
  return false;
}

static void canon_push_token(ZCanonicalTokenVec *vec, ZCanonicalToken token) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 64);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(ZCanonicalToken));
  }
  vec->items[vec->len++] = token;
}

static void canon_push_node(ZCanonicalTree *tree, ZCanonicalNode node) {
  if (tree->len == tree->cap) {
    tree->cap = z_grow_capacity(tree->cap, tree->len + 1, 64);
    tree->items = z_checked_reallocarray(tree->items, tree->cap, sizeof(ZCanonicalNode));
  }
  tree->items[tree->len++] = node;
}

static char *canon_slice(const char *source, size_t start, size_t end) { return z_strndup(source + start, end - start); }

static bool canon_word_start(char ch) { return isalpha((unsigned char)ch) || ch == '_'; }

static bool canon_word_part(char ch) { return isalnum((unsigned char)ch) || ch == '_'; }

static bool canon_is_reserved_word(const char *text) {
  const char *keywords[] = {"alias", "as", "break", "check", "choice", "const", "continue", "defer", "else", "enum", "expect", "export", "extern", "false", "fn", "for", "fun", "if", "import", "in", "interface", "let", "match", "meta", "mut", "null", "packed", "pub", "raise", "raises", "rescue", "ret", "return", "shape", "static", "test", "true", "type", "use", "var", "while", NULL};
  for (size_t i = 0; keywords[i]; i++) if (canon_text_eq(text, keywords[i])) return true;
  return false;
}

static int canon_hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

static bool canon_integer_suffix_text(const char *text) {
  const char *suffixes[] = {"i8", "i16", "i32", "i64", "isize", "u8", "u16", "u32", "u64", "usize", NULL};
  for (size_t i = 0; suffixes[i]; i++) if (canon_text_eq(text, suffixes[i])) return true;
  return false;
}

static bool canon_validate_integer_number_text(const char *text) {
  if (!text || !text[0]) return false;
  size_t text_len = strlen(text);
  size_t body_len = text_len;
  const char *last_underscore = strrchr(text, '_');
  if (last_underscore) {
    const char *candidate = last_underscore + 1;
    if (candidate[0] == 0) return false;
    if (canon_integer_suffix_text(candidate)) body_len = (size_t)(last_underscore - text);
    else if (candidate[0] == 'i' || candidate[0] == 'u') return false;
  }
  if (body_len == 0) return false;

  int radix = 10;
  size_t index = 0;
  if (body_len >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    radix = 16;
    index = 2;
  } else if (body_len >= 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
    radix = 2;
    index = 2;
  } else if (body_len >= 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) {
    radix = 8;
    index = 2;
  }
  if (index >= body_len) return false;

  bool saw_digit = false;
  bool previous_underscore = false;
  for (; index < body_len; index++) {
    char ch = text[index];
    if (ch == '_') {
      if (!saw_digit || previous_underscore) return false;
      previous_underscore = true;
      continue;
    }
    int digit = canon_hex_digit(ch);
    if (digit < 0 || digit >= radix) return false;
    saw_digit = true;
    previous_underscore = false;
  }
  return saw_digit && !previous_underscore;
}

static bool canon_validate_float_number_text(const char *text) {
  if (!text || !text[0] || strchr(text, '_')) return false;
  size_t index = 0;
  if (!isdigit((unsigned char)text[index])) return false;
  while (isdigit((unsigned char)text[index])) index++;
  if (text[index] != '.') return false;
  index++;
  if (!isdigit((unsigned char)text[index])) return false;
  while (isdigit((unsigned char)text[index])) index++;
  if (text[index] == 'e' || text[index] == 'E') {
    index++;
    if (text[index] == '+' || text[index] == '-') index++;
    if (!isdigit((unsigned char)text[index])) return false;
    while (isdigit((unsigned char)text[index])) index++;
  }
  return text[index] == 0;
}

static bool canon_validate_number_text(const char *text) {
  return text && strchr(text, '.') ? canon_validate_float_number_text(text) : canon_validate_integer_number_text(text);
}

static bool canon_scan_string(const char *source, size_t *offset, int *column, ZCanonicalTokenVec *tokens, ZDiag *diag, int line) {
  size_t start = *offset;
  int start_col = *column;
  (*offset)++;
  (*column)++;
  while (source[*offset] && source[*offset] != '"') {
    if (source[*offset] == '\n') return canon_fail(diag, NULL, "unterminated string literal", "closing quote", "newline");
    if (source[*offset] == '\\' && source[*offset + 1]) {
      ZCanonicalToken escape = {Z_CANON_TOKEN_STRING, NULL, line, *column, *offset, 1};
      if (source[*offset + 1] == '\n' || source[*offset + 1] == '\r') {
        return canon_fail(diag, &escape, "escaped newlines are not canonical string literals", "escaped byte or closing quote", "newline");
      }
      if (source[*offset + 1] == 'x') {
        char high_ch = source[*offset + 2];
        char low_ch = high_ch ? source[*offset + 3] : 0;
        int high = canon_hex_digit(high_ch);
        int low = canon_hex_digit(low_ch);
        if (high < 0 || low < 0) return canon_fail(diag, &escape, "malformed hex string escape", "two hex digits", "invalid escape");
        if (((high << 4) | low) == 0) return canon_fail(diag, &escape, "string literal cannot contain a zero byte", "nonzero byte", "zero byte");
        *offset += 4;
        *column += 4;
        continue;
      }
      *offset += 2;
      *column += 2;
      continue;
    }
    (*offset)++;
    (*column)++;
  }
  if (source[*offset] != '"') return canon_fail(diag, NULL, "unterminated string literal", "closing quote", "end of file");
  (*offset)++;
  (*column)++;
  canon_push_token(tokens, (ZCanonicalToken){Z_CANON_TOKEN_STRING, canon_slice(source, start, *offset), line, start_col, start, *offset - start});
  return true;
}

static bool canon_scan_char(const char *source, size_t *offset, int *column, ZCanonicalTokenVec *tokens, ZDiag *diag, int line) {
  size_t start = *offset;
  int start_col = *column;
  ZCanonicalToken at_start = {Z_CANON_TOKEN_CHAR, NULL, line, start_col, start, 1};
  (*offset)++;
  (*column)++;

  if (!source[*offset] || source[*offset] == '\n' || source[*offset] == '\r' || source[*offset] == '\'') {
    return canon_fail(diag, &at_start, "malformed character literal", "one byte character literal", "empty");
  }

  if (source[*offset] == '\\') {
    (*offset)++;
    (*column)++;
    char escaped = source[*offset];
    if (!escaped || escaped == '\n' || escaped == '\r') return canon_fail(diag, &at_start, "malformed character escape", "one byte character literal", "newline");
    if (escaped == 'x') {
      char high_ch = source[*offset + 1];
      char low_ch = high_ch ? source[*offset + 2] : 0;
      if (canon_hex_digit(high_ch) < 0 || canon_hex_digit(low_ch) < 0) {
        return canon_fail(diag, &at_start, "malformed hex character escape", "two hex digits", "invalid escape");
      }
      *offset += 3;
      *column += 3;
    } else {
      if (escaped != 'n' && escaped != 'r' && escaped != 't' && escaped != '0' && escaped != '\'' && escaped != '"' && escaped != '\\') {
        return canon_fail(diag, &at_start, "invalid character escape", "one byte character literal", "invalid escape");
      }
      (*offset)++;
      (*column)++;
    }
  } else {
    if ((unsigned char)source[*offset] >= 128) return canon_fail(diag, &at_start, "character literal must be one byte", "ASCII byte", "non-ASCII");
    (*offset)++;
    (*column)++;
  }

  if (source[*offset] != '\'') return canon_fail(diag, &at_start, "character literal must contain exactly one byte", "closing quote", "extra content");
  (*offset)++;
  (*column)++;
  canon_push_token(tokens, (ZCanonicalToken){Z_CANON_TOKEN_CHAR, canon_slice(source, start, *offset), line, start_col, start, *offset - start});
  return true;
}

static bool canon_two_char_symbol(const char *source, size_t offset) {
  const char *pairs[] = {"->", "==", "!=", "<=", ">=", "&&", "||", "..", "=>"};
  for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
    if (source[offset] == pairs[i][0] && source[offset + 1] == pairs[i][1]) return true;
  }
  return false;
}

ZCanonicalTokenVec z_canonical_text_tokenize(const char *source, ZDiag *diag) {
  ZCanonicalTokenVec tokens = {0};
  size_t offset = 0;
  int line = 1;
  int column = 1;
  while (source && source[offset]) {
    char ch = source[offset];
    if (ch == '\t') {
      canon_fail(diag, NULL, "tabs are not canonical text whitespace", "spaces", "tab");
      break;
    }
    if (ch == ' ' || ch == '\r') {
      offset++;
      column++;
      continue;
    }
    if (ch == '\n') {
      canon_push_token(&tokens, (ZCanonicalToken){Z_CANON_TOKEN_NEWLINE, z_strdup("\n"), line, column, offset, 1});
      offset++;
      line++;
      column = 1;
      continue;
    }
    if (ch == '/' && source[offset + 1] == '/') {
      size_t start = offset;
      int start_col = column;
      while (source[offset] && source[offset] != '\n') {
        offset++;
        column++;
      }
      canon_push_token(&tokens, (ZCanonicalToken){Z_CANON_TOKEN_COMMENT, canon_slice(source, start, offset), line, start_col, start, offset - start});
      continue;
    }
    if (ch == '"') {
      if (!canon_scan_string(source, &offset, &column, &tokens, diag, line)) break;
      continue;
    }
    if (ch == '\'') {
      if (!canon_scan_char(source, &offset, &column, &tokens, diag, line)) break;
      continue;
    }
    if (canon_word_start(ch)) {
      size_t start = offset;
      int start_col = column;
      while (canon_word_part(source[offset])) {
        offset++;
        column++;
      }
      canon_push_token(&tokens, (ZCanonicalToken){Z_CANON_TOKEN_WORD, canon_slice(source, start, offset), line, start_col, start, offset - start});
      continue;
    }
    if (isdigit((unsigned char)ch)) {
      size_t start = offset;
      int start_col = column;
      while (source[offset] && (isalnum((unsigned char)source[offset]) || source[offset] == '_' || source[offset] == '.' ||
             ((source[offset] == '+' || source[offset] == '-') && offset > start && (source[offset - 1] == 'e' || source[offset - 1] == 'E')))) {
        if (source[offset] == '.' && source[offset + 1] == '.') break;
        offset++;
        column++;
      }
      char *text = canon_slice(source, start, offset);
      ZCanonicalToken token = {Z_CANON_TOKEN_NUMBER, text, line, start_col, start, offset - start};
      if (!canon_validate_number_text(text)) {
        canon_fail(diag, &token, "malformed number literal", "number literal", text);
        free(text);
        break;
      }
      canon_push_token(&tokens, token);
      continue;
    }
    size_t len = canon_two_char_symbol(source, offset) ? 2 : 1;
    canon_push_token(&tokens, (ZCanonicalToken){Z_CANON_TOKEN_SYMBOL, canon_slice(source, offset, offset + len), line, column, offset, len});
    offset += len;
    column += (int)len;
  }
  canon_push_token(&tokens, (ZCanonicalToken){Z_CANON_TOKEN_EOF, z_strdup(""), line, column, offset, 0});
  return tokens;
}

static const ZCanonicalToken *canon_peek(CanonParser *parser) {
  return parser->pos < parser->tokens->len ? &parser->tokens->items[parser->pos] : NULL;
}

static void canon_skip_newlines(CanonParser *parser) {
  while (canon_peek(parser) && (canon_peek(parser)->kind == Z_CANON_TOKEN_NEWLINE || canon_peek(parser)->kind == Z_CANON_TOKEN_COMMENT)) {
    if (canon_peek(parser)->kind == Z_CANON_TOKEN_COMMENT && parser->facts) parser->facts->comment_count++;
    parser->pos++;
  }
}

static size_t canon_skip_newline_pos(const CanonParser *parser, size_t pos) {
  while (pos < parser->tokens->len && (parser->tokens->items[pos].kind == Z_CANON_TOKEN_NEWLINE || parser->tokens->items[pos].kind == Z_CANON_TOKEN_COMMENT)) pos++;
  return pos;
}

static bool canon_accept_symbol(CanonParser *parser, const char *text) { if (!canon_is_symbol_text(canon_peek(parser), text)) return false; parser->pos++; return true; }

static bool canon_accept_word(CanonParser *parser, const char *text) { if (!canon_is_word_text(canon_peek(parser), text)) return false; parser->pos++; return true; }

static bool canon_expect_symbol(CanonParser *parser, const char *text, const char *message) {
  if (canon_accept_symbol(parser, text)) return true;
  return canon_fail(parser->diag, canon_peek(parser), message, text, canon_peek(parser) ? canon_peek(parser)->text : "end of file");
}

static bool canon_expect_word_token(CanonParser *parser, const char *message) {
  const ZCanonicalToken *token = canon_peek(parser);
  if (token && token->kind == Z_CANON_TOKEN_WORD) {
    if (canon_is_reserved_word(token->text)) return canon_fail(parser->diag, token, "reserved word cannot be used as an identifier", "identifier", token->text);
    parser->pos++;
    return true;
  }
  return canon_fail(parser->diag, token, message, "identifier", token ? token->text : "end of file");
}

static char canon_delimiter_char(const ZCanonicalToken *token, bool balance_angles, bool opening) {
  if (!token || token->kind != Z_CANON_TOKEN_SYMBOL || !token->text || token->text[1]) return 0;
  char ch = token->text[0];
  if (opening) {
    if (ch == '(') return ')';
    if (ch == '[') return ']';
    if (ch == '{') return '}';
    if (balance_angles && ch == '<') return '>';
  } else if (ch == ')' || ch == ']' || ch == '}' || (balance_angles && ch == '>')) return ch;
  return 0;
}

static bool canon_parse_until(CanonParser *parser, const char *stop_a, const char *stop_b, bool allow_empty, bool balance_angles) {
  size_t start = parser->pos;
  char delimiter_stack[256];
  size_t delimiter_depth = 0;
  while (canon_peek(parser) && canon_peek(parser)->kind != Z_CANON_TOKEN_EOF) {
    const ZCanonicalToken *token = canon_peek(parser);
    if (delimiter_depth == 0) {
      if (token->kind == Z_CANON_TOKEN_COMMENT) break;
      if ((stop_a && canon_is_symbol_text(token, stop_a)) || (stop_b && canon_is_symbol_text(token, stop_b))) break;
      if ((stop_a && canon_is_word_text(token, stop_a)) || (stop_b && canon_is_word_text(token, stop_b))) break;
      if (token->kind == Z_CANON_TOKEN_NEWLINE) break;
    }
    char close = canon_delimiter_char(token, balance_angles, false);
    if (close) {
      if (delimiter_depth == 0) break;
      if (close != delimiter_stack[--delimiter_depth]) return canon_fail(parser->diag, token, "mismatched expression delimiter", "matching delimiter", token->text);
      parser->pos++;
      continue;
    }
    char expected_close = canon_delimiter_char(token, balance_angles, true);
    if (expected_close) {
      if (delimiter_depth >= sizeof(delimiter_stack) / sizeof(delimiter_stack[0])) {
        return canon_fail(parser->diag, token, "expression nesting is too deep", "shallower delimiters", token->text);
      }
      delimiter_stack[delimiter_depth++] = expected_close;
    }
    parser->pos++;
  }
  if (!allow_empty && parser->pos == start) return canon_fail(parser->diag, canon_peek(parser), "expected expression", "expression", "empty");
  if (delimiter_depth != 0) return canon_fail(parser->diag, canon_peek(parser), "unbalanced expression delimiters", "balanced delimiters", "unbalanced");
  return true;
}

static bool canon_expr_operand_token(const ZCanonicalToken *token) {
  return token && (token->kind == Z_CANON_TOKEN_WORD || token->kind == Z_CANON_TOKEN_STRING || token->kind == Z_CANON_TOKEN_CHAR || token->kind == Z_CANON_TOKEN_NUMBER);
}

static bool canon_expr_reserved_value(const ZCanonicalToken *token) {
  return token && token->kind == Z_CANON_TOKEN_WORD && (canon_text_eq(token->text, "true") || canon_text_eq(token->text, "false") || canon_text_eq(token->text, "null"));
}

static bool canon_expr_keyword_operator(const ZCanonicalToken *token) {
  return token && token->kind == Z_CANON_TOKEN_WORD && (canon_text_eq(token->text, "as") || canon_text_eq(token->text, "rescue") || canon_text_eq(token->text, "in"));
}

static bool canon_expr_open_symbol(const ZCanonicalToken *token) {
  return canon_is_symbol_text(token, "(") || canon_is_symbol_text(token, "[") || canon_is_symbol_text(token, "{");
}

static bool canon_expr_close_symbol(const ZCanonicalToken *token) {
  return canon_is_symbol_text(token, ")") || canon_is_symbol_text(token, "]") || canon_is_symbol_text(token, "}");
}

static bool canon_expr_binary_symbol(const ZCanonicalToken *token) {
  return canon_is_symbol_text(token, "+") || canon_is_symbol_text(token, "-") || canon_is_symbol_text(token, "*") || canon_is_symbol_text(token, "/") ||
    canon_is_symbol_text(token, "%") || canon_is_symbol_text(token, "==") || canon_is_symbol_text(token, "!=") || canon_is_symbol_text(token, "<") ||
    canon_is_symbol_text(token, "<=") || canon_is_symbol_text(token, ">") || canon_is_symbol_text(token, ">=") || canon_is_symbol_text(token, "&&") ||
    canon_is_symbol_text(token, "||") || canon_is_symbol_text(token, "=>");
}

static bool canon_expr_prefix_symbol(const ZCanonicalToken *token) {
  return canon_is_symbol_text(token, "&") || canon_is_symbol_text(token, "*") || canon_is_symbol_text(token, "!") || canon_is_symbol_text(token, "-") || canon_is_symbol_text(token, "+");
}

static bool canon_validate_type_span(CanonParser *parser, size_t start, size_t end);

static bool canon_generic_call_span(CanonParser *parser, size_t expr_start, size_t expr_end, size_t open_index, size_t *close_index) {
  if (open_index == expr_start || open_index >= expr_end) return false;
  const ZCanonicalToken *previous = &parser->tokens->items[open_index - 1];
  const ZCanonicalToken *open = &parser->tokens->items[open_index];
  if (previous->kind != Z_CANON_TOKEN_WORD || !canon_is_symbol_text(open, "<") || !canon_tokens_connected(previous, open)) return false;

  int angle = 1;
  for (size_t index = open_index + 1; index < expr_end; index++) {
    const ZCanonicalToken *token = &parser->tokens->items[index];
    if (canon_is_symbol_text(token, "<")) {
      angle++;
      continue;
    }
    if (!canon_is_symbol_text(token, ">")) continue;
    angle--;
    if (angle != 0) continue;

    if (index + 1 >= expr_end) return false;
    const ZCanonicalToken *next = &parser->tokens->items[index + 1];
    if (!(canon_is_symbol_text(next, "(") || canon_is_symbol_text(next, "{"))) return false;
    if (canon_is_symbol_text(next, "(") && !canon_tokens_connected(token, next)) return false;
    *close_index = index;
    return true;
  }
  return false;
}

static bool canon_empty_postfix_index_close(const ZCanonicalToken *token, const ZCanonicalToken *previous, size_t bracket_depth, const bool bracket_literal[], bool expect_operand) {
  return canon_is_symbol_text(token, "]") && bracket_depth > 0 && !bracket_literal[bracket_depth] && expect_operand && previous && canon_is_symbol_text(previous, "[");
}

typedef enum {
  CANON_EXPR_DELIM_GROUP,
  CANON_EXPR_DELIM_CALL,
  CANON_EXPR_DELIM_ARRAY,
  CANON_EXPR_DELIM_INDEX,
  CANON_EXPR_DELIM_OBJECT
} CanonExprDelimiterKind;

static void canon_close_expr_delimiter(const ZCanonicalToken *token, int *delimiter_depth, int *brace_depth, size_t *bracket_depth, bool bracket_literal[], size_t bracket_repeat_count[], size_t bracket_comma_count[]) {
  if (*delimiter_depth > 0) (*delimiter_depth)--;
  if (canon_is_symbol_text(token, "}") && *brace_depth > 0) (*brace_depth)--;
  if (canon_is_symbol_text(token, "]")) {
    bracket_repeat_count[*bracket_depth] = 0;
    bracket_comma_count[*bracket_depth] = 0;
    bracket_literal[*bracket_depth] = false;
    if (*bracket_depth > 0) (*bracket_depth)--;
  }
}

static bool canon_validate_member_access(CanonParser *parser, const ZCanonicalToken *previous, const ZCanonicalToken *token, const ZCanonicalToken *next, bool expect_operand) {
  if (expect_operand) return canon_fail(parser->diag, token, "expected expression before member access", "expression", ".");
  if (!previous || !canon_tokens_connected(previous, token)) return canon_fail(parser->diag, token, "member access must follow an expression directly", "adjacent '.'", ".");
  if (!next || next->kind != Z_CANON_TOKEN_WORD || canon_is_reserved_word(next->text)) return canon_fail(parser->diag, next ? next : token, "expected field name after member access", "field name", next ? next->text : "end of expression");
  if (!canon_tokens_connected(token, next)) return canon_fail(parser->diag, next, "field name must follow member access directly", "adjacent field name", next->text);
  return true;
}

static bool canon_empty_call_args_close(CanonParser *parser, size_t start, size_t index) {
  if (index < start + 2) return false;
  const ZCanonicalToken *open = &parser->tokens->items[index - 1];
  const ZCanonicalToken *callee = &parser->tokens->items[index - 2];
  if (!canon_is_symbol_text(open, "(") || !canon_tokens_connected(callee, open)) return false;
  return callee->kind == Z_CANON_TOKEN_WORD || canon_is_symbol_text(callee, ")") || canon_is_symbol_text(callee, "]") || canon_is_symbol_text(callee, "}") || canon_is_symbol_text(callee, ">");
}

static bool canon_validate_object_field_name(CanonParser *parser, size_t field_start, size_t colon_index) {
  if (field_start + 1 != colon_index) return canon_fail(parser->diag, &parser->tokens->items[colon_index], "expected object field name before ':'", "field name", ":");
  const ZCanonicalToken *field = &parser->tokens->items[field_start];
  if (field->kind != Z_CANON_TOKEN_WORD || canon_is_reserved_word(field->text)) {
    return canon_fail(parser->diag, field, "object field name must be an identifier", "field name", field->text);
  }
  return true;
}

static CanonExprDelimiterKind canon_expr_delimiter_kind(const ZCanonicalToken *token, bool expect_operand) {
  if (canon_is_symbol_text(token, "(")) return expect_operand ? CANON_EXPR_DELIM_GROUP : CANON_EXPR_DELIM_CALL;
  if (canon_is_symbol_text(token, "[")) return expect_operand ? CANON_EXPR_DELIM_ARRAY : CANON_EXPR_DELIM_INDEX;
  return CANON_EXPR_DELIM_OBJECT;
}

static void canon_note_expr_open_delimiter(const ZCanonicalToken *token, int *delimiter_depth, int *brace_depth, CanonExprDelimiterKind delimiter_kind[], size_t object_field_start[], bool object_field_has_colon[], size_t delimiter_cap, size_t field_start, bool expect_operand) {
  (*delimiter_depth)++;
  if ((size_t)*delimiter_depth < delimiter_cap) {
    delimiter_kind[*delimiter_depth] = canon_expr_delimiter_kind(token, expect_operand);
    object_field_start[*delimiter_depth] = field_start;
    object_field_has_colon[*delimiter_depth] = false;
  }
  if (canon_is_symbol_text(token, "{")) (*brace_depth)++;
}

static bool canon_note_expr_comma(CanonParser *parser, const ZCanonicalToken *token, int delimiter_depth, CanonExprDelimiterKind delimiter_kind[], size_t object_field_start[], bool object_field_has_colon[], size_t delimiter_cap, size_t next_field_start, size_t bracket_depth, const bool bracket_literal[], size_t bracket_repeat_count[], size_t bracket_comma_count[]) {
  if (delimiter_depth == 0) return canon_fail(parser->diag, token, "unexpected comma in expression", "expression delimiter", ",");
  if ((size_t)delimiter_depth >= delimiter_cap) return canon_fail(parser->diag, token, "expression nesting is too deep", "shallower delimiters", ",");
  CanonExprDelimiterKind kind = delimiter_kind[delimiter_depth];
  if (kind != CANON_EXPR_DELIM_CALL && kind != CANON_EXPR_DELIM_ARRAY && kind != CANON_EXPR_DELIM_OBJECT) {
    return canon_fail(parser->diag, token, "unexpected comma in expression", "call arguments, array literal, or object field", ",");
  }
  if (kind == CANON_EXPR_DELIM_ARRAY) {
    if (bracket_depth == 0 || !bracket_literal[bracket_depth]) return canon_fail(parser->diag, token, "unexpected comma in expression", "array literal", ",");
    if (bracket_repeat_count[bracket_depth] > 0) return canon_fail(parser->diag, token, "array repeat literal cannot contain comma elements", "one ';'", ",");
    bracket_comma_count[bracket_depth]++;
  }
  if (kind == CANON_EXPR_DELIM_OBJECT) {
    object_field_start[delimiter_depth] = next_field_start;
    object_field_has_colon[delimiter_depth] = false;
  }
  return true;
}

static bool canon_validate_object_field_complete(CanonParser *parser, const ZCanonicalToken *token, int delimiter_depth, CanonExprDelimiterKind delimiter_kind[], size_t object_field_start[], const bool object_field_has_colon[], size_t delimiter_cap, size_t boundary_index) {
  if (delimiter_depth <= 0 || (size_t)delimiter_depth >= delimiter_cap || delimiter_kind[delimiter_depth] != CANON_EXPR_DELIM_OBJECT) return true;
  if (object_field_start[delimiter_depth] >= boundary_index) return true;
  if (object_field_has_colon[delimiter_depth]) return true;
  const ZCanonicalToken *field = &parser->tokens->items[object_field_start[delimiter_depth]];
  return canon_fail(parser->diag, field, "expected ':' after object field name", ":", token ? token->text : "end of expression");
}

static bool canon_validate_object_colon(CanonParser *parser, const ZCanonicalToken *token, int delimiter_depth, int brace_depth, CanonExprDelimiterKind delimiter_kind[], size_t object_field_start[], bool object_field_has_colon[], size_t delimiter_cap, size_t colon_index) {
  if (brace_depth == 0) return canon_fail(parser->diag, token, "unexpected ':' in expression", "object field", ":");
  if ((size_t)delimiter_depth >= delimiter_cap || delimiter_kind[delimiter_depth] != CANON_EXPR_DELIM_OBJECT) return canon_fail(parser->diag, token, "unexpected ':' in expression", "object field", ":");
  if (object_field_has_colon[delimiter_depth]) return canon_fail(parser->diag, token, "object field already has a value separator", "one ':'", ":");
  if (!canon_validate_object_field_name(parser, object_field_start[delimiter_depth], colon_index)) return false;
  object_field_has_colon[delimiter_depth] = true;
  return true;
}

static bool canon_validate_expr_shape(CanonParser *parser, size_t start, size_t end) {
  if (start == end) return true;
  bool expect_operand = true;
  bool saw_operand = false;
  int delimiter_depth = 0;
  int brace_depth = 0;
  CanonExprDelimiterKind delimiter_kind[256] = {0};
  size_t object_field_start[256] = {0};
  bool object_field_has_colon[256] = {0};
  bool bracket_literal[128] = {0};
  size_t bracket_depth = 0, bracket_repeat_count[128] = {0}, bracket_comma_count[128] = {0};
  for (size_t i = start; i < end; i++) {
    const ZCanonicalToken *token = &parser->tokens->items[i];
    const ZCanonicalToken *previous = i > start ? &parser->tokens->items[i - 1] : NULL;
    const ZCanonicalToken *next = i + 1 < end ? &parser->tokens->items[i + 1] : NULL;

    if (token->kind == Z_CANON_TOKEN_WORD && canon_text_eq(token->text, "err") && previous && next && !canon_is_symbol_text(previous, ".") && !canon_is_symbol_text(next, ".")) {
      if (!expect_operand) {
        expect_operand = true;
        continue;
      }
      if (next->kind == Z_CANON_TOKEN_WORD || canon_is_symbol_text(next, "{")) {
        expect_operand = false;
        saw_operand = true;
        continue;
      }
      return canon_fail(parser->diag, token, "expected expression before keyword", "expression", token->text);
    }

    if (canon_expr_keyword_operator(token)) {
      if (!expect_operand) {
        expect_operand = true;
        continue;
      }
      return canon_fail(parser->diag, token, "expected expression before keyword", "expression", token->text);
    }

    if (token->kind != Z_CANON_TOKEN_SYMBOL) {
      if (!canon_expr_operand_token(token)) return canon_fail(parser->diag, token, "unexpected token in expression", "expression", token->text);
      if (canon_text_eq(token->text, "mut") && previous && canon_is_symbol_text(previous, "&") && expect_operand) continue;
      if (token->kind == Z_CANON_TOKEN_WORD && canon_is_reserved_word(token->text) && !canon_expr_reserved_value(token)) return canon_fail(parser->diag, token, "reserved word cannot be used as an expression", "expression", token->text);
      if (!expect_operand) return canon_fail(parser->diag, token, "expected operator between expressions", "operator", token->text);
      expect_operand = false;
      saw_operand = true;
      continue;
    }

    if (canon_expr_open_symbol(token)) {
      if (canon_is_symbol_text(token, "(") && !expect_operand && previous && !canon_tokens_connected(previous, token)) return canon_fail(parser->diag, token, "call parentheses must follow the callee directly", "adjacent '('", "(");
      if (canon_is_symbol_text(token, "[") && !expect_operand && (!previous || !canon_tokens_connected(previous, token))) return canon_fail(parser->diag, token, "index brackets must follow the indexed expression directly", "adjacent '['", "[");
      if (canon_is_symbol_text(token, "{") && expect_operand) return canon_fail(parser->diag, token, "expected expression before object literal", "expression", "{");
      canon_note_expr_open_delimiter(token, &delimiter_depth, &brace_depth, delimiter_kind, object_field_start, object_field_has_colon, sizeof(delimiter_kind) / sizeof(delimiter_kind[0]), i + 1, expect_operand);
      if (canon_is_symbol_text(token, "[")) {
        if (bracket_depth + 1 < sizeof(bracket_literal) / sizeof(bracket_literal[0])) bracket_depth++;
        bracket_literal[bracket_depth] = expect_operand; bracket_repeat_count[bracket_depth] = 0; bracket_comma_count[bracket_depth] = 0;
      }
      expect_operand = true;
      continue;
    }
    if (canon_expr_close_symbol(token)) {
      if (canon_empty_postfix_index_close(token, previous, bracket_depth, bracket_literal, expect_operand)) {
        return canon_fail(parser->diag, token, "expected index expression before ']'", "index expression", "]");
      }
      if (expect_operand && previous && canon_is_symbol_text(previous, "(") && !canon_empty_call_args_close(parser, start, i)) return canon_fail(parser->diag, token, "expected expression before delimiter", "expression", token->text);
      if (expect_operand && !(previous && (canon_expr_open_symbol(previous) || canon_is_symbol_text(previous, "..")))) {
        return canon_fail(parser->diag, token, "expected expression before delimiter", "expression", token->text);
      }
      if (canon_is_symbol_text(token, "}") && !canon_validate_object_field_complete(parser, token, delimiter_depth, delimiter_kind, object_field_start, object_field_has_colon, sizeof(delimiter_kind) / sizeof(delimiter_kind[0]), i)) return false;
      canon_close_expr_delimiter(token, &delimiter_depth, &brace_depth, &bracket_depth, bracket_literal, bracket_repeat_count, bracket_comma_count);
      expect_operand = false; saw_operand = true; continue;
    }
    if (canon_is_symbol_text(token, ",")) {
      if (expect_operand) return canon_fail(parser->diag, token, "expected expression before comma", "expression", ",");
      if (!canon_validate_object_field_complete(parser, token, delimiter_depth, delimiter_kind, object_field_start, object_field_has_colon, sizeof(delimiter_kind) / sizeof(delimiter_kind[0]), i)) return false;
      if (!canon_note_expr_comma(parser, token, delimiter_depth, delimiter_kind, object_field_start, object_field_has_colon, sizeof(delimiter_kind) / sizeof(delimiter_kind[0]), i + 1, bracket_depth, bracket_literal, bracket_repeat_count, bracket_comma_count)) return false;
      expect_operand = true; continue;
    }
    if (canon_is_symbol_text(token, ":")) {
      if (expect_operand) return canon_fail(parser->diag, token, "expected field name before ':'", "field name", ":");
      if (!canon_validate_object_colon(parser, token, delimiter_depth, brace_depth, delimiter_kind, object_field_start, object_field_has_colon, sizeof(delimiter_kind) / sizeof(delimiter_kind[0]), i)) return false;
      expect_operand = true;
      continue;
    }
    if (canon_is_symbol_text(token, ";")) {
      if (expect_operand) return canon_fail(parser->diag, token, "expected expression before array repeat separator", "expression", ";");
      if (bracket_depth == 0 || !bracket_literal[bracket_depth]) return canon_fail(parser->diag, token, "unexpected ';' in expression", "array repeat literal", ";");
      if (bracket_comma_count[bracket_depth] > 0) return canon_fail(parser->diag, token, "array repeat literal cannot contain comma elements", "one ';'", ";");
      if (++bracket_repeat_count[bracket_depth] > 1) return canon_fail(parser->diag, token, "array repeat literals use one separator", "one ';'", ";");
      expect_operand = true;
      continue;
    }
    if (canon_is_symbol_text(token, ".")) {
      if (!canon_validate_member_access(parser, previous, token, next, expect_operand)) return false;
      expect_operand = true;
      continue;
    }
    if (canon_is_symbol_text(token, "..")) {
      expect_operand = true;
      continue;
    }
    if (canon_is_symbol_text(token, "<")) {
      size_t generic_close = 0;
      if (canon_generic_call_span(parser, start, end, i, &generic_close)) {
        if (!canon_validate_type_span(parser, i + 1, generic_close)) return false;
        i = generic_close;
        continue;
      }
    }
    if (expect_operand && canon_expr_prefix_symbol(token)) continue;
    if (canon_expr_binary_symbol(token)) {
      if (expect_operand) return canon_fail(parser->diag, token, "expected expression before operator", "expression", token->text);
      expect_operand = true;
      continue;
    }
    return canon_fail(parser->diag, token, "unexpected token in expression", "expression", token->text);
  }
  if (!saw_operand) return canon_fail(parser->diag, end > start ? &parser->tokens->items[end - 1] : canon_peek(parser), "expected expression", "expression", "empty");
  if (expect_operand) return canon_fail(parser->diag, &parser->tokens->items[end - 1], "incomplete expression", "expression", parser->tokens->items[end - 1].text);
  return true;
}

static bool canon_validate_expr(CanonParser *parser, size_t start, size_t end) {
  if (!canon_validate_expr_shape(parser, start, end)) return false;
  size_t compare_counts[128] = {0};
  size_t compare_depth = 0;
  for (size_t i = start; i < end; i++) {
    const ZCanonicalToken *token = &parser->tokens->items[i];
    if (canon_is_symbol_text(token, "(") || canon_is_symbol_text(token, "[") || canon_is_symbol_text(token, "{")) {
      if (compare_depth + 1 < sizeof(compare_counts) / sizeof(compare_counts[0])) compare_depth++;
      compare_counts[compare_depth] = 0;
      continue;
    }
    if (canon_is_symbol_text(token, ")") || canon_is_symbol_text(token, "]") || canon_is_symbol_text(token, "}")) {
      compare_counts[compare_depth] = 0;
      if (compare_depth > 0) compare_depth--;
      continue;
    }
    if (canon_is_symbol_text(token, "&&") || canon_is_symbol_text(token, "||") || canon_is_symbol_text(token, ",")) {
      compare_counts[compare_depth] = 0;
      continue;
    }
    if (canon_is_symbol_text(token, "<")) {
      size_t generic_close = 0;
      if (canon_generic_call_span(parser, start, end, i, &generic_close)) {
        i = generic_close;
        continue;
      }
      if (++compare_counts[compare_depth] > 1) {
        return canon_fail(parser->diag, token, "chained comparisons are not canonical text", "separate comparisons", "chained comparison");
      }
      continue;
    }
    if ((canon_is_symbol_text(token, "==") || canon_is_symbol_text(token, "!=") || canon_is_symbol_text(token, "<=") || canon_is_symbol_text(token, ">") || canon_is_symbol_text(token, ">=")) && ++compare_counts[compare_depth] > 1) {
      return canon_fail(parser->diag, token, "chained comparisons are not canonical text", "separate comparisons", "chained comparison");
    }
    if (i + 1 < end && token->kind == Z_CANON_TOKEN_WORD) {
      const ZCanonicalToken *previous = i > start ? &parser->tokens->items[i - 1] : NULL;
      const ZCanonicalToken *next = &parser->tokens->items[i + 1];
      if (canon_text_eq(token->text, "as") || canon_text_eq(token->text, "rescue") || canon_text_eq(token->text, "err") || canon_text_eq(token->text, "in")) continue;
      if (next->kind == Z_CANON_TOKEN_WORD && (canon_text_eq(next->text, "as") || canon_text_eq(next->text, "rescue") || canon_text_eq(next->text, "err") || canon_text_eq(next->text, "in"))) continue;
      if (canon_text_eq(token->text, "mut") && previous && canon_is_symbol_text(previous, "&")) continue;
      if (next->kind == Z_CANON_TOKEN_STRING || next->kind == Z_CANON_TOKEN_NUMBER || next->kind == Z_CANON_TOKEN_WORD) {
        return canon_fail(parser->diag, next, "space calls are not canonical text", "call parentheses", next->text);
      }
    }
  }
  return true;
}

static bool canon_parse_expr_line(CanonParser *parser, bool allow_empty) {
  size_t start = parser->pos;
  if (!canon_parse_until(parser, NULL, NULL, allow_empty, false)) return false;
  return canon_validate_expr(parser, start, parser->pos);
}

static bool canon_type_static_value_word(const ZCanonicalToken *token) {
  return token && token->kind == Z_CANON_TOKEN_WORD && (canon_text_eq(token->text, "true") || canon_text_eq(token->text, "false") || canon_text_eq(token->text, "null"));
}

static bool canon_validate_type_span(CanonParser *parser, size_t start, size_t end) {
  bool bracket_prefix[64] = {0};
  size_t bracket_depth = 0;
  size_t angle_depth = 0;
  bool expect_atom = true;
  for (size_t i = start; i < end; i++) {
    const ZCanonicalToken *token = &parser->tokens->items[i];
    bool static_value_context = angle_depth > 0 || (bracket_depth > 0 && bracket_prefix[bracket_depth - 1]);
    if (token->kind == Z_CANON_TOKEN_WORD) {
      if (canon_is_reserved_word(token->text) && !(static_value_context && canon_type_static_value_word(token))) return canon_fail(parser->diag, token, "reserved word cannot be used as a type", "type", token->text);
      if (!expect_atom) return canon_fail(parser->diag, token, "missing separator in type", "comma or delimiter", token->text);
      expect_atom = false;
      continue;
    }
    if (token->kind == Z_CANON_TOKEN_NUMBER) {
      if (!static_value_context) return canon_fail(parser->diag, token, "literal cannot be used as a type", "type", token->text);
      if (!expect_atom) return canon_fail(parser->diag, token, "missing separator in type", "comma or delimiter", token->text);
      expect_atom = false;
      continue;
    }
    if (token->kind == Z_CANON_TOKEN_STRING || token->kind == Z_CANON_TOKEN_CHAR) return canon_fail(parser->diag, token, "literal cannot be used as a type", "type", token->text);
    if (canon_is_symbol_text(token, "[")) {
      if (bracket_depth < sizeof(bracket_prefix) / sizeof(bracket_prefix[0])) bracket_prefix[bracket_depth++] = expect_atom;
      expect_atom = true;
      continue;
    }
    if (canon_is_symbol_text(token, "<") || canon_is_symbol_text(token, "(")) {
      if (expect_atom) return canon_fail(parser->diag, token, "unexpected type delimiter", "type", token->text);
      if (canon_is_symbol_text(token, "<")) angle_depth++;
      expect_atom = true;
      continue;
    }
    if (canon_is_symbol_text(token, "]")) {
      if (expect_atom) return canon_fail(parser->diag, token, "expected type before delimiter", "type", token->text);
      bool prefix = bracket_depth > 0 && bracket_prefix[--bracket_depth];
      expect_atom = prefix;
      continue;
    }
    if (canon_is_symbol_text(token, ">") || canon_is_symbol_text(token, ")")) {
      if (expect_atom) return canon_fail(parser->diag, token, "expected type before delimiter", "type", token->text);
      if (canon_is_symbol_text(token, ">") && angle_depth > 0) angle_depth--;
      expect_atom = false;
      continue;
    }
    if (canon_is_symbol_text(token, ",")) {
      if (bracket_depth > 0 && bracket_prefix[bracket_depth - 1]) return canon_fail(parser->diag, token, "array type length cannot contain comma", "single array length", ",");
      if (expect_atom) return canon_fail(parser->diag, token, "expected type before comma", "type", ",");
      expect_atom = true;
      continue;
    }
    if (canon_is_symbol_text(token, ".") || canon_is_symbol_text(token, "->")) {
      if (expect_atom) return canon_fail(parser->diag, token, "expected type before separator", "type", token->text);
      expect_atom = true;
      continue;
    }
    if (canon_is_symbol_text(token, "&") || canon_is_symbol_text(token, "*") || canon_is_symbol_text(token, "?")) {
      if (!expect_atom) return canon_fail(parser->diag, token, "unexpected type prefix", "type separator", token->text);
      continue;
    }
    return canon_fail(parser->diag, token, "unexpected token in type", "type", token->text);
  }
  if (expect_atom) return canon_fail(parser->diag, end > start ? &parser->tokens->items[end - 1] : canon_peek(parser), "incomplete type", "type", "delimiter");
  return true;
}

static bool canon_parse_type_until(CanonParser *parser, const char *stop_a, const char *stop_b) {
  size_t start = parser->pos;
  if (!canon_parse_until(parser, stop_a, stop_b, false, true)) return false;
  return canon_validate_type_span(parser, start, parser->pos);
}

static bool canon_parse_type_params(CanonParser *parser) {
  if (!canon_accept_symbol(parser, "<")) return true;
  if (canon_accept_symbol(parser, ">")) return canon_fail(parser->diag, canon_peek(parser), "expected type parameter", "type parameter", ">");
  while (canon_peek(parser) && canon_peek(parser)->kind != Z_CANON_TOKEN_EOF) {
    if (canon_accept_word(parser, "static")) {
      if (!canon_expect_word_token(parser, "expected static type parameter name")) return false;
      if (!canon_expect_symbol(parser, ":", "expected ':' after static type parameter")) return false;
      if (!canon_parse_type_until(parser, ",", ">")) return false;
    } else {
      if (!canon_expect_word_token(parser, "expected type parameter name")) return false;
      if (canon_accept_symbol(parser, ":") && !canon_parse_type_until(parser, ",", ">")) return false;
    }
    if (canon_accept_symbol(parser, ">")) return true;
    if (!canon_expect_symbol(parser, ",", "expected comma between type parameters")) return false;
    if (canon_is_symbol_text(canon_peek(parser), ">")) return canon_fail(parser->diag, canon_peek(parser), "expected type parameter after comma", "type parameter", ">");
  }
  return canon_fail(parser->diag, canon_peek(parser), "unterminated type parameter list", ">", "end of file");
}

static bool canon_parse_params(CanonParser *parser) {
  if (!canon_expect_symbol(parser, "(", "expected parameter list")) return false;
  if (canon_accept_symbol(parser, ")")) return true;
  while (true) {
    if (!canon_expect_word_token(parser, "expected parameter name")) return false;
    if (!canon_expect_symbol(parser, ":", "expected ':' after parameter name")) return false;
    if (!canon_parse_type_until(parser, ",", ")")) return false;
    if (canon_accept_symbol(parser, ")")) return true;
    if (!canon_expect_symbol(parser, ",", "expected comma between parameters")) return false;
  }
}

static bool canon_parse_raises(CanonParser *parser) {
  if (!canon_accept_word(parser, "raises")) return true;
  if (!canon_accept_symbol(parser, "[")) {
    if (canon_is_symbol_text(canon_peek(parser), "{")) {
      const ZCanonicalToken *next = parser->pos + 1 < parser->tokens->len ? &parser->tokens->items[parser->pos + 1] : NULL;
      const ZCanonicalToken *after = parser->pos + 2 < parser->tokens->len ? &parser->tokens->items[parser->pos + 2] : NULL;
      if (next && next->kind == Z_CANON_TOKEN_WORD && after && canon_is_symbol_text(after, "}")) {
        return canon_fail(parser->diag, canon_peek(parser), "named error sets use brackets", "raises [Error]", "raises { Error }");
      }
    }
    return true;
  }
  if (!canon_expect_word_token(parser, "expected named error")) return false;
  while (canon_accept_symbol(parser, ",")) {
    if (!canon_expect_word_token(parser, "expected named error after comma")) return false;
  }
  return canon_expect_symbol(parser, "]", "expected closing named error list");
}

static bool canon_parse_block(CanonParser *parser, size_t depth);
static bool canon_parse_statement(CanonParser *parser, size_t depth);
static bool canon_expect_declaration_end(CanonParser *parser, const char *message);

static bool canon_expect_statement_end(CanonParser *parser, const char *message) {
  const ZCanonicalToken *token = canon_peek(parser);
  return (!token || token->kind == Z_CANON_TOKEN_EOF || token->kind == Z_CANON_TOKEN_NEWLINE || token->kind == Z_CANON_TOKEN_COMMENT || canon_is_symbol_text(token, "}")) ? true : canon_fail(parser->diag, token, message, "line end", token->text);
}

static bool canon_validate_match_pattern(CanonParser *parser, size_t start, size_t end) {
  if (end == start + 2) {
    const ZCanonicalToken *tag = &parser->tokens->items[start];
    const ZCanonicalToken *payload = &parser->tokens->items[start + 1];
    if (tag->kind == Z_CANON_TOKEN_WORD && payload->kind == Z_CANON_TOKEN_WORD && !canon_is_reserved_word(tag->text) && !canon_is_reserved_word(payload->text)) return true;
  }
  return canon_validate_expr(parser, start, end);
}

static bool canon_parse_signature(CanonParser *parser, bool body, size_t depth) {
  if (!canon_expect_word_token(parser, "expected function name")) return false;
  while (canon_accept_symbol(parser, ".")) {
    if (!canon_expect_word_token(parser, "expected method name")) return false;
  }
  if (!canon_parse_type_params(parser)) return false;
  if (!canon_parse_params(parser)) return false;
  if (!canon_expect_symbol(parser, "->", "expected return type arrow")) return false;
  if (!canon_parse_type_until(parser, "raises", "{")) return false;
  if (canon_accept_symbol(parser, "!")) return canon_fail(parser->diag, canon_peek(parser), "fallibility uses raises", "raises", "!");
  if (!canon_parse_raises(parser)) return false;
  return body ? canon_parse_block(parser, depth) : true;
}

static bool canon_parse_block(CanonParser *parser, size_t depth) {
  const ZCanonicalToken *open = canon_peek(parser);
  if (!canon_expect_symbol(parser, "{", "expected block")) return false;
  size_t node_index = parser->tree->len;
  canon_push_node(parser->tree, (ZCanonicalNode){Z_CANON_NODE_BLOCK, parser->pos - 1, 1, depth, open->line, open->column});
  if (parser->facts) {
    parser->facts->block_count++;
    if (depth > parser->facts->max_block_depth) parser->facts->max_block_depth = depth;
  }
  canon_skip_newlines(parser);
  while (canon_peek(parser) && !canon_is_symbol_text(canon_peek(parser), "}") && canon_peek(parser)->kind != Z_CANON_TOKEN_EOF) {
    if (!canon_parse_statement(parser, depth + 1)) return false;
    canon_skip_newlines(parser);
  }
  if (!canon_expect_symbol(parser, "}", "expected closing block")) return false;
  parser->tree->items[node_index].token_count = parser->pos - parser->tree->items[node_index].first_token;
  return true;
}

static bool canon_parse_let_or_var(CanonParser *parser) {
  if (!canon_expect_word_token(parser, "expected binding name")) return false;
  if (!canon_expect_symbol(parser, ":", "canonical local bindings require a type annotation")) return false;
  if (!canon_parse_type_until(parser, "=", NULL)) return false;
  if (!canon_expect_symbol(parser, "=", "expected binding initializer")) return false;
  return canon_parse_expr_line(parser, false);
}

static bool canon_parse_if(CanonParser *parser, size_t depth) {
  size_t start = parser->pos;
  if (!canon_parse_until(parser, "{", NULL, false, false)) return false;
  if (!canon_validate_expr(parser, start, parser->pos)) return false;
  if (!canon_parse_block(parser, depth)) return false;
  size_t after_then = parser->pos;
  size_t else_pos = canon_skip_newline_pos(parser, after_then);
  if (!canon_is_word_text(else_pos < parser->tokens->len ? &parser->tokens->items[else_pos] : NULL, "else")) return true;
  parser->pos = after_then;
  canon_skip_newlines(parser);
  if (!canon_accept_word(parser, "else")) return true;
  if (canon_accept_word(parser, "if")) return canon_parse_if(parser, depth);
  return canon_parse_block(parser, depth);
}

static bool canon_finish_statement(CanonParser *parser, size_t node_index, bool ok) {
  if (!ok) return false;
  parser->tree->items[node_index].token_count = parser->pos - parser->tree->items[node_index].first_token;
  return canon_expect_statement_end(parser, "unexpected tokens after statement");
}

static bool canon_parse_match(CanonParser *parser, size_t depth) {
  size_t expr_start = parser->pos;
  if (!canon_parse_until(parser, "{", NULL, false, false)) return false;
  if (!canon_validate_expr(parser, expr_start, parser->pos)) return false;
  const ZCanonicalToken *open = canon_peek(parser);
  if (!canon_expect_symbol(parser, "{", "expected match body")) return false;
  size_t node_index = parser->tree->len;
  canon_push_node(parser->tree, (ZCanonicalNode){Z_CANON_NODE_BLOCK, parser->pos - 1, 1, depth, open->line, open->column});
  if (parser->facts) {
    parser->facts->block_count++;
    if (depth > parser->facts->max_block_depth) parser->facts->max_block_depth = depth;
  }
  canon_skip_newlines(parser);
  while (canon_peek(parser) && !canon_is_symbol_text(canon_peek(parser), "}") && canon_peek(parser)->kind != Z_CANON_TOKEN_EOF) {
    size_t pattern_start = parser->pos;
    if (!canon_parse_until(parser, "{", NULL, false, false)) return false;
    if (!canon_validate_match_pattern(parser, pattern_start, parser->pos)) return false;
    if (!canon_parse_block(parser, depth + 1)) return false;
    canon_skip_newlines(parser);
  }
  if (!canon_expect_symbol(parser, "}", "expected closing match body")) return false;
  parser->tree->items[node_index].token_count = parser->pos - parser->tree->items[node_index].first_token;
  return true;
}

static bool canon_parse_statement(CanonParser *parser, size_t depth) {
  canon_skip_newlines(parser);
  const ZCanonicalToken *start = canon_peek(parser);
  if (!start || start->kind == Z_CANON_TOKEN_EOF || canon_is_symbol_text(start, "}")) return true;
  size_t node_index = parser->tree->len;
  canon_push_node(parser->tree, (ZCanonicalNode){Z_CANON_NODE_STMT, parser->pos, 0, depth, start->line, start->column});
  if (parser->facts) parser->facts->statement_count++;
  if (canon_accept_word(parser, "mut")) return canon_fail(parser->diag, start, "mutable locals use var", "var", "mut");
  if (canon_accept_word(parser, "let") || canon_accept_word(parser, "var")) return canon_finish_statement(parser, node_index, canon_parse_let_or_var(parser));
  if (canon_accept_word(parser, "return")) return canon_finish_statement(parser, node_index, canon_parse_expr_line(parser, true));
  if (canon_accept_word(parser, "check") || canon_accept_word(parser, "expect")) return canon_finish_statement(parser, node_index, canon_parse_expr_line(parser, false));
  if (canon_accept_word(parser, "raise")) return canon_finish_statement(parser, node_index, canon_expect_word_token(parser, "expected error name"));
  if (canon_accept_word(parser, "if")) return canon_finish_statement(parser, node_index, canon_parse_if(parser, depth));
  if (canon_accept_word(parser, "while")) {
    size_t expr_start = parser->pos;
    if (!canon_parse_until(parser, "{", NULL, false, false)) return false;
    if (!canon_validate_expr(parser, expr_start, parser->pos)) return false;
    return canon_finish_statement(parser, node_index, canon_parse_block(parser, depth));
  }
  if (canon_accept_word(parser, "for")) {
    if (!canon_expect_word_token(parser, "expected loop binding")) return false;
    if (!canon_accept_word(parser, "in")) return canon_fail(parser->diag, canon_peek(parser), "expected for range", "in", canon_peek(parser)->text);
    size_t expr_start = parser->pos;
    if (!canon_parse_until(parser, "{", NULL, false, false)) return false;
    if (!canon_validate_expr(parser, expr_start, parser->pos)) return false;
    return canon_finish_statement(parser, node_index, canon_parse_block(parser, depth));
  }
  if (canon_accept_word(parser, "match")) return canon_finish_statement(parser, node_index, canon_parse_match(parser, depth));
  if (canon_accept_word(parser, "break") || canon_accept_word(parser, "continue")) return canon_finish_statement(parser, node_index, true);
  if (canon_accept_word(parser, "defer")) {
    if (canon_is_symbol_text(canon_peek(parser), "{")) return canon_finish_statement(parser, node_index, canon_parse_block(parser, depth));
    return canon_finish_statement(parser, node_index, canon_parse_expr_line(parser, false));
  }
  return canon_finish_statement(parser, node_index, canon_parse_expr_line(parser, false));
}

static bool canon_parse_field_list(CanonParser *parser, bool typed_fields) {
  if (!canon_expect_symbol(parser, "{", "expected declaration body")) return false;
  canon_skip_newlines(parser);
  if (canon_accept_symbol(parser, "}")) return true;
  while (true) {
    canon_skip_newlines(parser);
    if (!canon_expect_word_token(parser, "expected field or variant name")) return false;
    if (typed_fields) {
      if (!canon_expect_symbol(parser, ":", "expected ':' after field name")) return false;
      if (!canon_parse_type_until(parser, ",", "}")) return false;
    }
    if (!canon_expect_symbol(parser, ",", "expected trailing comma in declaration list")) return false;
    canon_skip_newlines(parser);
    if (canon_accept_symbol(parser, "}")) return true;
  }
}

static bool canon_parse_interface(CanonParser *parser) {
  if (!canon_expect_word_token(parser, "expected interface name")) return false;
  if (!canon_parse_type_params(parser)) return false;
  if (!canon_expect_symbol(parser, "{", "expected interface body")) return false;
  canon_skip_newlines(parser);
  while (!canon_is_symbol_text(canon_peek(parser), "}") && canon_peek(parser)->kind != Z_CANON_TOKEN_EOF) {
    if (!canon_accept_word(parser, "fn")) return canon_fail(parser->diag, canon_peek(parser), "expected interface method", "fn", canon_peek(parser)->text);
    if (!canon_parse_signature(parser, false, 1)) return false;
    if (!canon_expect_declaration_end(parser, "unexpected tokens after interface method")) return false;
    canon_skip_newlines(parser);
  }
  return canon_expect_symbol(parser, "}", "expected interface close");
}

static bool canon_expect_declaration_end(CanonParser *parser, const char *message) {
  const ZCanonicalToken *token = canon_peek(parser);
  return (!token || token->kind == Z_CANON_TOKEN_EOF || token->kind == Z_CANON_TOKEN_NEWLINE || token->kind == Z_CANON_TOKEN_COMMENT) ? true : canon_fail(parser->diag, token, message, "line end", token->text);
}

static bool canon_parse_type_declaration(CanonParser *parser, bool was_extern) {
  if (!canon_expect_word_token(parser, "expected type name")) return false;
  if (!canon_parse_type_params(parser)) return false;
  if (was_extern) {
    if (canon_is_symbol_text(canon_peek(parser), "{")) return canon_parse_field_list(parser, true);
    return canon_expect_declaration_end(parser, "unexpected tokens after extern type declaration");
  }
  if (parser->facts) parser->facts->type_count++;
  return canon_parse_field_list(parser, true);
}

static bool canon_parse_extern_declaration(CanonParser *parser, const ZCanonicalToken *start) {
  if (canon_accept_word(parser, "type")) return canon_parse_type_declaration(parser, true);
  if (!canon_accept_word(parser, "c")) return canon_fail(parser->diag, canon_peek(parser), "expected extern c or extern type", "extern c", start->text);
  if (canon_peek(parser)->kind != Z_CANON_TOKEN_STRING) return canon_fail(parser->diag, canon_peek(parser), "expected C header string", "string", canon_peek(parser)->text);
  parser->pos++;
  if (!canon_accept_word(parser, "as")) return canon_fail(parser->diag, canon_peek(parser), "expected C import alias", "as", canon_peek(parser)->text);
  if (!canon_expect_word_token(parser, "expected C import alias name")) return false;
  return canon_expect_declaration_end(parser, "unexpected tokens after extern c declaration");
}

static bool canon_parse_enum_declaration(CanonParser *parser) {
  if (!canon_expect_word_token(parser, "expected enum name")) return false;
  if (!canon_parse_type_params(parser)) return false;
  if (parser->facts) parser->facts->enum_count++;
  return canon_parse_field_list(parser, false);
}

static bool canon_parse_choice_declaration(CanonParser *parser) {
  if (!canon_expect_word_token(parser, "expected choice name")) return false;
  if (!canon_parse_type_params(parser)) return false;
  if (parser->facts) parser->facts->choice_count++;
  return canon_parse_field_list(parser, true);
}

static bool canon_parse_alias_declaration(CanonParser *parser) {
  if (!canon_expect_word_token(parser, "expected alias name")) return false;
  if (!canon_expect_symbol(parser, "=", "expected alias target")) return false;
  return canon_parse_type_until(parser, NULL, NULL);
}

static bool canon_parse_const_declaration(CanonParser *parser) {
  if (!canon_expect_word_token(parser, "expected const name")) return false;
  if (!canon_expect_symbol(parser, ":", "expected const type")) return false;
  if (!canon_parse_type_until(parser, "=", NULL)) return false;
  if (!canon_expect_symbol(parser, "=", "expected const value")) return false;
  return canon_parse_expr_line(parser, false);
}

static bool canon_parse_use_declaration(CanonParser *parser) {
  if (!canon_expect_word_token(parser, "expected import module name")) return false;
  while (canon_accept_symbol(parser, ".")) {
    if (!canon_expect_word_token(parser, "expected import module segment")) return false;
  }
  if (canon_accept_word(parser, "as")) {
    if (!canon_expect_word_token(parser, "expected import alias")) return false;
  }
  return true;
}

static bool canon_parse_public_declaration(CanonParser *parser, const ZCanonicalToken *start) {
  if (canon_accept_word(parser, "fn")) {
    if (parser->facts) parser->facts->function_count++;
    return canon_parse_signature(parser, true, 1);
  }
  if (canon_accept_word(parser, "type")) return canon_parse_type_declaration(parser, false);
  if (canon_accept_word(parser, "extern")) return canon_parse_extern_declaration(parser, start);
  if (canon_accept_word(parser, "enum")) return canon_parse_enum_declaration(parser);
  if (canon_accept_word(parser, "choice")) return canon_parse_choice_declaration(parser);
  if (canon_accept_word(parser, "interface")) {
    if (parser->facts) parser->facts->interface_count++;
    return canon_parse_interface(parser);
  }
  if (canon_accept_word(parser, "alias")) return canon_parse_alias_declaration(parser);
  if (canon_accept_word(parser, "const")) return canon_parse_const_declaration(parser);
  return canon_fail(parser->diag, canon_peek(parser), "expected public declaration", "fn, type, extern type, enum, choice, interface, alias, or const", canon_peek(parser) ? canon_peek(parser)->text : "end of file");
}

static bool canon_parse_declaration_body(CanonParser *parser, const ZCanonicalToken *start) {
  if (canon_accept_word(parser, "fun")) return canon_fail(parser->diag, start, "function declarations use fn", "fn", "fun");
  if (canon_accept_word(parser, "shape")) return canon_fail(parser->diag, start, "record declarations use type", "type", "shape");
  if (canon_accept_word(parser, "pub")) return canon_parse_public_declaration(parser, start);
  if (canon_accept_word(parser, "export")) {
    if (!canon_accept_word(parser, "c") || !canon_accept_word(parser, "fn")) return canon_fail(parser->diag, canon_peek(parser), "expected export c fn", "export c fn", start->text);
    if (parser->facts) parser->facts->function_count++;
    return canon_parse_signature(parser, true, 1);
  }
  if (canon_accept_word(parser, "fn")) {
    if (parser->facts) parser->facts->function_count++;
    return canon_parse_signature(parser, true, 1);
  }
  if (canon_accept_word(parser, "type")) return canon_parse_type_declaration(parser, false);
  if (canon_accept_word(parser, "extern")) return canon_parse_extern_declaration(parser, start);
  if (canon_accept_word(parser, "enum")) return canon_parse_enum_declaration(parser);
  if (canon_accept_word(parser, "choice")) return canon_parse_choice_declaration(parser);
  if (canon_accept_word(parser, "interface")) {
    if (parser->facts) parser->facts->interface_count++;
    return canon_parse_interface(parser);
  }
  if (canon_accept_word(parser, "test")) {
    if (canon_peek(parser)->kind != Z_CANON_TOKEN_STRING) return canon_fail(parser->diag, canon_peek(parser), "expected test name", "string", canon_peek(parser)->text);
    parser->pos++;
    if (parser->facts) parser->facts->test_count++;
    return canon_parse_block(parser, 1);
  }
  if (canon_accept_word(parser, "use")) return canon_parse_use_declaration(parser);
  if (canon_accept_word(parser, "alias")) return canon_parse_alias_declaration(parser);
  if (canon_accept_word(parser, "const")) return canon_parse_const_declaration(parser);
  return canon_fail(parser->diag, start, "expected canonical declaration", "declaration", start->text);
}

static bool canon_parse_declaration(CanonParser *parser) {
  canon_skip_newlines(parser);
  const ZCanonicalToken *start = canon_peek(parser);
  if (!start || start->kind == Z_CANON_TOKEN_EOF) return true;
  size_t node_index = parser->tree->len;
  canon_push_node(parser->tree, (ZCanonicalNode){Z_CANON_NODE_DECL, parser->pos, 0, 0, start->line, start->column});
  if (parser->facts) parser->facts->declaration_count++;
  if (!canon_parse_declaration_body(parser, start)) return false;
  parser->tree->items[node_index].token_count = parser->pos - parser->tree->items[node_index].first_token;
  return canon_expect_declaration_end(parser, "unexpected tokens after declaration");
}

bool z_canonical_text_parse(const ZCanonicalTokenVec *tokens, ZCanonicalTree *tree, ZCanonicalFacts *facts, ZDiag *diag) {
  if (!tokens || !tree || !facts) return canon_fail(diag, NULL, "missing canonical text parser input", "tokens, tree, facts", "missing");
  *tree = (ZCanonicalTree){0};
  *facts = (ZCanonicalFacts){0};
  CanonParser parser = {.tokens = tokens, .tree = tree, .facts = facts, .diag = diag};
  canon_skip_newlines(&parser);
  while (canon_peek(&parser) && canon_peek(&parser)->kind != Z_CANON_TOKEN_EOF) {
    if (!canon_parse_declaration(&parser)) return false;
    canon_skip_newlines(&parser);
  }
  return true;
}

void z_free_canonical_text_tokens(ZCanonicalTokenVec *tokens) {
  if (!tokens) return;
  for (size_t i = 0; i < tokens->len; i++) free(tokens->items[i].text);
  free(tokens->items);
  *tokens = (ZCanonicalTokenVec){0};
}

void z_free_canonical_text_tree(ZCanonicalTree *tree) {
  if (!tree) return;
  free(tree->items);
  *tree = (ZCanonicalTree){0};
}
