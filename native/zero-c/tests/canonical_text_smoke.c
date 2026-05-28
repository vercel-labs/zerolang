#include "canonical_text.h"
#include "program_graph_compare.h"
#include "program_graph_import.h"
#include "program_graph_roundtrip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void expect_format_roundtrip(const char *source, const char *label) {
  ZDiag diag = {0};
  ZBuf first = {0};
  if (!z_canonical_text_format_source(source, &first, &diag)) {
    fprintf(stderr, "%s:%d:%d: failed to format: %s\n%s\n", label, diag.line, diag.column, diag.message, first.data ? first.data : "");
    free(first.data);
    exit(1);
  }

  ZDiag parse_diag = {0};
  ZCanonicalFacts facts = {0};
  if (!parse_source(first.data ? first.data : "", &facts, &parse_diag)) {
    fprintf(stderr, "%s:%d:%d: formatted source does not parse: %s\n", label, parse_diag.line, parse_diag.column, parse_diag.message);
    free(first.data);
    exit(1);
  }

  ZDiag second_diag = {0};
  ZBuf second = {0};
  if (!z_canonical_text_format_source(first.data ? first.data : "", &second, &second_diag)) {
    fprintf(stderr, "%s:%d:%d: failed to reformat: %s\n", label, second_diag.line, second_diag.column, second_diag.message);
    free(first.data);
    free(second.data);
    exit(1);
  }
  if (strcmp(first.data ? first.data : "", second.data ? second.data : "") != 0) {
    fprintf(stderr, "%s: formatter is not idempotent\nfirst:\n%s\nsecond:\n%s\n", label, first.data ? first.data : "", second.data ? second.data : "");
    free(first.data);
    free(second.data);
    exit(1);
  }
  free(first.data);
  free(second.data);
}

static void expect_accepts(const char *source, const char *label) {
  ZDiag diag = {0};
  ZCanonicalFacts facts = {0};
  if (parse_source(source, &facts, &diag)) {
    expect_format_roundtrip(source, label);
    return;
  }
  fprintf(stderr, "%s:%d:%d: %s\n", label, diag.line, diag.column, diag.message);
  exit(1);
}

static void expect_formats_to(const char *source, const char *expected, const char *label) {
  ZDiag diag = {0};
  ZBuf formatted = {0};
  if (!z_canonical_text_format_source(source, &formatted, &diag)) {
    fprintf(stderr, "%s:%d:%d: failed to format: %s\n%s\n", label, diag.line, diag.column, diag.message, formatted.data ? formatted.data : "");
    free(formatted.data);
    exit(1);
  }
  if (strcmp(formatted.data ? formatted.data : "", expected) != 0) {
    fprintf(stderr, "%s: unexpected format\nexpected:\n%s\nactual:\n%s\n", label, expected, formatted.data ? formatted.data : "");
    free(formatted.data);
    exit(1);
  }
  free(formatted.data);
  expect_format_roundtrip(expected, label);
}

static void expect_rejects(const char *source, const char *label) {
  ZDiag diag = {0};
  ZCanonicalFacts facts = {0};
  if (!parse_source(source, &facts, &diag) && diag.code == 100) return;
  fprintf(stderr, "%s: expected canonical text rejection\n", label);
  exit(1);
}

