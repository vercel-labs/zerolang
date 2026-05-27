#include "canonical_text.h"

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

char *z_strndup(const char *text, size_t len) {
  char *copy = z_checked_malloc(len + 1);
  memcpy(copy, text, len);
  copy[len] = 0;
  return copy;
}

char *z_strdup(const char *text) {
  size_t len = strlen(text ? text : "");
  char *copy = z_checked_malloc(len + 1);
  memcpy(copy, text ? text : "", len + 1);
  return copy;
}

static void expect(bool ok, const char *message) {
  if (ok) return;
  fprintf(stderr, "%s\n", message);
  exit(1);
}

static char *read_text_file(const char *path) {
  FILE *file = fopen(path, "rb");
  expect(file != NULL, "failed to open fixture");
  expect(fseek(file, 0, SEEK_END) == 0, "failed to seek fixture");
  long size = ftell(file);
  expect(size >= 0, "failed to size fixture");
  rewind(file);
  char *text = malloc((size_t)size + 1);
  expect(text != NULL, "failed to allocate fixture");
  size_t read = fread(text, 1, (size_t)size, file);
  expect(read == (size_t)size, "failed to read fixture");
  text[size] = 0;
  fclose(file);
  return text;
}

static bool parse_source(const char *source, ZCanonicalFacts *facts, ZDiag *diag) {
  ZCanonicalTokenVec tokens = z_canonical_text_tokenize(source, diag);
  if (diag->code != 0) {
    z_free_canonical_text_tokens(&tokens);
    return false;
  }
  ZCanonicalTree tree = {0};
  bool ok = z_canonical_text_parse(&tokens, &tree, facts, diag);
  z_free_canonical_text_tree(&tree);
  z_free_canonical_text_tokens(&tokens);
  return ok;
}

static void expect_accepts(const char *source, const char *label) {
  ZDiag diag = {0};
  ZCanonicalFacts facts = {0};
  if (parse_source(source, &facts, &diag)) return;
  fprintf(stderr, "%s:%d:%d: %s\n", label, diag.line, diag.column, diag.message);
  exit(1);
}

static void expect_rejects(const char *source, const char *label) {
  ZDiag diag = {0};
  ZCanonicalFacts facts = {0};
  if (!parse_source(source, &facts, &diag) && diag.code == 100) return;
  fprintf(stderr, "%s: expected canonical text rejection\n", label);
  exit(1);
}

static void parses_declarations_and_blocks(void) {
  const char *source =
    "type Point {\n"
    "    x: i32,\n"
    "    y: i32,\n"
    "}\n"
    "\n"
    "fn sum(point: Point) -> i32 {\n"
    "    return point.x + point.y\n"
    "}\n"
    "\n"
    "pub fn main(world: World) -> Void raises {\n"
    "    let point: Point = Point { x: 40, y: 2 }\n"
    "    let total: i32 = sum(point)\n"
    "    if total == 42 {\n"
    "        check world.out.write(\"point works\\n\")\n"
    "    }\n"
    "}\n";
  ZDiag diag = {0};
  ZCanonicalFacts facts = {0};
  expect(parse_source(source, &facts, &diag), diag.message);
  expect(facts.declaration_count == 3, "expected three declarations");
  expect(facts.type_count == 1, "expected one type declaration");
  expect(facts.function_count == 2, "expected two functions");
  expect(facts.block_count == 3, "expected function and if blocks");
  expect(facts.max_block_depth == 2, "expected nested block depth");
}

static void parses_fallibility_choices_and_interfaces(void) {
  const char *source =
    "choice Result {\n"
    "    ok: i32,\n"
    "    err: String,\n"
    "}\n"
    "\n"
    "interface Id {\n"
    "    fn id<X>(x: X) -> X\n"
    "}\n"
    "\n"
    "fn validate(ok: Bool) -> i32 raises [InvalidInput] {\n"
    "    if ok == false {\n"
    "        raise InvalidInput\n"
    "    }\n"
    "    return 42\n"
    "}\n";
  ZDiag diag = {0};
  ZCanonicalFacts facts = {0};
  expect(parse_source(source, &facts, &diag), diag.message);
  expect(facts.choice_count == 1, "expected choice declaration");
  expect(facts.interface_count == 1, "expected interface declaration");
  expect(facts.function_count == 1, "expected concrete function");
}

