#include "zero.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  size_t *items;
  size_t len;
  size_t cap;
} RowIndentStack;

typedef struct {
  const ZRowTokenVec *tokens;
  size_t pos;
  size_t end;
  ZDiag *diag;
} RowExprParser;

static void row_push_token(ZRowTokenVec *vec, ZRowToken token) {
  if (vec->len + 1 > vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 64);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(ZRowToken));
  }
  vec->items[vec->len++] = token;
}

static void row_push_node(ZRowTree *tree, ZRowNode node) {
  if (tree->len + 1 > tree->cap) {
    tree->cap = z_grow_capacity(tree->cap, tree->len + 1, 32);
    tree->items = z_checked_reallocarray(tree->items, tree->cap, sizeof(ZRowNode));
  }
  tree->items[tree->len++] = node;
}

static void *row_grow_items(void *items, size_t len, size_t *cap, size_t initial, size_t item_size) {
  if (len + 1 > *cap) {
    *cap = z_grow_capacity(*cap, len + 1, initial);
    return z_checked_reallocarray(items, *cap, item_size);
  }
  return items;
}

static void row_push_function(FunctionVec *vec, Function item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 8, sizeof(Function));
  vec->items[vec->len++] = item;
}

static void row_push_stmt(StmtVec *vec, Stmt *item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 8, sizeof(Stmt *));
  vec->items[vec->len++] = item;
}

static void row_push_expr(ExprVec *vec, Expr *item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Expr *));
  vec->items[vec->len++] = item;
}

static void row_push_type_arg(TypeArgVec *vec, TypeArg item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(TypeArg));
  vec->items[vec->len++] = item;
}

static void row_push_param(ParamVec *vec, Param item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Param));
  vec->items[vec->len++] = item;
}

static void row_push_field(FieldInitVec *vec, FieldInit item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(FieldInit));
  vec->items[vec->len++] = item;
}

static void row_push_use(UseImportVec *vec, UseImport item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(UseImport));
  vec->items[vec->len++] = item;
}

static void row_push_const(ConstVec *vec, ConstDecl item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(ConstDecl));
  vec->items[vec->len++] = item;
}

static void row_push_shape(ShapeVec *vec, Shape item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Shape));
  vec->items[vec->len++] = item;
}

static void row_push_enum(EnumVec *vec, EnumDecl item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(EnumDecl));
  vec->items[vec->len++] = item;
}

static void row_push_choice(ChoiceVec *vec, Choice item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Choice));
  vec->items[vec->len++] = item;
}

static void row_push_match_arm(MatchArmVec *vec, MatchArm item) {
  vec->items = row_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(MatchArm));
  vec->items[vec->len++] = item;
}

static void row_push_indent(RowIndentStack *stack, size_t indent) {
  if (stack->len + 1 > stack->cap) {
    stack->cap = z_grow_capacity(stack->cap, stack->len + 1, 16);
    stack->items = z_checked_reallocarray(stack->items, stack->cap, sizeof(size_t));
  }
  stack->items[stack->len++] = indent;
}

static ZRowToken row_token(ZRowTokenKind kind, char *text, int line, int column, size_t offset, size_t length) {
  return (ZRowToken){kind, text, line, column, offset, length};
}

static void row_diag(ZDiag *diag, int line, int column, int length, const char *message, const char *expected, const char *help) {
  if (!diag) return;
  if (diag->code != 0) return;
  diag->code = 100;
  diag->line = line;
  diag->column = column;
  diag->length = length > 0 ? length : 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "invalid row syntax");
  if (expected) snprintf(diag->expected, sizeof(diag->expected), "%s", expected);
  if (help) snprintf(diag->help, sizeof(diag->help), "%s", help);
}

static bool row_has_diag(const ZDiag *diag) {
  return diag && diag->code != 0;
}

static int row_hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

static void row_append_char(char **data, size_t *len, size_t *cap, char ch) {
  if (*len + 1 >= *cap) {
    *cap = z_grow_capacity(*cap, *len + 2, 16);
    *data = z_checked_reallocarray(*data, *cap, sizeof(char));
  }
  (*data)[(*len)++] = ch;
  (*data)[*len] = 0;
}

static bool row_two_char_symbol(const char *source, size_t offset) {
  const char *symbols[] = {"->", "=>", "..", "==", "!=", "<=", ">=", "&&", "||", "+%", "+|", NULL};
  for (int i = 0; symbols[i]; i++) {
    if (source[offset] == symbols[i][0] && source[offset + 1] == symbols[i][1]) return true;
  }
  return false;
}

static bool row_newline_at(const char *source, size_t offset, size_t *width) {
  if (source[offset] == '\r' && source[offset + 1] == '\n') {
    *width = 2;
    return true;
  }
  if (source[offset] == '\n' || source[offset] == '\r') {
    *width = 1;
    return true;
  }
  return false;
}

static bool row_scan_string(const char *source, size_t *offset, int *column, ZRowTokenVec *tokens, ZDiag *diag, int line) {
  size_t start = *offset;
  int start_column = *column;
  (*offset)++;
  (*column)++;
  char *value = NULL;
  size_t len = 0;
  size_t cap = 0;

  while (source[*offset] && source[*offset] != '"') {
    size_t newline_width = 0;
    if (row_newline_at(source, *offset, &newline_width)) {
      free(value);
      row_diag(diag, line, start_column, 1, "unterminated string literal", "closing quote", NULL);
      return false;
    }
    char next = source[(*offset)++];
    (*column)++;
    if (next == '\\') {
      char escaped = source[(*offset)++];
      (*column)++;
      if (!escaped) {
        free(value);
        row_diag(diag, line, start_column, 1, "unterminated string literal", "escape target", NULL);
        return false;
      }
      if (escaped == '\n' || escaped == '\r') {
        free(value);
        row_diag(diag, line, start_column, 1, "unterminated string literal", "closing quote", NULL);
        return false;
      }
      if (escaped == 'n') row_append_char(&value, &len, &cap, '\n');
      else if (escaped == 'r') row_append_char(&value, &len, &cap, '\r');
      else if (escaped == 't') row_append_char(&value, &len, &cap, '\t');
      else row_append_char(&value, &len, &cap, escaped);
    } else {
      row_append_char(&value, &len, &cap, next);
    }
  }

  if (source[*offset] != '"') {
    free(value);
    row_diag(diag, line, start_column, 1, "unterminated string literal", "closing quote", NULL);
    return false;
  }
  (*offset)++;
  (*column)++;
  if (!value) value = z_strdup("");
  row_push_token(tokens, row_token(Z_ROW_TOKEN_STRING, value, line, start_column, start, *offset - start));
  return true;
}

static bool row_scan_char(const char *source, size_t *offset, int *column, ZRowTokenVec *tokens, ZDiag *diag, int line) {
  size_t start = *offset;
  int start_column = *column;
  (*offset)++;
  (*column)++;
  unsigned value = 0;

  if (!source[*offset] || source[*offset] == '\n' || source[*offset] == '\r' || source[*offset] == '\'') {
    row_diag(diag, line, start_column, 1, "malformed character literal", "one byte character literal", "use a single ASCII byte or an escape like '\\n', '\\\\', '\\'', or '\\x41'");
    return false;
  }

  if (source[*offset] == '\\') {
    (*offset)++;
    (*column)++;
    char escaped = source[*offset];
    if (!escaped || escaped == '\n' || escaped == '\r') {
      row_diag(diag, line, start_column, 1, "malformed character escape", "one byte character literal", "use a single ASCII byte or an escape like '\\n', '\\\\', '\\'', or '\\x41'");
      return false;
    }
    if (escaped == 'n') value = '\n';
    else if (escaped == 'r') value = '\r';
    else if (escaped == 't') value = '\t';
    else if (escaped == '0') value = '\0';
    else if (escaped == '\'') value = '\'';
    else if (escaped == '"') value = '"';
    else if (escaped == '\\') value = '\\';
    else if (escaped == 'x') {
      char high_ch = source[*offset + 1];
      char low_ch = high_ch ? source[*offset + 2] : 0;
      int high = row_hex_digit(high_ch);
      int low = row_hex_digit(low_ch);
      if (high < 0 || low < 0) {
        row_diag(diag, line, start_column, 1, "malformed hex character escape", "one byte character literal", "use an escape like '\\x41'");
        return false;
      }
      value = (unsigned)((high << 4) | low);
      *offset += 2;
      *column += 2;
    } else {
      row_diag(diag, line, start_column, 1, "unsupported character escape", "one byte character literal", "use a single ASCII byte or an escape like '\\n', '\\\\', '\\'', or '\\x41'");
      return false;
    }
    (*offset)++;
    (*column)++;
  } else {
    unsigned char byte = (unsigned char)source[*offset];
    if (byte >= 128) {
      row_diag(diag, line, start_column, 1, "character literal must be one byte", "ASCII byte", NULL);
      return false;
    }
    value = byte;
    (*offset)++;
    (*column)++;
  }

  if (source[*offset] != '\'') {
    row_diag(diag, line, start_column, 1, "character literal must contain exactly one byte", "closing quote", NULL);
    return false;
  }
  (*offset)++;
  (*column)++;
  char text[4];
  snprintf(text, sizeof(text), "%u", value);
  row_push_token(tokens, row_token(Z_ROW_TOKEN_CHAR, z_strdup(text), line, start_column, start, *offset - start));
  return true;
}

