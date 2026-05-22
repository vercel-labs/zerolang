#include "zero.h"

#include <stdarg.h>
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

void *z_checked_calloc(size_t count, size_t item_size) {
  void *ptr = calloc(count ? count : 1, item_size ? item_size : 1);
  if (!ptr) abort();
  return ptr;
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

void zbuf_init(ZBuf *buf) {
  memset(buf, 0, sizeof(*buf));
}

void zbuf_append_char(ZBuf *buf, char ch) {
  if (buf->len + 1 >= buf->cap) {
    buf->cap = z_grow_capacity(buf->cap, buf->len + 2, 64);
    buf->data = z_checked_reallocarray(buf->data, buf->cap, sizeof(char));
  }
  buf->data[buf->len++] = ch;
  buf->data[buf->len] = 0;
}

void zbuf_append(ZBuf *buf, const char *text) {
  while (text && *text) zbuf_append_char(buf, *text++);
}

void zbuf_appendf(ZBuf *buf, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (needed > 0) {
    size_t start = buf->len;
    for (int i = 0; i < needed; i++) zbuf_append_char(buf, '\0');
    vsnprintf(buf->data + start, (size_t)needed + 1, fmt, args);
    buf->len = start + (size_t)needed;
  }
  va_end(args);
}

void zbuf_free(ZBuf *buf) {
  free(buf->data);
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

static size_t count_trivia(const ZRowTree *tree, ZRowTriviaKind kind) {
  size_t count = 0;
  for (size_t i = 0; i < tree->trivia_len; i++) {
    if (tree->trivia[i].kind == kind) count++;
  }
  return count;
}

static const ZRowTrivia *find_trivia(const ZRowTree *tree, ZRowTriviaKind kind, size_t row) {
  for (size_t i = 0; i < tree->trivia_len; i++) {
    if (tree->trivia[i].kind == kind && tree->trivia[i].row == row) return &tree->trivia[i];
  }
  return NULL;
}

static Program parse_row_program(const char *source, ZRowTokenVec *tokens, ZRowTree *tree) {
  ZDiag diag = {0};
  *tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  expect(z_row_parse_layout(tokens, tree, &diag), diag.message);
  Program program = z_parse_row(tokens, tree, &diag);
  expect(diag.code == 0, diag.message);
  return program;
}

static void expect_row_parse_failure(const char *source, const char *expected_message) {
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  Program program = z_parse_row(&tokens, &tree, &diag);
  expect(diag.code == 100, "expected row parse failure");
  expect(strstr(diag.message, expected_message) != NULL, "expected row parse diagnostic message");
  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
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
  expect(facts.blank_line_count == 0, "expected no blank-line separators");
  expect(facts.max_indent_depth == 1, "expected one indentation level");

  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  expect(tree.trivia_len == 2, "expected row trivia attachments");
  const ZRowTrivia *leading = find_trivia(&tree, Z_ROW_TRIVIA_LEADING_COMMENT, 0);
  expect(leading != NULL, "expected leading comment on function row");
  expect(strcmp(tokens.items[leading->token].text, "# leading comment") == 0, "expected leading comment token");
  const ZRowTrivia *trailing = find_trivia(&tree, Z_ROW_TRIVIA_TRAILING_COMMENT, 1);
  expect(trailing != NULL, "expected trailing comment on let row");
  expect(strcmp(tokens.items[trailing->token].text, "# trailing comment") == 0, "expected trailing comment token");
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void attaches_comment_and_blank_line_trivia(void) {
  const char *source =
    "# file header\n"
    "pub fn main Void\n"
    "  # first binding\n"
    "  let value i32 1 # same row\n"
    "\n"
    "  # return group\n"
    "  ret value\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowSyntaxFacts facts = {0};
  expect(z_row_analyze_layout(&tokens, &facts, &diag), diag.message);
  expect(facts.blank_line_count == 1, "expected one blank line in layout facts");
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  expect(tree.len == 3, "expected three rows with trivia");
  expect(count_trivia(&tree, Z_ROW_TRIVIA_LEADING_COMMENT) == 3, "expected three leading comments");
  expect(count_trivia(&tree, Z_ROW_TRIVIA_TRAILING_COMMENT) == 1, "expected one trailing comment");
  expect(count_trivia(&tree, Z_ROW_TRIVIA_BLANK_LINE) == 1, "expected one blank-line separator");
  const ZRowTrivia *file_header = find_trivia(&tree, Z_ROW_TRIVIA_LEADING_COMMENT, 0);
  expect(file_header && strcmp(tokens.items[file_header->token].text, "# file header") == 0, "expected file header on function");
  const ZRowTrivia *binding_comment = find_trivia(&tree, Z_ROW_TRIVIA_LEADING_COMMENT, 1);
  expect(binding_comment && strcmp(tokens.items[binding_comment->token].text, "# first binding") == 0, "expected binding leading comment");
  const ZRowTrivia *same_row = find_trivia(&tree, Z_ROW_TRIVIA_TRAILING_COMMENT, 1);
  expect(same_row && strcmp(tokens.items[same_row->token].text, "# same row") == 0, "expected same-row trailing comment");
  const ZRowTrivia *separator = find_trivia(&tree, Z_ROW_TRIVIA_BLANK_LINE, 2);
  expect(separator && separator->line == 5, "expected blank line before return row");
  const ZRowTrivia *return_comment = find_trivia(&tree, Z_ROW_TRIVIA_LEADING_COMMENT, 2);
  expect(return_comment && strcmp(tokens.items[return_comment->token].text, "# return group") == 0, "expected return leading comment");
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void attaches_block_comment_to_empty_block(void) {
  const char *source =
    "pub fn main Void\n"
    "  # intentionally empty\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  expect(tree.len == 1, "expected one row with block comment");
  expect(count_trivia(&tree, Z_ROW_TRIVIA_BLOCK_COMMENT) == 1, "expected one block comment");
  expect(tree.trivia[0].parent == 0, "expected block comment parent row");
  expect(strcmp(tokens.items[tree.trivia[0].token].text, "# intentionally empty") == 0, "expected block comment token");
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void formats_empty_block_comment_before_sibling(void) {
  const char *source =
    "pub fn main Void\n"
    "  if ready\n"
    "    # skipped branch\n"
    "  if done\n"
    "    check world.out.write \"done\\n\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  char *formatted = z_format_row_layout(&tokens, &tree);
  expect(strcmp(formatted, source) == 0, "expected empty block comment to stay with original parent");
  free(formatted);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void formats_block_footer_comment_after_child_rows(void) {
  const char *source =
    "pub fn main Void\n"
    "  if ready\n"
    "    check world.out.write \"ready\\n\"\n"
    "    # branch footer\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  char *formatted = z_format_row_layout(&tokens, &tree);
  expect(strcmp(formatted, source) == 0, "expected block footer comment to stay after child rows");
  free(formatted);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void formats_row_layout_with_trivia(void) {
  const char *source =
    "# file header\n"
    "pub   fn   main Void\n"
    "  # first binding\n"
    "  let   value i32 1 # same row\n"
    "\n"
    "  check world.out.write \"ok\\n\"\n";
  const char *expected =
    "# file header\n"
    "pub fn main Void\n"
    "  # first binding\n"
    "  let value i32 1 # same row\n"
    "\n"
    "  check world.out.write \"ok\\n\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  char *formatted = z_format_row_layout(&tokens, &tree);
  expect(strcmp(formatted, expected) == 0, "expected formatted row layout with trivia");

  ZDiag second_diag = {0};
  ZRowTokenVec second_tokens = z_row_tokenize(formatted, &second_diag);
  expect(second_diag.code == 0, second_diag.message);
  ZRowTree second_tree = {0};
  expect(z_row_parse_layout(&second_tokens, &second_tree, &second_diag), second_diag.message);
  char *formatted_again = z_format_row_layout(&second_tokens, &second_tree);
  expect(strcmp(formatted_again, expected) == 0, "expected row formatter to be idempotent");
  free(formatted_again);
  z_free_row_tree(&second_tree);
  z_free_row_tokens(&second_tokens);

  free(formatted);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void formats_blank_line_before_dedented_sibling(void) {
  const char *source =
    "pub fn main Void\n"
    "  if ready\n"
    "    check world.out.write \"ready\\n\"\n"
    "\n"
    "  if done\n"
    "    check world.out.write \"done\\n\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  char *formatted = z_format_row_layout(&tokens, &tree);
  expect(strcmp(formatted, source) == 0, "expected blank line before dedented sibling to be preserved");
  free(formatted);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void formats_terminal_top_level_comment(void) {
  const char *source =
    "const answer u32 42\n"
    "# keep me\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  char *formatted = z_format_row_layout(&tokens, &tree);
  expect(strcmp(formatted, source) == 0, "expected terminal top-level comment to be preserved");
  free(formatted);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void formats_comment_only_row_layout(void) {
  const char *source = "# file docs\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  expect(tree.len == 0, "expected no row nodes for comment-only source");
  expect(tree.trivia_len == 1, "expected unanchored comment trivia");
  expect(tree.trivia[0].row == Z_ROW_NO_PARENT, "expected comment without row anchor");
  char *formatted = z_format_row_layout(&tokens, &tree);
  expect(strcmp(formatted, source) == 0, "expected comment-only row layout to be preserved");
  free(formatted);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void formats_blank_comment_only_row_layout(void) {
  const char *source =
    "\n"
    "# file docs\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  expect(tree.len == 0, "expected no row nodes for blank/comment-only source");
  expect(count_trivia(&tree, Z_ROW_TRIVIA_BLANK_LINE) == 1, "expected unanchored blank-line trivia");
  expect(count_trivia(&tree, Z_ROW_TRIVIA_LEADING_COMMENT) == 1, "expected unanchored comment trivia");
  char *formatted = z_format_row_layout(&tokens, &tree);
  expect(strcmp(formatted, source) == 0, "expected blank/comment-only row layout to be preserved");
  free(formatted);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void formats_non_ascii_string_as_parseable_bytes(void) {
  const char *source =
    "pub fn main Void\n"
    "  check world.out.write \"\303\251\\n\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  char *formatted = z_format_row_layout(&tokens, &tree);
  expect(strcmp(formatted, source) == 0, "expected non-ASCII string bytes to remain parseable");

  ZDiag second_diag = {0};
  ZRowTokenVec second_tokens = z_row_tokenize(formatted, &second_diag);
  expect(second_diag.code == 0, second_diag.message);
  ZRowTree second_tree = {0};
  expect(z_row_parse_layout(&second_tokens, &second_tree, &second_diag), second_diag.message);
  char *formatted_again = z_format_row_layout(&second_tokens, &second_tree);
  expect(strcmp(formatted_again, source) == 0, "expected non-ASCII string formatting to be idempotent");
  free(formatted_again);
  z_free_row_tree(&second_tree);
  z_free_row_tokens(&second_tokens);

  free(formatted);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void formats_control_string_escape_as_parseable_bytes(void) {
  const char *source =
    "pub fn main Void\n"
    "  check world.out.write \"\\x08\"\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  char *formatted = z_format_row_layout(&tokens, &tree);
  expect(strcmp(formatted, source) == 0, "expected control string escape to remain parseable");

  ZDiag second_diag = {0};
  ZRowTokenVec second_tokens = z_row_tokenize(formatted, &second_diag);
  expect(second_diag.code == 0, second_diag.message);
  ZRowTree second_tree = {0};
  expect(z_row_parse_layout(&second_tokens, &second_tree, &second_diag), second_diag.message);
  char *formatted_again = z_format_row_layout(&second_tokens, &second_tree);
  expect(strcmp(formatted_again, source) == 0, "expected control string formatting to be idempotent");
  free(formatted_again);
  z_free_row_tree(&second_tree);
  z_free_row_tokens(&second_tokens);

  free(formatted);
  z_free_row_tree(&tree);
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

static void parses_core_function_program(void) {
  const char *source =
    "use std.mem as mem\n"
    "extern c \"stdint.h\" as cstdint\n"
    "alias Bytes Span<u8>\n"
    "const answer i32 42\n"
    "const enabled meta target.hasCapability(\"memory\")\n"
    "interface Id\n"
    "  fn id<T> T value T\n"
    "export c fn boot Void\n"
    "fn id<T> T value T\n"
    "  ret value\n"
    "fn validate Bool input String ![InvalidInput ParseError]\n"
    "  ret true\n"
    "extern type CPoint\n"
    "  x i32\n"
    "  y i32\n"
    "packed type PackedByte\n"
    "  value u8\n"
    "type Point\n"
    "  x i32\n"
    "  y i32\n"
    "pub fn main Void world World !\n"
    "  let point Point . x 40 y 2\n"
    "  let bytes [3]u8 [1 2 3]\n"
    "  let got i32 (id<i32> 42)\n"
    "  let total i32 (+ 40 2)\n"
    "  let byte u8 ('A' as u8)\n"
    "  let middle Span<u8> \"zero\"[1..3]\n"
    "  check world.out.write \"ok\\n\"\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  expect(program.use_imports.len == 1, "expected one use import");
  expect(strcmp(program.use_imports.items[0].module, "std.mem") == 0, "expected dotted use import");
  expect(strcmp(program.use_imports.items[0].alias, "mem") == 0, "expected use import alias");
  expect(program.c_imports.len == 1, "expected one C import");
  expect(strcmp(program.c_imports.items[0].alias, "cstdint") == 0, "expected C import alias");
  expect(program.aliases.len == 1, "expected one alias");
  expect(strcmp(program.aliases.items[0].target, "Span<u8>") == 0, "expected alias target type");
  expect(program.consts.len == 2, "expected two consts");
  expect(strcmp(program.consts.items[0].name, "answer") == 0, "expected const name");
  expect(strcmp(program.consts.items[0].type, "i32") == 0, "expected const type");
  expect(program.consts.items[1].type == NULL, "expected inferred const type");
  expect(program.consts.items[1].expr->kind == EXPR_META, "expected meta const expression");
  expect(program.interfaces.len == 1, "expected one interface");
  expect(program.interfaces.items[0].methods.len == 1, "expected one interface method");
  expect(program.shapes.len == 3, "expected three shapes");
  expect(strcmp(program.shapes.items[0].layout, "extern") == 0, "expected extern shape layout");
  expect(strcmp(program.shapes.items[1].layout, "packed") == 0, "expected packed shape layout");
  expect(program.functions.len == 4, "expected four functions");
  expect(program.functions.items[0].export_c, "expected exported function");
  expect(program.functions.items[1].type_params.len == 1, "expected generic function parameter");
  Function *validate_fun = &program.functions.items[2];
  expect(validate_fun->raises, "expected named error function to raise");
  expect(validate_fun->has_error_set, "expected named error set");
  expect(validate_fun->errors.len == 2, "expected named errors");
  expect(strcmp(validate_fun->errors.items[0].name, "InvalidInput") == 0, "expected first named error");
  Function *main_fun = &program.functions.items[3];
  expect(main_fun->is_public, "expected public function");
  expect(main_fun->raises, "expected open fallibility");
  expect(main_fun->params.len == 1, "expected one function parameter");
  expect(strcmp(main_fun->params.items[0].name, "world") == 0, "expected world parameter");
  expect(strcmp(main_fun->params.items[0].type, "World") == 0, "expected World parameter type");
  expect(main_fun->body.len == 7, "expected seven function statements");
  expect(main_fun->body.items[0]->kind == STMT_LET, "expected let statement");
  expect(main_fun->body.items[0]->expr->kind == EXPR_SHAPE_LITERAL, "expected shape literal expression");
  expect(main_fun->body.items[0]->expr->fields.len == 2, "expected shape literal fields");
  expect(main_fun->body.items[1]->expr->kind == EXPR_ARRAY_LITERAL, "expected array literal expression");
  expect(main_fun->body.items[1]->expr->args.len == 3, "expected array literal items");
  expect(main_fun->body.items[2]->expr->kind == EXPR_CALL, "expected generic call expression");
  expect(main_fun->body.items[2]->expr->left->type_args.len == 1, "expected generic call type argument");
  expect(main_fun->body.items[3]->expr->kind == EXPR_BINARY, "expected prefix operator expression");
  expect(main_fun->body.items[4]->expr->kind == EXPR_CAST, "expected cast expression");
  expect(main_fun->body.items[5]->expr->kind == EXPR_SLICE, "expected slice expression");
  expect(main_fun->body.items[6]->kind == STMT_CHECK, "expected check statement");
  expect(main_fun->body.items[6]->expr->kind == EXPR_CALL, "expected row call expression");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void parses_shape_literal_inside_array_literal(void) {
  const char *source =
    "type Point\n"
    "  x i32\n"
    "pub fn main Void\n"
    "  let points [1]Point [Point . x 7]\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  Function *main_fun = &program.functions.items[0];
  expect(main_fun->body.len == 1, "expected one main statement");
  Expr *array = main_fun->body.items[0]->expr;
  expect(array->kind == EXPR_ARRAY_LITERAL, "expected array literal expression");
  expect(array->args.len == 1, "expected one array literal item");
  Expr *point = array->args.items[0];
  expect(point->kind == EXPR_SHAPE_LITERAL, "expected nested shape literal expression");
  expect(strcmp(point->text, "Point") == 0, "expected nested shape literal type");
  expect(point->fields.len == 1, "expected nested shape literal field");
  expect(strcmp(point->fields.items[0].name, "x") == 0, "expected nested shape literal field name");
  expect(point->fields.items[0].value->kind == EXPR_NUMBER, "expected nested shape literal field value");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void parses_control_flow_statements(void) {
  const char *source =
    "fn classify i32 value i32\n"
    "  if <= value 0\n"
    "    ret 0\n"
    "  else if == value 1\n"
    "    ret 1\n"
    "  else\n"
    "    mut total i32 0\n"
    "    while < total value\n"
    "      set total (+ total 1)\n"
    "      if == total 2\n"
    "        continue\n"
    "    for index 0..3\n"
    "      if == index 2\n"
    "        break\n"
    "    defer cleanup total\n"
    "    ret total\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  Function *fun = &program.functions.items[0];
  expect(fun->body.len == 1, "expected one outer statement");
  Stmt *outer = fun->body.items[0];
  expect(outer->kind == STMT_IF, "expected if statement");
  expect(outer->then_body.len == 1, "expected if body");
  expect(outer->else_body.len == 1, "expected else-if body");
  Stmt *else_if = outer->else_body.items[0];
  expect(else_if->kind == STMT_IF, "expected else-if to map to nested if");
  expect(else_if->else_body.len == 5, "expected else block statements");
  expect(else_if->else_body.items[0]->kind == STMT_LET, "expected mutable binding in else");
  expect(else_if->else_body.items[0]->mutable_binding, "expected mut binding");
  expect(else_if->else_body.items[1]->kind == STMT_WHILE, "expected while statement");
  expect(else_if->else_body.items[1]->then_body.len == 2, "expected while body");
  expect(else_if->else_body.items[1]->then_body.items[1]->then_body.items[0]->kind == STMT_CONTINUE, "expected nested continue");
  expect(else_if->else_body.items[2]->kind == STMT_FOR, "expected for statement");
  expect(strcmp(else_if->else_body.items[2]->name, "index") == 0, "expected loop binding name");
  expect(else_if->else_body.items[2]->range_end->kind == EXPR_NUMBER, "expected range end expression");
  expect(else_if->else_body.items[2]->then_body.items[0]->then_body.items[0]->kind == STMT_BREAK, "expected nested break");
  expect(else_if->else_body.items[3]->kind == STMT_DEFER, "expected defer statement");
  expect(else_if->else_body.items[4]->kind == STMT_RETURN, "expected final return");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void parses_match_statement(void) {
  const char *source =
    "fn bucket i32 value u8\n"
    "  match value\n"
    "    0\n"
    "      ret 0\n"
    "    1..3\n"
    "      ret 1\n"
    "    _\n"
    "      ret 9\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  Function *fun = &program.functions.items[0];
  expect(fun->body.len == 1, "expected one match statement");
  Stmt *match = fun->body.items[0];
  expect(match->kind == STMT_MATCH, "expected match statement");
  expect(match->match_arms.len == 3, "expected three match arms");
  expect(strcmp(match->match_arms.items[1].case_name, "1") == 0, "expected range start case");
  expect(strcmp(match->match_arms.items[1].range_end, "3") == 0, "expected range end case");
  expect(strcmp(match->match_arms.items[2].case_name, "_") == 0, "expected fallback case");
  expect(match->match_arms.items[2].body.items[0]->kind == STMT_RETURN, "expected fallback body");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void parses_core_data_declarations(void) {
  const char *source =
    "type Point\n"
    "  x i32\n"
    "  y i32 0\n"
    "  fn clear Void self mutref<Self>\n"
    "    set self.x 0\n"
    "pub enum Mode\n"
    "  off\n"
    "  on\n"
    "pub choice Result\n"
    "  ok i32\n"
    "  err String\n"
    "test \"adds\"\n"
    "  expect == (+ 1 1) 2\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  expect(program.shapes.len == 1, "expected one shape");
  expect(strcmp(program.shapes.items[0].name, "Point") == 0, "expected shape name");
  expect(program.shapes.items[0].fields.len == 2, "expected shape fields");
  expect(program.shapes.items[0].fields.items[1].default_value, "expected shape field default");
  expect(program.shapes.items[0].fields.items[1].default_value->kind == EXPR_NUMBER, "expected numeric shape field default");
  expect(program.shapes.items[0].methods.len == 1, "expected shape method");
  expect(program.shapes.items[0].methods.items[0].body.len == 1, "expected method body");
  expect(program.shapes.items[0].methods.items[0].body.items[0]->kind == STMT_ASSIGN, "expected method assignment");
  expect(program.enums.len == 1, "expected one enum");
  expect(program.enums.items[0].cases.len == 2, "expected enum cases");
  expect(program.choices.len == 1, "expected one choice");
  expect(program.choices.items[0].cases.len == 2, "expected choice cases");
  expect(strcmp(program.choices.items[0].cases.items[1].type, "String") == 0, "expected choice payload type");
  expect(program.functions.len == 1, "expected one test function");
  expect(program.functions.items[0].is_test, "expected test function");
  expect(program.functions.items[0].body.items[0]->expr->kind == EXPR_CALL, "expected test expression call");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void parses_zero_arg_and_uppercase_member_calls(void) {
  const char *source =
    "fn answer i32\n"
    "  ret 42\n"
    "choice Result\n"
    "  ok i32\n"
    "  err String\n"
    "pub fn main Void\n"
    "  let value i32 answer()\n"
    "  let result Result.ok value\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  expect(program.functions.len == 2, "expected answer and main functions");
  Function *main_fun = &program.functions.items[1];
  expect(main_fun->body.len == 2, "expected two main statements");
  Expr *zero_arg = main_fun->body.items[0]->expr;
  expect(zero_arg->kind == EXPR_CALL, "expected connected parens to parse as a call");
  expect(zero_arg->args.len == 0, "expected zero call arguments");
  expect(zero_arg->left && zero_arg->left->kind == EXPR_IDENT, "expected zero-arg callee identifier");
  expect(strcmp(zero_arg->left->text, "answer") == 0, "expected answer callee");

  Stmt *constructor_stmt = main_fun->body.items[1];
  expect(constructor_stmt->type == NULL, "expected uppercase member call not to parse as a type annotation");
  expect(constructor_stmt->expr->kind == EXPR_CALL, "expected choice constructor call expression");
  expect(constructor_stmt->expr->left->kind == EXPR_MEMBER, "expected choice constructor member callee");
  expect(strcmp(constructor_stmt->expr->left->text, "ok") == 0, "expected ok choice constructor");
  expect(constructor_stmt->expr->args.len == 1, "expected one choice constructor argument");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void parses_check_space_call_expression(void) {
  const char *source =
    "fn parse i32 input i32 !\n"
    "  ret input\n"
    "pub fn main Void !\n"
    "  let value i32 check parse 41\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  expect(program.functions.len == 2, "expected parse and main functions");
  Function *main_fun = &program.functions.items[1];
  expect(main_fun->body.len == 1, "expected one main statement");
  Expr *checked = main_fun->body.items[0]->expr;
  expect(checked->kind == EXPR_CHECK, "expected let initializer to be a check expression");
  expect(checked->left && checked->left->kind == EXPR_CALL, "expected check operand to be the row call");
  expect(checked->left->left && checked->left->left->kind == EXPR_IDENT, "expected checked callee identifier");
  expect(strcmp(checked->left->left->text, "parse") == 0, "expected parse callee");
  expect(checked->left->args.len == 1, "expected checked row call argument");
  expect(checked->left->args.items[0]->kind == EXPR_NUMBER, "expected numeric checked argument");
  expect(strcmp(checked->left->args.items[0]->text, "41") == 0, "expected checked argument value");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void parses_lowercase_primitive_annotations(void) {
  const char *source =
    "pub fn main Void\n"
    "  let letter char 'A'\n"
    "  let small i8 1\n"
    "  let medium i16 2\n"
    "  let wide isize 3\n"
    "  let single f32 1.25\n"
    "  let double f64 2.5\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  Function *main_fun = &program.functions.items[0];
  expect(main_fun->body.len == 6, "expected all lowercase primitive annotations to parse");
  expect(strcmp(main_fun->body.items[0]->type, "char") == 0, "expected char annotation");
  expect(strcmp(main_fun->body.items[1]->type, "i8") == 0, "expected i8 annotation");
  expect(strcmp(main_fun->body.items[2]->type, "i16") == 0, "expected i16 annotation");
  expect(strcmp(main_fun->body.items[3]->type, "isize") == 0, "expected isize annotation");
  expect(strcmp(main_fun->body.items[4]->type, "f32") == 0, "expected f32 annotation");
  expect(strcmp(main_fun->body.items[5]->type, "f64") == 0, "expected f64 annotation");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void parses_static_array_length_names(void) {
  const char *source =
    "fn first<static N: usize> u8 bytes [N]u8\n"
    "  ret bytes[0]\n"
    "pub fn main Void\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  Function *fun = &program.functions.items[0];
  expect(fun->params.len == 1, "expected one array parameter");
  expect(strcmp(fun->params.items[0].type, "[N]u8") == 0, "expected named array length type");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void parses_untyped_array_literals(void) {
  const char *source =
    "pub fn main Void\n"
    "  let values [1 2]\n";
  ZRowTokenVec tokens = {0};
  ZRowTree tree = {0};
  Program program = parse_row_program(source, &tokens, &tree);

  Function *main_fun = &program.functions.items[0];
  expect(main_fun->body.len == 1, "expected one statement");
  expect(main_fun->body.items[0]->type == NULL, "expected no explicit array type");
  expect(main_fun->body.items[0]->expr->kind == EXPR_ARRAY_LITERAL, "expected array literal expression");
  expect(main_fun->body.items[0]->expr->args.len == 2, "expected two array literal items");

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void rejects_unbracketed_named_errors(void) {
  const char *source = "fn validate i32 ok Bool ! InvalidInput\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  Program program = z_parse_row(&tokens, &tree, &diag);
  expect(diag.code == 100, "expected unbracketed named error rejection");
  expect(strstr(diag.message, "'['") != NULL, "expected named error bracket diagnostic");
  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void rejects_else_after_explicit_else_block(void) {
  const char *source =
    "fn pick i32 a Bool b Bool\n"
    "  if a\n"
    "    ret 1\n"
    "  else\n"
    "    if b\n"
    "      ret 2\n"
    "  else\n"
    "    ret 3\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  Program program = z_parse_row(&tokens, &tree, &diag);
  expect(diag.code == 100, "expected explicit else block to close else-if chain");
  expect(strstr(diag.message, "else") != NULL, "expected else diagnostic");
  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void rejects_reserved_word_identifiers(void) {
  expect_row_parse_failure("fn if Void\n", "reserved word");
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  let match i32 1\n",
    "reserved word"
  );
  expect_row_parse_failure(
    "type Point\n"
    "  if i32\n"
    "pub fn main Void\n",
    "reserved word"
  );
}

static void rejects_unconsumed_row_expression_tokens(void) {
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  let x i32 1 )\n",
    "unexpected token"
  );
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  let x i32 1 ]\n",
    "unexpected token"
  );
}

static void rejects_missing_let_initializer_after_type_annotation(void) {
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  let value i32\n",
    "initializer"
  );
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  mut value String\n",
    "initializer"
  );
}

static void rejects_malformed_array_type_lengths(void) {
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  let bytes []u8 []\n",
    "array length"
  );
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  let bytes [1 2]u8 [1]\n",
    "array length"
  );
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  let bytes [pub]u8 []\n",
    "array length"
  );
}

static void rejects_empty_use_import(void) {
  expect_row_parse_failure("use\n", "import module");
  expect_row_parse_failure("pub use std.mem\n", "pub use");
  expect_row_parse_failure("use as mem\n", "import module");
  expect_row_parse_failure("use std.\n", "module segment");
  expect_row_parse_failure("use .std\n", "import module");
  expect_row_parse_failure("use std mem\n", "module");
  expect_row_parse_failure("use pub\n", "import module");
}

static void rejects_unexpected_child_rows(void) {
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  let x i32 1\n"
    "    raise Hidden\n",
    "indented row"
  );
  expect_row_parse_failure(
    "const answer i32 42\n"
    "  raise Hidden\n"
    "pub fn main Void\n",
    "indented row"
  );
  expect_row_parse_failure(
    "type Point\n"
    "  x i32\n"
    "    raise Hidden\n"
    "pub fn main Void\n",
    "indented row"
  );
  expect_row_parse_failure(
    "type Point\n"
    "  pub x i32\n"
    "pub fn main Void\n",
    "pub only applies"
  );
  expect_row_parse_failure(
    "enum Mode\n"
    "  off\n"
    "    raise Hidden\n"
    "pub fn main Void\n",
    "indented row"
  );
  expect_row_parse_failure(
    "choice Result\n"
    "  ok i32\n"
    "    raise Hidden\n"
    "pub fn main Void\n",
    "indented row"
  );
}

static void rejects_trailing_fixed_row_tokens(void) {
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  break ignored\n",
    "unexpected token"
  );
  expect_row_parse_failure(
    "pub fn main Void\n"
    "  raise Hidden ignored\n",
    "unexpected token"
  );
  expect_row_parse_failure(
    "test \"adds\" ignored\n"
    "  expect true\n",
    "unexpected token"
  );
  expect_row_parse_failure(
    "fn validate Void ![InvalidInput] ignored\n",
    "unexpected token"
  );
  expect_row_parse_failure(
    "enum Mode extra ignored\n"
    "  off\n"
    "pub fn main Void\n",
    "unexpected token"
  );
  expect_row_parse_failure(
    "enum Mode\n"
    "  off ignored\n"
    "pub fn main Void\n",
    "unexpected token"
  );
  expect_row_parse_failure(
    "choice Result extra\n"
    "  ok i32\n"
    "pub fn main Void\n",
    "unexpected token"
  );
  expect_row_parse_failure(
    "choice Result\n"
    "  ok i32 ignored\n"
    "pub fn main Void\n",
    "unexpected token"
  );
}

static void rejects_export_c_on_non_function_rows(void) {
  expect_row_parse_failure(
    "export c use std.mem\n",
    "export c"
  );
  expect_row_parse_failure(
    "export c const answer i32 42\n"
    "pub fn main Void\n",
    "export c"
  );
  expect_row_parse_failure(
    "export c type Point\n"
    "  x i32\n"
    "pub fn main Void\n",
    "export c"
  );
  expect_row_parse_failure(
    "export c enum Mode\n"
    "  off\n"
    "pub fn main Void\n",
    "export c"
  );
  expect_row_parse_failure(
    "export c choice Result\n"
    "  ok i32\n"
    "pub fn main Void\n",
    "export c"
  );
  expect_row_parse_failure(
    "export c test \"adds\"\n"
    "  expect true\n",
    "export c"
  );
}

int main(void) {
  tokenizes_layout_and_trivia();
  attaches_comment_and_blank_line_trivia();
  attaches_block_comment_to_empty_block();
  formats_empty_block_comment_before_sibling();
  formats_block_footer_comment_after_child_rows();
  formats_row_layout_with_trivia();
  formats_blank_line_before_dedented_sibling();
  formats_terminal_top_level_comment();
  formats_comment_only_row_layout();
  formats_blank_comment_only_row_layout();
  formats_non_ascii_string_as_parseable_bytes();
  formats_control_string_escape_as_parseable_bytes();
  tracks_nested_dedents();
  accepts_trailing_whitespace_only_rows();
  accepts_indented_final_row_without_newline();
  rejects_tabs();
  rejects_odd_indent();
  rejects_indent_jump();
  rejects_escaped_string_newline();
  rejects_short_hex_character_escape();
  parses_core_function_program();
  parses_shape_literal_inside_array_literal();
  parses_control_flow_statements();
  parses_match_statement();
  parses_core_data_declarations();
  parses_zero_arg_and_uppercase_member_calls();
  parses_check_space_call_expression();
  parses_lowercase_primitive_annotations();
  parses_static_array_length_names();
  parses_untyped_array_literals();
  rejects_unbracketed_named_errors();
  rejects_else_after_explicit_else_block();
  rejects_reserved_word_identifiers();
  rejects_unconsumed_row_expression_tokens();
  rejects_missing_let_initializer_after_type_annotation();
  rejects_malformed_array_type_lengths();
  rejects_empty_use_import();
  rejects_unexpected_child_rows();
  rejects_trailing_fixed_row_tokens();
  rejects_export_c_on_non_function_rows();
  printf("row syntax smoke ok\n");
  return 0;
}
