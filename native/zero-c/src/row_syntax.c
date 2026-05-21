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