ZRowTokenVec z_row_tokenize(const char *source, ZDiag *diag) {
  ZRowTokenVec tokens = {0};
  RowIndentStack indents = {0};
  row_push_indent(&indents, 0);

  size_t offset = 0;
  int line = 1;
  int column = 1;
  bool at_line_start = true;

  while (source && source[offset]) {
    if (at_line_start) {
      size_t indent = 0;
      size_t indent_start = offset;
      while (source[offset] == ' ' || source[offset] == '\t') {
        if (source[offset] == '\t') {
          row_diag(diag, line, column, 1, "tabs are not valid row indentation", "spaces", "use two spaces per indentation level");
          free(indents.items);
          return tokens;
        }
        offset++;
        column++;
        indent++;
      }

      size_t newline_width = 0;
      if (row_newline_at(source, offset, &newline_width)) {
        row_push_token(&tokens, row_token(Z_ROW_TOKEN_NEWLINE, z_strdup(""), line, column, offset, newline_width));
        offset += newline_width;
        line++;
        column = 1;
        continue;
      }

      if (source[offset] == 0) break;

      if (indent % 2 != 0) {
        row_diag(diag, line, 1, (int)(offset - indent_start), "row indentation must use two-space steps", "a multiple of two spaces", NULL);
        free(indents.items);
        return tokens;
      }

      if (source[offset] == '#') {
        size_t start = offset;
        int start_column = column;
        while (source[offset] && !row_newline_at(source, offset, &newline_width)) {
          offset++;
          column++;
        }
        row_push_token(&tokens, row_token(Z_ROW_TOKEN_COMMENT, z_strndup(source + start, offset - start), line, start_column, start, offset - start));
        if (row_newline_at(source, offset, &newline_width)) {
          row_push_token(&tokens, row_token(Z_ROW_TOKEN_NEWLINE, z_strdup(""), line, column, offset, newline_width));
          offset += newline_width;
          line++;
          column = 1;
        }
        continue;
      }

      size_t current_indent = indents.items[indents.len - 1];
      if (indent > current_indent) {
        if (indent != current_indent + 2) {
          row_diag(diag, line, 1, (int)(offset - indent_start), "row indentation may only increase by one level", "two more spaces than the parent row", NULL);
          free(indents.items);
          return tokens;
        }
        row_push_indent(&indents, indent);
        row_push_token(&tokens, row_token(Z_ROW_TOKEN_INDENT, z_strdup(""), line, 1, indent_start, indent));
      } else if (indent < current_indent) {
        while (indents.len > 1 && indent < indents.items[indents.len - 1]) {
          indents.len--;
          row_push_token(&tokens, row_token(Z_ROW_TOKEN_DEDENT, z_strdup(""), line, 1, indent_start, indent));
        }
        if (indent != indents.items[indents.len - 1]) {
          row_diag(diag, line, 1, (int)(offset - indent_start), "row indentation does not match an open block", "an existing indentation level", NULL);
          free(indents.items);
          return tokens;
        }
      }
      at_line_start = false;
    }

    size_t newline_width = 0;
    if (row_newline_at(source, offset, &newline_width)) {
      row_push_token(&tokens, row_token(Z_ROW_TOKEN_NEWLINE, z_strdup(""), line, column, offset, newline_width));
      offset += newline_width;
      line++;
      column = 1;
      at_line_start = true;
      continue;
    }

    char ch = source[offset];
    if (ch == ' ') {
      offset++;
      column++;
      continue;
    }
    if (ch == '\t') {
      row_diag(diag, line, column, 1, "tabs are not valid row whitespace", "spaces", "use spaces between row tokens");
      free(indents.items);
      return tokens;
    }
    if (ch == '#') {
      size_t start = offset;
      int start_column = column;
      while (source[offset] && !row_newline_at(source, offset, &newline_width)) {
        offset++;
        column++;
      }
      row_push_token(&tokens, row_token(Z_ROW_TOKEN_COMMENT, z_strndup(source + start, offset - start), line, start_column, start, offset - start));
      continue;
    }
    if (isalpha((unsigned char)ch) || ch == '_') {
      size_t start = offset;
      int start_column = column;
      while (isalnum((unsigned char)source[offset]) || source[offset] == '_') {
        offset++;
        column++;
      }
      row_push_token(&tokens, row_token(Z_ROW_TOKEN_WORD, z_strndup(source + start, offset - start), line, start_column, start, offset - start));
      continue;
    }
    if (isdigit((unsigned char)ch)) {
      size_t start = offset;
      int start_column = column;
      while (isalnum((unsigned char)source[offset]) || source[offset] == '_' || source[offset] == '.' ||
             ((source[offset] == '+' || source[offset] == '-') && offset > start && (source[offset - 1] == 'e' || source[offset - 1] == 'E'))) {
        if (source[offset] == '.' && source[offset + 1] == '.') break;
        offset++;
        column++;
      }
      row_push_token(&tokens, row_token(Z_ROW_TOKEN_NUMBER, z_strndup(source + start, offset - start), line, start_column, start, offset - start));
      continue;
    }
    if (ch == '"') {
      if (!row_scan_string(source, &offset, &column, &tokens, diag, line)) {
        free(indents.items);
        return tokens;
      }
      continue;
    }
    if (ch == '\'') {
      if (!row_scan_char(source, &offset, &column, &tokens, diag, line)) {
        free(indents.items);
        return tokens;
      }
      continue;
    }
    if (row_two_char_symbol(source, offset)) {
      row_push_token(&tokens, row_token(Z_ROW_TOKEN_SYMBOL, z_strndup(source + offset, 2), line, column, offset, 2));
      offset += 2;
      column += 2;
      continue;
    }
    if (strchr("()[]{}.,:;<>=+-*/%!&", ch)) {
      row_push_token(&tokens, row_token(Z_ROW_TOKEN_SYMBOL, z_strndup(source + offset, 1), line, column, offset, 1));
      offset++;
      column++;
      continue;
    }

    row_diag(diag, line, column, 1, "unexpected character in row syntax", "row token", NULL);
    free(indents.items);
    return tokens;
  }

  while (indents.len > 1) {
    indents.len--;
    row_push_token(&tokens, row_token(Z_ROW_TOKEN_DEDENT, z_strdup(""), line, column, offset, 0));
  }
  row_push_token(&tokens, row_token(Z_ROW_TOKEN_EOF, z_strdup(""), line, column, offset, 0));
  free(indents.items);

  if (row_has_diag(diag)) return tokens;
  return tokens;
}

bool z_row_analyze_layout(const ZRowTokenVec *tokens, ZRowSyntaxFacts *facts, ZDiag *diag) {
  if (facts) memset(facts, 0, sizeof(*facts));
  size_t depth = 0;
  bool in_row = false;

  for (size_t i = 0; tokens && i < tokens->len; i++) {
    const ZRowToken *token = &tokens->items[i];
    switch (token->kind) {
      case Z_ROW_TOKEN_INDENT:
        depth++;
        if (facts && depth > facts->max_indent_depth) facts->max_indent_depth = depth;
        in_row = false;
        break;
      case Z_ROW_TOKEN_DEDENT:
        if (depth == 0) {
          row_diag(diag, token->line, token->column, 1, "row layout contains an unmatched dedent", "matching indentation", NULL);
          return false;
        }
        depth--;
        in_row = false;
        break;
      case Z_ROW_TOKEN_NEWLINE:
        in_row = false;
        break;
      case Z_ROW_TOKEN_COMMENT:
        if (facts) facts->comment_count++;
        break;
      case Z_ROW_TOKEN_EOF:
        break;
      default:
        if (!in_row) {
          if (facts) facts->row_count++;
          in_row = true;
        }
        break;
    }
  }

  if (depth != 0) {
    row_diag(diag, 1, 1, 1, "row layout ended inside an indentation block", "dedent to column 1", NULL);
    return false;
  }
  return true;
}

static bool row_parent_stack_push(size_t **items, size_t *len, size_t *cap, size_t value) {
  if (*len + 1 > *cap) {
    *cap = z_grow_capacity(*cap, *len + 1, 16);
    *items = z_checked_reallocarray(*items, *cap, sizeof(size_t));
  }
  (*items)[(*len)++] = value;
  return true;
}

