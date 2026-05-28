#include "canonical_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANON_AST_NO_INDEX ((size_t)-1)

typedef struct {
  const ZCanonicalTokenVec *tokens;
  size_t pos;
  ZDiag *diag;
  size_t test_counter;
} CanonAstParser;

typedef struct {
  const ZCanonicalTokenVec *tokens;
  size_t pos;
  size_t end;
  ZDiag *diag;
} CanonExprParser;

static bool canon_ast_text_eq(const char *left, const char *right) {
  if (!left || !right) return left == right;
  while (*left && *right && *left == *right) {
    left++;
    right++;
  }
  return *left == 0 && *right == 0;
}

static bool canon_ast_has_diag(ZDiag *diag) { return diag && diag->code != 0; }

static int canon_ast_hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

static const ZCanonicalToken *canon_ast_token(const ZCanonicalTokenVec *tokens, size_t index) {
  return tokens && index < tokens->len ? &tokens->items[index] : NULL;
}

static const ZCanonicalToken *canon_ast_peek(CanonAstParser *parser) {
  return canon_ast_token(parser ? parser->tokens : NULL, parser ? parser->pos : 0);
}

static bool canon_ast_is_text(const ZCanonicalToken *token, const char *text) {
  return token && canon_ast_text_eq(token->text, text);
}

static bool canon_ast_token_text(const ZCanonicalTokenVec *tokens, size_t index, size_t end, const char *text) {
  const ZCanonicalToken *token = canon_ast_token(tokens, index);
  return index < end && canon_ast_is_text(token, text);
}

static bool canon_ast_tokens_connected(const ZCanonicalToken *left, const ZCanonicalToken *right) {
  return left && right && right->offset == left->offset + left->length;
}

static bool canon_ast_fail(ZDiag *diag, const ZCanonicalToken *token, const char *message, const char *expected, const char *actual) {
  if (!diag || diag->code != 0) return false;
  diag->code = 101;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "invalid canonical text program");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "canonical text program");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "");
  snprintf(diag->help, sizeof(diag->help), "use canonical .0 text source");
  diag->line = token ? token->line : 1;
  diag->column = token ? token->column : 1;
  diag->length = token && token->length ? (int)token->length : 1;
  return false;
}

static void canon_ast_skip_newlines(CanonAstParser *parser) {
  while (canon_ast_peek(parser) &&
         (canon_ast_peek(parser)->kind == Z_CANON_TOKEN_NEWLINE || canon_ast_peek(parser)->kind == Z_CANON_TOKEN_COMMENT)) {
    parser->pos++;
  }
}

static size_t canon_ast_skip_newline_pos(const ZCanonicalTokenVec *tokens, size_t pos) {
  while (pos < tokens->len &&
         (tokens->items[pos].kind == Z_CANON_TOKEN_NEWLINE || tokens->items[pos].kind == Z_CANON_TOKEN_COMMENT)) {
    pos++;
  }
  return pos;
}

static bool canon_ast_accept(CanonAstParser *parser, const char *text) {
  if (!canon_ast_is_text(canon_ast_peek(parser), text)) return false;
  parser->pos++;
  return true;
}

static bool canon_ast_expect(CanonAstParser *parser, const char *text, const char *message) {
  if (canon_ast_accept(parser, text)) return true;
  return canon_ast_fail(parser->diag, canon_ast_peek(parser), message, text, canon_ast_peek(parser) ? canon_ast_peek(parser)->text : "end of file");
}

static const ZCanonicalToken *canon_ast_expect_word(CanonAstParser *parser, const char *message) {
  const ZCanonicalToken *token = canon_ast_peek(parser);
  if (token && token->kind == Z_CANON_TOKEN_WORD) {
    parser->pos++;
    return token;
  }
  canon_ast_fail(parser->diag, token, message, "identifier", token ? token->text : "end of file");
  return NULL;
}

static bool canon_ast_decode_hex_byte(const char *text, size_t len, size_t index, unsigned *out) {
  if (index + 2 >= len) return false;
  int high = canon_ast_hex_digit(text[index + 1]);
  int low = canon_ast_hex_digit(text[index + 2]);
  if (high < 0 || low < 0) return false;
  *out = (unsigned)((high << 4) | low);
  return true;
}

static char *canon_ast_decode_string_literal(const ZCanonicalToken *token, ZDiag *diag) {
  const char *text = token && token->text ? token->text : "";
  size_t len = strlen(text);
  if (len < 2 || text[0] != '"' || text[len - 1] != '"') {
    canon_ast_fail(diag, token, "malformed string literal", "string literal", text);
    return z_strdup("");
  }
  ZBuf buf;
  zbuf_init(&buf);
  for (size_t i = 1; i + 1 < len; i++) {
    unsigned char ch = (unsigned char)text[i];
    if (ch != '\\') {
      zbuf_append_char(&buf, (char)ch);
      continue;
    }
    if (i + 1 >= len - 1) {
      canon_ast_fail(diag, token, "malformed string escape", "escaped byte", text);
      break;
    }
    char escaped = text[++i];
    if (escaped == 'n') zbuf_append_char(&buf, '\n');
    else if (escaped == 'r') zbuf_append_char(&buf, '\r');
    else if (escaped == 't') zbuf_append_char(&buf, '\t');
    else if (escaped == 'x') {
      unsigned value = 0;
      if (!canon_ast_decode_hex_byte(text, len - 1, i, &value) || value == 0) {
        canon_ast_fail(diag, token, "malformed hex string escape", "nonzero byte escape", text);
        break;
      }
      zbuf_append_char(&buf, (char)value);
      i += 2;
    } else {
      zbuf_append_char(&buf, escaped);
    }
  }
  if (canon_ast_has_diag(diag)) {
    free(buf.data);
    return z_strdup("");
  }
  return buf.data ? buf.data : z_strdup("");
}

static char *canon_ast_decode_char_literal(const ZCanonicalToken *token, ZDiag *diag) {
  const char *text = token && token->text ? token->text : "";
  size_t len = strlen(text);
  if (len < 3 || text[0] != '\'' || text[len - 1] != '\'') {
    canon_ast_fail(diag, token, "malformed character literal", "character literal", text);
    return z_strdup("0");
  }
  size_t i = 1;
  unsigned value = 0;
  if (text[i] == '\\') {
    i++;
    char escaped = text[i++];
    if (escaped == 'n') value = '\n';
    else if (escaped == 'r') value = '\r';
    else if (escaped == 't') value = '\t';
    else if (escaped == '0') value = '\0';
    else if (escaped == '\'') value = '\'';
    else if (escaped == '"') value = '"';
    else if (escaped == '\\') value = '\\';
    else if (escaped == 'x') {
      i--;
      if (!canon_ast_decode_hex_byte(text, len - 1, i, &value)) {
        canon_ast_fail(diag, token, "malformed hex character escape", "byte escape", text);
        return z_strdup("0");
      }
      i += 3;
    } else {
      canon_ast_fail(diag, token, "malformed character escape", "one byte character literal", text);
      return z_strdup("0");
    }
  } else {
    value = (unsigned char)text[i++];
  }
  if (i != len - 1 || value > 255) {
    canon_ast_fail(diag, token, "malformed character literal", "one byte character literal", text);
    return z_strdup("0");
  }
  char decoded[16];
  snprintf(decoded, sizeof(decoded), "%u", value);
  return z_strdup(decoded);
}

static void canon_push_param_ast(ParamVec *vec, Param item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(Param));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_function_ast(FunctionVec *vec, Function item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(Function));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_shape_ast(ShapeVec *vec, Shape item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(Shape));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_interface_ast(InterfaceVec *vec, InterfaceDecl item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(InterfaceDecl));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_enum_ast(EnumVec *vec, EnumDecl item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(EnumDecl));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_choice_ast(ChoiceVec *vec, Choice item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(Choice));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_const_ast(ConstVec *vec, ConstDecl item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(ConstDecl));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_alias_ast(TypeAliasVec *vec, TypeAlias item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(TypeAlias));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_use_ast(UseImportVec *vec, UseImport item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(UseImport));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_c_import_ast(CImportVec *vec, CImport item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(CImport));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_stmt_ast(StmtVec *vec, Stmt *item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(Stmt *));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_match_arm_ast(MatchArmVec *vec, MatchArm item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(MatchArm));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_expr_ast(ExprVec *vec, Expr *item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(Expr *));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_type_arg_ast(TypeArgVec *vec, TypeArg item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(TypeArg));
  }
  vec->items[vec->len++] = item;
}

static void canon_push_field_ast(FieldInitVec *vec, FieldInit item) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(FieldInit));
  }
  vec->items[vec->len++] = item;
}

static Expr *canon_ast_new_expr(ExprKind kind, const ZCanonicalToken *token) {
  Expr *expr = z_checked_calloc(1, sizeof(Expr));
  expr->kind = kind;
  expr->line = token ? token->line : 1;
  expr->column = token ? token->column : 1;
  return expr;
}

