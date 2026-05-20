#include "zero.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void push_token(TokenVec *vec, Token token) {
  if (vec->len + 1 > vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 64);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(Token));
  }
  vec->items[vec->len++] = token;
}

static Token make_token(TokenKind kind, char *text, int line, int column, size_t offset, size_t length) {
  return (Token){kind, text, line, column, offset, length};
}

static bool is_keyword(const char *text) {
  const char *keywords[] = {
    "as", "break", "check", "choice", "const", "continue", "decreases", "defer", "else", "enum", "export", "extern", "false", "for", "fun",
    "if", "import", "in", "let", "match", "meta", "mut", "null", "packed", "pub",
    "raise", "raises", "rescue", "return", "shape", "static", "test", "true", "type",
    "use", "var", "while", NULL
  };
  for (int i = 0; keywords[i]; i++) {
    if (strcmp(text, keywords[i]) == 0) return true;
  }
  return false;
}

static bool two_char_symbol(const char *source, size_t offset) {
  const char *symbols[] = {"->", "=>", "..", "==", "!=", "<=", ">=", "&&", "||", "+%", "+|", NULL};
  for (int i = 0; symbols[i]; i++) {
    if (source[offset] == symbols[i][0] && source[offset + 1] == symbols[i][1]) return true;
  }
  return false;
}

static int hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

static void set_char_diag(ZDiag *diag, int line, int column, const char *message) {
  diag->code = 3024;
  diag->line = line;
  diag->column = column;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message);
  snprintf(diag->expected, sizeof(diag->expected), "%s", "one byte character literal");
  snprintf(diag->help, sizeof(diag->help), "%s", "use a single ASCII byte or an escape like '\\n', '\\\\', '\\'', or '\\x41'");
}

TokenVec z_tokenize(const char *source, ZDiag *diag) {
  TokenVec tokens = {0};
  size_t offset = 0;
  int line = 1;
  int column = 1;

  while (source[offset]) {
    char ch = source[offset];
    if (isspace((unsigned char)ch)) {
      offset++;
      if (ch == '\n') {
        line++;
        column = 1;
      } else {
        column++;
      }
      continue;
    }

    if (ch == '/' && source[offset + 1] == '/') {
      while (source[offset] && source[offset] != '\n') {
        offset++;
        column++;
      }
      continue;
    }

    int start_line = line;
    int start_column = column;
    size_t start = offset;

    if (isalpha((unsigned char)ch) || ch == '_') {
      while (isalnum((unsigned char)source[offset]) || source[offset] == '_') {
        offset++;
        column++;
      }
      char *text = z_strndup(source + start, offset - start);
      push_token(&tokens, make_token(is_keyword(text) ? TOK_KEYWORD : TOK_IDENT, text, start_line, start_column, start, offset - start));
      continue;
    }

    if (isdigit((unsigned char)ch)) {
      while (isalnum((unsigned char)source[offset]) || source[offset] == '_' || source[offset] == '.' ||
             ((source[offset] == '+' || source[offset] == '-') && offset > start && (source[offset - 1] == 'e' || source[offset - 1] == 'E'))) {
        if (source[offset] == '.' && source[offset + 1] == '.') break;
        offset++;
        column++;
      }
      push_token(&tokens, make_token(TOK_NUMBER, z_strndup(source + start, offset - start), start_line, start_column, start, offset - start));
      continue;
    }

    if (ch == '"') {
      offset++;
      column++;
      ZBuf value;
      zbuf_init(&value);
      while (source[offset] && source[offset] != '"') {
        char next = source[offset++];
        column++;
        if (next == '\\') {
          char escaped = source[offset++];
          column++;
          zbuf_append_char(&value, escaped == 'n' ? '\n' : escaped);
        } else {
          zbuf_append_char(&value, next);
        }
      }
      if (source[offset] != '"') {
        diag->code = 100;
        diag->line = start_line;
        diag->column = start_column;
        snprintf(diag->message, sizeof(diag->message), "unterminated string literal");
        return tokens;
      }
      offset++;
      column++;
      push_token(&tokens, make_token(TOK_STRING, value.data ? value.data : z_strdup(""), start_line, start_column, start, offset - start));
      continue;
    }

    if (ch == '\'') {
      offset++;
      column++;
      unsigned value = 0;
      if (!source[offset] || source[offset] == '\n' || source[offset] == '\'') {
        set_char_diag(diag, start_line, start_column, "malformed character literal");
        return tokens;
      }
      if (source[offset] == '\\') {
        offset++;
        column++;
        char escaped = source[offset];
        if (!escaped || escaped == '\n') {
          set_char_diag(diag, start_line, start_column, "malformed character escape");
          return tokens;
        }
        if (escaped == 'n') value = '\n';
        else if (escaped == 'r') value = '\r';
        else if (escaped == 't') value = '\t';
        else if (escaped == '0') value = '\0';
        else if (escaped == '\'') value = '\'';
        else if (escaped == '"') value = '"';
        else if (escaped == '\\') value = '\\';
        else if (escaped == 'x') {
          int high = hex_digit(source[offset + 1]);
          int low = hex_digit(source[offset + 2]);
          if (high < 0 || low < 0) {
            set_char_diag(diag, start_line, start_column, "malformed hex character escape");
            return tokens;
          }
          value = (unsigned)((high << 4) | low);
          offset += 2;
          column += 2;
        } else {
          set_char_diag(diag, start_line, start_column, "unsupported character escape");
          return tokens;
        }
        offset++;
        column++;
      } else {
        unsigned char byte = (unsigned char)source[offset];
        if (byte >= 128) {
          set_char_diag(diag, start_line, start_column, "character literal must be one byte");
          return tokens;
        }
        value = byte;
        offset++;
        column++;
      }
      if (source[offset] != '\'') {
        set_char_diag(diag, start_line, start_column, "character literal must contain exactly one byte");
        return tokens;
      }
      offset++;
      column++;
      char text[4];
      snprintf(text, sizeof(text), "%u", value);
      push_token(&tokens, make_token(TOK_CHAR, z_strdup(text), start_line, start_column, start, offset - start));
      continue;
    }

    if (two_char_symbol(source, offset)) {
      push_token(&tokens, make_token(TOK_SYMBOL, z_strndup(source + offset, 2), start_line, start_column, start, 2));
      offset += 2;
      column += 2;
      continue;
    }

    if (strchr("(){}[],.;:<>=+-*/%!&", ch)) {
      push_token(&tokens, make_token(TOK_SYMBOL, z_strndup(source + offset, 1), start_line, start_column, start, 1));
      offset++;
      column++;
      continue;
    }

    diag->code = 101;
    diag->line = start_line;
    diag->column = start_column;
    snprintf(diag->message, sizeof(diag->message), "unexpected character '%c'", ch);
    return tokens;
  }

  push_token(&tokens, make_token(TOK_EOF, z_strdup(""), line, column, offset, 0));
  return tokens;
}

void z_free_tokens(TokenVec *tokens) {
  for (size_t i = 0; i < tokens->len; i++) free(tokens->items[i].text);
  free(tokens->items);
  tokens->items = NULL;
  tokens->len = 0;
  tokens->cap = 0;
}