static size_t row_finalize_node(ZRowTree *tree, size_t parent, size_t first_token, size_t token_count, size_t depth, const ZRowTokenVec *tokens) {
  const ZRowToken *start = &tokens->items[first_token];
  row_push_node(tree, (ZRowNode){
    .parent = parent,
    .first_token = first_token,
    .token_count = token_count,
    .indent_depth = depth,
    .line = start->line,
    .column = start->column
  });
  return tree->len - 1;
}

static bool row_dedent_leads_to_eof(const ZRowTokenVec *tokens, size_t index) {
  for (size_t i = index; tokens && i < tokens->len; i++) {
    if (tokens->items[i].kind == Z_ROW_TOKEN_EOF) return true;
    if (tokens->items[i].kind != Z_ROW_TOKEN_DEDENT) return false;
  }
  return false;
}

static bool row_token_text(const ZRowTokenVec *tokens, size_t index, const char *text) {
  return tokens && index < tokens->len && strcmp(tokens->items[index].text, text) == 0;
}

static bool row_token_text_at(const ZRowTokenVec *tokens, size_t index, size_t end, const char *text) {
  return index < end && row_token_text(tokens, index, text);
}

static bool row_expect_end(const ZRowTokenVec *tokens, size_t pos, size_t end, ZDiag *diag, const char *context) {
  if (row_has_diag(diag)) return false;
  if (pos >= end) return true;
  const ZRowToken *token = &tokens->items[pos];
  char message[128];
  snprintf(message, sizeof(message), "unexpected token after %s", context ? context : "row");
  row_diag(diag, token->line, token->column, token->length > 0 ? (int)token->length : 1, message, "end of row", NULL);
  return false;
}

static bool row_expect_text(const ZRowTokenVec *tokens, size_t *pos, size_t end, ZDiag *diag, const char *text, const char *message) {
  if (*pos < end && row_token_text(tokens, *pos, text)) {
    (*pos)++;
    return true;
  }
  const ZRowToken *token = *pos < end ? &tokens->items[*pos] : (end > 0 ? &tokens->items[end - 1] : NULL);
  row_diag(diag, token ? token->line : 1, token ? token->column : 1, 1, message, text, NULL);
  return false;
}

static bool row_is_reserved_word(const char *text) {
  const char *keywords[] = {
    "as", "break", "check", "choice", "const", "continue", "defer", "else", "enum", "export", "extern", "false",
    "fn", "for", "fun", "if", "import", "in", "let", "match", "meta", "mut", "null", "packed", "pub",
    "raise", "raises", "rescue", "ret", "return", "set", "shape", "static", "test", "true", "type",
    "use", "var", "while", NULL
  };
  for (int i = 0; keywords[i]; i++) {
    if (strcmp(text, keywords[i]) == 0) return true;
  }
  return false;
}

static const ZRowToken *row_expect_word(const ZRowTokenVec *tokens, size_t *pos, size_t end, ZDiag *diag, const char *message) {
  if (*pos < end && tokens->items[*pos].kind == Z_ROW_TOKEN_WORD) {
    if (!row_is_reserved_word(tokens->items[*pos].text)) return &tokens->items[(*pos)++];
    const ZRowToken *token = &tokens->items[*pos];
    row_diag(diag, token->line, token->column, token->length > 0 ? (int)token->length : 1, "reserved word cannot be used as an identifier", "identifier", "choose a non-keyword name");
    return NULL;
  }
  const ZRowToken *token = *pos < end ? &tokens->items[*pos] : (end > 0 ? &tokens->items[end - 1] : NULL);
  row_diag(diag, token ? token->line : 1, token ? token->column : 1, 1, message, "identifier", NULL);
  return NULL;
}

static bool row_validate_use_module(const ZRowTokenVec *tokens, size_t start, size_t end, ZDiag *diag) {
  if (start >= end) {
    const ZRowToken *token = start < tokens->len ? &tokens->items[start] : (start > 0 ? &tokens->items[start - 1] : NULL);
    row_diag(diag, token ? token->line : 1, token ? token->column : 1, 1, "expected import module name", "import module name", NULL);
    return false;
  }

  bool expect_segment = true;
  for (size_t i = start; i < end; i++) {
    const ZRowToken *token = &tokens->items[i];
    if (expect_segment) {
      if (token->kind != Z_ROW_TOKEN_WORD || row_is_reserved_word(token->text)) {
        row_diag(diag, token->line, token->column, token->length > 0 ? (int)token->length : 1,
                 i == start ? "expected import module name" : "expected import module segment",
                 i == start ? "import module name" : "import module segment", NULL);
        return false;
      }
      expect_segment = false;
      continue;
    }
    if (!row_token_text(tokens, i, ".")) {
      row_diag(diag, token->line, token->column, token->length > 0 ? (int)token->length : 1, "expected '.' between import module segments", "'.' or import alias", NULL);
      return false;
    }
    expect_segment = true;
  }

  if (expect_segment) {
    const ZRowToken *token = &tokens->items[end - 1];
    row_diag(diag, token->line, token->column, token->length > 0 ? (int)token->length : 1, "expected import module segment", "import module segment", NULL);
    return false;
  }
  return true;
}

static bool row_is_type_start(const ZRowTokenVec *tokens, size_t pos, size_t end) {
  if (pos >= end) return false;
  const ZRowToken *token = &tokens->items[pos];
  if (strcmp(token->text, "[") == 0) return true;
  if (token->kind != Z_ROW_TOKEN_WORD) return false;
  const char *text = token->text;
  if (isupper((unsigned char)text[0])) return true;
  const char *known[] = {
    "Bool", "String", "Void", "char", "i8", "i16", "i32", "i64", "isize",
    "u8", "u16", "u32", "u64", "usize", "f32", "f64",
    "owned", "ref", "mutref", "const", "span", "Span", "MutSpan", "Maybe", NULL
  };
  for (int i = 0; known[i]; i++) {
    if (strcmp(text, known[i]) == 0) return true;
  }
  return false;
}

static bool row_append_type_atom(const ZRowTokenVec *tokens, size_t *pos, size_t end, ZBuf *buf, ZDiag *diag) {
  if (*pos >= end) {
    row_diag(diag, 1, 1, 1, "expected type name", "type", NULL);
    return false;
  }
  if (row_token_text_at(tokens, *pos, end, "[")) {
    const ZRowToken *start = &tokens->items[*pos];
    zbuf_append_char(buf, '[');
    (*pos)++;
    if (*pos >= end || row_token_text(tokens, *pos, "]")) {
      row_diag(diag, start->line, start->column, 1, "expected array length", "array length", NULL);
      return false;
    }
    const ZRowToken *length = &tokens->items[*pos];
    if (length->kind != Z_ROW_TOKEN_NUMBER && (length->kind != Z_ROW_TOKEN_WORD || row_is_reserved_word(length->text))) {
      row_diag(diag, length->line, length->column, length->length > 0 ? (int)length->length : 1, "expected array length", "integer literal or static length name", NULL);
      return false;
    }
    zbuf_append(buf, length->text);
    (*pos)++;
    if (!row_expect_text(tokens, pos, end, diag, "]", "expected ']' after array length")) return false;
    zbuf_append_char(buf, ']');
    return row_append_type_atom(tokens, pos, end, buf, diag);
  }
  if (row_token_text_at(tokens, *pos, end, "const")) {
    zbuf_append(buf, "const ");
    (*pos)++;
    return row_append_type_atom(tokens, pos, end, buf, diag);
  }
  const ZRowToken *name = row_expect_word(tokens, pos, end, diag, "expected type name");
  if (!name) return false;
  zbuf_append(buf, name->text);
  if (*pos < end && row_token_text(tokens, *pos, "<")) {
    int depth = 0;
    do {
      if (row_token_text(tokens, *pos, "<")) depth++;
      else if (row_token_text(tokens, *pos, ">")) depth--;
      zbuf_append(buf, tokens->items[*pos].text);
      (*pos)++;
    } while (*pos < end && depth > 0);
    if (depth != 0) {
      row_diag(diag, name->line, name->column, 1, "unterminated generic type", "closing '>'", NULL);
      return false;
    }
  }
  return true;
}

static char *row_parse_type_text(const ZRowTokenVec *tokens, size_t *pos, size_t end, ZDiag *diag) {
  ZBuf buf;
  zbuf_init(&buf);
  if (!row_append_type_atom(tokens, pos, end, &buf, diag)) {
    zbuf_free(&buf);
    return z_strdup("Void");
  }
  return buf.data ? buf.data : z_strdup("Void");
}

static bool row_parse_type_params(const ZRowTokenVec *tokens, size_t *pos, size_t end, ParamVec *out, ZDiag *diag) {
  if (!row_token_text_at(tokens, *pos, end, "<")) return true;
  (*pos)++;
  while (*pos < end && !row_token_text(tokens, *pos, ">")) {
    bool is_static = false;
    if (row_token_text_at(tokens, *pos, end, "static")) {
      is_static = true;
      (*pos)++;
    }
    const ZRowToken *name = row_expect_word(tokens, pos, end, diag, "expected generic parameter name");
    if (!name) return false;
    char *type = NULL;
    if (row_token_text_at(tokens, *pos, end, ":")) {
      (*pos)++;
      type = row_parse_type_text(tokens, pos, end, diag);
    }
    row_push_param(out, (Param){.name = z_strdup(name->text), .type = type, .is_static = is_static, .line = name->line, .column = name->column});
    if (row_token_text_at(tokens, *pos, end, ",")) (*pos)++;
  }
  return row_expect_text(tokens, pos, end, diag, ">", "expected '>' after generic parameters");
}