static Stmt *canon_ast_new_stmt(StmtKind kind, const ZCanonicalToken *token) {
  Stmt *stmt = z_checked_calloc(1, sizeof(Stmt));
  stmt->kind = kind;
  stmt->line = token ? token->line : 1;
  stmt->column = token ? token->column : 1;
  return stmt;
}

static bool canon_ast_join_has_char(ZBuf *buf, char ch) {
  return buf->len > 0 && buf->data && buf->data[buf->len - 1] == ch;
}

static char *canon_ast_join_tokens(const ZCanonicalTokenVec *tokens, size_t start, size_t end, bool type_text) {
  ZBuf buf;
  zbuf_init(&buf);
  for (size_t i = start; i < end; i++) {
    const char *text = tokens->items[i].text ? tokens->items[i].text : "";
    if (type_text && canon_ast_text_eq(text, ",")) {
      zbuf_append(&buf, ", ");
      continue;
    }
    if (type_text && canon_ast_text_eq(text, "const")) {
      zbuf_append(&buf, "const ");
      continue;
    }
    if (type_text && buf.len > 0 && canon_ast_join_has_char(&buf, ' ') &&
        (canon_ast_text_eq(text, ">") || canon_ast_text_eq(text, "]") || canon_ast_text_eq(text, ")"))) {
      buf.data[--buf.len] = 0;
    }
    zbuf_append(&buf, text);
  }
  return buf.data ? buf.data : z_strdup("");
}

static char *canon_ast_join_line_tokens(const ZCanonicalTokenVec *tokens, size_t start, size_t end) {
  return canon_ast_join_tokens(tokens, start, end, false);
}

static size_t canon_ast_matching(const ZCanonicalTokenVec *tokens, size_t open, size_t end) {
  const char *open_text = tokens->items[open].text;
  const char *close_text = canon_ast_text_eq(open_text, "(") ? ")" : canon_ast_text_eq(open_text, "[") ? "]" : "}";
  size_t depth = 1;
  for (size_t i = open + 1; i < end; i++) {
    if (canon_ast_token_text(tokens, i, end, open_text)) depth++;
    else if (canon_ast_token_text(tokens, i, end, close_text)) {
      depth--;
      if (depth == 0) return i;
    }
  }
  return CANON_AST_NO_INDEX;
}

static size_t canon_ast_find_top_level(const ZCanonicalTokenVec *tokens, size_t start, size_t end, const char *text) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t i = start; i < end; i++) {
    const ZCanonicalToken *token = &tokens->items[i];
    if (paren == 0 && bracket == 0 && brace == 0 && canon_ast_is_text(token, text)) return i;
    if (canon_ast_is_text(token, "(")) paren++;
    else if (canon_ast_is_text(token, ")") && paren > 0) paren--;
    else if (canon_ast_is_text(token, "[")) bracket++;
    else if (canon_ast_is_text(token, "]") && bracket > 0) bracket--;
    else if (canon_ast_is_text(token, "{")) brace++;
    else if (canon_ast_is_text(token, "}") && brace > 0) brace--;
  }
  return CANON_AST_NO_INDEX;
}

static size_t canon_ast_generic_call_close(const ZCanonicalTokenVec *tokens, size_t start, size_t end, size_t open_index) {
  if (open_index == start || open_index >= end || !canon_ast_token_text(tokens, open_index, end, "<")) return CANON_AST_NO_INDEX;
  const ZCanonicalToken *previous = &tokens->items[open_index - 1];
  const ZCanonicalToken *open = &tokens->items[open_index];
  if (previous->kind != Z_CANON_TOKEN_WORD || !canon_ast_tokens_connected(previous, open)) return CANON_AST_NO_INDEX;
  int angle = 1;
  for (size_t i = open_index + 1; i < end; i++) {
    if (canon_ast_token_text(tokens, i, end, "<")) angle++;
    else if (canon_ast_token_text(tokens, i, end, ">")) {
      angle--;
      if (angle == 0) {
        if (i + 1 < end && canon_ast_token_text(tokens, i + 1, end, "(") &&
            canon_ast_tokens_connected(&tokens->items[i], &tokens->items[i + 1])) {
          return i;
        }
        return CANON_AST_NO_INDEX;
      }
    }
  }
  return CANON_AST_NO_INDEX;
}

static size_t canon_ast_find_top_level_separator(const ZCanonicalTokenVec *tokens, size_t start, size_t end, const char *text) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t i = start; i < end; i++) {
    const ZCanonicalToken *token = &tokens->items[i];
    if (paren == 0 && bracket == 0 && brace == 0) {
      size_t generic_close = canon_ast_generic_call_close(tokens, start, end, i);
      if (generic_close != CANON_AST_NO_INDEX) {
        i = generic_close;
        continue;
      }
      if (canon_ast_is_text(token, text)) return i;
    }
    if (canon_ast_is_text(token, "(")) paren++;
    else if (canon_ast_is_text(token, ")") && paren > 0) paren--;
    else if (canon_ast_is_text(token, "[")) bracket++;
    else if (canon_ast_is_text(token, "]") && bracket > 0) bracket--;
    else if (canon_ast_is_text(token, "{")) brace++;
    else if (canon_ast_is_text(token, "}") && brace > 0) brace--;
  }
  return CANON_AST_NO_INDEX;
}

static size_t canon_ast_find_rescue_err(const ZCanonicalTokenVec *tokens, size_t rescue, size_t end) {
  int paren = 0, bracket = 0, brace = 0, nested = 0;
  for (size_t i = rescue + 1; i < end; i++) {
    const ZCanonicalToken *token = &tokens->items[i];
    const ZCanonicalToken *previous = i > rescue + 1 ? &tokens->items[i - 1] : NULL;
    const ZCanonicalToken *next = i + 1 < end ? &tokens->items[i + 1] : NULL;
    bool top = paren == 0 && bracket == 0 && brace == 0;
    if (top && canon_ast_is_text(token, "rescue")) nested++;
    else if (top && canon_ast_is_text(token, "err") && !canon_ast_is_text(previous, ".") && !canon_ast_is_text(next, ".")) {
      if (nested == 0) return i;
      nested--;
    }
    if (canon_ast_is_text(token, "(")) paren++;
    else if (canon_ast_is_text(token, ")") && paren > 0) paren--;
    else if (canon_ast_is_text(token, "[")) bracket++;
    else if (canon_ast_is_text(token, "]") && bracket > 0) bracket--;
    else if (canon_ast_is_text(token, "{")) brace++;
    else if (canon_ast_is_text(token, "}") && brace > 0) brace--;
  }
  return CANON_AST_NO_INDEX;
}

static size_t canon_ast_line_end(const ZCanonicalTokenVec *tokens, size_t start) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t i = start; i < tokens->len; i++) {
    const ZCanonicalToken *token = &tokens->items[i];
    if (paren == 0 && bracket == 0 && brace == 0 &&
        (token->kind == Z_CANON_TOKEN_EOF || token->kind == Z_CANON_TOKEN_NEWLINE || token->kind == Z_CANON_TOKEN_COMMENT || canon_ast_is_text(token, "}"))) {
      return i;
    }
    if (canon_ast_is_text(token, "(")) paren++;
    else if (canon_ast_is_text(token, ")") && paren > 0) paren--;
    else if (canon_ast_is_text(token, "[")) bracket++;
    else if (canon_ast_is_text(token, "]") && bracket > 0) bracket--;
    else if (canon_ast_is_text(token, "{")) brace++;
    else if (canon_ast_is_text(token, "}") && brace > 0) brace--;
  }
  return tokens->len;
}

static size_t canon_ast_block_open(const ZCanonicalTokenVec *tokens, size_t start) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t i = start; i < tokens->len; i++) {
    const ZCanonicalToken *token = &tokens->items[i];
    if (paren == 0 && bracket == 0 && brace == 0 && canon_ast_is_text(token, "{")) return i;
    if (token->kind == Z_CANON_TOKEN_EOF || token->kind == Z_CANON_TOKEN_NEWLINE || token->kind == Z_CANON_TOKEN_COMMENT) return CANON_AST_NO_INDEX;
    if (canon_ast_is_text(token, "(")) paren++;
    else if (canon_ast_is_text(token, ")") && paren > 0) paren--;
    else if (canon_ast_is_text(token, "[")) bracket++;
    else if (canon_ast_is_text(token, "]") && bracket > 0) bracket--;
    else if (canon_ast_is_text(token, "{")) brace++;
    else if (canon_ast_is_text(token, "}") && brace > 0) brace--;
  }
  return CANON_AST_NO_INDEX;
}

static char *canon_expr_shape_name_text(const Expr *expr) {
  ZBuf buf;
  zbuf_init(&buf);
  if (!expr) return z_strdup("Unknown");
  if (expr->kind == EXPR_MEMBER) {
    char *left = canon_expr_shape_name_text(expr->left);
    zbuf_append(&buf, left);
    zbuf_append_char(&buf, '.');
    zbuf_append(&buf, expr->text ? expr->text : "");
    free(left);
  } else {
    zbuf_append(&buf, expr->text ? expr->text : "Unknown");
  }
  return buf.data ? buf.data : z_strdup("Unknown");
}