static void parses_nested_generic_type_commas(void) {
  const char *source =
    "type Pair<T, U> {\n"
    "    left: T,\n"
    "    right: U,\n"
    "}\n"
    "\n"
    "fn takes_pair(pair: Pair<i32, u8>) -> Void {\n"
    "    return\n"
    "}\n";
  expect_accepts(source, "nested generic type commas");
}

static void parses_separate_boolean_comparisons(void) {
  const char *source =
    "fn compare(a: i32, b: i32, c: i32, d: i32) -> Void {\n"
    "    if a < b && c < d {\n"
    "        return\n"
    "    }\n"
    "}\n";
  expect_accepts(source, "separate boolean comparisons");
}

static void parses_parenthesized_comparisons(void) {
  const char *source =
    "fn compare(a: i32, b: i32, expected: Bool) -> Void {\n"
    "    let same: Bool = (a < b) == expected\n"
    "}\n";
  expect_accepts(source, "parenthesized comparison");
}

static void parses_else_if_chains(void) {
  const char *source =
    "fn branch(a: Bool, b: Bool) -> Void {\n"
    "    if a {\n"
    "        return\n"
    "    } else if b {\n"
    "        return\n"
    "    } else {\n"
    "        return\n"
    "    }\n"
    "}\n";
  expect_accepts(source, "else-if chain");
}

static void records_block_open_locations(void) {
  const char *source =
    "fn main() -> Void {\n"
    "    return\n"
    "}\n";
  ZDiag diag = {0};
  ZCanonicalFacts facts = {0};
  ZCanonicalTokenVec tokens = z_canonical_text_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZCanonicalTree tree = {0};
  expect(z_canonical_text_parse(&tokens, &tree, &facts, &diag), diag.message);
  bool found = false;
  for (size_t i = 0; i < tree.len; i++) {
    if (tree.items[i].kind != Z_CANON_NODE_BLOCK) continue;
    found = true;
    expect(tree.items[i].line == 1, "expected block node at opening brace line");
    expect(tree.items[i].column == 19, "expected block node at opening brace column");
    expect(tokens.items[tree.items[i].first_token].line == tree.items[i].line, "expected block token line to match node");
    expect(tokens.items[tree.items[i].first_token].column == tree.items[i].column, "expected block token column to match node");
    break;
  }
  expect(found, "expected block node");
  z_free_canonical_text_tree(&tree);
  z_free_canonical_text_tokens(&tokens);
}

static void records_node_token_spans(void) {
  const char *source =
    "choice Result {\n"
    "    ok: i32,\n"
    "    err: String,\n"
    "}\n"
    "\n"
    "fn main(result: Result) -> Void {\n"
    "    match result {\n"
    "        ok value {\n"
    "            return\n"
    "        }\n"
    "        err message {\n"
    "            return\n"
    "        }\n"
    "    }\n"
    "}\n";
  ZDiag diag = {0};
  ZCanonicalFacts facts = {0};
  ZCanonicalTokenVec tokens = z_canonical_text_tokenize(source, &diag);
  expect(diag.code == 0, diag.message);
  ZCanonicalTree tree = {0};
  expect(z_canonical_text_parse(&tokens, &tree, &facts, &diag), diag.message);
  bool saw_decl = false;
  bool saw_stmt = false;
  bool saw_block = false;
  for (size_t i = 0; i < tree.len; i++) {
    ZCanonicalNode *node = &tree.items[i];
    expect(node->token_count > 0, "expected canonical tree nodes to record token spans");
    expect(node->first_token + node->token_count <= tokens.len, "expected canonical node span inside token stream");
    if (node->kind == Z_CANON_NODE_DECL) saw_decl = true;
    if (node->kind == Z_CANON_NODE_STMT) saw_stmt = true;
    if (node->kind == Z_CANON_NODE_BLOCK) {
      saw_block = true;
      expect(node->token_count > 1, "expected block node span to include the closing brace");
      expect(strcmp(tokens.items[node->first_token + node->token_count - 1].text, "}") == 0, "expected block node span to end at closing brace");
    }
  }
  expect(saw_decl, "expected declaration node span");
  expect(saw_stmt, "expected statement node span");
  expect(saw_block, "expected block node span");
  z_free_canonical_text_tree(&tree);
  z_free_canonical_text_tokens(&tokens);
}