static Expr *row_new_expr(ExprKind kind, const ZRowToken *token) {
  Expr *expr = z_checked_malloc(sizeof(Expr));
  memset(expr, 0, sizeof(*expr));
  expr->kind = kind;
  expr->line = token ? token->line : 1;
  expr->column = token ? token->column : 1;
  return expr;
}

static Stmt *row_new_stmt(StmtKind kind, const ZRowToken *token) {
  Stmt *stmt = z_checked_malloc(sizeof(Stmt));
  memset(stmt, 0, sizeof(*stmt));
  stmt->kind = kind;
  stmt->line = token ? token->line : 1;
  stmt->column = token ? token->column : 1;
  return stmt;
}

static bool row_is_binary_operator(const char *text) {
  const char *ops[] = {"+", "-", "*", "/", "%", "&&", "||", "==", "!=", "<", "<=", ">", ">=", "+%", "+|", NULL};
  for (int i = 0; ops[i]; i++) {
    if (strcmp(text, ops[i]) == 0) return true;
  }
  return false;
}

static bool row_token_connected_to_previous(const ZRowTokenVec *tokens, size_t index) {
  if (!tokens || index == 0 || index >= tokens->len) return false;
  const ZRowToken *prev = &tokens->items[index - 1];
  const ZRowToken *token = &tokens->items[index];
  return token->offset == prev->offset + prev->length;
}

static char *row_join_tokens(const ZRowTokenVec *tokens, size_t start, size_t end);
static size_t row_find_top_level_token(const ZRowTokenVec *tokens, size_t start, size_t end, const char *text);
static size_t row_find_closing_bracket(const ZRowTokenVec *tokens, size_t start, size_t end);

static char *row_parse_generic_arg_text(const ZRowTokenVec *tokens, size_t *pos, size_t end, ZDiag *diag) {
  size_t start = *pos;
  int depth = 0;
  while (*pos < end) {
    if (depth == 0 && (row_token_text(tokens, *pos, ",") || row_token_text(tokens, *pos, ">"))) break;
    if (row_token_text(tokens, *pos, "<")) depth++;
    else if (row_token_text(tokens, *pos, ">")) {
      if (depth == 0) break;
      depth--;
    }
    (*pos)++;
  }
  if (start == *pos) {
    const ZRowToken *token = start < end ? &tokens->items[start] : NULL;
    row_diag(diag, token ? token->line : 1, token ? token->column : 1, 1, "expected generic argument", "type or static value", NULL);
    return z_strdup("Void");
  }
  return row_join_tokens(tokens, start, *pos);
}

static bool row_parse_expr_type_args(const ZRowTokenVec *tokens, size_t *pos, size_t end, TypeArgVec *out, ZDiag *diag) {
  if (!row_token_text_at(tokens, *pos, end, "<")) return true;
  (*pos)++;
  while (*pos < end && !row_token_text(tokens, *pos, ">")) {
    const ZRowToken *start = &tokens->items[*pos];
    char *arg = row_parse_generic_arg_text(tokens, pos, end, diag);
    row_push_type_arg(out, (TypeArg){.type = arg, .line = start->line, .column = start->column});
    if (row_token_text_at(tokens, *pos, end, ",")) (*pos)++;
  }
  return row_expect_text(tokens, pos, end, diag, ">", "expected '>' after generic arguments");
}

static Expr *row_parse_expr(RowExprParser *parser);
static Expr *row_parse_expr_atom(RowExprParser *parser);
static Expr *row_parse_expr_range(const ZRowTokenVec *tokens, size_t start, size_t end, ZDiag *diag);

static Expr *row_parse_primary(RowExprParser *parser) {
  if (parser->pos >= parser->end) {
    row_diag(parser->diag, 1, 1, 1, "expected expression", "expression", NULL);
    return row_new_expr(EXPR_IDENT, NULL);
  }

  const ZRowToken *token = &parser->tokens->items[parser->pos++];
  Expr *expr = NULL;
  if (token->kind == Z_ROW_TOKEN_STRING) {
    expr = row_new_expr(EXPR_STRING, token);
    expr->text = z_strdup(token->text);
  } else if (token->kind == Z_ROW_TOKEN_CHAR) {
    expr = row_new_expr(EXPR_CHAR, token);
    expr->text = z_strdup(token->text);
  } else if (token->kind == Z_ROW_TOKEN_NUMBER) {
    expr = row_new_expr(EXPR_NUMBER, token);
    expr->text = z_strdup(token->text);
  } else if (token->kind == Z_ROW_TOKEN_WORD && strcmp(token->text, "true") == 0) {
    expr = row_new_expr(EXPR_BOOL, token);
    expr->bool_value = true;
  } else if (token->kind == Z_ROW_TOKEN_WORD && strcmp(token->text, "false") == 0) {
    expr = row_new_expr(EXPR_BOOL, token);
    expr->bool_value = false;
  } else if (token->kind == Z_ROW_TOKEN_WORD && strcmp(token->text, "null") == 0) {
    expr = row_new_expr(EXPR_NULL, token);
  } else if (token->kind == Z_ROW_TOKEN_WORD && !row_is_reserved_word(token->text)) {
    expr = row_new_expr(EXPR_IDENT, token);
    expr->text = z_strdup(token->text);
  } else if (strcmp(token->text, "[") == 0) {
    expr = row_new_expr(EXPR_ARRAY_LITERAL, token);
    while (parser->pos < parser->end && !row_token_text(parser->tokens, parser->pos, "]")) {
      row_push_expr(&expr->args, row_parse_expr_atom(parser));
    }
    row_expect_text(parser->tokens, &parser->pos, parser->end, parser->diag, "]", "expected ']' after array literal");
  } else {
    row_diag(parser->diag, token->line, token->column, 1, "expected expression", "expression", NULL);
    expr = row_new_expr(EXPR_IDENT, token);
    expr->text = z_strdup("<error>");
  }

  while (parser->pos < parser->end) {
    if (row_token_text(parser->tokens, parser->pos, "<") && row_token_connected_to_previous(parser->tokens, parser->pos)) {
      row_parse_expr_type_args(parser->tokens, &parser->pos, parser->end, &expr->type_args, parser->diag);
      continue;
    }
    if (row_token_text(parser->tokens, parser->pos, ".") && row_token_connected_to_previous(parser->tokens, parser->pos)) {
      const ZRowToken *dot = &parser->tokens->items[parser->pos++];
      const ZRowToken *name = row_expect_word(parser->tokens, &parser->pos, parser->end, parser->diag, "expected field name after '.'");
      if (!name) return expr;
      Expr *member = row_new_expr(EXPR_MEMBER, dot);
      member->left = expr;
      member->text = z_strdup(name->text);
      expr = member;
      continue;
    }
    if (row_token_text(parser->tokens, parser->pos, "[") && row_token_connected_to_previous(parser->tokens, parser->pos)) {
      const ZRowToken *bracket = &parser->tokens->items[parser->pos++];
      size_t content_start = parser->pos;
      size_t close = row_find_closing_bracket(parser->tokens, content_start, parser->end);
      if (close == Z_ROW_NO_PARENT) {
        row_diag(parser->diag, bracket->line, bracket->column, 1, "expected ']' after index", "]", NULL);
        return expr;
      }
      size_t range = row_find_top_level_token(parser->tokens, content_start, close, "..");
      if (range != Z_ROW_NO_PARENT) {
        Expr *slice = row_new_expr(EXPR_SLICE, bracket);
        slice->left = expr;
        row_push_expr(&slice->args, range > content_start ? row_parse_expr_range(parser->tokens, content_start, range, parser->diag) : NULL);
        row_push_expr(&slice->args, range + 1 < close ? row_parse_expr_range(parser->tokens, range + 1, close, parser->diag) : NULL);
        expr = slice;
      } else {
        Expr *index = row_new_expr(EXPR_INDEX, bracket);
        index->left = expr;
        index->right = row_parse_expr_range(parser->tokens, content_start, close, parser->diag);
        expr = index;
      }
      parser->pos = close + 1;
      continue;
    }
    if (row_token_text(parser->tokens, parser->pos, "(") && row_token_connected_to_previous(parser->tokens, parser->pos)) {
      const ZRowToken *paren = &parser->tokens->items[parser->pos++];
      Expr *call = row_new_expr(EXPR_CALL, paren);
      call->left = expr;
      while (parser->pos < parser->end && !row_token_text(parser->tokens, parser->pos, ")")) {
        if (row_token_text(parser->tokens, parser->pos, ",")) {
          parser->pos++;
          continue;
        }
        row_push_expr(&call->args, row_parse_expr_atom(parser));
      }
      row_expect_text(parser->tokens, &parser->pos, parser->end, parser->diag, ")", "expected ')' after call arguments");
      expr = call;
      continue;
    }
    break;
  }
  return expr;
}

