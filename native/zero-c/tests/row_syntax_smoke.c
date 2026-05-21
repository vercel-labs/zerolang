#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *z_checked_malloc(size_t size) {
  void *ptr = malloc(size ? size : 1);
  if (!ptr) abort();
  return ptr;
}

void *z_checked_reallocarray(void *ptr, size_t count, size_t item_size) {
  if (item_size && count > ((size_t)-1) / item_size) abort();
  void *next = realloc(ptr, count * item_size);
  if (!next && count) abort();
  return next;
}

size_t z_grow_capacity(size_t current, size_t required, size_t initial) {
  size_t next = current ? current : initial;
  while (next < required) next *= 2;
  return next;
}

char *z_strdup(const char *text) {
  size_t len = strlen(text ? text : "");
  char *copy = z_checked_malloc(len + 1);
  memcpy(copy, text ? text : "", len + 1);
  return copy;
}

char *z_strndup(const char *text, size_t len) {
  char *copy = z_checked_malloc(len + 1);
  memcpy(copy, text, len);
  copy[len] = 0;
  return copy;
}

static void expect(bool ok, const char *message) {
  if (ok) return;
  fprintf(stderr, "%s\n", message);
  exit(1);
}

static size_t count_kind(const ZRowTokenVec *tokens, ZRowTokenKind kind) {
  size_t count = 0;
  for (size_t i = 0; i < tokens->len; i++) {
    if (tokens->items[i].kind == kind) count++;
  }
  return count;
}

static bool has_text(const ZRowTokenVec *tokens, ZRowTokenKind kind, const char *text) {
  for (size_t i = 0; i < tokens->len; i++) {
    if (tokens->items[i].kind == kind && strcmp(tokens->items[i].text, text) == 0) return true;
  }
  return false;
}

static void tokenizes_layout_and_trivia(void) {
  const char *source =
    "# leading comment\n"
    "pub fn main Void world World !\n"
    "  let point Point . x 1 y 2 # trailing comment\n"
    "  check world.out.write \"ok\\n\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  expect(count_kind(&tokens, Z_ROW_TOKEN_INDENT) == 1, "expected one indent token");
  expect(count_kind(&tokens, Z_ROW_TOKEN_DEDENT) == 1, "expected one dedent token");
  expect(count_kind(&tokens, Z_ROW_TOKEN_COMMENT) == 2, "expected leading and trailing comments");
  expect(has_text(&tokens, Z_ROW_TOKEN_STRING, "ok\n"), "expected decoded string token");
  expect(has_text(&tokens, Z_ROW_TOKEN_SYMBOL, "."), "expected shape literal marker token");

  ZRowSyntaxFacts facts = {0};
  expect(z_row_analyze_layout(&tokens, &facts, &diag), diag.message);
  expect(facts.row_count == 3, "expected three source rows");
  expect(facts.comment_count == 2, "expected two comments");
  expect(facts.max_indent_depth == 1, "expected one indentation level");
  z_free_row_tokens(&tokens);
}