static Expr *canon_parse_expr_span(const ZCanonicalTokenVec *tokens, size_t start, size_t end, ZDiag *diag);
static Expr *canon_parse_rescue_span(const ZCanonicalTokenVec *tokens, size_t start, size_t end, ZDiag *diag);

static int canon_expr_precedence(const ZCanonicalToken *token) {
  if (!token) return 0;
  if (canon_ast_is_text(token, "||")) return 1;
  if (canon_ast_is_text(token, "&&")) return 2;
  if (canon_ast_is_text(token, "==") || canon_ast_is_text(token, "!=")) return 3;
  if (canon_ast_is_text(token, "<") || canon_ast_is_text(token, "<=") ||
      canon_ast_is_text(token, ">") || canon_ast_is_text(token, ">=")) return 4;
  if (canon_ast_is_text(token, "+") || canon_ast_is_text(token, "-") ||
      canon_ast_is_text(token, "+%") || canon_ast_is_text(token, "+|")) return 5;
  if (canon_ast_is_text(token, "*") || canon_ast_is_text(token, "/") || canon_ast_is_text(token, "%")) return 6;
  if (canon_ast_is_text(token, "as")) return 7;
  return 0;
}

static Expr *canon_parse_expr_prec(CanonExprParser *parser, int min_prec);

static bool canon_expr_accept(CanonExprParser *parser, const char *text) {
  if (parser->pos >= parser->end || !canon_ast_is_text(&parser->tokens->items[parser->pos], text)) return false;
  parser->pos++;
  return true;
}

static void canon_parse_expr_type_args(CanonExprParser *parser, Expr *expr, size_t open, size_t close) {
  parser->pos = open + 1;
  while (parser->pos < close) {
    size_t arg_start = parser->pos;
    int angle = 0, bracket = 0, paren = 0;
    while (parser->pos < close) {
      const ZCanonicalToken *token = &parser->tokens->items[parser->pos];
      if (angle == 0 && bracket == 0 && paren == 0 && canon_ast_is_text(token, ",")) break;
      if (canon_ast_is_text(token, "<")) angle++;
      else if (canon_ast_is_text(token, ">") && angle > 0) angle--;
      else if (canon_ast_is_text(token, "[")) bracket++;
      else if (canon_ast_is_text(token, "]") && bracket > 0) bracket--;
      else if (canon_ast_is_text(token, "(")) paren++;
      else if (canon_ast_is_text(token, ")") && paren > 0) paren--;
      parser->pos++;
    }
    if (arg_start < parser->pos) {
      const ZCanonicalToken *start = &parser->tokens->items[arg_start];
      canon_push_type_arg_ast(&expr->type_args, (TypeArg){
        .type = canon_ast_join_tokens(parser->tokens, arg_start, parser->pos, true),
        .line = start->line,
        .column = start->column
      });
    }
    if (parser->pos < close && canon_ast_is_text(&parser->tokens->items[parser->pos], ",")) parser->pos++;
  }
  parser->pos = close + 1;
}

static size_t canon_expr_generic_close(CanonExprParser *parser) {
  if (parser->pos >= parser->end || !canon_ast_is_text(&parser->tokens->items[parser->pos], "<")) return CANON_AST_NO_INDEX;
  int angle = 1;
  for (size_t i = parser->pos + 1; i < parser->end; i++) {
    if (canon_ast_token_text(parser->tokens, i, parser->end, "<")) angle++;
    else if (canon_ast_token_text(parser->tokens, i, parser->end, ">")) {
      angle--;
      if (angle == 0) {
        if (i + 1 < parser->end && canon_ast_token_text(parser->tokens, i + 1, parser->end, "(")) {
          return i;
        }
        return CANON_AST_NO_INDEX;
      }
    }
  }
  return CANON_AST_NO_INDEX;
}

static void canon_parse_call_args(CanonExprParser *parser, Expr *call, size_t close) {
  while (parser->pos < close && !canon_ast_has_diag(parser->diag)) {
    if (canon_expr_accept(parser, ",")) continue;
    size_t arg_start = parser->pos;
    size_t arg_end = canon_ast_find_top_level_separator(parser->tokens, arg_start, close, ",");
    if (arg_end == CANON_AST_NO_INDEX) arg_end = close;
    if (arg_start < arg_end) canon_push_expr_ast(&call->args, canon_parse_expr_span(parser->tokens, arg_start, arg_end, parser->diag));
    parser->pos = arg_end;
    if (parser->pos < close && canon_ast_is_text(&parser->tokens->items[parser->pos], ",")) parser->pos++;
  }
  parser->pos = close + 1;
}

static void canon_parse_shape_fields(CanonExprParser *parser, Expr *literal, size_t close) {
  parser->pos++;
  while (parser->pos < close) {
    if (canon_expr_accept(parser, ",")) continue;
    const ZCanonicalToken *field = parser->pos < close ? &parser->tokens->items[parser->pos++] : NULL;
    if (!field || field->kind != Z_CANON_TOKEN_WORD) {
      canon_ast_fail(parser->diag, field, "expected object field name", "field", field ? field->text : "end of object");
      break;
    }
    if (!canon_expr_accept(parser, ":")) {
      canon_ast_fail(parser->diag, field, "expected ':' after object field name", ":", field->text);
      break;
    }
    size_t value_start = parser->pos;
    size_t value_end = canon_ast_find_top_level_separator(parser->tokens, value_start, close, ",");
    if (value_end == CANON_AST_NO_INDEX) value_end = close;
    parser->pos = value_end;
    canon_push_field_ast(&literal->fields, (FieldInit){
      .name = z_strdup(field->text),
      .value = canon_parse_expr_span(parser->tokens, value_start, parser->pos, parser->diag),
      .line = field->line,
      .column = field->column
    });
    if (parser->pos < close && canon_ast_is_text(&parser->tokens->items[parser->pos], ",")) parser->pos++;
  }
  parser->pos = close + 1;
}

static Expr *canon_parse_postfix(CanonExprParser *parser, Expr *expr) {
  while (parser->pos < parser->end && !canon_ast_has_diag(parser->diag)) {
    size_t generic_close = canon_expr_generic_close(parser);
    if (generic_close != CANON_AST_NO_INDEX) {
      canon_parse_expr_type_args(parser, expr, parser->pos, generic_close);
      continue;
    }
    if (canon_expr_accept(parser, ".")) {
      const ZCanonicalToken *name = parser->pos < parser->end ? &parser->tokens->items[parser->pos++] : NULL;
      Expr *member = canon_ast_new_expr(EXPR_MEMBER, name);
      member->left = expr;
      member->text = name && name->text ? z_strdup(name->text) : z_strdup("");
      expr = member;
      continue;
    }
    if (parser->pos < parser->end && canon_ast_is_text(&parser->tokens->items[parser->pos], "(")) {
      const ZCanonicalToken *open = &parser->tokens->items[parser->pos];
      size_t close = canon_ast_matching(parser->tokens, parser->pos, parser->end);
      if (close == CANON_AST_NO_INDEX) break;
      parser->pos++;
      Expr *call = canon_ast_new_expr(EXPR_CALL, open);
      call->left = expr;
      canon_parse_call_args(parser, call, close);
      expr = call;
      continue;
    }
    if (parser->pos < parser->end && canon_ast_is_text(&parser->tokens->items[parser->pos], "[")) {
      const ZCanonicalToken *open = &parser->tokens->items[parser->pos];
      size_t close = canon_ast_matching(parser->tokens, parser->pos, parser->end);
      if (close == CANON_AST_NO_INDEX) break;
      size_t range = canon_ast_find_top_level(parser->tokens, parser->pos + 1, close, "..");
      Expr *next = canon_ast_new_expr(range == CANON_AST_NO_INDEX ? EXPR_INDEX : EXPR_SLICE, open);
      next->left = expr;
      if (range == CANON_AST_NO_INDEX) next->right = canon_parse_expr_span(parser->tokens, parser->pos + 1, close, parser->diag);
      else {
        canon_push_expr_ast(&next->args, range > parser->pos + 1 ? canon_parse_expr_span(parser->tokens, parser->pos + 1, range, parser->diag) : NULL);
        canon_push_expr_ast(&next->args, range + 1 < close ? canon_parse_expr_span(parser->tokens, range + 1, close, parser->diag) : NULL);
      }
      parser->pos = close + 1;
      expr = next;
      continue;
    }
    if (parser->pos < parser->end && canon_ast_is_text(&parser->tokens->items[parser->pos], "{")) {
      size_t close = canon_ast_matching(parser->tokens, parser->pos, parser->end);
      if (close == CANON_AST_NO_INDEX) break;
      Expr *literal = canon_ast_new_expr(EXPR_SHAPE_LITERAL, &parser->tokens->items[parser->pos]);
      literal->text = canon_expr_shape_name_text(expr);
      literal->left = expr;
      canon_parse_shape_fields(parser, literal, close);
      expr = literal;
      continue;
    }
    break;
  }
  return expr;
}