static bool row_next_starts_shape_literal(RowExprParser *parser) {
  if (!row_is_type_start(parser->tokens, parser->pos, parser->end)) return false;
  size_t copy = parser->pos;
  ZDiag scratch = {0};
  char *type = row_parse_type_text(parser->tokens, &copy, parser->end, &scratch);
  free(type);
  if (scratch.code != 0 || copy >= parser->end) return false;
  return row_token_text(parser->tokens, copy, ".") && !row_token_connected_to_previous(parser->tokens, copy);
}

static bool row_shape_literal_at_boundary(RowExprParser *parser) {
  if (parser->pos >= parser->end) return true;
  return row_token_text(parser->tokens, parser->pos, ")") ||
         row_token_text(parser->tokens, parser->pos, "]") ||
         row_token_text(parser->tokens, parser->pos, ",") ||
         row_token_text(parser->tokens, parser->pos, "as");
}

static Expr *row_parse_shape_literal(RowExprParser *parser) {
  const ZRowToken *start = &parser->tokens->items[parser->pos];
  char *type = row_parse_type_text(parser->tokens, &parser->pos, parser->end, parser->diag);
  row_expect_text(parser->tokens, &parser->pos, parser->end, parser->diag, ".", "expected '.' after shape literal type");
  Expr *expr = row_new_expr(EXPR_SHAPE_LITERAL, start);
  expr->text = type;
  while (!row_shape_literal_at_boundary(parser)) {
    const ZRowToken *field = row_expect_word(parser->tokens, &parser->pos, parser->end, parser->diag, "expected shape literal field name");
    if (!field) break;
    Expr *value = row_parse_expr_atom(parser);
    row_push_field(&expr->fields, (FieldInit){.name = z_strdup(field->text), .value = value, .line = field->line, .column = field->column});
  }
  return expr;
}

static Expr *row_parse_expr_atom(RowExprParser *parser) {
  if (parser->pos < parser->end && row_token_text(parser->tokens, parser->pos, "(")) {
    parser->pos++;
    Expr *expr = row_parse_expr(parser);
    row_expect_text(parser->tokens, &parser->pos, parser->end, parser->diag, ")", "expected ')' after expression group");
    return expr;
  }
  if (row_next_starts_shape_literal(parser)) return row_parse_shape_literal(parser);
  if (parser->pos < parser->end && row_token_text(parser->tokens, parser->pos, "check")) {
    const ZRowToken *token = &parser->tokens->items[parser->pos++];
    Expr *expr = row_new_expr(EXPR_CHECK, token);
    expr->left = row_parse_expr(parser);
    return expr;
  }
  if (parser->pos < parser->end && row_token_text(parser->tokens, parser->pos, "rescue")) {
    const ZRowToken *token = &parser->tokens->items[parser->pos++];
    Expr *expr = row_new_expr(EXPR_RESCUE, token);
    expr->left = row_parse_expr_atom(parser);
    const ZRowToken *name = row_expect_word(parser->tokens, &parser->pos, parser->end, parser->diag, "expected error binding after rescue");
    if (name) expr->text = z_strdup(name->text);
    expr->right = row_parse_expr_atom(parser);
    return expr;
  }
  if (parser->pos < parser->end && row_token_text(parser->tokens, parser->pos, "&")) {
    const ZRowToken *token = &parser->tokens->items[parser->pos++];
    Expr *expr = row_new_expr(EXPR_BORROW, token);
    if (parser->pos < parser->end && row_token_text(parser->tokens, parser->pos, "mut")) {
      expr->mutable_borrow = true;
      parser->pos++;
    }
    expr->left = row_parse_expr_atom(parser);
    return expr;
  }
  if (parser->pos < parser->end && row_is_binary_operator(parser->tokens->items[parser->pos].text)) {
    const ZRowToken *op = &parser->tokens->items[parser->pos++];
    Expr *expr = row_new_expr(EXPR_BINARY, op);
    expr->text = z_strdup(op->text);
    expr->left = row_parse_expr_atom(parser);
    expr->right = row_parse_expr_atom(parser);
    return expr;
  }
  return row_parse_primary(parser);
}

static Expr *row_parse_expr(RowExprParser *parser) {
  Expr *expr = row_parse_expr_atom(parser);
  if (parser->pos >= parser->end || row_token_text(parser->tokens, parser->pos, ")") || row_token_text(parser->tokens, parser->pos, "]")) return expr;

  if (row_token_text_at(parser->tokens, parser->pos, parser->end, "as")) {
    const ZRowToken *token = &parser->tokens->items[parser->pos++];
    Expr *cast = row_new_expr(EXPR_CAST, token);
    cast->left = expr;
    cast->text = row_parse_type_text(parser->tokens, &parser->pos, parser->end, parser->diag);
    return cast;
  }

  Expr *call = row_new_expr(EXPR_CALL, parser->pos < parser->end ? &parser->tokens->items[parser->pos] : NULL);
  call->left = expr;
  while (parser->pos < parser->end && !row_token_text(parser->tokens, parser->pos, ")") && !row_token_text(parser->tokens, parser->pos, "]")) {
    row_push_expr(&call->args, row_parse_expr_atom(parser));
  }
  return call;
}

static Expr *row_parse_expr_range(const ZRowTokenVec *tokens, size_t start, size_t end, ZDiag *diag) {
  RowExprParser parser = {.tokens = tokens, .pos = start, .end = end, .diag = diag};
  Expr *expr = row_parse_expr(&parser);
  if (!row_has_diag(diag) && parser.pos < end) {
    const ZRowToken *token = &tokens->items[parser.pos];
    row_diag(diag, token->line, token->column, 1, "unexpected token after row expression", "end of expression", NULL);
  }
  return expr;
}

static size_t row_find_top_level_token(const ZRowTokenVec *tokens, size_t start, size_t end, const char *text) {
  int paren_depth = 0;
  int bracket_depth = 0;
  for (size_t i = start; i < end; i++) {
    if (paren_depth == 0 && bracket_depth == 0 && row_token_text(tokens, i, text)) return i;
    if (row_token_text(tokens, i, "(")) paren_depth++;
    else if (row_token_text(tokens, i, ")") && paren_depth > 0) paren_depth--;
    else if (row_token_text(tokens, i, "[")) bracket_depth++;
    else if (row_token_text(tokens, i, "]") && bracket_depth > 0) bracket_depth--;
  }
  return Z_ROW_NO_PARENT;
}

static size_t row_find_closing_bracket(const ZRowTokenVec *tokens, size_t start, size_t end) {
  int depth = 0;
  for (size_t i = start; i < end; i++) {
    if (depth == 0 && row_token_text(tokens, i, "]")) return i;
    if (row_token_text(tokens, i, "[")) depth++;
    else if (row_token_text(tokens, i, "]") && depth > 0) depth--;
  }
  return Z_ROW_NO_PARENT;
}

static bool row_malformed_type_annotation_candidate(const ZRowTokenVec *tokens, size_t pos, size_t end) {
  if (!row_token_text_at(tokens, pos, end, "[")) return true;
  size_t close = row_find_closing_bracket(tokens, pos + 1, end);
  return close != Z_ROW_NO_PARENT && close + 1 < end && row_is_type_start(tokens, close + 1, end);
}

static bool row_explicit_type_annotation(const ZRowTokenVec *tokens, size_t pos, size_t end, ZDiag *diag) {
  if (!row_is_type_start(tokens, pos, end)) return false;
  size_t copy = pos;
  ZDiag scratch = {0};
  char *type = row_parse_type_text(tokens, &copy, end, &scratch);
  free(type);
  if (scratch.code != 0) {
    if (row_malformed_type_annotation_candidate(tokens, pos, end)) {
      row_diag(diag, scratch.line, scratch.column, scratch.length, scratch.message, scratch.expected, scratch.help);
    }
    return false;
  }
  if (copy < end && row_token_text(tokens, copy, ".") && !row_token_connected_to_previous(tokens, copy)) return false;
  if (copy < end && row_token_connected_to_previous(tokens, copy) &&
      (row_token_text(tokens, copy, ".") || row_token_text(tokens, copy, "(") || row_token_text(tokens, copy, "["))) return false;
  return copy < end;
}