static void expect_format_rejects_without_diag(const char *source, const char *label) {
  ZBuf formatted = {0};
  if (!z_canonical_text_format_source(source, &formatted, NULL)) {
    free(formatted.data);
    return;
  }
  fprintf(stderr, "%s: expected format rejection without caller diag\n%s\n", label, formatted.data ? formatted.data : "");
  free(formatted.data);
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

static void formats_core_declarations_and_blocks(void) {
  const char *source =
    "use std.mem\n"
    "\n"
    "\n"
    "type Point{x:i32,y:i32,}\n"
    "fn sum(point:Point)->i32{return point.x+point.y}\n"
    "pub fn main(world:World)->Void raises{let point:Point=Point{x:40,y:2}\n"
    "let total:i32=sum(point)\n"
    "if total==42{check world.out.write(\"point works\\n\")}}\n";
  const char *expected =
    "use std.mem\n"
    "\n"
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
  expect_formats_to(source, expected, "core declaration formatting");
}

static void formats_angles_comparisons_and_ranges_canonically(void) {
  const char *source =
    "type Box < T > {value:T,}\n"
    "fn compare < T > (value:i32,limit:i32,bytes:MutSpan < u8 >)->Void{\n"
    "if value<limit{return}\n"
    "if value >limit{return}\n"
    "for item in 0 .. 4{return}\n"
    "let box:Box < i32 > = Box{value:value}\n"
    "}\n";
  const char *expected =
    "type Box<T> {\n"
    "    value: T,\n"
    "}\n"
    "\n"
    "fn compare<T>(value: i32, limit: i32, bytes: MutSpan<u8>) -> Void {\n"
    "    if value < limit {\n"
    "        return\n"
    "    }\n"
    "    if value > limit {\n"
    "        return\n"
    "    }\n"
    "    for item in 0..4 {\n"
    "        return\n"
    "    }\n"
    "    let box: Box<i32> = Box { value: value }\n"
    "}\n";
  expect_formats_to(source, expected, "angle, comparison, and range formatting");
}

static void formats_deep_nested_blocks(void) {
  ZBuf source = {0};
  zbuf_append(&source, "fn deep(flag: Bool) -> Void {\n");
  for (size_t i = 0; i < 260; i++) zbuf_append(&source, "if flag {\n");
  zbuf_append(&source, "return\n");
  for (size_t i = 0; i < 260; i++) zbuf_append(&source, "}\n");
  zbuf_append(&source, "}\n");
  expect_format_roundtrip(source.data ? source.data : "", "deep nested block formatting");
  free(source.data);
}

static void formats_public_lists_match_patterns_and_prefix_forms(void) {
  const char *source =
    "pub type Point{x:i32,y:i32,}\n"
    "pub choice Result{ok:u8,err:String,}\n"
    "fn handle(result:Result,ok:Bool,value:i32,ptr:Ptr,bytes:MutSpan<u8>)->u8{\n"
    "if !ok{\n"
    "let neg:i32=-value\n"
    "let pos:i32=+value\n"
    "let deref:i32=*ptr\n"
    "let borrowed:MutSpan<u8> = &mut bytes\n"
    "let array:[4]u8=bytes\n"
    "return 0_u8\n"
    "}\n"
    "match result{.ok(payload){return payload}\n"
    "_{return 0_u8}}\n"
    "}\n";
  const char *expected =
    "pub type Point {\n"
    "    x: i32,\n"
    "    y: i32,\n"
    "}\n"
    "\n"
    "pub choice Result {\n"
    "    ok: u8,\n"
    "    err: String,\n"
    "}\n"
    "\n"
    "fn handle(result: Result, ok: Bool, value: i32, ptr: Ptr, bytes: MutSpan<u8>) -> u8 {\n"
    "    if !ok {\n"
    "        let neg: i32 = -value\n"
    "        let pos: i32 = +value\n"
    "        let deref: i32 = *ptr\n"
    "        let borrowed: MutSpan<u8> = &mut bytes\n"
    "        let array: [4]u8 = bytes\n"
    "        return 0_u8\n"
    "    }\n"
    "    match result {\n"
    "        .ok(payload) {\n"
    "            return payload\n"
    "        }\n"
    "        _ {\n"
    "            return 0_u8\n"
    "        }\n"
    "    }\n"
    "}\n";
  expect_formats_to(source, expected, "public lists, match patterns, and prefix forms");
}

static void formats_type_defaults_methods_and_generic_field_commas(void) {
  const char *source =
    "type Pair<T,U>{left:T,right:U,}\n"
    "type Counter{value:i32=0,pair:Pair<i32,u8> = Pair{left:1,right:2_u8},\n"
    "pub fn reset(self:mutref<Self>)->Void{self.value=0}\n"
    "fn bump(self:mutref<Self>,amount:i32)->Void{self.value=self.value+amount}\n"
    "}\n";
  const char *expected =
    "type Pair<T, U> {\n"
    "    left: T,\n"
    "    right: U,\n"
    "}\n"
    "\n"
    "type Counter {\n"
    "    value: i32 = 0,\n"
    "    pair: Pair<i32, u8> = Pair { left: 1, right: 2_u8 },\n"
    "    pub fn reset(self: mutref<Self>) -> Void {\n"
    "        self.value = 0\n"
    "    }\n"
    "    fn bump(self: mutref<Self>, amount: i32) -> Void {\n"
    "        self.value = self.value + amount\n"
    "    }\n"
    "}\n";
  expect_formats_to(source, expected, "type defaults, methods, and generic field commas");
}

static void formats_else_and_line_start_prefix_forms(void) {
  const char *else_source =
    "fn choose(flag:Bool)->Void{if flag{return}\n"
    "else{return}}\n";
  const char *else_expected =
    "fn choose(flag: Bool) -> Void {\n"
    "    if flag {\n"
    "        return\n"
    "    } else {\n"
    "        return\n"
    "    }\n"
    "}\n";
  expect_formats_to(else_source, else_expected, "else newline canonical formatting");

  const char *prefix_source =
    "fn prefix(value:i32,ptr:Ptr,ok:Bool)->Void{call()\n"
    "-value\n"
    "+value\n"
    "*ptr\n"
    "!ok}\n";
  const char *prefix_expected =
    "fn prefix(value: i32, ptr: Ptr, ok: Bool) -> Void {\n"
    "    call()\n"
    "    -value\n"
    "    +value\n"
    "    *ptr\n"
    "    !ok\n"
    "}\n";
  expect_formats_to(prefix_source, prefix_expected, "line-start prefix expression formatting");
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

static void imports_nested_generic_declaration_field_types(void) {
  const char *source =
    "type Pair<T, U> {\n"
    "    left: T,\n"
    "    right: U,\n"
    "}\n"
    "\n"
    "type Holder {\n"
    "    value: Pair<i32, u8>,\n"
    "}\n"
    "\n"
    "choice MaybePair {\n"
    "    some: Pair<i32, u8>,\n"
    "    none: Void,\n"
    "}\n";
  ZDiag diag = {0};
  Program program = {0};
  expect(z_parse_canonical_text_program_source(source, &program, &diag), diag.message);
  expect(program.shapes.len == 2, "expected pair and holder shapes");
  expect(program.shapes.items[1].fields.len == 1, "expected holder field");
  expect(strcmp(program.shapes.items[1].fields.items[0].type, "Pair<i32, u8>") == 0, "expected nested generic shape field type");
  expect(program.choices.len == 1, "expected maybe pair choice");
  expect(program.choices.items[0].cases.len == 2, "expected maybe pair cases");
  expect(strcmp(program.choices.items[0].cases.items[0].type, "Pair<i32, u8>") == 0, "expected nested generic choice payload type");
  z_free_program(&program);
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
    "        .ok(value) {\n"
    "            return\n"
    "        }\n"
    "        .err(message) {\n"
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

static void parses_layout_enum_choice_and_const_type_forms(void) {
  const char *source =
    "extern type CPoint {\n"
    "    x: i32,\n"
    "    y: i32,\n"
    "}\n"
    "\n"
    "pub packed type Header {\n"
    "    tag: u8,\n"
    "    len: u16,\n"
    "}\n"
    "\n"
    "enum Color: u8 {\n"
    "    red,\n"
    "    blue,\n"
    "}\n"
    "\n"
    "enum Status: u8 {\n"
    "    ready,\n"
    "    failed,\n"
    "}\n"
    "\n"
    "choice Result {\n"
    "    ok: Void,\n"
    "    err: String,\n"
    "}\n"
    "\n"
    "type Wrapper {\n"
    "    value: const i32,\n"
    "}\n"
    "\n"
    "fn ops(left: u8, right: u8) -> Void {\n"
    "    let wrapped: u8 = left +% right\n"
    "    let saturated: u8 = left +| right\n"
    "    let wrapper: Wrapper = Wrapper { value: 1 }\n"
    "}\n";
  expect_accepts(source, "layout, enum, choice, const type, and overflow operators");
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
    "    let box: Box<i32> = Box { value: value }\n"
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
    "        .ok(value) {\n"
    "            return\n"
    "        }\n"
    "        .err(message) {\n"
    "            return\n"
    "        }\n"
    "    }\n"
    "}\n";
  expect_accepts(source, "choice payload match patterns");
}

static void parses_control_flow_tests_and_static_forms(void) {
  const char *source =
    "choice Result {\n"
    "    ok: u8,\n"
    "    err: String,\n"
    "}\n"
    "\n"
    "fn fixed<static N: usize, T>(items: [N]T) -> Void {\n"
    "    return\n"
    "}\n"
    "\n"
    "fn flow(bytes: MutSpan<u8>, result: Result, value: i32) -> u8 raises [InvalidInput] {\n"
    "    var index: usize = 0\n"
    "    while index < 4 {\n"
    "        index = index + 1\n"
    "    }\n"
    "    for item in 0..4 {\n"
    "        defer cleanup()\n"
    "        check item >= 0\n"
    "    }\n"
    "    let first: u8 = bytes[0]\n"
    "    let part: Span<u8> = bytes[1..4]\n"
    "    let byte: u8 = value as u8\n"
    "    let borrowed: MutSpan<u8> = &mut bytes\n"
    "    let vec: FixedVec<u8, 4> = FixedVec.init<u8, 4>()\n"
    "    match result {\n"
    "        .ok(payload) {\n"
    "            return payload\n"
    "        }\n"
    "        .err(message) {\n"
    "            raise InvalidInput\n"
    "        }\n"
    "        _ {\n"
    "            return byte\n"
    "        }\n"
    "    }\n"
    "}\n"
    "\n"
    "test \"flow syntax\" {\n"
    "    expect true\n"
    "}\n";
  expect_accepts(source, "control flow, tests, and static forms");
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

static void imports_public_c_and_use_source_ranges(void) {
  const char *source =
    "pub extern c \"conformance/c/simple.h\" as c\n"
    "use package.module as module\n";
  ZDiag diag = {0};
  Program program = {0};
  expect(z_parse_canonical_text_program_source(source, &program, &diag), diag.message);
  expect(program.c_imports.len == 1, "expected public C import");
  CImport *c_import = &program.c_imports.items[0];
  expect(strcmp(c_import->header, "conformance/c/simple.h") == 0, "expected decoded public C header path");
  expect(strcmp(c_import->alias, "c") == 0, "expected public C import alias");
  expect(c_import->line == 1, "expected public C import source range to start at declaration line");
  expect(c_import->column == 1, "expected public C import source range to start at declaration column");
  expect(program.use_imports.len == 1, "expected use import");
  UseImport *use_import = &program.use_imports.items[0];
  expect(strcmp(use_import->module, "package.module") == 0, "expected use module");
  expect(strcmp(use_import->alias, "module") == 0, "expected use alias");
  expect(use_import->column == 1, "expected use source range to start at declaration");
  expect(use_import->end_column == 29, "expected use source range to include alias");
  z_free_program(&program);
}

static void records_declaration_start_source_locations(void) {
  const char *source =
    "pub type PublicPoint {\n"
    "    x: i32,\n"
    "    pub fn reset(self: Self) -> Void {\n"
    "        return\n"
    "    }\n"
    "}\n"
    "pub const answer: i32 = 42\n"
    "pub alias Count = i32\n"
    "pub interface Reader {\n"
    "    fn read(self: Self) -> i32\n"
    "}\n"
    "pub fn main(world: World) -> Void raises {\n"
    "    return\n"
    "}\n"
    "export c fn exported() -> i32 {\n"
    "    return 1\n"
    "}\n"
    "extern type CPoint\n"
    "enum Color: u8 {\n"
    "    red,\n"
    "}\n"
    "choice Result {\n"
    "    ok: i32,\n"
    "}\n"
    "test \"range\" {\n"
    "    expect true\n"
    "}\n";
  ZDiag diag = {0};
  Program program = {0};
  expect(z_parse_canonical_text_program_source(source, &program, &diag), diag.message);

  expect(program.shapes.len == 2, "expected public and extern shapes");
  expect(program.shapes.items[0].line == 1, "expected public type to start at pub line");
  expect(program.shapes.items[0].column == 1, "expected public type to start at pub column");
  expect(program.shapes.items[0].methods.len == 1, "expected public type method");
  expect(program.shapes.items[0].methods.items[0].line == 3, "expected type method to start at pub line");
  expect(program.shapes.items[0].methods.items[0].column == 5, "expected type method to start at pub column");
  expect(program.shapes.items[1].line == 18, "expected extern type to start at extern line");
  expect(program.shapes.items[1].column == 1, "expected extern type to start at extern column");

  expect(program.consts.len == 1, "expected const declaration");
  expect(program.consts.items[0].line == 7, "expected public const to start at pub line");
  expect(program.consts.items[0].column == 1, "expected public const to start at pub column");
  expect(program.aliases.len == 1, "expected alias declaration");
  expect(program.aliases.items[0].line == 8, "expected public alias to start at pub line");
  expect(program.aliases.items[0].column == 1, "expected public alias to start at pub column");

  expect(program.interfaces.len == 1, "expected interface declaration");
  expect(program.interfaces.items[0].line == 9, "expected public interface to start at pub line");
  expect(program.interfaces.items[0].column == 1, "expected public interface to start at pub column");
  expect(program.interfaces.items[0].methods.len == 1, "expected interface method");
  expect(program.interfaces.items[0].methods.items[0].line == 10, "expected interface method to start at fn line");
  expect(program.interfaces.items[0].methods.items[0].column == 5, "expected interface method to start at fn column");

  expect(program.functions.len == 3, "expected function, export, and test declarations");
  expect(program.functions.items[0].line == 12, "expected public function to start at pub line");
  expect(program.functions.items[0].column == 1, "expected public function to start at pub column");
  expect(program.functions.items[1].line == 15, "expected exported function to start at export line");
  expect(program.functions.items[1].column == 1, "expected exported function to start at export column");
  expect(program.functions.items[2].is_test, "expected generated test function");
  expect(program.functions.items[2].line == 25, "expected test function to start at test line");
  expect(program.functions.items[2].column == 1, "expected test function to start at test column");

  expect(program.enums.len == 1, "expected enum declaration");
  expect(program.enums.items[0].line == 19, "expected enum to start at enum line");
  expect(program.enums.items[0].column == 1, "expected enum to start at enum column");
  expect(program.choices.len == 1, "expected choice declaration");
  expect(program.choices.items[0].line == 22, "expected choice to start at choice line");
  expect(program.choices.items[0].column == 1, "expected choice to start at choice column");

  z_free_program(&program);
}

static void parses_assignment_statements(void) {
  const char *source =
    "type Point {\n"
    "    x: i32,\n"
    "}\n"
    "\n"
    "fn mutate(items: MutSpan<i32>, point: Point) -> Void {\n"
    "    var count: i32 = 0\n"
    "    count = count + 1\n"
    "    point.x = count\n"
    "    items[0] = point.x\n"
    "    items[count + 1] = items[0]\n"
    "}\n";
  expect_accepts(source, "assignment statements");
}

static void parses_effectful_expression_forms(void) {
  const char *source =
    "const pointer_width: usize = meta target.pointerWidth\n"
    "\n"
    "fn maybe_value(bytes: Bytes, index: usize) -> u8 raises [Missing] {\n"
    "    raise Missing\n"
    "}\n"
    "\n"
    "fn effect(bytes: Bytes) -> Void raises [Missing] {\n"
    "    let found: u8 = check maybe_value(bytes, 0)\n"
    "    let fallback: u8 = rescue maybe_value(bytes, 1) err 9_u8\n"
    "    let nested: u8 = rescue (rescue maybe_value(bytes, 2) err 1_u8) err 2_u8\n"
    "    check found == fallback || meta target.hasCapability(\"fs\")\n"
    "}\n";
  expect_accepts(source, "effectful expression forms");
}

static void imports_decoded_literals_and_prefix_forms(void) {
  const char *source =
    "extern c \"conformance/c/simple.h\" as c\n"
    "\n"
    "const message: String = \"hi\\n\"\n"
    "const letter: char = 'a'\n"
    "\n"
    "fn prefix(ok: Bool, value: i32, ptr: Ptr) -> Void {\n"
    "    let neg: i32 = -value\n"
    "    let pos: i32 = +value\n"
    "    let not_ok: Bool = !ok\n"
    "    let deref_value: i32 = *ptr\n"
    "}\n"
    "\n"
    "test \"literal\\nname\" {\n"
    "    expect true\n"
    "}\n";
  ZDiag diag = {0};
  Program program = {0};
  expect(z_parse_canonical_text_program_source(source, &program, &diag), diag.message);
  expect(program.c_imports.len == 1, "expected C import");
  expect(strcmp(program.c_imports.items[0].header, "conformance/c/simple.h") == 0, "expected decoded C header path");
  expect(program.consts.len == 2, "expected decoded literal constants");
  expect(program.consts.items[0].expr && program.consts.items[0].expr->kind == EXPR_STRING, "expected string constant");
  expect(strcmp(program.consts.items[0].expr->text, "hi\n") == 0, "expected decoded string literal");
  expect(program.consts.items[1].expr && program.consts.items[1].expr->kind == EXPR_CHAR, "expected char constant");
  expect(strcmp(program.consts.items[1].expr->text, "97") == 0, "expected decoded character literal");
  expect(program.functions.len == 2, "expected prefix function and generated test");
  Function *prefix = &program.functions.items[0];
  expect(prefix->body.len == 4, "expected prefix function body");
  expect(prefix->body.items[1]->expr && prefix->body.items[1]->expr->kind == EXPR_BINARY, "expected unary plus import");
  expect(strcmp(prefix->body.items[1]->expr->text, "+") == 0, "expected unary plus to import as addition");
  expect(prefix->body.items[2]->expr && prefix->body.items[2]->expr->kind == EXPR_BINARY, "expected prefix not import");
  expect(strcmp(prefix->body.items[2]->expr->text, "==") == 0, "expected prefix not to import as comparison");
  expect(prefix->body.items[2]->expr->right && prefix->body.items[2]->expr->right->kind == EXPR_BOOL && !prefix->body.items[2]->expr->right->bool_value, "expected prefix not false operand");
  expect(prefix->body.items[3]->expr && prefix->body.items[3]->expr->kind == EXPR_CALL, "expected deref prefix import");
  expect(prefix->body.items[3]->expr->left && strcmp(prefix->body.items[3]->expr->left->text, "deref") == 0, "expected deref prefix callee");
  Function *test = &program.functions.items[1];
  expect(test->is_test, "expected generated test function");
  expect(strcmp(test->test_name, "literal\nname") == 0, "expected decoded test name");
  z_free_program(&program);

  Program invalid = {0};
  expect(!z_parse_canonical_text_program_source("fun main() -> Void {}\n", &invalid, NULL), "expected canonical Program parse failure without caller diag");
  z_free_program(&invalid);
}

static void imports_call_arguments_with_casts(void) {
  const char *source =
    "fn take(first: u8, second: u8) -> u8 {\n"
    "    return second\n"
    "}\n"
    "\n"
    "fn caller(value: u32, other: u8) -> u8 {\n"
    "    return take(value as u8, other)\n"
    "}\n";
  ZDiag diag = {0};
  Program program = {0};
  expect(z_parse_canonical_text_program_source(source, &program, &diag), diag.message);
  expect(program.functions.len == 2, "expected take and caller functions");
  Function *caller = &program.functions.items[1];
  expect(caller->body.len == 1, "expected caller return body");
  Expr *call = caller->body.items[0]->expr;
  expect(call && call->kind == EXPR_CALL, "expected return call expression");
  expect(call->args.len == 2, "expected cast call to keep both arguments");
  expect(call->args.items[0] && call->args.items[0]->kind == EXPR_CAST, "expected first argument cast");
  expect(strcmp(call->args.items[0]->text, "u8") == 0, "expected cast target to stop before comma");
  expect(call->args.items[1] && call->args.items[1]->kind == EXPR_IDENT, "expected second argument identifier");
  expect(strcmp(call->args.items[1]->text, "other") == 0, "expected second argument to survive cast parsing");
  z_free_program(&program);
}

static void expect_program_checks_and_roundtrips(const char *source, const char *label, bool library) {
  ZDiag diag = {0};
  Program program = {0};
  if (!z_parse_canonical_text_program_source(source, &program, &diag)) {
    fprintf(stderr, "%s:%d:%d: canonical Program parse failed: %s\n", label, diag.line, diag.column, diag.message);
    exit(1);
  }

  bool checked = library ? z_check_program_library(&program, &diag) : z_check_program(&program, &diag);
  if (!checked) {
    fprintf(stderr, "%s:%d:%d: canonical Program check failed: %s\n", label, diag.line, diag.column, diag.message);
    z_free_program(&program);
    exit(1);
  }

  SourceInput input = {.source_file = (char *)label, .source = (char *)source};
  ZProgramGraph graph = {0};
  ZProgramGraph roundtrip = {0};
  ZProgramGraphCompare comparison = {0};
  if (!z_program_graph_from_program(&input, &program, &graph)) {
    fprintf(stderr, "%s: canonical Program graph import failed\n", label);
    z_free_program(&program);
    exit(1);
  }
  if (!z_program_graph_direct_roundtrip_graph(&graph, label, &roundtrip, &comparison, &diag)) {
    fprintf(stderr, "%s:%d:%d: canonical Program graph roundtrip failed: %s\n", label, diag.line, diag.column, diag.message);
    z_program_graph_free(&graph);
    z_free_program(&program);
    exit(1);
  }
  if (!comparison.ok) {
    fprintf(stderr, "%s: canonical Program graph comparison failed: %s %s\n", label, comparison.code, comparison.message);
    z_program_graph_free(&roundtrip);
    z_program_graph_free(&graph);
    z_free_program(&program);
    exit(1);
  }
  z_program_graph_free(&roundtrip);
  z_program_graph_free(&graph);
  z_free_program(&program);
}

static void parses_checks_and_graph_roundtrips_core_program(void) {
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
    "        check world.out.write(\"canonical program ok\\n\")\n"
    "    } else {\n"
    "        check world.err.write(\"canonical program failed\\n\")\n"
    "    }\n"
    "}\n";
  expect_program_checks_and_roundtrips(source, "canonical core program", false);
}

static void parses_checks_and_graph_roundtrips_library_program(void) {
  const char *source =
    "fn fib(n: u32) -> u32 {\n"
    "    var index: u32 = 0\n"
    "    var a: u32 = 0\n"
    "    var b: u32 = 1\n"
    "    while index < n {\n"
    "        let next: u32 = a + b\n"
    "        a = b\n"
    "        b = next\n"
    "        index = index + 1\n"
    "    }\n"
    "    return a\n"
    "}\n"
    "\n"
    "test \"fibonacci\" {\n"
    "    expect fib(10) == 55\n"
    "}\n";
  expect_program_checks_and_roundtrips(source, "canonical library program", true);
}

static void parses_checks_and_graph_roundtrips_generic_shape_literal(void) {
  const char *source =
    "type Box<T> {\n"
    "    value: T,\n"
    "}\n"
    "\n"
    "fn caller(value: i32) -> Box<i32> {\n"
    "    let box: Box<i32> = Box { value: value }\n"
    "    return box\n"
    "}\n";
  expect_program_checks_and_roundtrips(source, "canonical generic shape literal", true);
}

static void parses_checks_and_graph_roundtrips_rescue_operand(void) {
  const char *source =
    "fn maybe_value() -> u8 raises [Missing] {\n"
    "    raise Missing\n"
    "}\n"
    "\n"
    "fn fallback() -> u8 {\n"
    "    return 1_u8 + rescue maybe_value() err 2_u8\n"
    "}\n";
  expect_program_checks_and_roundtrips(source, "canonical rescue operand", true);
}

static void parses_checks_and_graph_roundtrips_match_guards(void) {
  const char *source =
    "fn bucket(value: u8, enabled: Bool) -> i32 {\n"
    "    match value {\n"
    "        0 if enabled {\n"
    "            return 10\n"
    "        }\n"
    "        0 {\n"
    "            return 11\n"
    "        }\n"
    "        1..3 {\n"
    "            return 20\n"
    "        }\n"
    "        4..255 {\n"
    "            return 30\n"
    "        }\n"
    "    }\n"
    "    return 0\n"
    "}\n";
  expect_program_checks_and_roundtrips(source, "canonical match guards", true);
}

static void parses_checks_and_graph_roundtrips_type_defaults_and_methods(void) {
  const char *source =
    "type Counter {\n"
    "    value: i32 = 0,\n"
    "    pub fn add(self: mutref<Self>, amount: i32) -> Void {\n"
    "        self.value = self.value + amount\n"
    "    }\n"
    "}\n"
    "\n"
    "fn make_counter() -> i32 {\n"
    "    var counter: Counter = Counter {}\n"
    "    counter.add(3)\n"
    "    return counter.value\n"
    "}\n";
  expect_program_checks_and_roundtrips(source, "canonical type defaults and methods", true);
}

static void rejects_noncanonical_spellings(void) {
  expect_format_rejects_without_diag("fn ok() -> Void {}\n123abc\n", "formatter rejects malformed trailing input without diag");
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
  expect_rejects("fn bad(items: Items) -> Void {\n    for item in items {\n        return\n    }\n}\n", "non-range for loop");
  expect_rejects("fn bad() -> Void {\n    for item in ..4 {\n        return\n    }\n}\n", "missing for range start");
  expect_rejects("fn bad() -> Void {\n    for item in 0.. {\n        return\n    }\n}\n", "missing for range end");
  expect_rejects("fn bad() -> Void {\n    for item in 0..4..8 {\n        return\n    }\n}\n", "extra for range separator");
  expect_rejects("fn bad(items: Items) -> Void {\n    for item in 1 < 2 < 3 {\n        return\n    }\n}\n", "chained comparison in for range");
  expect_rejects("type Pair<T U> {\n    left: T,\n    right: U,\n}\n", "missing type parameter comma");
  expect_rejects("fn id<T U>(value: T) -> T {\n    return value\n}\n", "missing function type parameter comma");
  expect_rejects("type Box<T> {\n    value: T,\n}\n\nfn bad(value: i32) -> Void {\n    let box: Box<i32> = Box<i32> { value: value }\n}\n", "generic shape literal type arguments");
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
  expect_rejects("fn bad() -> Void {\n    let text: String = \"hello\rworld\"\n}\n", "raw carriage return string newline");
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
  expect_rejects("fn bad() -> Void {\n    let value: i32 = 1..4\n}\n", "range expression outside range syntax");
  expect_rejects("use \"not-module\"\n", "string use import");
  expect_rejects("use std.mem()\n", "call use import");
  expect_rejects("use std.\n", "trailing use import separator");
  expect_rejects("fn bad() -> Void {\n    set value value + 1\n}\n", "assignment missing equals");
  expect_rejects("fn bad() -> Void {\n    set value = value + 1\n}\n", "set assignment");
  expect_rejects("fn bad() -> Void {\n    set 1 = value\n}\n", "numeric assignment target");
  expect_rejects("fn bad() -> Void {\n    set items [] = value\n}\n", "spaced assignment index");
  expect_rejects("fn bad() -> Void {\n    set items[] = value\n}\n", "empty assignment index");
  expect_rejects("fn bad() -> Void {\n    defer { cleanup() }\n}\n", "defer block form");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = check\n}\n", "empty check expression");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = meta\n}\n", "empty meta expression");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = rescue maybe_value()\n}\n", "rescue missing err fallback");
  expect_rejects("fn bad() -> Void {\n    let value: i32 = maybe_value() err 1\n}\n", "err without rescue");
  expect_rejects("enum Bad u8 {\n    one,\n}\n", "enum storage type without colon");
  expect_rejects("enum Bad<T> {\n    one,\n}\n", "generic enum declaration");
  expect_rejects("choice Bad<T> {\n    ok: i32,\n}\n", "generic choice declaration");
  expect_accepts("choice Bad {\n    ok,\n}\n", "choice variant without payload");
  expect_rejects("choice Result {\n    ok: i32,\n}\n\nfn bad(result: Result) -> Void {\n    match result {\n        ok value {\n            return\n        }\n    }\n}\n", "choice match pattern without dot payload syntax");
  expect_rejects("choice Result {\n    ok: i32,\n}\n\nfn bad(result: Result) -> Void {\n    match result {\n        .ok(left, right) {\n            return\n        }\n    }\n}\n", "multiple choice match payload bindings");
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
  formats_core_declarations_and_blocks();
  formats_angles_comparisons_and_ranges_canonically();
  formats_deep_nested_blocks();
  formats_public_lists_match_patterns_and_prefix_forms();
  formats_type_defaults_methods_and_generic_field_commas();
  formats_else_and_line_start_prefix_forms();
  parses_fallibility_choices_and_interfaces();
  parses_nested_generic_type_commas();
  imports_nested_generic_declaration_field_types();
  parses_separate_boolean_comparisons();
  parses_parenthesized_comparisons();
  parses_else_if_chains();
  records_block_open_locations();
  records_node_token_spans();
  parses_public_declarations_and_extern_types();
  parses_layout_enum_choice_and_const_type_forms();
  parses_character_literals();
  parses_generic_calls_and_array_repeats();
  parses_error_members_and_prefix_not();
  parses_generic_interfaces();
  parses_choice_payload_match_patterns();
  parses_control_flow_tests_and_static_forms();
  parses_empty_return_but_not_empty_checks();
  parses_use_declarations_and_zero_arg_calls();
  imports_public_c_and_use_source_ranges();
  records_declaration_start_source_locations();
  parses_assignment_statements();
  parses_effectful_expression_forms();
  imports_decoded_literals_and_prefix_forms();
  imports_call_arguments_with_casts();
  parses_checks_and_graph_roundtrips_core_program();
  parses_checks_and_graph_roundtrips_library_program();
  parses_checks_and_graph_roundtrips_generic_shape_literal();
  parses_checks_and_graph_roundtrips_rescue_operand();
  parses_checks_and_graph_roundtrips_match_guards();
  parses_checks_and_graph_roundtrips_type_defaults_and_methods();
  rejects_noncanonical_spellings();
  for (int i = 1; i + 1 < argc; i += 2) parse_file_arg(argv[i], argv[i + 1]);
  printf("canonical text smoke ok\n");
  return 0;
}
