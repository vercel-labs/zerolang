#include "program_graph_contracts.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool contract_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool contract_text_span_eq(const char *start, const char *end, const char *text) {
  if (!start || !end || !text) return false;
  while (start < end && *text) {
    if (*start++ != *text++) return false;
  }
  return start == end && *text == '\0';
}

static bool contract_starts_with(const char *text, const char *prefix) {
  size_t prefix_len = prefix ? strlen(prefix) : 0;
  return text && prefix && strncmp(text, prefix, prefix_len) == 0;
}

static bool identifier_start(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static bool identifier_part(char ch) {
  return identifier_start(ch) || (ch >= '0' && ch <= '9');
}

static bool reserved_word_span(const char *start, const char *end) {
  const char *keywords[] = {
    "as", "break", "check", "choice", "const", "continue", "defer", "else", "enum", "export", "extern", "false",
    "fn", "for", "fun", "if", "import", "in", "let", "match", "meta", "mut", "null", "packed", "pub",
    "raise", "raises", "rescue", "ret", "return", "shape", "static", "test", "true", "type",
    "use", "var", "while", NULL
  };
  for (size_t i = 0; keywords[i]; i++) {
    if (contract_text_span_eq(start, end, keywords[i])) return true;
  }
  return false;
}

static bool identifier_valid(const char *text) {
  if (!text || !identifier_start(*text)) return false;
  const char *cursor = text + 1;
  for (; *cursor; cursor++) {
    if (!identifier_part(*cursor)) return false;
  }
  return !reserved_word_span(text, cursor);
}

static bool module_name_valid(const char *text) {
  if (!text || !*text) return false;
  const char *cursor = text;
  for (;;) {
    const char *segment = cursor;
    if (!identifier_start(*cursor)) return false;
    cursor++;
    while (identifier_part(*cursor)) cursor++;
    if (reserved_word_span(segment, cursor)) return false;
    if (*cursor == '\0') return true;
    if (*cursor != '.') return false;
    cursor++;
    if (*cursor == '\0') return false;
  }
}

static bool module_is_stdlib(const char *module) {
  return contract_starts_with(module, "std.");
}

static bool graph_has_module_named(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && contract_text_eq(node->name, name)) return true;
  }
  return false;
}

static bool literal_case_valid(const char *text) {
  const char *cursor = text;
  if (!cursor || !*cursor) return false;
  if (*cursor == '-') cursor++;
  if (!isdigit((unsigned char)*cursor)) return false;
  while (*cursor) {
    if (!isdigit((unsigned char)*cursor) && *cursor != '_') return false;
    cursor++;
  }
  return true;
}

static bool match_case_valid(const char *text) {
  return contract_text_eq(text, "true") ||
         contract_text_eq(text, "false") ||
         identifier_valid(text) ||
         literal_case_valid(text);
}

static bool operator_valid(const char *text) {
  const char *ops[] = {"+", "-", "*", "/", "%", "&&", "||", "==", "!=", "<", "<=", ">", ">=", "+%", "+|", NULL};
  for (size_t i = 0; ops[i]; i++) {
    if (contract_text_eq(text, ops[i])) return true;
  }
  return false;
}

static bool fail_node(ZDiag *diag, const ZProgramGraphNode *node, const char *path, const char *message, const char *expected, const char *actual, const char *help) {
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 2002;
    diag->path = node && node->path && node->path[0] ? node->path : path;
    diag->line = node && node->line > 0 ? node->line : 1;
    diag->column = node && node->column > 0 ? node->column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "cannot validate program graph contract");
    snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "valid ProgramGraph semantic contract");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : (node ? z_program_graph_node_kind_name(node->kind) : "missing graph node"));
    if (help) snprintf(diag->help, sizeof(diag->help), "%s", help);
  }
  return false;
}

static bool require_identifier(const ZProgramGraphNode *node, const char *text, const char *path, const char *message, ZDiag *diag) {
  if (identifier_valid(text)) return true;
  return fail_node(diag,
                   node,
                   path,
                   message ? message : "program graph name is not valid Zero identifier syntax",
                   "identifier",
                   text && text[0] ? text : "missing name",
                   "use a non-keyword identifier");
}

static bool require_top_level_identifier(const ZProgramGraphNode *node, const char *kind, const char *path, const char *message, bool allow_test_name, ZDiag *diag) {
  const char *name = node ? node->name : NULL;
  if (!require_identifier(node, name, path, message, diag)) return false;
  if (!contract_starts_with(name, "__zero_")) return true;
  if (allow_test_name && contract_starts_with(name, "__zero_test_")) return true;
  if (contract_starts_with(name, "__zero_std_")) return true;
  char expected[128];
  snprintf(expected, sizeof(expected), "top-level %s name without the __zero_ prefix", kind ? kind : "declaration");
  return fail_node(diag,
                   node,
                   path,
                   "program graph declaration uses a reserved compiler-internal symbol name",
                   expected,
                   name,
                   "rename the declaration; __zero_ names are reserved for compiler-provided helpers");
}