static Expr *canon_parse_array_literal(CanonExprParser *parser, const ZCanonicalToken *open) {
  size_t close = canon_ast_matching(parser->tokens, parser->pos - 1, parser->end);
  Expr *expr = canon_ast_new_expr(EXPR_ARRAY_LITERAL, open);
  if (close == CANON_AST_NO_INDEX) {
    canon_ast_fail(parser->diag, open, "expected ']' after array literal", "]", "end of expression");
    return expr;
  }
  while (parser->pos < close) {
    if (canon_expr_accept(parser, ",")) continue;
    size_t sep = canon_ast_find_top_level(parser->tokens, parser->pos, close, ";");
    size_t item_end = sep == CANON_AST_NO_INDEX ? canon_ast_find_top_level(parser->tokens, parser->pos, close, ",") : sep;
    if (item_end == CANON_AST_NO_INDEX) item_end = close;
    canon_push_expr_ast(&expr->args, canon_parse_expr_span(parser->tokens, parser->pos, item_end, parser->diag));
    parser->pos = item_end;
    if (sep != CANON_AST_NO_INDEX && sep == item_end) {
      expr->array_repeat = true;
      parser->pos++;
      canon_push_expr_ast(&expr->args, canon_parse_expr_span(parser->tokens, parser->pos, close, parser->diag));
      break;
    }
    if (parser->pos < close && canon_ast_is_text(&parser->tokens->items[parser->pos], ",")) parser->pos++;
  }
  parser->pos = close + 1;
  return expr;
}

static Expr *canon_parse_primary(CanonExprParser *parser) {
  if (parser->pos >= parser->end) {
    canon_ast_fail(parser->diag, NULL, "expected expression", "expression", "empty");
    return canon_ast_new_expr(EXPR_IDENT, NULL);
  }
  const ZCanonicalToken *token = &parser->tokens->items[parser->pos++];
  if (token->kind == Z_CANON_TOKEN_STRING) {
    Expr *expr = canon_ast_new_expr(EXPR_STRING, token);
    expr->text = canon_ast_decode_string_literal(token, parser->diag);
    return canon_parse_postfix(parser, expr);
  }
  if (token->kind == Z_CANON_TOKEN_CHAR) {
    Expr *expr = canon_ast_new_expr(EXPR_CHAR, token);
    expr->text = canon_ast_decode_char_literal(token, parser->diag);
    return canon_parse_postfix(parser, expr);
  }
  if (token->kind == Z_CANON_TOKEN_NUMBER) {
    Expr *expr = canon_ast_new_expr(EXPR_NUMBER, token);
    expr->text = z_strdup(token->text);
    return canon_parse_postfix(parser, expr);
  }
  if (token->kind == Z_CANON_TOKEN_WORD && (canon_ast_text_eq(token->text, "true") || canon_ast_text_eq(token->text, "false"))) {
    Expr *expr = canon_ast_new_expr(EXPR_BOOL, token);
    expr->bool_value = canon_ast_text_eq(token->text, "true");
    return canon_parse_postfix(parser, expr);
  }
  if (token->kind == Z_CANON_TOKEN_WORD && canon_ast_text_eq(token->text, "null")) return canon_parse_postfix(parser, canon_ast_new_expr(EXPR_NULL, token));
  if (token->kind == Z_CANON_TOKEN_WORD) {
    Expr *expr = canon_ast_new_expr(EXPR_IDENT, token);
    expr->text = z_strdup(token->text);
    return canon_parse_postfix(parser, expr);
  }
  if (canon_ast_is_text(token, "(")) {
    size_t close = canon_ast_matching(parser->tokens, parser->pos - 1, parser->end);
    Expr *expr = close == CANON_AST_NO_INDEX ? canon_ast_new_expr(EXPR_IDENT, token) : canon_parse_expr_span(parser->tokens, parser->pos, close, parser->diag);
    parser->pos = close == CANON_AST_NO_INDEX ? parser->end : close + 1;
    return canon_parse_postfix(parser, expr);
  }
  if (canon_ast_is_text(token, "[")) return canon_parse_postfix(parser, canon_parse_array_literal(parser, token));
  canon_ast_fail(parser->diag, token, "expected expression", "expression", token->text);
  return canon_ast_new_expr(EXPR_IDENT, token);
}

static Expr *canon_parse_prefix(CanonExprParser *parser) {
  const ZCanonicalToken *token = parser->pos < parser->end ? &parser->tokens->items[parser->pos] : NULL;
  if (token && canon_ast_is_text(token, "check")) {
    parser->pos++;
    Expr *expr = canon_ast_new_expr(EXPR_CHECK, token);
    expr->left = canon_parse_expr_prec(parser, 8);
    return expr;
  }
  if (token && canon_ast_is_text(token, "meta")) {
    parser->pos++;
    Expr *expr = canon_ast_new_expr(EXPR_META, token);
    expr->left = canon_parse_expr_prec(parser, 8);
    return expr;
  }
  if (token && canon_ast_is_text(token, "rescue")) {
    Expr *expr = canon_parse_rescue_span(parser->tokens, parser->pos, parser->end, parser->diag);
    parser->pos = parser->end;
    return expr;
  }
  if (token && canon_ast_is_text(token, "&")) {
    parser->pos++;
    Expr *expr = canon_ast_new_expr(EXPR_BORROW, token);
    if (canon_expr_accept(parser, "mut")) expr->mutable_borrow = true;
    expr->left = canon_parse_expr_prec(parser, 8);
    return expr;
  }
  if (token && canon_ast_is_text(token, "-")) {
    parser->pos++;
    Expr *expr = canon_ast_new_expr(EXPR_BINARY, token);
    expr->text = z_strdup("-");
    expr->left = canon_ast_new_expr(EXPR_NUMBER, token);
    expr->left->text = z_strdup("0");
    expr->right = canon_parse_expr_prec(parser, 8);
    return expr;
  }
  if (token && canon_ast_is_text(token, "+")) {
    parser->pos++;
    Expr *expr = canon_ast_new_expr(EXPR_BINARY, token);
    expr->text = z_strdup("+");
    expr->left = canon_ast_new_expr(EXPR_NUMBER, token);
    expr->left->text = z_strdup("0");
    expr->right = canon_parse_expr_prec(parser, 8);
    return expr;
  }
  if (token && canon_ast_is_text(token, "!")) {
    parser->pos++;
    Expr *expr = canon_ast_new_expr(EXPR_BINARY, token);
    expr->text = z_strdup("==");
    expr->left = canon_parse_expr_prec(parser, 8);
    expr->right = canon_ast_new_expr(EXPR_BOOL, token);
    expr->right->bool_value = false;
    return expr;
  }
  if (token && canon_ast_is_text(token, "*")) {
    parser->pos++;
    Expr *callee = canon_ast_new_expr(EXPR_IDENT, token);
    callee->text = z_strdup("deref");
    Expr *call = canon_ast_new_expr(EXPR_CALL, token);
    call->left = callee;
    canon_push_expr_ast(&call->args, canon_parse_expr_prec(parser, 8));
    return call;
  }
  return canon_parse_primary(parser);
}

static Expr *canon_parse_rescue_span(const ZCanonicalTokenVec *tokens, size_t start, size_t end, ZDiag *diag) {
  if (!canon_ast_token_text(tokens, start, end, "rescue")) return NULL;
  size_t err = canon_ast_find_rescue_err(tokens, start, end);
  if (err == CANON_AST_NO_INDEX) {
    canon_ast_fail(diag, &tokens->items[start], "rescue expression requires an err fallback", "err fallback", "missing err");
    return canon_ast_new_expr(EXPR_RESCUE, &tokens->items[start]);
  }
  Expr *expr = canon_ast_new_expr(EXPR_RESCUE, &tokens->items[start]);
  expr->text = z_strdup("err");
  expr->left = canon_parse_expr_span(tokens, start + 1, err, diag);
  expr->right = canon_parse_expr_span(tokens, err + 1, end, diag);
  return expr;
}

static Expr *canon_parse_expr_prec(CanonExprParser *parser, int min_prec) {
  Expr *left = canon_parse_prefix(parser);
  while (parser->pos < parser->end && !canon_ast_has_diag(parser->diag)) {
    const ZCanonicalToken *op = &parser->tokens->items[parser->pos];
    int prec = canon_expr_precedence(op);
    if (prec == 0 || prec < min_prec) break;
    parser->pos++;
    if (canon_ast_is_text(op, "as")) {
      Expr *cast = canon_ast_new_expr(EXPR_CAST, op);
      cast->left = left;
      cast->text = canon_ast_join_tokens(parser->tokens, parser->pos, parser->end, true);
      parser->pos = parser->end;
      left = cast;
      continue;
    }
    Expr *binary = canon_ast_new_expr(EXPR_BINARY, op);
    binary->text = z_strdup(op->text);
    binary->left = left;
    binary->right = canon_parse_expr_prec(parser, prec + 1);
    left = binary;
  }
  return left;
}