static bool row_reject_child_rows(const ZRowTree *tree, size_t parent, ZDiag *diag, const char *context) {
  if (row_has_diag(diag)) return false;
  for (size_t i = 0; tree && i < tree->len; i++) {
    if (tree->items[i].parent != parent) continue;
    const ZRowNode *child = &tree->items[i];
    char message[128];
    snprintf(message, sizeof(message), "unexpected indented row after %s", context ? context : "row");
    row_diag(diag, child->line, child->column, 1, message, "row without an indented body", "dedent the row or move it under a block-owning row");
    return false;
  }
  return true;
}

static bool row_statement_owns_child_rows(const Stmt *stmt) {
  if (!stmt) return false;
  return stmt->kind == STMT_IF || stmt->kind == STMT_WHILE || stmt->kind == STMT_FOR || stmt->kind == STMT_MATCH;
}

static StmtVec row_parse_child_statements(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t parent, ZDiag *diag);
static MatchArmVec row_parse_match_arms(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t parent, ZDiag *diag);

static Stmt *row_parse_statement(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t row_index, ZDiag *diag) {
  const ZRowNode *node = &tree->items[row_index];
  size_t pos = node->first_token;
  size_t end = node->first_token + node->token_count;
  const ZRowToken *start = &tokens->items[pos];

  if (row_token_text_at(tokens, pos, end, "let") || row_token_text_at(tokens, pos, end, "mut")) {
    bool is_mut = row_token_text_at(tokens, pos, end, "mut");
    pos++;
    const ZRowToken *name = row_expect_word(tokens, &pos, end, diag, "expected binding name");
    Stmt *stmt = row_new_stmt(STMT_LET, start);
    stmt->mutable_binding = is_mut;
    if (name) stmt->name = z_strdup(name->text);
    if (row_explicit_type_annotation(tokens, pos, end, diag)) stmt->type = row_parse_type_text(tokens, &pos, end, diag);
    else if (!row_has_diag(diag) && row_is_type_start(tokens, pos, end)) {
      size_t copy = pos;
      ZDiag scratch = {0};
      char *type = row_parse_type_text(tokens, &copy, end, &scratch);
      free(type);
      if (scratch.code == 0 && copy >= end) {
        const ZRowToken *type_token = &tokens->items[pos];
        row_diag(diag, type_token->line, type_token->column, type_token->length > 0 ? (int)type_token->length : 1,
                 "expected initializer after let type annotation", "expression after type annotation", "add an initializer or remove the type annotation");
      }
    }
    stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "set")) {
    pos++;
    Stmt *stmt = row_new_stmt(STMT_ASSIGN, start);
    RowExprParser target = {.tokens = tokens, .pos = pos, .end = end, .diag = diag};
    stmt->target = row_parse_primary(&target);
    pos = target.pos;
    stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "ret")) {
    Stmt *stmt = row_new_stmt(STMT_RETURN, start);
    pos++;
    if (pos < end) stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "defer")) {
    Stmt *stmt = row_new_stmt(STMT_DEFER, start);
    pos++;
    stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "check")) {
    Stmt *stmt = row_new_stmt(STMT_CHECK, start);
    pos++;
    stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "raise")) {
    Stmt *stmt = row_new_stmt(STMT_RAISE, start);
    pos++;
    const ZRowToken *name = row_expect_word(tokens, &pos, end, diag, "expected error name after raise");
    if (name) stmt->name = z_strdup(name->text);
    row_expect_end(tokens, pos, end, diag, "raise statement");
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "if")) {
    Stmt *stmt = row_new_stmt(STMT_IF, start);
    pos++;
    stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
    stmt->then_body = row_parse_child_statements(tokens, tree, row_index, diag);
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "while")) {
    Stmt *stmt = row_new_stmt(STMT_WHILE, start);
    pos++;
    stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
    stmt->then_body = row_parse_child_statements(tokens, tree, row_index, diag);
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "for")) {
    Stmt *stmt = row_new_stmt(STMT_FOR, start);
    pos++;
    const ZRowToken *name = row_expect_word(tokens, &pos, end, diag, "expected loop binding name");
    if (name) stmt->name = z_strdup(name->text);
    size_t range = row_find_top_level_token(tokens, pos, end, "..");
    if (range == Z_ROW_NO_PARENT) {
      row_diag(diag, start->line, start->column, 1, "expected '..' in range loop", "range expression", NULL);
      return stmt;
    }
    stmt->expr = row_parse_expr_range(tokens, pos, range, diag);
    stmt->range_end = row_parse_expr_range(tokens, range + 1, end, diag);
    stmt->then_body = row_parse_child_statements(tokens, tree, row_index, diag);
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "break")) {
    Stmt *stmt = row_new_stmt(STMT_BREAK, start);
    pos++;
    row_expect_end(tokens, pos, end, diag, "break statement");
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "continue")) {
    Stmt *stmt = row_new_stmt(STMT_CONTINUE, start);
    pos++;
    row_expect_end(tokens, pos, end, diag, "continue statement");
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "match")) {
    Stmt *stmt = row_new_stmt(STMT_MATCH, start);
    pos++;
    stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
    stmt->match_arms = row_parse_match_arms(tokens, tree, row_index, diag);
    return stmt;
  }

  if (row_token_text_at(tokens, pos, end, "else")) {
    row_diag(diag, start->line, start->column, 1, "else must follow an if row", "preceding if row", NULL);
    return row_new_stmt(STMT_EXPR, start);
  }

  Stmt *stmt = row_new_stmt(STMT_EXPR, start);
  stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
  return stmt;
}

static Stmt *row_parse_else_if_statement(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t row_index, size_t pos, size_t end, ZDiag *diag) {
  const ZRowToken *start = &tokens->items[pos];
  Stmt *stmt = row_new_stmt(STMT_IF, start);
  pos++;
  stmt->expr = row_parse_expr_range(tokens, pos, end, diag);
  stmt->then_body = row_parse_child_statements(tokens, tree, row_index, diag);
  return stmt;
}

static StmtVec row_parse_child_statements(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t parent, ZDiag *diag) {
  StmtVec body = {0};
  Stmt *pending_else_target = NULL;
  for (size_t i = 0; i < tree->len && !row_has_diag(diag); i++) {
    if (tree->items[i].parent != parent) continue;
    const ZRowNode *node = &tree->items[i];
    size_t pos = node->first_token;
    size_t end = node->first_token + node->token_count;
    if (row_token_text_at(tokens, pos, end, "else")) {
      Stmt *target = pending_else_target;
      if (!target) {
        row_diag(diag, node->line, node->column, 1, "else must follow an if row", "preceding if row", NULL);
        continue;
      }
      pos++;
      if (pos < end) {
        if (!row_token_text_at(tokens, pos, end, "if")) {
          row_diag(diag, node->line, node->column, 1, "expected if after else row condition", "else or else if", NULL);
          continue;
        }
        Stmt *else_if = row_parse_else_if_statement(tokens, tree, i, pos, end, diag);
        row_push_stmt(&target->else_body, else_if);
        pending_else_target = else_if;
      } else {
        target->else_body = row_parse_child_statements(tokens, tree, i, diag);
        pending_else_target = NULL;
      }
      continue;
    }
    Stmt *stmt = row_parse_statement(tokens, tree, i, diag);
    row_push_stmt(&body, stmt);
    if (!row_statement_owns_child_rows(stmt)) row_reject_child_rows(tree, i, diag, "statement");
    pending_else_target = stmt && stmt->kind == STMT_IF && stmt->else_body.len == 0 ? stmt : NULL;
  }
  return body;
}

static MatchArmVec row_parse_match_arms(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t parent, ZDiag *diag) {
  MatchArmVec arms = {0};
  for (size_t i = 0; i < tree->len && !row_has_diag(diag); i++) {
    if (tree->items[i].parent != parent) continue;
    const ZRowNode *node = &tree->items[i];
    size_t pos = node->first_token;
    size_t end = node->first_token + node->token_count;
    if (pos >= end) continue;

    const ZRowToken *case_name = &tokens->items[pos++];
    if (case_name->kind != Z_ROW_TOKEN_WORD && case_name->kind != Z_ROW_TOKEN_NUMBER) {
      row_diag(diag, case_name->line, case_name->column, 1, "expected match case name", "case name", NULL);
      continue;
    }
    if (case_name->kind == Z_ROW_TOKEN_WORD && row_is_reserved_word(case_name->text) &&
        strcmp(case_name->text, "true") != 0 && strcmp(case_name->text, "false") != 0) {
      row_diag(diag, case_name->line, case_name->column, case_name->length > 0 ? (int)case_name->length : 1, "reserved word cannot be used as a match case name", "case name", "choose a non-keyword case name");
      continue;
    }

    MatchArm arm = {
      .case_name = z_strdup(case_name->text),
      .line = case_name->line,
      .column = case_name->column
    };

    if (row_token_text_at(tokens, pos, end, "..")) {
      pos++;
      const ZRowToken *range_end = pos < end ? &tokens->items[pos++] : NULL;
      if (!range_end || (range_end->kind != Z_ROW_TOKEN_WORD && range_end->kind != Z_ROW_TOKEN_NUMBER)) {
        row_diag(diag, case_name->line, case_name->column, 1, "expected match range end", "range end", NULL);
      } else {
        arm.range_end = z_strdup(range_end->text);
      }
    }

    if (pos < end && !row_token_text_at(tokens, pos, end, "if")) {
      const ZRowToken *payload = row_expect_word(tokens, &pos, end, diag, "expected match payload binding");
      if (payload) arm.payload_name = z_strdup(payload->text);
    }
    if (row_token_text_at(tokens, pos, end, "if")) {
      pos++;
      arm.guard = row_parse_expr_range(tokens, pos, end, diag);
    } else if (pos < end) {
      row_diag(diag, tokens->items[pos].line, tokens->items[pos].column, 1, "unexpected token in match arm", "payload binding or guard", NULL);
    }

    arm.body = row_parse_child_statements(tokens, tree, i, diag);
    row_push_match_arm(&arms, arm);
  }
  return arms;
}