static void parses_public_declarations_and_extern_types(void) {
  const char *source =
    "pub extern type CPoint\n"
    "\n"
    "pub type Point {\n"
    "    x: i32,\n"
    "}\n"
    "\n"
    "pub const answer: i32 = 42\n"
    "pub alias Count = i32\n"
    "\n"
    "pub interface Reader {\n"
    "    fn read(self: Self) -> i32\n"
    "}\n";
  ZDiag diag = {0};
  ZCanonicalFacts facts = {0};
  expect(parse_source(source, &facts, &diag), diag.message);
  expect(facts.declaration_count == 5, "expected public declarations");
  expect(facts.type_count == 1, "expected public concrete type declaration");
  expect(facts.interface_count == 1, "expected public interface declaration");
}

static void parses_character_literals(void) {
  const char *source =
    "fn chars() -> Void {\n"
    "    let letter: char = 'a'\n"
    "    let newline: char = '\\n'\n"
    "    let hex: char = '\\x41'\n"
    "}\n";
  expect_accepts(source, "character literals");
}

static void parses_generic_calls_and_array_repeats(void) {
  const char *source =
    "type Box<T> {\n"
    "    value: T,\n"
    "}\n"
    "\n"
    "fn choose<T, U>(left: T, right: U) -> T {\n"
    "    return left\n"
    "}\n"
    "\n"
    "fn caller(a: i32, b: u8) -> Void {\n"
    "    let value: i32 = choose<i32, u8>(a, b)\n"
    "    let box: Box<i32> = Box<i32> { value: value }\n"
    "    let bytes: [4]u8 = [0_u8; 4]\n"
    "    check value == 1 || bytes[0] == 0_u8\n"
    "}\n";
  expect_accepts(source, "generic calls and array repeats");
}

static void parses_error_members_and_prefix_not(void) {
  const char *source =
    "fn main(world: World, ok: Bool) -> Void raises {\n"
    "    if !ok {\n"
    "        check world.err.write(\"bad\\n\")\n"
    "    }\n"
    "}\n";
  expect_accepts(source, "error member and prefix not");
}

static void parses_generic_interfaces(void) {
  const char *source =
    "interface Reader<T> {\n"
    "    fn read(self: Self) -> T\n"
    "}\n";
  expect_accepts(source, "generic interface");
}

static void parses_choice_payload_match_patterns(void) {
  const char *source =
    "choice Result {\n"
    "    ok: i32,\n"
    "    err: String,\n"
    "}\n"
    "\n"
    "fn handle(result: Result) -> Void {\n"
    "    match result {\n"
    "        ok value {\n"
    "            return\n"
    "        }\n"
    "        err message {\n"
    "            return\n"
    "        }\n"
    "    }\n"
    "}\n";
  expect_accepts(source, "choice payload match patterns");
}

static void parses_empty_return_but_not_empty_checks(void) {
  expect_accepts("fn ok() -> Void {\n    return\n}\n", "empty return");
  expect_rejects("fn bad() -> Void {\n    check\n}\n", "empty check");
  expect_rejects("fn bad() -> Void {\n    expect\n}\n", "empty expect");
}

static void parses_use_declarations_and_zero_arg_calls(void) {
  const char *source =
    "use std.mem\n"
    "use package.module as module\n"
    "\n"
    "fn answer() -> i32 {\n"
    "    return 42\n"
    "}\n"
    "\n"
    "fn caller() -> Void {\n"
    "    let value: i32 = answer()\n"
    "}\n";
  expect_accepts(source, "use declarations and zero-arg calls");
}