static bool import_contract_ok(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *path, ZDiag *diag) {
  const char *module = node->name ? node->name : "";
  const char *alias = node->value && node->value[0] ? node->value : NULL;
  if (!module_name_valid(module)) {
    return fail_node(diag, node, path, "program graph import module is not valid Zero import syntax", "dot-separated module identifiers", module, "use module names like std.mem or package.local");
  }
  if (alias && !identifier_valid(alias)) {
    return fail_node(diag, node, path, "program graph import alias is not valid Zero identifier syntax", "identifier", alias, "use an identifier alias or clear the alias value");
  }
  if (!module_is_stdlib(module) && !graph_has_module_named(graph, module)) {
    return fail_node(diag, node, path, "program graph import target module is missing", "Module node for package-local import or std.* import", module, "add the imported module to the artifact or remove the import");
  }
  return true;
}

bool z_program_graph_name_contracts_ok(const ZProgramGraph *graph, const char *path, ZDiag *diag) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    switch (node->kind) {
      case Z_PROGRAM_GRAPH_NODE_IMPORT:
        if (!import_contract_ok(graph, node, path, diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_C_IMPORT:
        if (!require_identifier(node, node->name, path, "program graph C import alias is not valid Zero identifier syntax", diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_CONST:
        if (!require_top_level_identifier(node, "const", path, "program graph const name is not valid Zero identifier syntax", false, diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
        if (!require_top_level_identifier(node, "type-alias", path, "program graph alias name is not valid Zero identifier syntax", false, diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_SHAPE:
        if (!require_top_level_identifier(node, "shape", path, "program graph type name is not valid Zero identifier syntax", false, diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_INTERFACE:
        if (!require_top_level_identifier(node, "interface", path, "program graph interface name is not valid Zero identifier syntax", false, diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_ENUM:
        if (!require_top_level_identifier(node, "enum", path, "program graph enum name is not valid Zero identifier syntax", false, diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_CHOICE:
        if (!require_top_level_identifier(node, "choice", path, "program graph choice name is not valid Zero identifier syntax", false, diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_FUNCTION:
        if (!require_top_level_identifier(node, "function", path, "program graph function name is not valid Zero identifier syntax", true, diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_PARAM:
      case Z_PROGRAM_GRAPH_NODE_FIELD:
      case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
      case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
        if (!require_identifier(node, node->name, path, "program graph parameter name is not valid Zero identifier syntax", diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_FIELD_INIT:
        if (!require_identifier(node, node->name, path, "program graph field initializer name is not valid Zero identifier syntax", diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
        if (!require_identifier(node, node->name, path, "program graph identifier name is not valid Zero identifier syntax", diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
        if (!require_identifier(node, node->name, path, "program graph field access name is not valid Zero identifier syntax", diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_CALL:
      case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
        if (node->name && node->name[0] && !identifier_valid(node->name) && !operator_valid(node->name)) {
          return fail_node(diag, node, path, "program graph call callee name is not valid Zero identifier syntax", "identifier", node->name, "use an identifier callee or a supported operator node");
        }
        break;
      case Z_PROGRAM_GRAPH_NODE_RESCUE:
        if (!require_identifier(node, node->name, path, "program graph rescue binding name is not valid Zero identifier syntax", diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_MATCH_ARM:
        if (!match_case_valid(node->name)) {
          return fail_node(diag, node, path, "program graph match case name is not valid Zero match syntax", "identifier, true, false, or number", node->name && node->name[0] ? node->name : "missing name", "use a non-keyword case name");
        }
        if (node->value && node->value[0] && !require_identifier(node, node->value, path, "program graph match payload name is not valid Zero identifier syntax", diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_LET:
        if (!require_identifier(node, node->name, path, "program graph binding name is not valid Zero identifier syntax", diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_FOR:
        if (!require_identifier(node, node->name, path, "program graph loop binding name is not valid Zero identifier syntax", diag)) return false;
        break;
      case Z_PROGRAM_GRAPH_NODE_RAISE:
        if (!require_identifier(node, node->name, path, "program graph raise error name is not valid Zero identifier syntax", diag)) return false;
        break;
      default:
        break;
    }
  }
  return true;
}

bool z_program_graph_semantic_contracts_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *path, ZDiag *diag) {
  if (!z_program_graph_effect_contracts_ok(graph, resolution, path, diag)) return false;
  if (!z_program_graph_memory_contracts_ok(graph, resolution, path, diag)) return false;
  if (!z_program_graph_fixed_array_length_contracts_ok(graph, resolution, path, diag)) return false;
  return z_program_graph_borrow_contracts_ok(graph, path, diag);
}