static Function row_parse_function_decl(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t row_index, bool is_public, bool export_c, ZDiag *diag) {
  const ZRowNode *node = &tree->items[row_index];
  size_t pos = node->first_token;
  size_t end = node->first_token + node->token_count;
  if (row_token_text_at(tokens, pos, end, "pub")) pos++;
  if (row_token_text_at(tokens, pos, end, "export")) {
    export_c = true;
    pos++;
    row_expect_text(tokens, &pos, end, diag, "c", "expected 'c' after export");
  }
  row_expect_text(tokens, &pos, end, diag, "fn", "expected function declaration");
  const ZRowToken *name = row_expect_word(tokens, &pos, end, diag, "expected function name");

  Function fun = {.is_public = is_public, .export_c = export_c, .line = node->line, .column = node->column};
  if (name) fun.name = z_strdup(name->text);
  row_parse_type_params(tokens, &pos, end, &fun.type_params, diag);
  fun.return_type = row_parse_type_text(tokens, &pos, end, diag);

  while (pos < end && !row_token_text(tokens, pos, "!")) {
    const ZRowToken *param_name = row_expect_word(tokens, &pos, end, diag, "expected parameter name");
    if (!param_name) break;
    char *type = row_parse_type_text(tokens, &pos, end, diag);
    row_push_param(&fun.params, (Param){.name = z_strdup(param_name->text), .type = type, .line = param_name->line, .column = param_name->column});
  }

  if (pos < end && row_token_text(tokens, pos, "!")) {
    fun.raises = true;
    pos++;
    if (pos < end) {
      fun.has_error_set = true;
      if (!row_expect_text(tokens, &pos, end, diag, "[", "expected '[' before named error set")) return fun;
      while (pos < end && !row_token_text(tokens, pos, "]")) {
        const ZRowToken *error = row_expect_word(tokens, &pos, end, diag, "expected error name");
        if (!error) break;
        row_push_param(&fun.errors, (Param){.name = z_strdup(error->text), .line = error->line, .column = error->column});
      }
      if (!row_expect_text(tokens, &pos, end, diag, "]", "expected ']' after named error set")) return fun;
    }
  }
  row_expect_end(tokens, pos, end, diag, "function declaration");

  fun.body = row_parse_child_statements(tokens, tree, row_index, diag);
  return fun;
}

static char *row_join_tokens(const ZRowTokenVec *tokens, size_t start, size_t end) {
  ZBuf buf;
  zbuf_init(&buf);
  for (size_t i = start; i < end; i++) zbuf_append(&buf, tokens->items[i].text);
  return buf.data ? buf.data : z_strdup("");
}

static void row_parse_type_decl(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t row_index, bool is_public, Program *program, ZDiag *diag) {
  const ZRowNode *node = &tree->items[row_index];
  size_t pos = node->first_token;
  size_t end = node->first_token + node->token_count;
  if (row_token_text_at(tokens, pos, end, "pub")) pos++;
  row_expect_text(tokens, &pos, end, diag, "type", "expected type declaration");
  const ZRowToken *name = row_expect_word(tokens, &pos, end, diag, "expected type name");

  Shape shape = {.layout = z_strdup("auto"), .is_public = is_public, .line = node->line, .column = node->column};
  if (name) shape.name = z_strdup(name->text);
  row_parse_type_params(tokens, &pos, end, &shape.type_params, diag);
  if (!row_expect_end(tokens, pos, end, diag, "type declaration")) return;

  for (size_t i = 0; i < tree->len && !row_has_diag(diag); i++) {
    if (tree->items[i].parent != row_index) continue;
    const ZRowNode *child = &tree->items[i];
    size_t child_pos = child->first_token;
    size_t child_end = child->first_token + child->token_count;
    bool method_public = false;
    const ZRowToken *pub_token = NULL;
    if (row_token_text_at(tokens, child_pos, child_end, "pub")) {
      pub_token = &tokens->items[child_pos];
      method_public = true;
      child_pos++;
    }
    if (row_token_text_at(tokens, child_pos, child_end, "fn")) {
      row_push_function(&shape.methods, row_parse_function_decl(tokens, tree, i, method_public, false, diag));
      continue;
    }
    if (method_public) {
      row_diag(diag, pub_token ? pub_token->line : child->line, pub_token ? pub_token->column : child->column, 1,
               "pub only applies to type methods", "fn method declaration", "remove pub from the field row or change it to a method");
      continue;
    }
    const ZRowToken *field = row_expect_word(tokens, &child_pos, child_end, diag, "expected field name");
    if (!field) continue;
    char *type = row_parse_type_text(tokens, &child_pos, child_end, diag);
    Expr *default_value = child_pos < child_end ? row_parse_expr_range(tokens, child_pos, child_end, diag) : NULL;
    row_push_param(&shape.fields, (Param){.name = z_strdup(field->text), .type = type, .default_value = default_value, .line = field->line, .column = field->column});
    row_reject_child_rows(tree, i, diag, "field declaration");
  }

  row_push_shape(&program->shapes, shape);
}

static void row_parse_enum_decl(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t row_index, Program *program, ZDiag *diag) {
  const ZRowNode *node = &tree->items[row_index];
  size_t pos = node->first_token;
  size_t end = node->first_token + node->token_count;
  if (row_token_text_at(tokens, pos, end, "pub")) pos++;
  row_expect_text(tokens, &pos, end, diag, "enum", "expected enum declaration");
  const ZRowToken *name = row_expect_word(tokens, &pos, end, diag, "expected enum name");
  EnumDecl item = {.line = node->line, .column = node->column};
  if (name) item.name = z_strdup(name->text);
  if (pos < end) item.type = row_parse_type_text(tokens, &pos, end, diag);
  if (!row_expect_end(tokens, pos, end, diag, "enum declaration")) return;
  for (size_t i = 0; i < tree->len && !row_has_diag(diag); i++) {
    if (tree->items[i].parent != row_index) continue;
    const ZRowNode *child = &tree->items[i];
    size_t child_pos = child->first_token;
    size_t child_end = child->first_token + child->token_count;
    const ZRowToken *case_name = row_expect_word(tokens, &child_pos, child_end, diag, "expected enum case name");
    row_expect_end(tokens, child_pos, child_end, diag, "enum case");
    if (case_name) row_push_param(&item.cases, (Param){.name = z_strdup(case_name->text), .line = case_name->line, .column = case_name->column});
    row_reject_child_rows(tree, i, diag, "enum case");
  }
  row_push_enum(&program->enums, item);
}

static void row_parse_choice_decl(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t row_index, Program *program, ZDiag *diag) {
  const ZRowNode *node = &tree->items[row_index];
  size_t pos = node->first_token;
  size_t end = node->first_token + node->token_count;
  if (row_token_text_at(tokens, pos, end, "pub")) pos++;
  row_expect_text(tokens, &pos, end, diag, "choice", "expected choice declaration");
  const ZRowToken *name = row_expect_word(tokens, &pos, end, diag, "expected choice name");
  Choice item = {.line = node->line, .column = node->column};
  if (name) item.name = z_strdup(name->text);
  if (!row_expect_end(tokens, pos, end, diag, "choice declaration")) return;
  for (size_t i = 0; i < tree->len && !row_has_diag(diag); i++) {
    if (tree->items[i].parent != row_index) continue;
    const ZRowNode *child = &tree->items[i];
    size_t child_pos = child->first_token;
    size_t child_end = child->first_token + child->token_count;
    const ZRowToken *case_name = row_expect_word(tokens, &child_pos, child_end, diag, "expected choice case name");
    if (!case_name) continue;
    char *type = child_pos < child_end ? row_parse_type_text(tokens, &child_pos, child_end, diag) : NULL;
    row_expect_end(tokens, child_pos, child_end, diag, "choice case");
    row_push_param(&item.cases, (Param){.name = z_strdup(case_name->text), .type = type, .line = case_name->line, .column = case_name->column});
    row_reject_child_rows(tree, i, diag, "choice case");
  }
  row_push_choice(&program->choices, item);
}

