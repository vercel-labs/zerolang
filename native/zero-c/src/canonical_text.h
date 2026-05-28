#ifndef ZERO_C_CANONICAL_TEXT_H
#define ZERO_C_CANONICAL_TEXT_H

#include "zero.h"

typedef enum {
  Z_CANON_TOKEN_WORD,
  Z_CANON_TOKEN_STRING,
  Z_CANON_TOKEN_CHAR,
  Z_CANON_TOKEN_NUMBER,
  Z_CANON_TOKEN_SYMBOL,
  Z_CANON_TOKEN_COMMENT,
  Z_CANON_TOKEN_NEWLINE,
  Z_CANON_TOKEN_EOF
} ZCanonicalTokenKind;

typedef struct {
  ZCanonicalTokenKind kind;
  char *text;
  int line;
  int column;
  size_t offset;
  size_t length;
} ZCanonicalToken;

typedef struct {
  ZCanonicalToken *items;
  size_t len;
  size_t cap;
} ZCanonicalTokenVec;

typedef enum {
  Z_CANON_NODE_DECL,
  Z_CANON_NODE_BLOCK,
  Z_CANON_NODE_STMT
} ZCanonicalNodeKind;

typedef struct {
  ZCanonicalNodeKind kind;
  size_t first_token;
  size_t token_count;
  size_t depth;
  int line;
  int column;
} ZCanonicalNode;

typedef struct {
  ZCanonicalNode *items;
  size_t len;
  size_t cap;
} ZCanonicalTree;

typedef struct {
  size_t declaration_count;
  size_t function_count;
  size_t type_count;
  size_t enum_count;
  size_t choice_count;
  size_t interface_count;
  size_t test_count;
  size_t block_count;
  size_t statement_count;
  size_t comment_count;
  size_t max_block_depth;
} ZCanonicalFacts;

ZCanonicalTokenVec z_canonical_text_tokenize(const char *source, ZDiag *diag);
bool z_canonical_text_parse(const ZCanonicalTokenVec *tokens, ZCanonicalTree *tree, ZCanonicalFacts *facts, ZDiag *diag);
bool z_canonical_text_format(const ZCanonicalTokenVec *tokens, ZBuf *out, ZDiag *diag);
bool z_canonical_text_format_source(const char *source, ZBuf *out, ZDiag *diag);
bool z_canonical_text_write_program(const Program *program, ZBuf *out, ZDiag *diag);
Program z_parse_canonical_text_program(const ZCanonicalTokenVec *tokens, ZDiag *diag);
bool z_parse_canonical_text_program_source(const char *source, Program *out, ZDiag *diag);
void z_free_canonical_text_tokens(ZCanonicalTokenVec *tokens);
void z_free_canonical_text_tree(ZCanonicalTree *tree);

#endif