static Expr *canon_parse_expr_span(const ZCanonicalTokenVec *tokens, size_t start, size_t end, ZDiag *diag) {
  while (start < end && tokens->items[start].kind == Z_CANON_TOKEN_NEWLINE) start++;
  while (end > start && tokens->items[end - 1].kind == Z_CANON_TOKEN_NEWLINE) end--;
  CanonExprParser parser = {.tokens = tokens, .pos = start, .end = end, .diag = diag};
  Expr *expr = canon_parse_expr_prec(&parser, 1);
  if (!canon_ast_has_diag(diag) && parser.pos < end) {
    canon_ast_fail(diag, &tokens->items[parser.pos], "unexpected token after expression", "end of expression", tokens->items[parser.pos].text);
  }
  return expr;
}

static char *canon_parse_type_between(CanonAstParser *parser, size_t start, size_t end) {
  return canon_ast_join_tokens(parser->tokens, start, end, true);
}

static bool canon_parse_type_params_ast(CanonAstParser *parser, ParamVec *out) {
  if (!canon_ast_accept(parser, "<")) return true;
  while (!canon_ast_has_diag(parser->diag) && !canon_ast_accept(parser, ">")) {
    bool is_static = canon_ast_accept(parser, "static");
    const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected generic parameter name");
    char *type = NULL;
    if (canon_ast_accept(parser, ":")) {
      size_t type_start = parser->pos;
      int angle = 0;
      while (parser->pos < parser->tokens->len) {
        const ZCanonicalToken *token = canon_ast_peek(parser);
        if (angle == 0 && (canon_ast_is_text(token, ",") || canon_ast_is_text(token, ">"))) break;
        if (canon_ast_is_text(token, "<")) angle++;
        else if (canon_ast_is_text(token, ">") && angle > 0) angle--;
        parser->pos++;
      }
      type = canon_parse_type_between(parser, type_start, parser->pos);
    }
    if (name) canon_push_param_ast(out, (Param){.name = z_strdup(name->text), .type = type, .is_static = is_static, .line = name->line, .column = name->column});
    if (canon_ast_accept(parser, ",")) continue;
    if (!canon_ast_is_text(canon_ast_peek(parser), ">")) return canon_ast_expect(parser, ">", "expected '>' after generic parameter list");
  }
  return !canon_ast_has_diag(parser->diag);
}

static bool canon_parse_params_ast(CanonAstParser *parser, ParamVec *out) {
  if (!canon_ast_expect(parser, "(", "expected parameter list")) return false;
  while (!canon_ast_has_diag(parser->diag) && !canon_ast_accept(parser, ")")) {
    const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected parameter name");
    if (!canon_ast_expect(parser, ":", "expected ':' after parameter name")) return false;
    size_t type_start = parser->pos;
    int angle = 0, bracket = 0;
    while (parser->pos < parser->tokens->len) {
      const ZCanonicalToken *token = canon_ast_peek(parser);
      if (angle == 0 && bracket == 0 && (canon_ast_is_text(token, ",") || canon_ast_is_text(token, ")"))) break;
      if (canon_ast_is_text(token, "<")) angle++;
      else if (canon_ast_is_text(token, ">") && angle > 0) angle--;
      else if (canon_ast_is_text(token, "[")) bracket++;
      else if (canon_ast_is_text(token, "]") && bracket > 0) bracket--;
      parser->pos++;
    }
    if (name) canon_push_param_ast(out, (Param){.name = z_strdup(name->text), .type = canon_parse_type_between(parser, type_start, parser->pos), .line = name->line, .column = name->column});
    if (canon_ast_accept(parser, ",")) continue;
    if (!canon_ast_is_text(canon_ast_peek(parser), ")")) return canon_ast_expect(parser, ")", "expected ')' after parameters");
  }
  return !canon_ast_has_diag(parser->diag);
}

static bool canon_ast_is_type_stop(const ZCanonicalToken *token, const char *stop_a, const char *stop_b, const char *stop_c) {
  return canon_ast_is_text(token, stop_a) || canon_ast_is_text(token, stop_b) || canon_ast_is_text(token, stop_c);
}

static char *canon_parse_type_until_any_ast(CanonAstParser *parser, const char *stop_a, const char *stop_b, const char *stop_c) {
  size_t start = parser->pos;
  int angle = 0, bracket = 0, paren = 0;
  while (parser->pos < parser->tokens->len) {
    const ZCanonicalToken *token = canon_ast_peek(parser);
    if (angle == 0 && bracket == 0 && paren == 0 && canon_ast_is_type_stop(token, stop_a, stop_b, stop_c)) break;
    if (token->kind == Z_CANON_TOKEN_EOF || token->kind == Z_CANON_TOKEN_NEWLINE || token->kind == Z_CANON_TOKEN_COMMENT) break;
    if (canon_ast_is_text(token, "<")) angle++;
    else if (canon_ast_is_text(token, ">") && angle > 0) angle--;
    else if (canon_ast_is_text(token, "[")) bracket++;
    else if (canon_ast_is_text(token, "]") && bracket > 0) bracket--;
    else if (canon_ast_is_text(token, "(")) paren++;
    else if (canon_ast_is_text(token, ")") && paren > 0) paren--;
    parser->pos++;
  }
  return canon_parse_type_between(parser, start, parser->pos);
}

static char *canon_parse_type_until_ast(CanonAstParser *parser, const char *stop_a, const char *stop_b) {
  return canon_parse_type_until_any_ast(parser, stop_a, stop_b, NULL);
}

static int canon_ast_line_or_one(const ZCanonicalToken *token) { return token ? token->line : 1; }
static int canon_ast_column_or_one(const ZCanonicalToken *token) { return token ? token->column : 1; }

static bool canon_parse_raises_ast(CanonAstParser *parser, Function *fun) {
  if (!canon_ast_accept(parser, "raises")) return true;
  fun->raises = true;
  if (!canon_ast_accept(parser, "[")) return true;
  fun->has_error_set = true;
  while (!canon_ast_has_diag(parser->diag) && !canon_ast_accept(parser, "]")) {
    const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected named error");
    if (name) canon_push_param_ast(&fun->errors, (Param){.name = z_strdup(name->text), .line = name->line, .column = name->column});
    if (canon_ast_accept(parser, ",")) continue;
    if (!canon_ast_is_text(canon_ast_peek(parser), "]")) return canon_ast_expect(parser, "]", "expected ']' after named error set");
  }
  return !canon_ast_has_diag(parser->diag);
}

static StmtVec canon_parse_block_ast(CanonAstParser *parser);
static Stmt *canon_parse_statement_ast(CanonAstParser *parser);

static Function canon_parse_signature_ast(CanonAstParser *parser, const ZCanonicalToken *start, bool is_public, bool export_c, bool has_body) {
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected function name");
  ZBuf full_name;
  zbuf_init(&full_name);
  if (name) zbuf_append(&full_name, name->text);
  while (canon_ast_accept(parser, ".")) {
    const ZCanonicalToken *part = canon_ast_expect_word(parser, "expected method name");
    zbuf_append_char(&full_name, '.');
    if (part) zbuf_append(&full_name, part->text);
  }
  Function fun = {.name = full_name.data ? full_name.data : z_strdup(""), .is_public = is_public, .export_c = export_c, .line = canon_ast_line_or_one(start ? start : name), .column = canon_ast_column_or_one(start ? start : name)};
  canon_parse_type_params_ast(parser, &fun.type_params);
  canon_parse_params_ast(parser, &fun.params);
  canon_ast_expect(parser, "->", "expected return type arrow");
  fun.return_type = canon_parse_type_until_ast(parser, "raises", "{");
  canon_parse_raises_ast(parser, &fun);
  if (has_body) fun.body = canon_parse_block_ast(parser);
  return fun;
}

static Stmt *canon_parse_binding_stmt(CanonAstParser *parser, const ZCanonicalToken *start, bool is_mutable) {
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected binding name");
  Stmt *stmt = canon_ast_new_stmt(STMT_LET, start);
  stmt->mutable_binding = is_mutable;
  if (name) stmt->name = z_strdup(name->text);
  canon_ast_expect(parser, ":", "expected binding type");
  size_t type_start = parser->pos;
  while (parser->pos < parser->tokens->len && !canon_ast_is_text(canon_ast_peek(parser), "=")) parser->pos++;
  stmt->type = canon_parse_type_between(parser, type_start, parser->pos);
  canon_ast_expect(parser, "=", "expected binding initializer");
  size_t end = canon_ast_line_end(parser->tokens, parser->pos);
  stmt->expr = canon_parse_expr_span(parser->tokens, parser->pos, end, parser->diag);
  parser->pos = end;
  return stmt;
}