static void rejects_noncanonical_spellings(void) {
  expect_rejects("fun main() -> Void {}\n", "fun keyword");
  expect_rejects("shape Point {\n    x: i32,\n}\n", "shape keyword");
  expect_rejects("pub fn main(world: World) -> Void raises {\n    let value = 1\n}\n", "missing local type");
  expect_rejects("pub fn main(world: World) -> Void raises {\n    mut value: i32 = 1\n}\n", "mut keyword");
  expect_rejects("type Point {\n    x: i32\n    y: i32\n}\n", "missing commas");
  expect_rejects("fn load() -> Void ! {}\n", "bang fallibility");
  expect_rejects("fn load() -> Void raises { IoError } {}\n", "brace errors");
  expect_rejects("pub fn main(world: World) -> Void raises {\n    check world.out.write \"bad\\n\"\n}\n", "space call");
  expect_rejects("fn bad() -> Void {\n    check foo (1)\n}\n", "space call parentheses");
  expect_rejects("pub fn main(world: World) -> Void raises {\n    let ok: Bool = 1 < 2 < 3\n}\n", "chained comparison");
  expect_rejects("pub fn main(world: World) -> Void raises {\n    let ok: Bool = a == b == c\n}\n", "chained equality comparison");
  expect_rejects("fn bad(a: i32, b: i32, c: i32) -> Void {\n    let ok: Bool = a < b > (c)\n}\n", "generic-looking chained comparison");
  expect_rejects("fn bad(items: Items) -> Void {\n    for item in items all {\n        return\n    }\n}\n", "space call in for range");
  expect_rejects("fn bad(items: Items) -> Void {\n    for item in 1 < 2 < 3 {\n        return\n    }\n}\n", "chained comparison in for range");
  expect_rejects("type Pair<T U> {\n    left: T,\n    right: U,\n}\n", "missing type parameter comma");
  expect_rejects("fn id<T U>(value: T) -> T {\n    return value\n}\n", "missing function type parameter comma");
  expect_rejects("fn missing_initializer() -> Void {\n    let value: i32 = // missing\n}\n", "comment-only initializer");
  expect_rejects("type Point {\n    x: i32\n    y: i32,\n}\n", "missing field comma before later comma");
  expect_rejects("fn bad(a: i32\n    b: i32) -> Void {\n    return\n}\n", "missing parameter comma before close");
  expect_rejects("fn bad(a: i32 b: i32) -> Void {\n    return\n}\n", "missing same-line parameter comma");
  expect_rejects("type Point {\n    x: i32 y: i32,\n}\n", "missing same-line field comma");
  expect_rejects("fn bad() -> Void {\n    let value: i32 label = 1\n}\n", "extra local type token");
  expect_rejects("fn bad() -> Void Label {\n    return\n}\n", "extra return type token");
  expect_rejects("fn bad() -> Void {\n    break extra\n}\n", "trailing break tokens");
  expect_rejects("fn bad() -> Void {\n    continue extra\n}\n", "trailing continue tokens");
  expect_rejects("fn bad() -> Void raises {\n    raise InvalidInput extra\n}\n", "trailing raise tokens");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = +\n}\n", "operandless operator");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = ;\n}\n", "unexpected expression punctuation");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = 1 +\n}\n", "trailing expression operator");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = return\n}\n", "reserved word expression");
  expect_rejects("fn bad() -> Void {\n    if else {\n        return\n    }\n}\n", "reserved word condition");
  expect_rejects("fn bad() -> Void {\n    check fn\n}\n", "reserved word check expression");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = (items[0)]\n}\n", "mismatched expression delimiters");
  expect_rejects("fn if() -> Void {\n    return\n}\n", "reserved function name");
  expect_rejects("fn interface() -> Void {\n    return\n}\n", "reserved interface function name");
  expect_rejects("fn alias() -> Void {\n    return\n}\n", "reserved alias function name");
  expect_rejects("fn bad() -> Void {\n    let expect: i32 = 1\n}\n", "reserved expect binding name");
  expect_rejects("fn bad() -> 1 {\n    return\n}\n", "literal return type");
  expect_rejects("fn bad(value: \"nope\") -> Void {\n    return\n}\n", "literal parameter type");
  expect_rejects("fn bad() -> Void {\n    let value: char = ''\n}\n", "empty character literal");
  expect_rejects("fn bad() -> Void {\n    let value: char = 'ab'\n}\n", "wide character literal");
  expect_rejects("fn bad() -> Void {\n    let value: char = '\\q'\n}\n", "invalid character escape");
  expect_rejects("fn bad() -> Void {\n    let text: String = \"hello\\\x0aworld\"\n}\n", "escaped string newline");
  expect_rejects("fn bad() -> Void {\n    let text: String = \"hello\\xZZ\"\n}\n", "malformed hex string escape");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = 123abc\n}\n", "malformed number literal");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = 0b102\n}\n", "malformed radix number literal");
  expect_rejects("type Point {\n    x: i32,\n}\n\nfn bad() -> Void {\n    let point: Point = Point { 1: 2 }\n}\n", "numeric object field");
  expect_rejects("type Point {\n    x: i32,\n}\n\nfn bad() -> Void {\n    let point: Point = Point { x }\n}\n", "object field without value");
  expect_rejects("type Point {\n    x: i32,\n    y: i32,\n}\n\nfn bad() -> Void {\n    let point: Point = Point { x, y: 1 }\n}\n", "object field without value before comma");
  expect_rejects("alias Bad = 1\n", "literal alias target");
  expect_rejects("alias Bad = 1 + 2\n", "alias expression target");
  expect_rejects("pub alias Bad = 1 + 2\n", "public alias expression target");
  expect_rejects("fn first() -> Void {} fn second() -> Void {}\n", "same-line declarations");
  expect_rejects("fn bad(ok: Bool) -> Void {\n    if ok {\n        return\n    } return\n}\n", "same-line statement after if block");
  expect_rejects("fn bad() -> Void {\n    defer {\n        return\n    } return\n}\n", "same-line statement after defer block");
  expect_rejects("fn bad() -> Void {\n    let ok: Bool = 1 < 2 > (3)\n}\n", "numeric comparison mistaken for generic call");
  expect_rejects("fn bad(foo: Foo) -> Void {\n    let value: i32 = foo.1\n}\n", "numeric member access");
  expect_rejects("fn bad(foo: Foo) -> Void {\n    let value: i32 = foo.()\n}\n", "group member access");
  expect_rejects("fn bad(foo: Foo) -> Void {\n    let value: i32 = foo.[0]\n}\n", "index member access");
  expect_rejects("fn bad(items: Items) -> Void {\n    let value: i32 = items [0]\n}\n", "spaced index access");
  expect_rejects("fn bad(items: Items) -> Void {\n    let value: i32 = items[]\n}\n", "empty index expression");
  expect_rejects("fn bad(items: Items) -> Void {\n    let value: i32 = items[0, 1]\n}\n", "comma index expression");
  expect_rejects("fn bad() -> Void {\n    check ()\n}\n", "empty grouped check expression");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = ()\n}\n", "empty grouped initializer expression");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = (1, 2)\n}\n", "comma grouped expression");
  expect_rejects("fn bad() -> Void {\n    let bytes: [4, 5]u8 = [0_u8; 4]\n}\n", "comma in array type length");
  expect_rejects("fn bad() -> Void {\n    let bytes: [4]u8 = [0_u8; 4, 5]\n}\n", "comma after array repeat count");
  expect_rejects("fn bad() -> Void {\n    let bytes: [4]u8 = [0_u8, 1_u8; 4]\n}\n", "mixed array literal and repeat");
  expect_rejects("use \"not-module\"\n", "string use import");
  expect_rejects("use std.mem()\n", "call use import");
  expect_rejects("use std.\n", "trailing use import separator");
}

static void parse_file_arg(const char *mode, const char *path) {
  char *source = read_text_file(path);
  if (strcmp(mode, "--accept") == 0) expect_accepts(source, path);
  else if (strcmp(mode, "--reject") == 0) expect_rejects(source, path);
  else expect(false, "unknown fixture mode");
  free(source);
}

int main(int argc, char **argv) {
  parses_declarations_and_blocks();
  parses_fallibility_choices_and_interfaces();
  parses_nested_generic_type_commas();
  parses_separate_boolean_comparisons();
  parses_parenthesized_comparisons();
  parses_else_if_chains();
  records_block_open_locations();
  records_node_token_spans();
  parses_public_declarations_and_extern_types();
  parses_character_literals();
  parses_generic_calls_and_array_repeats();
  parses_error_members_and_prefix_not();
  parses_generic_interfaces();
  parses_choice_payload_match_patterns();
  parses_empty_return_but_not_empty_checks();
  parses_use_declarations_and_zero_arg_calls();
  rejects_noncanonical_spellings();
  for (int i = 1; i + 1 < argc; i += 2) parse_file_arg(argv[i], argv[i + 1]);
  printf("canonical text smoke ok\n");
  return 0;
}