static void tracks_nested_dedents(void) {
  const char *source =
    "type Point\n"
    "  fn move Void self mutref<Self> dx i32 dy i32\n"
    "    set self.x (+ self.x dx)\n"
    "pub fn main Void\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  expect(count_kind(&tokens, Z_ROW_TOKEN_INDENT) == 2, "expected nested indents");
  expect(count_kind(&tokens, Z_ROW_TOKEN_DEDENT) == 2, "expected nested dedents");
  ZRowSyntaxFacts facts = {0};
  expect(z_row_analyze_layout(&tokens, &facts, &diag), diag.message);
  expect(facts.row_count == 4, "expected four rows");
  expect(facts.max_indent_depth == 2, "expected two indentation levels");

  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  expect(tree.len == 4, "expected four row nodes");
  expect(tree.items[0].parent == Z_ROW_NO_PARENT, "expected top-level type row");
  expect(tree.items[1].parent == 0, "expected method row parent");
  expect(tree.items[2].parent == 1, "expected method body row parent");
  expect(tree.items[3].parent == Z_ROW_NO_PARENT, "expected top-level function row");
  expect(tree.items[2].indent_depth == 2, "expected method body depth");
  expect(strcmp(tokens.items[tree.items[2].first_token].text, "set") == 0, "expected method body token span");
  expect(tree.items[2].token_count > 1, "expected method body tokens");
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void accepts_trailing_whitespace_only_rows(void) {
  ZDiag diag = {0};
  ZRowTokenVec top_tokens = z_row_tokenize("pub fn main Void\n ", &diag);
  expect(diag.code == 0, diag.message);
  expect(count_kind(&top_tokens, Z_ROW_TOKEN_INDENT) == 0, "expected no indent for trailing blank row");
  expect(count_kind(&top_tokens, Z_ROW_TOKEN_DEDENT) == 0, "expected no dedent for trailing blank row");
  ZRowSyntaxFacts facts = {0};
  expect(z_row_analyze_layout(&top_tokens, &facts, &diag), diag.message);
  expect(facts.row_count == 1, "expected trailing blank row not to count as a source row");
  z_free_row_tokens(&top_tokens);

  diag = (ZDiag){0};
  const char *nested_source =
    "pub fn main Void\n"
    "  check world.out.write \"ok\\n\"\n"
    "  ";
  ZRowTokenVec nested_tokens = z_row_tokenize(nested_source, &diag);
  expect(diag.code == 0, diag.message);
  expect(count_kind(&nested_tokens, Z_ROW_TOKEN_INDENT) == 1, "expected nested row indent");
  expect(count_kind(&nested_tokens, Z_ROW_TOKEN_DEDENT) == 1, "expected EOF dedent after trailing blank row");
  facts = (ZRowSyntaxFacts){0};
  expect(z_row_analyze_layout(&nested_tokens, &facts, &diag), diag.message);
  expect(facts.row_count == 2, "expected two source rows with trailing blank row");

  ZRowTree tree = {0};
  expect(z_row_parse_layout(&nested_tokens, &tree, &diag), diag.message);
  expect(tree.len == 2, "expected two row nodes with trailing blank row");
  expect(tree.items[1].parent == 0, "expected nested row parent after trailing blank row");
  z_free_row_tree(&tree);
  z_free_row_tokens(&nested_tokens);
}

static void accepts_indented_final_row_without_newline(void) {
  const char *source =
    "pub fn main Void\n"
    "  check world.out.write \"ok\\n\"";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  expect(count_kind(&tokens, Z_ROW_TOKEN_INDENT) == 1, "expected nested final row indent");
  expect(count_kind(&tokens, Z_ROW_TOKEN_DEDENT) == 1, "expected EOF dedent after nested final row");

  ZRowSyntaxFacts facts = {0};
  expect(z_row_analyze_layout(&tokens, &facts, &diag), diag.message);
  expect(facts.row_count == 2, "expected final row without newline to count");
  expect(facts.max_indent_depth == 1, "expected final row indentation depth");

  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  expect(tree.len == 2, "expected two row nodes without final newline");
  expect(tree.items[1].parent == 0, "expected final row parent without newline");
  expect(strcmp(tokens.items[tree.items[1].first_token].text, "check") == 0, "expected final row token span");
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void rejects_tabs(void) {
  const char *source =
    "pub fn main Void\n"
    "\tcheck world.out.write \"bad\\n\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 100, "expected tab rejection");
  expect(strstr(diag.message, "tabs") != NULL, "expected tab diagnostic");
  z_free_row_tokens(&tokens);
}

static void rejects_odd_indent(void) {
  const char *source =
    "pub fn main Void\n"
    " check world.out.write \"bad\\n\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 100, "expected odd-indent rejection");
  expect(strstr(diag.message, "two-space") != NULL, "expected two-space diagnostic");
  z_free_row_tokens(&tokens);
}

static void rejects_indent_jump(void) {
  const char *source =
    "pub fn main Void\n"
    "    if true\n"
    "      check world.out.write \"bad\\n\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 100, "expected indentation jump rejection");
  expect(strstr(diag.message, "one level") != NULL, "expected one-level diagnostic");
  z_free_row_tokens(&tokens);
}

static void rejects_escaped_string_newline(void) {
  const char *source =
    "pub fn main Void\n"
    "  check world.out.write \"bad\\\n"
    "next\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 100, "expected escaped string newline rejection");
  expect(strstr(diag.message, "unterminated string") != NULL, "expected unterminated string diagnostic");
  z_free_row_tokens(&tokens);
}

static void rejects_short_hex_character_escape(void) {
  const char *source = "pub fn main Void\n  let bad u8 '\\x";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 100, "expected short hex character escape rejection");
  expect(strstr(diag.message, "hex") != NULL, "expected hex escape diagnostic");
  z_free_row_tokens(&tokens);
}

int main(void) {
  tokenizes_layout_and_trivia();
  tracks_nested_dedents();
  accepts_trailing_whitespace_only_rows();
  accepts_indented_final_row_without_newline();
  rejects_tabs();
  rejects_odd_indent();
  rejects_indent_jump();
  rejects_escaped_string_newline();
  rejects_short_hex_character_escape();
  printf("row syntax smoke ok\n");
  return 0;
}