static Stmt *canon_parse_if_after_keyword(CanonAstParser *parser, const ZCanonicalToken *start) {
  Stmt *stmt = canon_ast_new_stmt(STMT_IF, start);
  size_t open = canon_ast_block_open(parser->tokens, parser->pos);
  stmt->expr = canon_parse_expr_span(parser->tokens, parser->pos, open, parser->diag);
  parser->pos = open;
  stmt->then_body = canon_parse_block_ast(parser);
  size_t else_pos = canon_ast_skip_newline_pos(parser->tokens, parser->pos);
  if (canon_ast_token_text(parser->tokens, else_pos, parser->tokens->len, "else")) {
    parser->pos = else_pos + 1;
    if (canon_ast_accept(parser, "if")) {
      Stmt *else_if = canon_parse_if_after_keyword(parser, canon_ast_token(parser->tokens, else_pos));
      canon_push_stmt_ast(&stmt->else_body, else_if);
    } else {
      stmt->else_body = canon_parse_block_ast(parser);
    }
  }
  return stmt;
}

static Stmt *canon_parse_while_after_keyword(CanonAstParser *parser, const ZCanonicalToken *start) {
  Stmt *stmt = canon_ast_new_stmt(STMT_WHILE, start);
  size_t open = canon_ast_block_open(parser->tokens, parser->pos);
  stmt->expr = canon_parse_expr_span(parser->tokens, parser->pos, open, parser->diag);
  parser->pos = open;
  stmt->then_body = canon_parse_block_ast(parser);
  return stmt;
}

static Stmt *canon_parse_for_after_keyword(CanonAstParser *parser, const ZCanonicalToken *start) {
  Stmt *stmt = canon_ast_new_stmt(STMT_FOR, start);
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected loop binding name");
  if (name) stmt->name = z_strdup(name->text);
  canon_ast_expect(parser, "in", "expected 'in' in for loop");
  size_t open = canon_ast_block_open(parser->tokens, parser->pos);
  size_t range = canon_ast_find_top_level(parser->tokens, parser->pos, open, "..");
  if (range == CANON_AST_NO_INDEX) {
    canon_ast_fail(parser->diag, start, "expected '..' in range loop", "range expression", "missing '..'");
    stmt->expr = canon_parse_expr_span(parser->tokens, parser->pos, open, parser->diag);
  } else {
    stmt->expr = canon_parse_expr_span(parser->tokens, parser->pos, range, parser->diag);
    stmt->range_end = canon_parse_expr_span(parser->tokens, range + 1, open, parser->diag);
  }
  parser->pos = open;
  stmt->then_body = canon_parse_block_ast(parser);
  return stmt;
}

static MatchArm canon_parse_match_arm_ast(CanonAstParser *parser) {
  size_t pattern_start = parser->pos, open = canon_ast_block_open(parser->tokens, parser->pos);
  size_t guard = canon_ast_find_top_level(parser->tokens, pattern_start, open, "if");
  size_t pattern_end = guard == CANON_AST_NO_INDEX ? open : guard, range = canon_ast_find_top_level(parser->tokens, pattern_start, pattern_end, "..");
  MatchArm arm = {.line = parser->tokens->items[pattern_start].line, .column = parser->tokens->items[pattern_start].column};
  if (canon_ast_token_text(parser->tokens, pattern_start, pattern_end, ".")) {
    const ZCanonicalToken *case_name = canon_ast_token(parser->tokens, pattern_start + 1);
    arm.case_name = z_strdup(case_name && case_name->text ? case_name->text : "");
    if (pattern_start + 2 < pattern_end && canon_ast_token_text(parser->tokens, pattern_start + 2, pattern_end, "(")) {
      const ZCanonicalToken *payload = canon_ast_token(parser->tokens, pattern_start + 3);
      if (payload && payload->kind == Z_CANON_TOKEN_WORD) arm.payload_name = z_strdup(payload->text);
    }
  } else if (range != CANON_AST_NO_INDEX) {
    arm.case_name = canon_ast_join_line_tokens(parser->tokens, pattern_start, range);
    arm.range_end = canon_ast_join_line_tokens(parser->tokens, range + 1, pattern_end);
  } else {
    arm.case_name = canon_ast_join_line_tokens(parser->tokens, pattern_start, pattern_end);
  }
  if (guard != CANON_AST_NO_INDEX) arm.guard = canon_parse_expr_span(parser->tokens, guard + 1, open, parser->diag);
  parser->pos = open;
  arm.body = canon_parse_block_ast(parser);
  return arm;
}

static Stmt *canon_parse_match_after_keyword(CanonAstParser *parser, const ZCanonicalToken *start) {
  Stmt *stmt = canon_ast_new_stmt(STMT_MATCH, start);
  size_t open = canon_ast_block_open(parser->tokens, parser->pos);
  stmt->expr = canon_parse_expr_span(parser->tokens, parser->pos, open, parser->diag);
  parser->pos = open + 1;
  canon_ast_skip_newlines(parser);
  while (!canon_ast_has_diag(parser->diag) && !canon_ast_accept(parser, "}")) {
    canon_push_match_arm_ast(&stmt->match_arms, canon_parse_match_arm_ast(parser));
    canon_ast_skip_newlines(parser);
  }
  return stmt;
}

static Stmt *canon_parse_return_stmt(CanonAstParser *parser, const ZCanonicalToken *start) {
  Stmt *stmt = canon_ast_new_stmt(STMT_RETURN, start);
  size_t end = canon_ast_line_end(parser->tokens, parser->pos);
  if (parser->pos < end) stmt->expr = canon_parse_expr_span(parser->tokens, parser->pos, end, parser->diag);
  parser->pos = end;
  return stmt;
}

static Stmt *canon_parse_expect_stmt(CanonAstParser *parser, const ZCanonicalToken *start) {
  Stmt *stmt = canon_ast_new_stmt(STMT_EXPR, start);
  size_t end = canon_ast_line_end(parser->tokens, parser->pos);
  Expr *callee = canon_ast_new_expr(EXPR_IDENT, start);
  callee->text = z_strdup("expect");
  Expr *call = canon_ast_new_expr(EXPR_CALL, start);
  call->left = callee;
  canon_push_expr_ast(&call->args, canon_parse_expr_span(parser->tokens, parser->pos, end, parser->diag));
  stmt->expr = call;
  parser->pos = end;
  return stmt;
}

static Stmt *canon_parse_simple_stmt(CanonAstParser *parser, const ZCanonicalToken *start, StmtKind kind) {
  Stmt *stmt = canon_ast_new_stmt(kind, start);
  size_t end = canon_ast_line_end(parser->tokens, parser->pos);
  if (parser->pos < end) stmt->expr = canon_parse_expr_span(parser->tokens, parser->pos, end, parser->diag);
  parser->pos = end;
  return stmt;
}

static Stmt *canon_parse_raise_stmt(CanonAstParser *parser, const ZCanonicalToken *start) {
  Stmt *stmt = canon_ast_new_stmt(STMT_RAISE, start);
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected error name after raise");
  if (name) stmt->name = z_strdup(name->text);
  parser->pos = canon_ast_line_end(parser->tokens, parser->pos);
  return stmt;
}

static Stmt *canon_parse_assignment_or_expr_stmt(CanonAstParser *parser, const ZCanonicalToken *start) {
  size_t end = canon_ast_line_end(parser->tokens, parser->pos);
  size_t equals = canon_ast_find_top_level(parser->tokens, parser->pos, end, "=");
  if (equals != CANON_AST_NO_INDEX) {
    Stmt *stmt = canon_ast_new_stmt(STMT_ASSIGN, start);
    stmt->target = canon_parse_expr_span(parser->tokens, parser->pos, equals, parser->diag);
    stmt->expr = canon_parse_expr_span(parser->tokens, equals + 1, end, parser->diag);
    parser->pos = end;
    return stmt;
  }
  Stmt *stmt = canon_ast_new_stmt(STMT_EXPR, start);
  stmt->expr = canon_parse_expr_span(parser->tokens, parser->pos, end, parser->diag);
  parser->pos = end;
  return stmt;
}

static Stmt *canon_parse_statement_ast(CanonAstParser *parser) {
  canon_ast_skip_newlines(parser);
  const ZCanonicalToken *start = canon_ast_peek(parser);
  if (!start || start->kind == Z_CANON_TOKEN_EOF || canon_ast_is_text(start, "}")) return NULL;
  if (canon_ast_accept(parser, "let")) return canon_parse_binding_stmt(parser, start, false);
  if (canon_ast_accept(parser, "var")) return canon_parse_binding_stmt(parser, start, true);
  if (canon_ast_accept(parser, "return")) return canon_parse_return_stmt(parser, start);
  if (canon_ast_accept(parser, "check")) return canon_parse_simple_stmt(parser, start, STMT_CHECK);
  if (canon_ast_accept(parser, "expect")) return canon_parse_expect_stmt(parser, start);
  if (canon_ast_accept(parser, "raise")) return canon_parse_raise_stmt(parser, start);
  if (canon_ast_accept(parser, "if")) return canon_parse_if_after_keyword(parser, start);
  if (canon_ast_accept(parser, "while")) return canon_parse_while_after_keyword(parser, start);
  if (canon_ast_accept(parser, "for")) return canon_parse_for_after_keyword(parser, start);
  if (canon_ast_accept(parser, "match")) return canon_parse_match_after_keyword(parser, start);
  if (canon_ast_accept(parser, "break")) return canon_ast_new_stmt(STMT_BREAK, start);
  if (canon_ast_accept(parser, "continue")) return canon_ast_new_stmt(STMT_CONTINUE, start);
  if (canon_ast_accept(parser, "defer")) return canon_parse_simple_stmt(parser, start, STMT_DEFER);
  return canon_parse_assignment_or_expr_stmt(parser, start);
}

