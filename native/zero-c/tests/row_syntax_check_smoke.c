#include "zero.h"

#include <stdio.h>
#include <stdlib.h>

static void expect(int ok, const char *message) {
  if (ok) return;
  fprintf(stderr, "%s\n", message);
  exit(1);
}

static void check_row_source(const char *name, const char *source) {
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  if (diag.code != 0) {
    fprintf(stderr, "%s tokenize failed: %s\n", name, diag.message);
    exit(1);
  }

  ZRowTree tree = {0};
  if (!z_row_parse_layout(&tokens, &tree, &diag)) {
    fprintf(stderr, "%s layout failed: %s\n", name, diag.message);
    exit(1);
  }

  Program program = z_parse_row(&tokens, &tree, &diag);
  if (diag.code != 0) {
    fprintf(stderr, "%s parse failed: %s\n", name, diag.message);
    exit(1);
  }

  if (!z_check_program(&program, &diag)) {
    fprintf(stderr, "%s check failed: %s\n", name, diag.message);
    exit(1);
  }

  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

static void check_row_file(const char *path) {
  ZDiag diag = {0};
  char *source = z_read_file(path, &diag);
  if (diag.code != 0) {
    fprintf(stderr, "%s read failed: %s\n", path, diag.message);
    exit(1);
  }
  check_row_source(path, source);
  free(source);
}

static void function_calls_check(void) {
  check_row_source(
    "function calls",
    "fn add i32 a i32 b i32\n"
    "  ret + a b\n"
    "\n"
    "pub fn main Void\n"
    "  let total i32 add 40 2\n"
  );
}

static void zero_arg_and_uppercase_member_calls_check(void) {
  check_row_source(
    "zero arg and uppercase member calls",
    "fn answer i32\n"
    "  ret 42\n"
    "\n"
    "choice Result\n"
    "  ok i32\n"
    "  err String\n"
    "\n"
    "pub fn main Void\n"
    "  let value i32 answer()\n"
    "  let result Result.ok value\n"
  );
}

static void check_space_call_expression_check(void) {
  check_row_source(
    "check space call expression",
    "fn parse i32 input i32 ![InvalidInput]\n"
    "  if == input 0\n"
    "    raise InvalidInput\n"
    "  ret input\n"
    "\n"
    "pub fn main Void ![InvalidInput]\n"
    "  let value i32 check parse 41\n"
  );
}

static void shapes_and_members_check(void) {
  check_row_source(
    "shapes and members",
    "type Point\n"
    "  x i32\n"
    "  y i32 2\n"
    "\n"
    "pub fn main Void\n"
    "  let point Point . x 40\n"
    "  let total i32 + point.x point.y\n"
  );
}

static void control_flow_check(void) {
  check_row_source(
    "control flow",
    "fn fib i32 n i32\n"
    "  if <= n 1\n"
    "    ret n\n"
    "  else\n"
    "    ret + (fib (- n 1)) (fib (- n 2))\n"
    "\n"
    "fn countdown i32 start i32\n"
    "  mut value i32 start\n"
    "  while > value 0\n"
    "    set value (- value 1)\n"
    "  ret value\n"
    "\n"
    "pub fn main Void\n"
    "  let a i32 fib 5\n"
    "  let b i32 countdown 3\n"
  );
}

static void slices_and_casts_check(void) {
  check_row_source(
    "slices and casts",
    "pub fn main Void\n"
    "  let byte u8 ('A' as u8)\n"
    "  let part Span<u8> \"zero\"[1..3]\n"
    "  let suffix Span<u8> \"zero\"[2..]\n"
    "  let prefix Span<u8> \"zero\"[..2]\n"
    "  let all Span<u8> \"zero\"[..]\n"
  );
}

static void match_check(void) {
  check_row_source(
    "match",
    "fn bucket i32 value u8\n"
    "  match value\n"
    "    0\n"
    "      ret 0\n"
    "    1..3\n"
    "      ret 1\n"
    "    _\n"
    "      ret 9\n"
    "\n"
    "pub fn main Void\n"
    "  let value i32 bucket 2_u8\n"
  );
}

static void public_data_declarations_check(void) {
  check_row_source(
    "public data declarations",
    "pub enum Mode\n"
    "  off\n"
    "  on\n"
    "\n"
    "pub choice Result\n"
    "  ok i32\n"
    "  err String\n"
    "\n"
    "pub fn main Void\n"
  );
}

static void lowercase_primitive_annotations_check(void) {
  check_row_source(
    "lowercase primitive annotations",
    "pub fn main Void\n"
    "  let letter char 'A'\n"
    "  let small i8 1\n"
    "  let medium i16 2\n"
    "  let wide isize 3\n"
    "  let single f32 1.25\n"
    "  let double f64 2.5\n"
  );
}

static void named_error_rejects_without_brackets(void) {
  const char *source = "fn validate i32 ok Bool ! InvalidInput\n";
  ZDiag diag = {0};
  ZRowTokenVec tokens = z_row_tokenize(source, &diag);
  expect(diag.code == 0, "expected tokenization to pass");
  ZRowTree tree = {0};
  expect(z_row_parse_layout(&tokens, &tree, &diag), diag.message);
  Program program = z_parse_row(&tokens, &tree, &diag);
  expect(diag.code == 100, "expected parser rejection");
  z_free_program(&program);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
}

int main(int argc, char **argv) {
  function_calls_check();
  zero_arg_and_uppercase_member_calls_check();
  check_space_call_expression_check();
  shapes_and_members_check();
  control_flow_check();
  slices_and_casts_check();
  match_check();
  public_data_declarations_check();
  lowercase_primitive_annotations_check();
  named_error_rejects_without_brackets();
  for (int i = 1; i < argc; i++) {
    check_row_file(argv[i]);
  }
  printf("row syntax check smoke ok\n");
  return 0;
}
