// External scanner for tree-sitter-zero.
//
// Purpose: emit a `GENERIC_LT` token for a `<` that opens generic arguments
// at a call site (i.e., `foo<T, U>(args)`). This unblocks the nested-generic
// call form where tree-sitter's GLR otherwise commits to a binary-comparison
// parse before discovering the `,` doesn't fit.
//
// Strategy: when the parser is in a state where GENERIC_LT is valid, look
// ahead through a balanced `<...>` run. If the closing `>` is followed by
// `(`, emit GENERIC_LT (consuming only the leading `<`). Otherwise return
// false and let tree-sitter's normal lexer fall back to regular `<`.

#include "tree_sitter/parser.h"
#include <wctype.h>

enum TokenType {
  GENERIC_LT,
};

// Safety cap on the lookahead loop. Generic args longer than this aren't
// reachable in practice and a malformed input shouldn't pin the parser.
#define MAX_GENERIC_LOOKAHEAD 1024

void *tree_sitter_zero_external_scanner_create(void) { return NULL; }
void tree_sitter_zero_external_scanner_destroy(void *payload) { (void)payload; }
unsigned tree_sitter_zero_external_scanner_serialize(void *payload, char *buffer) {
  (void)payload; (void)buffer;
  return 0;
}
void tree_sitter_zero_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  (void)payload; (void)buffer; (void)length;
}

bool tree_sitter_zero_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  (void)payload;

  if (!valid_symbols[GENERIC_LT]) return false;

  // Skip leading whitespace ourselves so we can look at the next significant byte.
  while (iswspace(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }

  if (lexer->lookahead != '<') return false;

  // Consume the `<` and lock that as the end of the emitted token.
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);

  // Scan ahead through balanced angle brackets without extending the token.
  int depth = 1;
  int budget = MAX_GENERIC_LOOKAHEAD;
  while (budget-- > 0 && lexer->lookahead != 0) {
    int32_t ch = lexer->lookahead;
    if (ch == '<') {
      depth++;
    } else if (ch == '>') {
      depth--;
      if (depth == 0) {
        lexer->advance(lexer, false);
        while (iswspace(lexer->lookahead)) {
          lexer->advance(lexer, false);
        }
        if (lexer->lookahead == '(') {
          lexer->result_symbol = GENERIC_LT;
          return true;
        }
        return false;
      }
    } else if (ch == '{' || ch == '}' || ch == ';') {
      // Statement-level tokens — definitely not inside a generic-args list.
      return false;
    }
    lexer->advance(lexer, false);
  }

  return false;
}