static StmtVec canon_parse_block_ast(CanonAstParser *parser) {
  StmtVec body = {0};
  if (!canon_ast_expect(parser, "{", "expected block")) return body;
  canon_ast_skip_newlines(parser);
  while (!canon_ast_has_diag(parser->diag) && !canon_ast_accept(parser, "}")) {
    Stmt *stmt = canon_parse_statement_ast(parser);
    if (stmt) canon_push_stmt_ast(&body, stmt);
    canon_ast_skip_newlines(parser);
  }
  return body;
}

static void canon_parse_field_list_ast(CanonAstParser *parser, ParamVec *out, bool typed_fields) {
  if (!canon_ast_expect(parser, "{", "expected declaration body")) return;
  canon_ast_skip_newlines(parser);
  while (!canon_ast_has_diag(parser->diag) && !canon_ast_accept(parser, "}")) {
    const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected field or variant name");
    char *type = NULL;
    if (typed_fields && canon_ast_accept(parser, ":")) {
      type = canon_parse_type_until_ast(parser, ",", "}");
    }
    if (name) canon_push_param_ast(out, (Param){.name = z_strdup(name->text), .type = type, .line = name->line, .column = name->column});
    canon_ast_expect(parser, ",", "expected trailing comma in declaration list");
    canon_ast_skip_newlines(parser);
  }
}

static void canon_parse_type_field_ast(CanonAstParser *parser, ParamVec *out) {
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected field or method name");
  canon_ast_expect(parser, ":", "expected ':' after field name");
  char *type = canon_parse_type_until_any_ast(parser, "=", ",", "}");
  Expr *default_value = NULL;
  if (canon_ast_accept(parser, "=")) {
    size_t value_start = parser->pos;
    size_t value_end = canon_ast_find_top_level_separator(parser->tokens, value_start, parser->tokens->len, ",");
    if (value_end == CANON_AST_NO_INDEX) value_end = canon_ast_find_top_level(parser->tokens, value_start, parser->tokens->len, "}");
    if (value_end == CANON_AST_NO_INDEX) value_end = canon_ast_line_end(parser->tokens, value_start);
    default_value = canon_parse_expr_span(parser->tokens, value_start, value_end, parser->diag);
    parser->pos = value_end;
  }
  if (name) {
    canon_push_param_ast(out, (Param){
      .name = z_strdup(name->text),
      .type = type,
      .default_value = default_value,
      .line = name->line,
      .column = name->column
    });
  } else {
    free(type);
    (void)default_value;
  }
  canon_ast_expect(parser, ",", "expected trailing comma in type field");
}

static void canon_parse_type_method_ast(CanonAstParser *parser, Shape *shape) {
  const ZCanonicalToken *start = canon_ast_peek(parser);
  bool is_public = canon_ast_accept(parser, "pub");
  canon_ast_expect(parser, "fn", is_public ? "expected public type method" : "expected type method");
  canon_push_function_ast(&shape->methods, canon_parse_signature_ast(parser, start, is_public, false, true));
}

static void canon_parse_type_body_ast(CanonAstParser *parser, Shape *shape) {
  if (!canon_ast_expect(parser, "{", "expected type body")) return;
  canon_ast_skip_newlines(parser);
  while (!canon_ast_has_diag(parser->diag) && !canon_ast_accept(parser, "}")) {
    const ZCanonicalToken *token = canon_ast_peek(parser);
    if (canon_ast_is_text(token, "fn") || canon_ast_is_text(token, "pub")) canon_parse_type_method_ast(parser, shape);
    else canon_parse_type_field_ast(parser, &shape->fields);
    canon_ast_skip_newlines(parser);
  }
}

static void canon_parse_type_decl_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start, bool is_public, const char *layout) {
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected type name");
  Shape shape = {.layout = z_strdup(layout ? layout : "auto"), .is_public = is_public, .line = canon_ast_line_or_one(start ? start : name), .column = canon_ast_column_or_one(start ? start : name)};
  if (name) shape.name = z_strdup(name->text);
  canon_parse_type_params_ast(parser, &shape.type_params);
  if (canon_ast_is_text(canon_ast_peek(parser), "{")) canon_parse_type_body_ast(parser, &shape);
  canon_push_shape_ast(&program->shapes, shape);
}

static void canon_parse_enum_decl_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start) {
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected enum name");
  EnumDecl item = {.line = canon_ast_line_or_one(start ? start : name), .column = canon_ast_column_or_one(start ? start : name)};
  if (name) item.name = z_strdup(name->text);
  if (canon_ast_is_text(canon_ast_peek(parser), "<")) {
    canon_ast_fail(parser->diag, canon_ast_peek(parser), "enum declarations do not support generic parameters", "enum name or storage type", "<");
    free(item.name);
    return;
  }
  if (canon_ast_accept(parser, ":")) item.type = canon_parse_type_until_ast(parser, "{", NULL);
  canon_parse_field_list_ast(parser, &item.cases, false);
  canon_push_enum_ast(&program->enums, item);
}

static void canon_parse_choice_decl_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start) {
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected choice name");
  Choice item = {.line = canon_ast_line_or_one(start ? start : name), .column = canon_ast_column_or_one(start ? start : name)};
  if (name) item.name = z_strdup(name->text);
  if (canon_ast_is_text(canon_ast_peek(parser), "<")) {
    canon_ast_fail(parser->diag, canon_ast_peek(parser), "choice declarations do not support generic parameters", "choice name or body", "<");
    free(item.name);
    return;
  }
  canon_parse_field_list_ast(parser, &item.cases, true);
  canon_push_choice_ast(&program->choices, item);
}

static void canon_parse_interface_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start, bool is_public) {
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected interface name");
  InterfaceDecl item = {.is_public = is_public, .line = canon_ast_line_or_one(start ? start : name), .column = canon_ast_column_or_one(start ? start : name)};
  if (name) item.name = z_strdup(name->text);
  canon_parse_type_params_ast(parser, &item.type_params);
  canon_ast_expect(parser, "{", "expected interface body");
  canon_ast_skip_newlines(parser);
  while (!canon_ast_has_diag(parser->diag) && !canon_ast_accept(parser, "}")) {
    const ZCanonicalToken *method_start = canon_ast_peek(parser);
    canon_ast_expect(parser, "fn", "expected interface method");
    canon_push_function_ast(&item.methods, canon_parse_signature_ast(parser, method_start, false, false, false));
    canon_ast_skip_newlines(parser);
  }
  canon_push_interface_ast(&program->interfaces, item);
}

static void canon_parse_use_decl_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start) {
  size_t module_start = parser->pos;
  while (parser->pos < parser->tokens->len && !canon_ast_is_text(canon_ast_peek(parser), "as") &&
         canon_ast_peek(parser)->kind != Z_CANON_TOKEN_NEWLINE && canon_ast_peek(parser)->kind != Z_CANON_TOKEN_COMMENT &&
         canon_ast_peek(parser)->kind != Z_CANON_TOKEN_EOF) {
    parser->pos++;
  }
  size_t module_end = parser->pos;
  char *alias = NULL;
  int end_column = module_end > module_start ? parser->tokens->items[module_end - 1].column + (int)parser->tokens->items[module_end - 1].length : 1;
  if (canon_ast_accept(parser, "as")) {
    const ZCanonicalToken *alias_token = canon_ast_expect_word(parser, "expected import alias");
    if (alias_token) {
      alias = z_strdup(alias_token->text);
      end_column = alias_token->column + (int)alias_token->length;
    }
  }
  canon_push_use_ast(&program->use_imports, (UseImport){
    .module = canon_ast_join_line_tokens(parser->tokens, module_start, module_end),
    .alias = alias,
    .line = start ? start->line : 1,
    .column = start ? start->column : 1,
    .end_column = end_column
  });
}