Program z_parse_row(const ZRowTokenVec *tokens, const ZRowTree *tree, ZDiag *diag) {
  Program program = {0};
  if (!tokens || !tree) {
    row_diag(diag, 1, 1, 1, "missing row syntax input", "row tokens and layout tree", NULL);
    return program;
  }

  static size_t test_counter = 0;
  for (size_t i = 0; i < tree->len; i++) {
    if (tree->items[i].parent != Z_ROW_NO_PARENT) continue;
    const ZRowNode *node = &tree->items[i];
    size_t pos = node->first_token;
    size_t end = node->first_token + node->token_count;
    bool is_public = false;
    bool export_c = false;
    const ZRowToken *export_token = NULL;
    if (row_token_text_at(tokens, pos, end, "pub")) {
      is_public = true;
      pos++;
    }
    if (row_token_text_at(tokens, pos, end, "export")) {
      export_token = &tokens->items[pos];
      export_c = true;
      pos++;
      if (row_token_text_at(tokens, pos, end, "c")) pos++;
    }
    if (export_c && !row_token_text_at(tokens, pos, end, "fn")) {
      row_diag(diag, export_token ? export_token->line : node->line, export_token ? export_token->column : node->column, 1, "export c only applies to function declarations", "fn declaration", "remove export c or change the row to a function declaration");
      return program;
    }

    if (row_token_text_at(tokens, pos, end, "use")) {
      if (is_public) {
        row_diag(diag, node->line, node->column, 1, "pub use is not supported in row syntax", "use declaration without pub", "remove pub from the import row");
        return program;
      }
      pos++;
      size_t module_start = pos;
      size_t module_end = pos;
      while (module_end < end && !row_token_text(tokens, module_end, "as")) module_end++;
      if (module_end == module_start) {
        const ZRowToken *token = module_start < end ? &tokens->items[module_start] : &tokens->items[pos - 1];
        row_diag(diag, token->line, token->column, 1, "expected import module name", "import module name", NULL);
        return program;
      }
      if (!row_validate_use_module(tokens, module_start, module_end, diag)) return program;
      char *alias = NULL;
      int end_column = node->column;
      if (module_end > module_start) {
        const ZRowToken *last_module = &tokens->items[module_end - 1];
        end_column = last_module->column + (int)last_module->length;
      }
      if (row_token_text_at(tokens, module_end, end, "as")) {
        size_t alias_pos = module_end + 1;
        const ZRowToken *alias_token = row_expect_word(tokens, &alias_pos, end, diag, "expected import alias");
        if (alias_token) {
          alias = z_strdup(alias_token->text);
          end_column = alias_token->column + (int)alias_token->length;
        }
        if (alias_pos < end) {
          row_diag(diag, tokens->items[alias_pos].line, tokens->items[alias_pos].column, 1, "expected end of row after import alias", "end of row", NULL);
        }
      }
      row_push_use(&program.use_imports, (UseImport){
        .module = row_join_tokens(tokens, module_start, module_end),
        .alias = alias,
        .line = node->line,
        .column = node->column,
        .end_column = end_column
      });
      row_reject_child_rows(tree, i, diag, "use declaration");
    } else if (row_token_text_at(tokens, pos, end, "const")) {
      pos++;
      const ZRowToken *name = row_expect_word(tokens, &pos, end, diag, "expected const name");
      ConstDecl item = {.is_public = is_public, .line = node->line, .column = node->column};
      if (name) item.name = z_strdup(name->text);
      item.type = row_parse_type_text(tokens, &pos, end, diag);
      item.expr = row_parse_expr_range(tokens, pos, end, diag);
      row_push_const(&program.consts, item);
      row_reject_child_rows(tree, i, diag, "const declaration");
    } else if (row_token_text_at(tokens, pos, end, "fn")) {
      row_push_function(&program.functions, row_parse_function_decl(tokens, tree, i, is_public, export_c, diag));
    } else if (row_token_text_at(tokens, pos, end, "type")) {
      row_parse_type_decl(tokens, tree, i, is_public, &program, diag);
    } else if (row_token_text_at(tokens, pos, end, "enum")) {
      row_parse_enum_decl(tokens, tree, i, &program, diag);
    } else if (row_token_text_at(tokens, pos, end, "choice")) {
      row_parse_choice_decl(tokens, tree, i, &program, diag);
    } else if (row_token_text_at(tokens, pos, end, "test")) {
      pos++;
      const ZRowToken *name = pos < end && tokens->items[pos].kind == Z_ROW_TOKEN_STRING ? &tokens->items[pos++] : NULL;
      if (!row_expect_end(tokens, pos, end, diag, "test declaration")) return program;
      ZBuf generated;
      zbuf_init(&generated);
      zbuf_appendf(&generated, "__zero_test_%zu", test_counter++);
      Function fun = {
        .name = generated.data,
        .test_name = name ? z_strdup(name->text) : z_strdup(""),
        .return_type = z_strdup("Void"),
        .is_test = true,
        .body = row_parse_child_statements(tokens, tree, i, diag),
        .line = node->line,
        .column = node->column
      };
      row_push_function(&program.functions, fun);
    } else {
      row_diag(diag, node->line, node->column, 1, "expected row declaration", "use, const, fn, type, enum, choice, or test", NULL);
      return program;
    }
    if (row_has_diag(diag)) return program;
  }

  return program;
}

bool z_row_parse_layout(const ZRowTokenVec *tokens, ZRowTree *tree, ZDiag *diag) {
  if (!tokens || !tree) {
    row_diag(diag, 1, 1, 1, "missing row token stream", "tokens", NULL);
    return false;
  }

  memset(tree, 0, sizeof(*tree));
  size_t *parents = NULL;
  size_t parent_len = 0;
  size_t parent_cap = 0;
  row_parent_stack_push(&parents, &parent_len, &parent_cap, Z_ROW_NO_PARENT);

  size_t depth = 0;
  size_t last_row = Z_ROW_NO_PARENT;
  size_t first_token = Z_ROW_NO_PARENT;
  size_t token_count = 0;

  for (size_t i = 0; i < tokens->len; i++) {
    const ZRowToken *token = &tokens->items[i];
    switch (token->kind) {
      case Z_ROW_TOKEN_INDENT:
        if (first_token != Z_ROW_NO_PARENT) {
          row_diag(diag, token->line, token->column, 1, "indent appeared before row newline", "newline before indent", NULL);
          free(parents);
          return false;
        }
        if (last_row == Z_ROW_NO_PARENT) {
          row_diag(diag, token->line, token->column, 1, "indented row has no parent row", "parent row before indentation", NULL);
          free(parents);
          return false;
        }
        depth++;
        if (parent_len <= depth) row_parent_stack_push(&parents, &parent_len, &parent_cap, last_row);
        else parents[depth] = last_row;
        break;
      case Z_ROW_TOKEN_DEDENT:
        if (first_token != Z_ROW_NO_PARENT) {
          if (!row_dedent_leads_to_eof(tokens, i)) {
            row_diag(diag, token->line, token->column, 1, "dedent appeared before row newline", "newline before dedent", NULL);
            free(parents);
            return false;
          }
          last_row = row_finalize_node(tree, parents[depth], first_token, token_count, depth, tokens);
          first_token = Z_ROW_NO_PARENT;
          token_count = 0;
        }
        if (depth == 0) {
          row_diag(diag, token->line, token->column, 1, "row layout contains an unmatched dedent", "matching indentation", NULL);
          free(parents);
          return false;
        }
        depth--;
        break;
      case Z_ROW_TOKEN_NEWLINE:
        if (first_token != Z_ROW_NO_PARENT) {
          last_row = row_finalize_node(tree, parents[depth], first_token, token_count, depth, tokens);
          first_token = Z_ROW_NO_PARENT;
          token_count = 0;
        }
        break;
      case Z_ROW_TOKEN_COMMENT:
        break;
      case Z_ROW_TOKEN_EOF:
        if (first_token != Z_ROW_NO_PARENT) {
          last_row = row_finalize_node(tree, parents[depth], first_token, token_count, depth, tokens);
          first_token = Z_ROW_NO_PARENT;
          token_count = 0;
        }
        (void)last_row;
        break;
      default:
        if (first_token == Z_ROW_NO_PARENT) {
          first_token = i;
          token_count = 1;
        } else {
          token_count++;
        }
        break;
    }
  }

  free(parents);
  return true;
}

void z_free_row_tree(ZRowTree *tree) {
  if (!tree) return;
  free(tree->items);
  tree->items = NULL;
  tree->len = 0;
  tree->cap = 0;
}

void z_free_row_tokens(ZRowTokenVec *tokens) {
  if (!tokens) return;
  for (size_t i = 0; i < tokens->len; i++) free(tokens->items[i].text);
  free(tokens->items);
  tokens->items = NULL;
  tokens->len = 0;
  tokens->cap = 0;
}