static void canon_parse_const_decl_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start, bool is_public) {
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected const name");
  ConstDecl item = {.is_public = is_public, .line = canon_ast_line_or_one(start ? start : name), .column = canon_ast_column_or_one(start ? start : name)};
  if (name) item.name = z_strdup(name->text);
  canon_ast_expect(parser, ":", "expected const type");
  size_t type_start = parser->pos;
  while (parser->pos < parser->tokens->len && !canon_ast_is_text(canon_ast_peek(parser), "=")) parser->pos++;
  item.type = canon_parse_type_between(parser, type_start, parser->pos);
  canon_ast_expect(parser, "=", "expected const value");
  size_t end = canon_ast_line_end(parser->tokens, parser->pos);
  item.expr = canon_parse_expr_span(parser->tokens, parser->pos, end, parser->diag);
  parser->pos = end;
  canon_push_const_ast(&program->consts, item);
}

static void canon_parse_alias_decl_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start, bool is_public) {
  const ZCanonicalToken *name = canon_ast_expect_word(parser, "expected alias name");
  TypeAlias item = {.is_public = is_public, .line = canon_ast_line_or_one(start ? start : name), .column = canon_ast_column_or_one(start ? start : name)};
  if (name) item.name = z_strdup(name->text);
  canon_ast_expect(parser, "=", "expected alias target");
  size_t end = canon_ast_line_end(parser->tokens, parser->pos);
  item.target = canon_parse_type_between(parser, parser->pos, end);
  parser->pos = end;
  canon_push_alias_ast(&program->aliases, item);
}

static void canon_parse_c_import_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start) {
  const ZCanonicalToken *header = canon_ast_peek(parser);
  if (!header || header->kind != Z_CANON_TOKEN_STRING) {
    canon_ast_fail(parser->diag, header, "expected C header string", "string", header ? header->text : "end of file");
    return;
  }
  parser->pos++;
  canon_ast_expect(parser, "as", "expected C import alias");
  const ZCanonicalToken *alias = canon_ast_expect_word(parser, "expected C import alias name");
  char *header_text = canon_ast_decode_string_literal(header, parser->diag);
  if (alias) {
    canon_push_c_import_ast(&program->c_imports, (CImport){.header = header_text, .alias = z_strdup(alias->text), .line = canon_ast_line_or_one(start ? start : header), .column = canon_ast_column_or_one(start ? start : header)});
  } else {
    free(header_text);
  }
}

static void canon_parse_test_decl_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start) {
  const ZCanonicalToken *name = canon_ast_peek(parser);
  if (!name || name->kind != Z_CANON_TOKEN_STRING) {
    canon_ast_fail(parser->diag, name, "expected test name", "string", name ? name->text : "end of file");
    return;
  }
  parser->pos++;
  ZBuf generated;
  zbuf_init(&generated);
  zbuf_appendf(&generated, "__zero_test_%zu", parser->test_counter++);
  Function fun = {.name = generated.data, .test_name = canon_ast_decode_string_literal(name, parser->diag), .return_type = z_strdup("Void"), .is_test = true, .line = canon_ast_line_or_one(start ? start : name), .column = canon_ast_column_or_one(start ? start : name)};
  fun.body = canon_parse_block_ast(parser);
  canon_push_function_ast(&program->functions, fun);
}

static void canon_parse_decl_after_pub_ast(CanonAstParser *parser, Program *program, const ZCanonicalToken *start, bool is_public) {
  if (canon_ast_accept(parser, "fn")) canon_push_function_ast(&program->functions, canon_parse_signature_ast(parser, start, is_public, false, true));
  else if (canon_ast_accept(parser, "type")) canon_parse_type_decl_ast(parser, program, start, is_public, NULL);
  else if (canon_ast_accept(parser, "packed")) { canon_ast_expect(parser, "type", "expected packed type declaration"); canon_parse_type_decl_ast(parser, program, start, is_public, "packed"); }
  else if (canon_ast_accept(parser, "extern")) {
    if (canon_ast_accept(parser, "c")) canon_parse_c_import_ast(parser, program, start);
    else { canon_ast_expect(parser, "type", "expected extern type declaration"); canon_parse_type_decl_ast(parser, program, start, is_public, "extern"); }
  }
  else if (canon_ast_accept(parser, "enum")) canon_parse_enum_decl_ast(parser, program, start);
  else if (canon_ast_accept(parser, "choice")) canon_parse_choice_decl_ast(parser, program, start);
  else if (canon_ast_accept(parser, "interface")) canon_parse_interface_ast(parser, program, start, is_public);
  else if (canon_ast_accept(parser, "alias")) canon_parse_alias_decl_ast(parser, program, start, is_public);
  else if (canon_ast_accept(parser, "const")) canon_parse_const_decl_ast(parser, program, start, is_public);
  else canon_ast_fail(parser->diag, canon_ast_peek(parser), "expected public declaration", "declaration", canon_ast_peek(parser) ? canon_ast_peek(parser)->text : "end of file");
}

static void canon_parse_declaration_ast(CanonAstParser *parser, Program *program) {
  canon_ast_skip_newlines(parser);
  if (!canon_ast_peek(parser) || canon_ast_peek(parser)->kind == Z_CANON_TOKEN_EOF) return;
  const ZCanonicalToken *start = canon_ast_peek(parser);
  if (canon_ast_accept(parser, "pub")) canon_parse_decl_after_pub_ast(parser, program, start, true);
  else if (canon_ast_accept(parser, "export")) { canon_ast_expect(parser, "c", "expected export c fn"); canon_ast_expect(parser, "fn", "expected export c fn"); canon_push_function_ast(&program->functions, canon_parse_signature_ast(parser, start, false, true, true)); }
  else if (canon_ast_accept(parser, "fn")) canon_push_function_ast(&program->functions, canon_parse_signature_ast(parser, start, false, false, true));
  else if (canon_ast_accept(parser, "type")) canon_parse_type_decl_ast(parser, program, start, false, NULL);
  else if (canon_ast_accept(parser, "packed")) { canon_ast_expect(parser, "type", "expected packed type declaration"); canon_parse_type_decl_ast(parser, program, start, false, "packed"); }
  else if (canon_ast_accept(parser, "extern")) {
    if (canon_ast_accept(parser, "c")) canon_parse_c_import_ast(parser, program, start);
    else { canon_ast_expect(parser, "type", "expected extern type declaration"); canon_parse_type_decl_ast(parser, program, start, false, "extern"); }
  } else if (canon_ast_accept(parser, "enum")) canon_parse_enum_decl_ast(parser, program, start);
  else if (canon_ast_accept(parser, "choice")) canon_parse_choice_decl_ast(parser, program, start);
  else if (canon_ast_accept(parser, "interface")) canon_parse_interface_ast(parser, program, start, false);
  else if (canon_ast_accept(parser, "test")) canon_parse_test_decl_ast(parser, program, start);
  else if (canon_ast_accept(parser, "use")) canon_parse_use_decl_ast(parser, program, start);
  else if (canon_ast_accept(parser, "alias")) canon_parse_alias_decl_ast(parser, program, start, false);
  else if (canon_ast_accept(parser, "const")) canon_parse_const_decl_ast(parser, program, start, false);
  else canon_ast_fail(parser->diag, canon_ast_peek(parser), "expected canonical declaration", "declaration", canon_ast_peek(parser) ? canon_ast_peek(parser)->text : "end of file");
}

Program z_parse_canonical_text_program(const ZCanonicalTokenVec *tokens, ZDiag *diag) {
  ZDiag local_diag = {0};
  ZDiag *active_diag = diag ? diag : &local_diag;
  Program program = {0};
  if (!tokens) {
    canon_ast_fail(active_diag, NULL, "missing canonical text token stream", "tokens", "missing");
    return program;
  }
  ZCanonicalTree tree = {0};
  ZCanonicalFacts facts = {0};
  if (!z_canonical_text_parse(tokens, &tree, &facts, active_diag)) {
    z_free_canonical_text_tree(&tree);
    return program;
  }
  z_free_canonical_text_tree(&tree);
  CanonAstParser parser = {.tokens = tokens, .diag = active_diag};
  canon_ast_skip_newlines(&parser);
  while (!canon_ast_has_diag(active_diag) && canon_ast_peek(&parser) && canon_ast_peek(&parser)->kind != Z_CANON_TOKEN_EOF) {
    canon_parse_declaration_ast(&parser, &program);
    canon_ast_skip_newlines(&parser);
  }
  if (canon_ast_has_diag(active_diag)) z_free_program(&program);
  return program;
}

bool z_parse_canonical_text_program_source(const char *source, Program *out, ZDiag *diag) {
  ZDiag local_diag = {0};
  ZDiag *active_diag = diag ? diag : &local_diag;
  if (!out) return canon_ast_fail(active_diag, NULL, "missing canonical text program output", "Program output", "missing");
  *out = (Program){0};
  ZCanonicalTokenVec tokens = z_canonical_text_tokenize(source, active_diag);
  if (canon_ast_has_diag(active_diag)) {
    z_free_canonical_text_tokens(&tokens);
    return false;
  }
  *out = z_parse_canonical_text_program(&tokens, active_diag);
  z_free_canonical_text_tokens(&tokens);
  return !canon_ast_has_diag(active_diag);
}
