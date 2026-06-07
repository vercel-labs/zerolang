#include "program_graph_patch.h"
#include "type_core.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void patch_replace_text(char **slot, const char *value) {
  if (!slot) return;
  char *copy = z_strdup(value ? value : "");
  free(*slot);
  *slot = copy;
}

static void patch_result_fail(ZProgramGraphPatchResult *result, const char *code, const char *message, const char *expected, const char *actual) {
  if (!result) return;
  result->ok = false;
  snprintf(result->code, sizeof(result->code), "%s", code ? code : "GPH000");
  snprintf(result->message, sizeof(result->message), "%s", message ? message : "program graph patch failed");
  patch_replace_text(&result->expected, expected);
  patch_replace_text(&result->actual, actual);
}

static void patch_op_fail(ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *code, const char *message, const char *expected, const char *actual) {
  char *expected_copy = z_strdup(expected ? expected : "");
  char *actual_copy = z_strdup(actual ? actual : "");
  if (op) {
    op->ok = false;
    snprintf(op->code, sizeof(op->code), "%s", code ? code : "GPH000");
    snprintf(op->message, sizeof(op->message), "%s", message ? message : "program graph patch operation failed");
    patch_replace_text(&op->expected, expected_copy);
    patch_replace_text(&op->actual, actual_copy);
  }
  patch_result_fail(result, code, message, expected_copy, actual_copy);
  free(expected_copy);
  free(actual_copy);
}

static bool patch_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool patch_parse_bool(const char *text, bool *out) {
  if (strcmp(text ? text : "", "true") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(text ? text : "", "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

static bool patch_parse_node_kind(const char *text, ZProgramGraphNodeKind *out) {
  for (int kind = Z_PROGRAM_GRAPH_NODE_MODULE; kind <= Z_PROGRAM_GRAPH_NODE_STATEMENT; kind++) {
    if (patch_text_eq(text, z_program_graph_node_kind_name((ZProgramGraphNodeKind)kind))) {
      *out = (ZProgramGraphNodeKind)kind;
      return true;
    }
  }
  return false;
}

static bool patch_parse_edge_target(const char *text, ZProgramGraphEdgeTarget *out) {
  for (int target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE; target <= Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT; target++) {
    if (patch_text_eq(text, z_program_graph_edge_target_name((ZProgramGraphEdgeTarget)target))) {
      *out = (ZProgramGraphEdgeTarget)target;
      return true;
    }
  }
  return false;
}

static bool patch_node_id_valid(const char *text) {
  if (!text || text[0] != '#' || !text[1]) return false;
  for (const char *cursor = text + 1; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.')) return false;
  }
  return true;
}

static bool patch_edge_kind_valid(const char *text) {
  if (!text || !text[0]) return false;
  for (const unsigned char *cursor = (const unsigned char *)text; *cursor; cursor++) {
    if (!(isalnum(*cursor) || *cursor == '_' || *cursor == '-')) return false;
  }
  return true;
}

static ZProgramGraphNode *patch_find_node(ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (patch_text_eq(graph->nodes[i].id, node_id)) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphNode *patch_find_node_const(const ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (patch_text_eq(graph->nodes[i].id, node_id)) return &graph->nodes[i];
  }
  return NULL;
}

static bool patch_symbol_exists(const ZProgramGraph *graph, const char *symbol_id) {
  if (!symbol_id || !symbol_id[0]) return false;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].symbol_id && patch_text_eq(graph->nodes[i].symbol_id, symbol_id)) return true;
  }
  return false;
}

static bool patch_type_exists(const ZProgramGraph *graph, const char *type_id) {
  if (!type_id || !type_id[0]) return false;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].type_id && patch_text_eq(graph->nodes[i].type_id, type_id)) return true;
  }
  return false;
}

static bool patch_effect_exists(const ZProgramGraph *graph, const char *effect_id) {
  if (!effect_id || !effect_id[0]) return false;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].effect_id && patch_text_eq(graph->nodes[i].effect_id, effect_id)) return true;
  }
  return false;
}

static bool patch_edge_target_exists(const ZProgramGraph *graph, ZProgramGraphEdgeTarget target, const char *to) {
  switch (target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE: return patch_find_node_const(graph, to) != NULL;
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL: return patch_symbol_exists(graph, to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE: return patch_type_exists(graph, to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT: return patch_effect_exists(graph, to);
  }
  return false;
}

static void patch_reserve_nodes(ZProgramGraph *graph, size_t len) {
  if (graph->node_cap >= len) return;
  size_t next = graph->node_cap ? graph->node_cap * 2 : 8;
  while (next < len) next *= 2;
  graph->nodes = z_checked_reallocarray(graph->nodes, next, sizeof(ZProgramGraphNode));
  for (size_t i = graph->node_cap; i < next; i++) graph->nodes[i] = (ZProgramGraphNode){0};
  graph->node_cap = next;
}

static void patch_reserve_edges(ZProgramGraph *graph, size_t len) {
  if (graph->edge_cap >= len) return;
  size_t next = graph->edge_cap ? graph->edge_cap * 2 : 8;
  while (next < len) next *= 2;
  graph->edges = z_checked_reallocarray(graph->edges, next, sizeof(ZProgramGraphEdge));
  for (size_t i = graph->edge_cap; i < next; i++) graph->edges[i] = (ZProgramGraphEdge){0};
  graph->edge_cap = next;
}

static ZProgramGraphNode *patch_append_node(ZProgramGraph *graph) {
  patch_reserve_nodes(graph, graph->node_len + 1);
  ZProgramGraphNode *node = &graph->nodes[graph->node_len++];
  *node = (ZProgramGraphNode){0};
  return node;
}

static ZProgramGraphEdge *patch_append_edge(ZProgramGraph *graph) {
  patch_reserve_edges(graph, graph->edge_len + 1);
  ZProgramGraphEdge *edge = &graph->edges[graph->edge_len++];
  *edge = (ZProgramGraphEdge){0};
  return edge;
}

static bool patch_duplicate_ordered_edge(const ZProgramGraph *graph, const char *from, const char *kind, ZProgramGraphEdgeTarget target, size_t order) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == target && edge->order == order && patch_text_eq(edge->from, from) && patch_text_eq(edge->kind, kind)) return true;
  }
  return false;
}

static char **patch_node_text_field(ZProgramGraphNode *node, const char *field) {
  if (!node || !field) return NULL;
  if (strcmp(field, "name") == 0) return &node->name;
  if (strcmp(field, "type") == 0) return &node->type;
  if (strcmp(field, "value") == 0) return &node->value;
  return NULL;
}

static bool *patch_node_bool_field(ZProgramGraphNode *node, const char *field) {
  if (!node || !field) return NULL;
  if (strcmp(field, "public") == 0) return &node->is_public;
  if (strcmp(field, "mutable") == 0) return &node->is_mutable;
  if (strcmp(field, "static") == 0) return &node->is_static;
  if (strcmp(field, "fallible") == 0) return &node->fallible;
  if (strcmp(field, "exportC") == 0) return &node->export_c;
  return NULL;
}

static bool patch_name_operator_valid(const char *text) {
  static const char *operators[] = {
    "+", "-", "*", "/", "%", "&&", "||", "==", "!=", "<", "<=", ">", ">=", "+%", "+|", NULL,
  };
  for (size_t i = 0; operators[i]; i++) {
    if (patch_text_eq(text, operators[i])) return true;
  }
  return false;
}

static bool patch_name_segment_char(char ch) {
  return isalnum((unsigned char)ch) || ch == '_';
}

static bool patch_name_value_valid(const char *text) {
  if (!text || !text[0]) return true;
  if (patch_name_operator_valid(text)) return true;
  const char *cursor = text;
  while (*cursor) {
    if (!(isalpha((unsigned char)*cursor) || *cursor == '_')) return false;
    cursor++;
    while (patch_name_segment_char(*cursor)) cursor++;
    if (*cursor == '.') {
      cursor++;
      if (!*cursor) return false;
      continue;
    }
    return *cursor == '\0';
  }
  return true;
}

static bool patch_identifier_value_valid(const char *text) {
  if (!text || !text[0]) return true;
  const char *cursor = text;
  if (!(isalpha((unsigned char)*cursor) || *cursor == '_')) return false;
  cursor++;
  while (patch_name_segment_char(*cursor)) cursor++;
  return *cursor == '\0';
}

static bool patch_import_module_value_valid(const char *text) {
  if (!text || !text[0] || patch_name_operator_valid(text)) return false;
  return patch_name_value_valid(text);
}

static bool patch_text_has_control(const char *text) {
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    if (*cursor < 0x20 || *cursor == 0x7f) return true;
  }
  return false;
}

static bool patch_type_value_valid(const char *text) {
  if (!text || !text[0]) return true;
  if (patch_text_has_control(text)) return false;
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId type = Z_TYPE_ID_INVALID;
  ZTypeParseError error = {0};
  bool ok = z_type_parse(&arena, text, &type, &error);
  z_type_arena_free(&arena);
  return ok;
}

static bool patch_validate_text_field(const ZProgramGraphNode *node, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *field, const char *value) {
  if (patch_text_eq(field, "name") && !patch_name_value_valid(value)) {
    patch_op_fail(result, op, "GPH003", "patch name value must be a Zero identifier path or operator", "identifier path or operator", value);
    return false;
  }
  if (patch_text_eq(field, "type") && !patch_type_value_valid(value)) {
    patch_op_fail(result, op, "GPH003", "patch type value must be valid Zero type syntax", "Zero type syntax", value);
    return false;
  }
  if (node && node->kind == Z_PROGRAM_GRAPH_NODE_MATCH_ARM && patch_text_eq(field, "value") && !patch_name_value_valid(value)) {
    patch_op_fail(result, op, "GPH003", "patch match payload value must be a Zero identifier path or operator", "identifier path or operator", value);
    return false;
  }
  if (node && node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT && patch_text_eq(field, "value") && !patch_identifier_value_valid(value)) {
    patch_op_fail(result, op, "GPH003", "patch import alias value must be a Zero identifier", "identifier", value);
    return false;
  }
  return true;
}

static bool patch_validate_text_value(const ZProgramGraphNode *node, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  return patch_validate_text_field(node, result, op, op->field, op->value);
}

static bool patch_validate_node_payload(const ZProgramGraphNode *node, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (node->name) {
    if (!patch_validate_text_field(node, result, op, "name", node->name)) return false;
  }
  if (node->type) {
    if (!patch_validate_text_field(node, result, op, "type", node->type)) return false;
  }
  if (node->value) {
    if (!patch_validate_text_field(node, result, op, "value", node->value)) return false;
  }
  return true;
}

static void patch_copy_node_attrs(ZProgramGraphNode *node, const ZProgramGraphPatchOpResult *op) {
  if (op->name) patch_replace_text(&node->name, op->name);
  if (op->type) patch_replace_text(&node->type, op->type);
  if (op->value) patch_replace_text(&node->value, op->value);
  if (op->path) patch_replace_text(&node->path, op->path);
  if (op->has_line_value) node->line = op->line_value;
  if (op->has_column_value) node->column = op->column_value;
  if (op->has_public_value) node->is_public = op->public_value;
  if (op->has_mutable_value) node->is_mutable = op->mutable_value;
  if (op->has_static_value) node->is_static = op->static_value;
  if (op->has_fallible_value) node->fallible = op->fallible_value;
  if (op->has_export_c_value) node->export_c = op->export_c_value;
}

static bool patch_apply_insert_edge(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphEdgeTarget target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE;
  if (!patch_parse_edge_target(op->target, &target)) {
    patch_op_fail(result, op, "GPH003", "patch edge target must name a ProgramGraph target domain", "node, symbol, type, or effect", op->target);
    return false;
  }
  if (!patch_edge_kind_valid(op->edge)) {
    patch_op_fail(result, op, "GPH003", "patch edge kind must be a simple ProgramGraph edge name", "edge identifier", op->edge);
    return false;
  }
  if (!patch_find_node(graph, op->from)) {
    patch_op_fail(result, op, "GPH004", "patch edge source was not found", op->from, "");
    return false;
  }
  if (!patch_edge_target_exists(graph, target, op->to)) {
    patch_op_fail(result, op, "GPH004", "patch edge target was not found", op->to, "");
    return false;
  }
  if (patch_duplicate_ordered_edge(graph, op->from, op->edge, target, op->order)) {
    patch_op_fail(result, op, "GPH005", "patch edge order is already occupied", "unused ordered edge slot", op->edge);
    return false;
  }
  ZProgramGraphEdge *edge = patch_append_edge(graph);
  edge->from = z_strdup(op->from);
  edge->to = z_strdup(op->to);
  edge->kind = z_strdup(op->edge);
  edge->target = target;
  edge->order = op->order;
  op->ok = true;
  return true;
}

static bool patch_apply_insert(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!patch_node_id_valid(op->node)) {
    patch_op_fail(result, op, "GPH003", "insert node id must be a ProgramGraph node id", "#<id>", op->node);
    return false;
  }
  if (patch_find_node(graph, op->node)) {
    patch_op_fail(result, op, "GPH005", "insert node id already exists", "unused node id", op->node);
    return false;
  }
  if (!patch_find_node(graph, op->parent)) {
    patch_op_fail(result, op, "GPH004", "insert parent node was not found", op->parent, "");
    return false;
  }
  if (!patch_edge_kind_valid(op->edge)) {
    patch_op_fail(result, op, "GPH003", "insert edge kind must be a simple ProgramGraph edge name", "edge identifier", op->edge);
    return false;
  }
  ZProgramGraphNodeKind kind = Z_PROGRAM_GRAPH_NODE_EXPRESSION;
  if (!patch_parse_node_kind(op->kind, &kind)) {
    patch_op_fail(result, op, "GPH003", "insert kind must name a ProgramGraph node kind", "ProgramGraph node kind", op->kind);
    return false;
  }
  if (patch_duplicate_ordered_edge(graph, op->parent, op->edge, Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, op->order)) {
    patch_op_fail(result, op, "GPH005", "insert edge order is already occupied", "unused ordered edge slot", op->edge);
    return false;
  }
  ZProgramGraphNode candidate = {
    .id = op->node,
    .kind = kind,
    .name = op->name,
    .type = op->type,
    .value = op->value,
    .path = op->path,
    .line = op->has_line_value ? op->line_value : 0,
    .column = op->has_column_value ? op->column_value : 0,
    .is_public = op->has_public_value && op->public_value,
    .is_mutable = op->has_mutable_value && op->mutable_value,
    .is_static = op->has_static_value && op->static_value,
    .fallible = op->has_fallible_value && op->fallible_value,
    .export_c = op->has_export_c_value && op->export_c_value,
  };
  if (!patch_validate_node_payload(&candidate, result, op)) return false;
  ZProgramGraphNode *node = patch_append_node(graph);
  node->id = z_strdup(op->node);
  node->kind = kind;
  patch_copy_node_attrs(node, op);
  ZProgramGraphEdge *edge = patch_append_edge(graph);
  edge->from = z_strdup(op->parent);
  edge->to = z_strdup(op->node);
  edge->kind = z_strdup(op->edge);
  edge->target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE;
  edge->order = op->order;
  op->ok = true;
  return true;
}

static bool patch_apply_replace(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "patch node was not found", op->node, "");
    return false;
  }
  patch_replace_text(&op->actual, node->node_hash);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "patch node hash precondition failed", op->expected, op->actual);
    return false;
  }
  ZProgramGraphNodeKind kind = node->kind;
  if (op->kind && !patch_parse_node_kind(op->kind, &kind)) {
    patch_op_fail(result, op, "GPH003", "replace kind must name a ProgramGraph node kind", "ProgramGraph node kind", op->kind);
    return false;
  }
  ZProgramGraphNode candidate = *node;
  candidate.kind = kind;
  if (op->name) candidate.name = op->name;
  if (op->type) candidate.type = op->type;
  if (op->value) candidate.value = op->value;
  if (op->path) candidate.path = op->path;
  if (op->has_line_value) candidate.line = op->line_value;
  if (op->has_column_value) candidate.column = op->column_value;
  if (op->has_public_value) candidate.is_public = op->public_value;
  if (op->has_mutable_value) candidate.is_mutable = op->mutable_value;
  if (op->has_static_value) candidate.is_static = op->static_value;
  if (op->has_fallible_value) candidate.fallible = op->fallible_value;
  if (op->has_export_c_value) candidate.export_c = op->export_c_value;
  if (!patch_validate_node_payload(&candidate, result, op)) return false;
  node->kind = kind;
  patch_copy_node_attrs(node, op);
  op->ok = true;
  return true;
}

static bool patch_apply_rename(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "patch node was not found", op->node, "");
    return false;
  }
  patch_replace_text(&op->actual, node->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "patch rename precondition failed", op->expected, op->actual);
    return false;
  }
  if (!patch_validate_text_field(node, result, op, "name", op->value)) return false;
  patch_replace_text(&node->name, op->value);
  op->ok = true;
  return true;
}

static ZProgramGraphNode *patch_ordered_node(ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->order == order && patch_text_eq(edge->from, from) && patch_text_eq(edge->kind, kind)) return patch_find_node(graph, edge->to);
  }
  return NULL;
}

static ZProgramGraphNode *patch_parent_module_for_function(ZProgramGraph *graph, const char *function_id) {
  ZProgramGraphNode *parent = NULL;
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->to, function_id) || !patch_text_eq(edge->kind, "function")) continue;
    ZProgramGraphNode *node = patch_find_node(graph, edge->from);
    if (!node || node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    parent = node;
    count++;
  }
  return count == 1 ? parent : NULL;
}

static bool patch_function_name_exists_in_module(ZProgramGraph *graph, const char *module_id, const char *except_id, const char *name) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, module_id) || !patch_text_eq(edge->kind, "function")) continue;
    ZProgramGraphNode *node = patch_find_node(graph, edge->to);
    if (node && node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && !patch_text_eq(node->id, except_id) && patch_text_eq(node->name, name)) return true;
  }
  return false;
}

static bool patch_apply_rename_symbol(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "renameSymbol target node was not found", op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) {
    patch_op_fail(result, op, "GPH003", "renameSymbol currently supports Function nodes only", "Function node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  patch_replace_text(&op->actual, node->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "renameSymbol precondition failed", op->expected, op->actual);
    return false;
  }
  if (!patch_identifier_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "renameSymbol value must be a Zero identifier", "identifier", op->value);
    return false;
  }
  ZProgramGraphNode *module = patch_parent_module_for_function(graph, node->id);
  if (!module) {
    patch_op_fail(result, op, "GPH004", "renameSymbol function owner module was not found", "single owning Module node", node->id);
    return false;
  }
  if (patch_function_name_exists_in_module(graph, module->id, node->id, op->value)) {
    patch_op_fail(result, op, "GPH005", "renameSymbol target function name already exists in module", "unused function name", op->value);
    return false;
  }
  const char *old_name = node->name ? node->name : "";
  patch_replace_text(&node->name, op->value);
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    ZProgramGraphNode *candidate = &graph->nodes[i];
    if (candidate->kind != Z_PROGRAM_GRAPH_NODE_CALL || !patch_text_eq(candidate->name, old_name)) continue;
    patch_replace_text(&candidate->name, op->value);
    ZProgramGraphNode *callee = patch_ordered_node(graph, candidate->id, "left", 0);
    if (callee && callee->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER && patch_text_eq(callee->name, old_name)) patch_replace_text(&callee->name, op->value);
  }
  op->ok = true;
  return true;
}

static bool patch_apply_replace_callee(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "patch call node was not found", op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_CALL && node->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL) {
    patch_op_fail(result, op, "GPH003", "replaceCallee target must be a call node", "Call or MethodCall node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  patch_replace_text(&op->actual, node->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "replaceCallee precondition failed", op->expected, op->actual);
    return false;
  }
  if (!patch_identifier_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "replaceCallee value must be a Zero identifier", "identifier", op->value);
    return false;
  }
  ZProgramGraphNode *callee = patch_ordered_node(graph, node->id, "left", 0);
  if (callee && (callee->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER || callee->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) && patch_text_eq(callee->name, op->actual)) patch_replace_text(&callee->name, op->value);
  else if (callee) {
    patch_op_fail(result, op, "GPH005", "replaceCallee could not prove the callee leaf", "call name matching callee leaf", callee->name);
    return false;
  }
  patch_replace_text(&node->name, op->value);
  op->ok = true;
  return true;
}

static ZProgramGraphNode *patch_default_module_node(ZProgramGraph *graph) {
  ZProgramGraphNode *module = NULL;
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    module = &graph->nodes[i];
    count++;
  }
  return count == 1 ? module : NULL;
}

static bool patch_import_exists(const ZProgramGraph *graph, const char *parent, const char *module, const char *alias) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, parent) || !patch_text_eq(edge->kind, "import")) continue;
    const ZProgramGraphNode *node = patch_find_node_const(graph, edge->to);
    if (node && node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT && patch_text_eq(node->name, module) && patch_text_eq(node->value, alias)) return true;
  }
  return false;
}

static size_t patch_next_import_order(const ZProgramGraph *graph, const char *parent) {
  size_t next = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && patch_text_eq(edge->from, parent) && patch_text_eq(edge->kind, "import") && edge->order >= next) next = edge->order + 1;
  }
  return next;
}

static uint64_t patch_hash_text(uint64_t hash, const char *text) {
  const unsigned char *p = (const unsigned char *)(text ? text : "");
  while (*p) { hash ^= (uint64_t)*p++; hash *= 1099511628211ull; }
  hash ^= 0xffu; hash *= 1099511628211ull; return hash;
}

static char *patch_import_node_id(const char *parent, const char *module, const char *alias) {
  uint64_t hash = patch_hash_text(1469598103934665603ull, "addImport");
  hash = patch_hash_text(hash, parent);
  hash = patch_hash_text(hash, module);
  hash = patch_hash_text(hash, alias);
  char text[32];
  snprintf(text, sizeof(text), "#import_%08llx", (unsigned long long)(hash & 0xffffffffull));
  return z_strdup(text);
}

static char *patch_generated_node_id(const char *label, const char *parent, const char *name, const char *type, const char *suffix) {
  uint64_t hash = patch_hash_text(1469598103934665603ull, label);
  hash = patch_hash_text(hash, parent);
  hash = patch_hash_text(hash, name);
  hash = patch_hash_text(hash, type);
  hash = patch_hash_text(hash, suffix);
  char text[40];
  snprintf(text, sizeof(text), "#%s_%08llx", suffix && suffix[0] ? suffix : "node", (unsigned long long)(hash & 0xffffffffull));
  return z_strdup(text);
}

static size_t patch_next_function_order(const ZProgramGraph *graph, const char *parent) {
  size_t next = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && patch_text_eq(edge->from, parent) && patch_text_eq(edge->kind, "function") && edge->order >= next) next = edge->order + 1;
  }
  return next;
}

static size_t patch_next_param_order(const ZProgramGraph *graph, const char *function_id) {
  size_t next = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && patch_text_eq(edge->from, function_id) && patch_text_eq(edge->kind, "param") && edge->order >= next) next = edge->order + 1;
  }
  return next;
}

static void patch_append_node_record(ZProgramGraph *graph, const char *id, ZProgramGraphNodeKind kind, const char *name, const char *type, const char *value, const char *path, bool is_public, bool fallible, bool export_c) {
  ZProgramGraphNode *node = patch_append_node(graph);
  node->id = z_strdup(id);
  node->kind = kind;
  node->name = z_strdup(name ? name : "");
  node->type = z_strdup(type ? type : "");
  node->value = (kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF || kind == Z_PROGRAM_GRAPH_NODE_EFFECT_REF) ? NULL : z_strdup(value ? value : "");
  node->path = z_strdup(path ? path : "");
  node->line = 1;
  node->column = 1;
  node->is_public = is_public;
  node->fallible = fallible;
  node->export_c = export_c;
}

static void patch_append_node_edge(ZProgramGraph *graph, const char *from, const char *kind, const char *to, size_t order) {
  ZProgramGraphEdge *edge = patch_append_edge(graph);
  edge->from = z_strdup(from);
  edge->to = z_strdup(to);
  edge->kind = z_strdup(kind);
  edge->target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE;
  edge->order = order;
}

static size_t patch_node_index(const ZProgramGraph *graph, const char *node_id);
static bool patch_param_name_exists_in_function(ZProgramGraph *graph, const char *function_id, const char *except_id, const char *name);

static bool patch_apply_add_import(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!patch_import_module_value_valid(op->name)) {
    patch_op_fail(result, op, "GPH003", "addImport name must be a Zero module path", "dot-separated module identifiers", op->name);
    return false;
  }
  if (op->value && !patch_identifier_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "addImport alias must be a Zero identifier", "identifier", op->value);
    return false;
  }
  ZProgramGraphNode *parent = op->parent ? patch_find_node(graph, op->parent) : patch_default_module_node(graph);
  if (!parent || parent->kind != Z_PROGRAM_GRAPH_NODE_MODULE) {
    patch_op_fail(result, op, "GPH004", "addImport parent module was not found", "Module node", op->parent ? op->parent : "single module graph");
    return false;
  }
  patch_replace_text(&op->actual, parent->id);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "addImport parent precondition failed", op->expected, op->actual);
    return false;
  }
  const char *alias = op->value ? op->value : "";
  if (patch_import_exists(graph, parent->id, op->name, alias)) {
    patch_op_fail(result, op, "GPH005", "addImport duplicate import", "import not already present", op->name);
    return false;
  }
  char *generated_id = op->node ? NULL : patch_import_node_id(parent->id, op->name, alias);
  const char *node_id = op->node ? op->node : generated_id;
  if (!patch_node_id_valid(node_id) || patch_find_node(graph, node_id)) {
    patch_op_fail(result, op, "GPH005", "addImport node id is invalid or already exists", "unused ProgramGraph node id", node_id);
    free(generated_id);
    return false;
  }
  size_t order = op->has_order ? op->order : patch_next_import_order(graph, parent->id);
  if (patch_duplicate_ordered_edge(graph, parent->id, "import", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, order)) {
    patch_op_fail(result, op, "GPH005", "addImport edge order is already occupied", "unused import edge slot", "import");
    free(generated_id);
    return false;
  }
  ZProgramGraphNode *node = patch_append_node(graph);
  node->id = z_strdup(node_id);
  node->kind = Z_PROGRAM_GRAPH_NODE_IMPORT;
  node->name = z_strdup(op->name);
  node->value = z_strdup(alias);
  node->path = z_strdup(parent->path ? parent->path : "");
  node->line = 1;
  node->column = 1;
  ZProgramGraphEdge *edge = patch_append_edge(graph);
  edge->from = z_strdup(parent->id);
  edge->to = z_strdup(node_id);
  edge->kind = z_strdup("import");
  edge->target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE;
  edge->order = order;
  patch_replace_text(&op->node, node_id);
  patch_replace_text(&op->value, op->name);
  op->ok = true;
  free(generated_id);
  return true;
}

static size_t patch_call_arg_count(const ZProgramGraph *graph, const char *call_id) {
  size_t count = 0;
  for (size_t i = 0; graph && call_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && patch_text_eq(edge->from, call_id) && patch_text_eq(edge->kind, "arg")) count++;
  }
  return count;
}

static void patch_update_calls_for_added_function(ZProgramGraph *graph, const char *name, const char *return_type, char **param_types, size_t param_len) {
  for (size_t i = 0; graph && name && return_type && i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_CALL) continue;
    if (!patch_text_eq(node->name, name) || !patch_text_eq(node->type, "")) continue;
    if (patch_call_arg_count(graph, node->id) != param_len) continue;
    patch_replace_text(&node->type, return_type);
    for (size_t order = 0; order < param_len; order++) {
      ZProgramGraphNode *arg = patch_ordered_node(graph, node->id, "arg", order);
      if (arg && patch_text_eq(arg->type, "")) patch_replace_text(&arg->type, param_types[order]);
    }
  }
}

static void patch_free_param_specs(char **names, char **types, char **param_ids, char **type_ids, size_t len) {
  for (size_t i = 0; i < len; i++) {
    free(names ? names[i] : NULL);
    free(types ? types[i] : NULL);
    free(param_ids ? param_ids[i] : NULL);
    free(type_ids ? type_ids[i] : NULL);
  }
}

static bool patch_parse_add_function_params(ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, char **names, char **types, size_t *len) {
  *len = 0;
  const char *params = op->params ? op->params : "";
  if (!params[0]) return true;
  const char *cursor = params;
  while (*cursor) {
    if (*len >= 16) {
      patch_op_fail(result, op, "GPH003", "addFunction params has too many parameters", "at most 16 params", op->params);
      return false;
    }
    const char *name_start = cursor;
    while (*cursor && *cursor != ':' && *cursor != ',') cursor++;
    if (*cursor != ':' || cursor == name_start) {
      patch_op_fail(result, op, "GPH003", "addFunction params entry is invalid", "name:type", op->params);
      return false;
    }
    char *name = z_strndup(name_start, (size_t)(cursor - name_start));
    cursor++;
    const char *type_start = cursor;
    while (*cursor && *cursor != ',') cursor++;
    if (cursor == type_start) {
      free(name);
      patch_op_fail(result, op, "GPH003", "addFunction params entry is missing a type", "name:type", op->params);
      return false;
    }
    char *type = z_strndup(type_start, (size_t)(cursor - type_start));
    if (!patch_identifier_value_valid(name) || !patch_type_value_valid(type)) {
      free(name);
      free(type);
      patch_op_fail(result, op, "GPH003", "addFunction params entry has invalid name or type", "Zero identifier and type syntax", op->params);
      return false;
    }
    for (size_t i = 0; i < *len; i++) {
      if (patch_text_eq(names[i], name)) {
        free(name);
        free(type);
        patch_op_fail(result, op, "GPH005", "addFunction duplicate parameter", "unique parameter names", op->params);
        return false;
      }
    }
    names[*len] = name;
    types[*len] = type;
    (*len)++;
    if (*cursor == ',') cursor++;
  }
  return true;
}

static bool patch_apply_add_function(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!patch_identifier_value_valid(op->name)) {
    patch_op_fail(result, op, "GPH003", "addFunction name must be a Zero identifier", "identifier", op->name);
    return false;
  }
  if (!patch_type_value_valid(op->type)) {
    patch_op_fail(result, op, "GPH003", "addFunction type must be valid Zero type syntax", "Zero type syntax", op->type);
    return false;
  }
  if (patch_text_eq(op->type, "Void") && op->value && op->value[0]) {
    patch_op_fail(result, op, "GPH003", "addFunction Void function cannot have a return literal", "empty value for Void", op->value);
    return false;
  }
  if (!patch_text_eq(op->type, "Void") && (!op->value || !op->value[0])) {
    patch_op_fail(result, op, "GPH001", "addFunction non-Void function requires a return literal", "value", "");
    return false;
  }
  ZProgramGraphNode *parent = op->parent ? patch_find_node(graph, op->parent) : patch_default_module_node(graph);
  if (!parent || parent->kind != Z_PROGRAM_GRAPH_NODE_MODULE) {
    patch_op_fail(result, op, "GPH004", "addFunction parent module was not found", "Module node", op->parent ? op->parent : "single module graph");
    return false;
  }
  patch_replace_text(&op->actual, parent->id);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "addFunction parent precondition failed", op->expected, op->actual);
    return false;
  }
  if (patch_function_name_exists_in_module(graph, parent->id, NULL, op->name)) {
    patch_op_fail(result, op, "GPH005", "addFunction duplicate function", "function name not already present in module", op->name);
    return false;
  }
  char *param_names[16] = {0};
  char *param_types[16] = {0};
  char *param_ids[16] = {0};
  char *param_type_ids[16] = {0};
  size_t param_len = 0;
  if (!patch_parse_add_function_params(result, op, param_names, param_types, &param_len)) {
    patch_free_param_specs(param_names, param_types, param_ids, param_type_ids, param_len);
    return false;
  }
  char *generated_function_id = op->node ? NULL : patch_generated_node_id("addFunction", parent->id, op->name, op->type, "fn");
  const char *function_id = op->node ? op->node : generated_function_id;
  char *type_id = patch_generated_node_id("addFunction", function_id, op->name, op->type, "ret");
  char *body_id = patch_generated_node_id("addFunction", function_id, op->name, op->type, "body");
  char *return_id = patch_generated_node_id("addFunction", function_id, op->name, op->type, "return");
  char *literal_id = patch_generated_node_id("addFunction", function_id, op->name, op->type, "literal");
  for (size_t i = 0; i < param_len; i++) {
    char suffix[32];
    snprintf(suffix, sizeof(suffix), "param%zu", i);
    param_ids[i] = patch_generated_node_id("addFunction", function_id, param_names[i], param_types[i], suffix);
    snprintf(suffix, sizeof(suffix), "paramType%zu", i);
    param_type_ids[i] = patch_generated_node_id("addFunction", function_id, param_names[i], param_types[i], suffix);
  }
  if (!patch_node_id_valid(function_id) || patch_find_node(graph, function_id) ||
      patch_find_node(graph, type_id) || patch_find_node(graph, body_id) ||
      (!patch_text_eq(op->type, "Void") && (patch_find_node(graph, return_id) || patch_find_node(graph, literal_id)))) {
    patch_op_fail(result, op, "GPH005", "addFunction node id is invalid or already exists", "unused ProgramGraph node ids", function_id);
    free(generated_function_id);
    free(type_id);
    free(body_id);
    free(return_id);
    free(literal_id);
    patch_free_param_specs(param_names, param_types, param_ids, param_type_ids, param_len);
    return false;
  }
  for (size_t i = 0; i < param_len; i++) {
    if (patch_find_node(graph, param_ids[i]) || patch_find_node(graph, param_type_ids[i])) {
      patch_op_fail(result, op, "GPH005", "addFunction generated parameter node id already exists", "unused ProgramGraph node ids", param_ids[i]);
      free(generated_function_id);
      free(type_id);
      free(body_id);
      free(return_id);
      free(literal_id);
      patch_free_param_specs(param_names, param_types, param_ids, param_type_ids, param_len);
      return false;
    }
  }
  size_t order = op->has_order ? op->order : patch_next_function_order(graph, parent->id);
  if (patch_duplicate_ordered_edge(graph, parent->id, "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, order)) {
    patch_op_fail(result, op, "GPH005", "addFunction edge order is already occupied", "unused function edge slot", "function");
    free(generated_function_id);
    free(type_id);
    free(body_id);
    free(return_id);
    free(literal_id);
    patch_free_param_specs(param_names, param_types, param_ids, param_type_ids, param_len);
    return false;
  }
  const char *path = parent->path ? parent->path : "";
  patch_append_node_record(graph, function_id, Z_PROGRAM_GRAPH_NODE_FUNCTION, op->name, op->type, "", path, op->has_public_value && op->public_value, op->has_fallible_value && op->fallible_value, op->has_export_c_value && op->export_c_value);
  patch_append_node_record(graph, type_id, Z_PROGRAM_GRAPH_NODE_TYPE_REF, "", op->type, "", path, false, false, false);
  patch_append_node_record(graph, body_id, Z_PROGRAM_GRAPH_NODE_BLOCK, "body", "", "", path, false, false, false);
  patch_append_node_edge(graph, parent->id, "function", function_id, order);
  for (size_t i = 0; i < param_len; i++) {
    patch_append_node_record(graph, param_ids[i], Z_PROGRAM_GRAPH_NODE_PARAM, param_names[i], param_types[i], "", path, false, false, false);
    patch_append_node_record(graph, param_type_ids[i], Z_PROGRAM_GRAPH_NODE_TYPE_REF, "", param_types[i], "", path, false, false, false);
    patch_append_node_edge(graph, function_id, "param", param_ids[i], i);
    patch_append_node_edge(graph, param_ids[i], "type", param_type_ids[i], 0);
  }
  patch_append_node_edge(graph, function_id, "returnType", type_id, 0);
  patch_append_node_edge(graph, function_id, "body", body_id, 0);
  if (!patch_text_eq(op->type, "Void")) {
    patch_append_node_record(graph, return_id, Z_PROGRAM_GRAPH_NODE_RETURN, "", "", "", path, false, false, false);
    patch_append_node_record(graph, literal_id, Z_PROGRAM_GRAPH_NODE_LITERAL, "", op->type, op->value, path, false, false, false);
    patch_append_node_edge(graph, body_id, "statement", return_id, 0);
    patch_append_node_edge(graph, return_id, "expr", literal_id, 0);
  }
  patch_update_calls_for_added_function(graph, op->name, op->type, param_types, param_len);
  patch_replace_text(&op->node, function_id);
  op->ok = true;
  free(generated_function_id);
  free(type_id);
  free(body_id);
  free(return_id);
  free(literal_id);
  patch_free_param_specs(param_names, param_types, param_ids, param_type_ids, param_len);
  return true;
}

static bool patch_call_arg_order_available(const ZProgramGraph *graph, const char *call_id, size_t order) {
  for (size_t i = 0; graph && call_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && patch_text_eq(edge->from, call_id) && patch_text_eq(edge->kind, "arg") && edge->order == order) return false;
  }
  return true;
}

static bool patch_apply_add_param(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *function = patch_find_node(graph, op->node);
  if (!function) {
    patch_op_fail(result, op, "GPH004", "addParam target function was not found", op->node, "");
    return false;
  }
  if (function->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) {
    patch_op_fail(result, op, "GPH003", "addParam target must be a Function node", "Function node", z_program_graph_node_kind_name(function->kind));
    return false;
  }
  if (!patch_identifier_value_valid(op->name)) {
    patch_op_fail(result, op, "GPH003", "addParam name must be a Zero identifier", "identifier", op->name);
    return false;
  }
  if (!patch_type_value_valid(op->type)) {
    patch_op_fail(result, op, "GPH003", "addParam type must be valid Zero type syntax", "Zero type syntax", op->type);
    return false;
  }
  patch_replace_text(&op->actual, function->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "addParam function precondition failed", op->expected, op->actual);
    return false;
  }
  if (patch_param_name_exists_in_function(graph, function->id, NULL, op->name)) {
    patch_op_fail(result, op, "GPH005", "addParam duplicate parameter", "parameter name not already present", op->name);
    return false;
  }
  size_t order = op->has_order ? op->order : patch_next_param_order(graph, function->id);
  if (patch_duplicate_ordered_edge(graph, function->id, "param", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, order)) {
    patch_op_fail(result, op, "GPH005", "addParam edge order is already occupied", "unused param edge slot", "param");
    return false;
  }
  size_t direct_call_count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    ZProgramGraphNode *candidate = &graph->nodes[i];
    if (candidate->kind != Z_PROGRAM_GRAPH_NODE_CALL || !patch_text_eq(candidate->name, function->name)) continue;
    direct_call_count++;
    if (!op->value || !op->value[0]) {
      patch_op_fail(result, op, "GPH001", "addParam requires value to update direct call sites", "value for new call argument", candidate->id);
      return false;
    }
    if (!patch_call_arg_order_available(graph, candidate->id, order)) {
      patch_op_fail(result, op, "GPH005", "addParam call argument order is already occupied", "unused call arg slot", candidate->id);
      return false;
    }
  }
  char *param_id = patch_generated_node_id("addParam", function->id, op->name, op->type, "param");
  char *type_id = patch_generated_node_id("addParam", param_id, op->name, op->type, "type");
  if (patch_find_node(graph, param_id) || patch_find_node(graph, type_id)) {
    patch_op_fail(result, op, "GPH005", "addParam generated node id already exists", "unused ProgramGraph node ids", param_id);
    free(param_id);
    free(type_id);
    return false;
  }
  const char *path = function->path ? function->path : "";
  patch_append_node_record(graph, param_id, Z_PROGRAM_GRAPH_NODE_PARAM, op->name, op->type, "", path, false, false, false);
  patch_append_node_record(graph, type_id, Z_PROGRAM_GRAPH_NODE_TYPE_REF, "", op->type, "", path, false, false, false);
  patch_append_node_edge(graph, function->id, "param", param_id, order);
  patch_append_node_edge(graph, param_id, "type", type_id, 0);
  if (direct_call_count > 0) {
    for (size_t i = 0; graph && i < graph->node_len; i++) {
      ZProgramGraphNode *candidate = &graph->nodes[i];
      if (candidate->kind != Z_PROGRAM_GRAPH_NODE_CALL || !patch_text_eq(candidate->name, function->name)) continue;
      char *literal_id = patch_generated_node_id("addParamArg", candidate->id, op->name, op->type, "arg");
      if (patch_find_node(graph, literal_id)) {
        patch_op_fail(result, op, "GPH005", "addParam generated argument node id already exists", "unused ProgramGraph node id", literal_id);
        free(param_id);
        free(type_id);
        free(literal_id);
        return false;
      }
      patch_append_node_record(graph, literal_id, Z_PROGRAM_GRAPH_NODE_LITERAL, "", op->type, op->value, candidate->path ? candidate->path : path, false, false, false);
      patch_append_node_edge(graph, candidate->id, "arg", literal_id, order);
      free(literal_id);
    }
  }
  patch_replace_text(&op->to, param_id);
  op->ok = true;
  free(param_id);
  free(type_id);
  return true;
}

static ZProgramGraphNode *patch_find_import(ZProgramGraph *graph, const char *parent, const char *module, const char *alias, bool filter_alias, bool *ambiguous) {
  ZProgramGraphNode *match = NULL;
  size_t count = 0;
  if (ambiguous) *ambiguous = false;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->kind, "import")) continue;
    if (parent && !patch_text_eq(edge->from, parent)) continue;
    ZProgramGraphNode *node = patch_find_node(graph, edge->to);
    if (!node || node->kind != Z_PROGRAM_GRAPH_NODE_IMPORT) continue;
    if (module && !patch_text_eq(node->name, module)) continue;
    if (filter_alias && !patch_text_eq(node->value, alias)) continue;
    match = node;
    count++;
  }
  if (ambiguous && count > 1) *ambiguous = true;
  return count == 1 ? match : NULL;
}

static bool patch_import_has_parent(const ZProgramGraph *graph, const char *parent, const char *node_id) {
  for (size_t i = 0; graph && parent && node_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && patch_text_eq(edge->kind, "import") &&
        patch_text_eq(edge->from, parent) && patch_text_eq(edge->to, node_id)) return true;
  }
  return false;
}

static ZProgramGraphNode *patch_parent_module_for_import(ZProgramGraph *graph, const char *node_id) {
  ZProgramGraphNode *parent = NULL;
  size_t count = 0;
  for (size_t i = 0; graph && node_id && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->kind, "import") || !patch_text_eq(edge->to, node_id)) continue;
    ZProgramGraphNode *candidate = patch_find_node(graph, edge->from);
    if (!candidate || candidate->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    parent = candidate;
    count++;
  }
  return count == 1 ? parent : NULL;
}

static bool patch_import_duplicate_after_replace(const ZProgramGraph *graph, const char *parent, const char *node_id, const char *module, const char *alias) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, parent) || !patch_text_eq(edge->kind, "import")) continue;
    if (patch_text_eq(edge->to, node_id)) continue;
    const ZProgramGraphNode *node = patch_find_node_const(graph, edge->to);
    if (node && node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT && patch_text_eq(node->name, module) && patch_text_eq(node->value, alias)) return true;
  }
  return false;
}

static bool patch_apply_replace_import(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!patch_import_module_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "replaceImport value must be a Zero module path", "dot-separated module identifiers", op->value);
    return false;
  }
  if (op->name && !patch_import_module_value_valid(op->name)) {
    patch_op_fail(result, op, "GPH003", "replaceImport name must be a Zero module path", "dot-separated module identifiers", op->name);
    return false;
  }
  ZProgramGraphNode *parent = op->parent ? patch_find_node(graph, op->parent) : NULL;
  if (op->parent && (!parent || parent->kind != Z_PROGRAM_GRAPH_NODE_MODULE)) {
    patch_op_fail(result, op, "GPH004", "replaceImport parent module was not found", "Module node", op->parent);
    return false;
  }
  ZProgramGraphNode *node = op->node ? patch_find_node(graph, op->node) : NULL;
  if (op->node && !node) {
    patch_op_fail(result, op, "GPH004", "replaceImport target import was not found", op->node, "");
    return false;
  }
  if (!node) {
    if (!parent) parent = patch_default_module_node(graph);
    if (!parent || parent->kind != Z_PROGRAM_GRAPH_NODE_MODULE) {
      patch_op_fail(result, op, "GPH004", "replaceImport parent module was not found", "Module node", "single module graph");
      return false;
    }
    bool ambiguous = false;
    node = patch_find_import(graph, parent->id, op->name, "", false, &ambiguous);
    if (ambiguous) {
      patch_op_fail(result, op, "GPH005", "replaceImport matched multiple imports", "one Import node", op->name);
      return false;
    }
  }
  if (!node) {
    patch_op_fail(result, op, "GPH004", "replaceImport target import was not found", op->name ? op->name : op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_IMPORT) {
    patch_op_fail(result, op, "GPH003", "replaceImport target must be an Import node", "Import node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (op->parent && !patch_import_has_parent(graph, op->parent, node->id)) {
    patch_op_fail(result, op, "GPH005", "replaceImport parent precondition failed", op->parent, node->id);
    return false;
  }
  if (op->name && !patch_text_eq(node->name, op->name)) {
    patch_op_fail(result, op, "GPH005", "replaceImport name precondition failed", op->name, node->name);
    return false;
  }
  patch_replace_text(&op->actual, node->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "replaceImport precondition failed", op->expected, op->actual);
    return false;
  }
  if (!parent) parent = patch_parent_module_for_import(graph, node->id);
  if (!parent) {
    patch_op_fail(result, op, "GPH004", "replaceImport parent module was not found", "single owning Module node", node->id);
    return false;
  }
  if (patch_import_duplicate_after_replace(graph, parent->id, node->id, op->value, node->value ? node->value : "")) {
    patch_op_fail(result, op, "GPH005", "replaceImport duplicate import", "import not already present", op->value);
    return false;
  }
  patch_replace_text(&op->node, node->id);
  patch_replace_text(&node->name, op->value);
  op->ok = true;
  return true;
}

static bool patch_apply_rename_import_alias(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!patch_identifier_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "renameImportAlias value must be a Zero identifier or empty alias", "identifier or empty string", op->value);
    return false;
  }
  if (op->name && !patch_import_module_value_valid(op->name)) {
    patch_op_fail(result, op, "GPH003", "renameImportAlias name must be a Zero module path", "dot-separated module identifiers", op->name);
    return false;
  }
  ZProgramGraphNode *parent = op->parent ? patch_find_node(graph, op->parent) : NULL;
  if (op->parent && (!parent || parent->kind != Z_PROGRAM_GRAPH_NODE_MODULE)) {
    patch_op_fail(result, op, "GPH004", "renameImportAlias parent module was not found", "Module node", op->parent);
    return false;
  }
  ZProgramGraphNode *node = op->node ? patch_find_node(graph, op->node) : NULL;
  if (op->node && !node) {
    patch_op_fail(result, op, "GPH004", "renameImportAlias target import was not found", op->node, "");
    return false;
  }
  if (!node) {
    if (!parent) parent = patch_default_module_node(graph);
    if (!parent || parent->kind != Z_PROGRAM_GRAPH_NODE_MODULE) {
      patch_op_fail(result, op, "GPH004", "renameImportAlias parent module was not found", "Module node", "single module graph");
      return false;
    }
    bool ambiguous = false;
    node = patch_find_import(graph, parent->id, op->name, "", false, &ambiguous);
    if (ambiguous) {
      patch_op_fail(result, op, "GPH005", "renameImportAlias matched multiple imports", "one Import node", op->name);
      return false;
    }
  }
  if (!node) {
    patch_op_fail(result, op, "GPH004", "renameImportAlias target import was not found", op->name ? op->name : op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_IMPORT) {
    patch_op_fail(result, op, "GPH003", "renameImportAlias target must be an Import node", "Import node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (op->parent && !patch_import_has_parent(graph, op->parent, node->id)) {
    patch_op_fail(result, op, "GPH005", "renameImportAlias parent precondition failed", op->parent, node->id);
    return false;
  }
  if (op->name && !patch_text_eq(node->name, op->name)) {
    patch_op_fail(result, op, "GPH005", "renameImportAlias name precondition failed", op->name, node->name);
    return false;
  }
  patch_replace_text(&op->actual, node->value);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "renameImportAlias precondition failed", op->expected, op->actual);
    return false;
  }
  if (!parent) parent = patch_parent_module_for_import(graph, node->id);
  if (!parent) {
    patch_op_fail(result, op, "GPH004", "renameImportAlias parent module was not found", "single owning Module node", node->id);
    return false;
  }
  if (patch_import_duplicate_after_replace(graph, parent->id, node->id, node->name, op->value)) {
    patch_op_fail(result, op, "GPH005", "renameImportAlias duplicate import", "import alias not already present", op->value);
    return false;
  }
  patch_replace_text(&op->node, node->id);
  patch_replace_text(&node->value, op->value);
  op->ok = true;
  return true;
}

static bool patch_return_type_walk_edge(const char *kind) {
  return patch_text_eq(kind, "body") ||
         patch_text_eq(kind, "statement") ||
         patch_text_eq(kind, "then") ||
         patch_text_eq(kind, "else") ||
         patch_text_eq(kind, "arm");
}

static bool patch_body_expr_walk_edge(const char *kind) {
  return patch_return_type_walk_edge(kind) ||
         patch_text_eq(kind, "expr") ||
         patch_text_eq(kind, "left") ||
         patch_text_eq(kind, "right") ||
         patch_text_eq(kind, "arg") ||
         patch_text_eq(kind, "rangeEnd") ||
         patch_text_eq(kind, "guard") ||
         patch_text_eq(kind, "target") ||
         patch_text_eq(kind, "value") ||
         patch_text_eq(kind, "default");
}

static void patch_update_return_expr_types(ZProgramGraph *graph, const char *node_id, const char *old_type, const char *new_type, bool *visited) {
  size_t index = patch_node_index(graph, node_id);
  if (index == (size_t)-1 || !visited || visited[index]) return;
  visited[index] = true;
  ZProgramGraphNode *node = &graph->nodes[index];
  if (node->kind == Z_PROGRAM_GRAPH_NODE_RETURN) {
    ZProgramGraphNode *expr = patch_ordered_node(graph, node->id, "expr", 0);
    if (expr && patch_text_eq(expr->type, old_type)) patch_replace_text(&expr->type, new_type);
  }
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, node_id) || !patch_return_type_walk_edge(edge->kind)) continue;
    patch_update_return_expr_types(graph, edge->to, old_type, new_type, visited);
  }
}

static ZProgramGraphNode *patch_param_owner_function(ZProgramGraph *graph, const char *param_id) {
  for (size_t i = 0; graph && param_id && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->to, param_id) || !patch_text_eq(edge->kind, "param")) continue;
    ZProgramGraphNode *owner = patch_find_node(graph, edge->from);
    if (owner && owner->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION) return owner;
  }
  return NULL;
}

static void patch_update_param_identifier_types(ZProgramGraph *graph, const char *node_id, const char *name, const char *old_type, const char *new_type, bool *visited) {
  size_t index = patch_node_index(graph, node_id);
  if (index == (size_t)-1 || !visited || visited[index]) return;
  visited[index] = true;
  ZProgramGraphNode *node = &graph->nodes[index];
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER && patch_text_eq(node->name, name) && patch_text_eq(node->type, old_type)) {
    patch_replace_text(&node->type, new_type);
  }
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, node_id) || !patch_body_expr_walk_edge(edge->kind)) continue;
    patch_update_param_identifier_types(graph, edge->to, name, old_type, new_type, visited);
  }
}

static void patch_rename_param_identifiers(ZProgramGraph *graph, const char *node_id, const char *old_name, const char *new_name, bool *visited) {
  size_t index = patch_node_index(graph, node_id);
  if (index == (size_t)-1 || !visited || visited[index]) return;
  visited[index] = true;
  ZProgramGraphNode *node = &graph->nodes[index];
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER && patch_text_eq(node->name, old_name)) {
    patch_replace_text(&node->name, new_name);
  }
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, node_id) || !patch_body_expr_walk_edge(edge->kind)) continue;
    patch_rename_param_identifiers(graph, edge->to, old_name, new_name, visited);
  }
}

static bool patch_param_name_exists_in_function(ZProgramGraph *graph, const char *function_id, const char *except_id, const char *name) {
  for (size_t i = 0; graph && function_id && name && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, function_id) || !patch_text_eq(edge->kind, "param")) continue;
    ZProgramGraphNode *param = patch_find_node(graph, edge->to);
    if (param && param->kind == Z_PROGRAM_GRAPH_NODE_PARAM && !patch_text_eq(param->id, except_id) && patch_text_eq(param->name, name)) return true;
  }
  return false;
}

static ZProgramGraphNode *patch_field_owner_shape(ZProgramGraph *graph, const char *field_id) {
  ZProgramGraphNode *owner = NULL;
  size_t count = 0;
  for (size_t i = 0; graph && field_id && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->to, field_id) || !patch_text_eq(edge->kind, "field")) continue;
    ZProgramGraphNode *node = patch_find_node(graph, edge->from);
    if (!node || node->kind != Z_PROGRAM_GRAPH_NODE_SHAPE) continue;
    owner = node;
    count++;
  }
  return count == 1 ? owner : NULL;
}

static bool patch_field_name_exists_in_shape(ZProgramGraph *graph, const char *shape_id, const char *except_id, const char *name) {
  for (size_t i = 0; graph && shape_id && name && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, shape_id) || !patch_text_eq(edge->kind, "field")) continue;
    ZProgramGraphNode *field = patch_find_node(graph, edge->to);
    if (field && field->kind == Z_PROGRAM_GRAPH_NODE_FIELD && !patch_text_eq(field->id, except_id) && patch_text_eq(field->name, name)) return true;
  }
  return false;
}

static bool patch_type_names_shape(const char *type, const char *shape_name) {
  if (!type || !shape_name || !shape_name[0]) return false;
  if (patch_text_eq(type, shape_name)) return true;
  size_t shape_len = strlen(shape_name);
  return strncmp(type, shape_name, shape_len) == 0 && type[shape_len] == '<';
}

static bool patch_field_access_belongs_to_shape(ZProgramGraph *graph, ZProgramGraphNode *access, const char *shape_name) {
  ZProgramGraphNode *left = patch_ordered_node(graph, access ? access->id : NULL, "left", 0);
  return left && patch_type_names_shape(left->type, shape_name);
}

static void patch_update_expr_types(ZProgramGraph *graph, const char *node_id, const char *old_type, const char *new_type, bool *visited) {
  size_t index = patch_node_index(graph, node_id);
  if (index == (size_t)-1 || !visited || visited[index]) return;
  visited[index] = true;
  ZProgramGraphNode *node = &graph->nodes[index];
  if (patch_text_eq(node->type, old_type)) patch_replace_text(&node->type, new_type);
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, node_id) || !patch_body_expr_walk_edge(edge->kind)) continue;
    patch_update_expr_types(graph, edge->to, old_type, new_type, visited);
  }
}

static ZProgramGraphNode *patch_field_init_owner_shape_literal(ZProgramGraph *graph, const char *field_init_id) {
  for (size_t i = 0; graph && field_init_id && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->to, field_init_id) || !patch_text_eq(edge->kind, "field")) continue;
    ZProgramGraphNode *node = patch_find_node(graph, edge->from);
    if (node && node->kind == Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL) return node;
  }
  return NULL;
}

static bool patch_apply_rename_field(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "renameField target field was not found", op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_FIELD) {
    patch_op_fail(result, op, "GPH003", "renameField target must be a Field node", "Field node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (!patch_identifier_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "renameField value must be a Zero identifier", "identifier", op->value);
    return false;
  }
  ZProgramGraphNode *shape = patch_field_owner_shape(graph, node->id);
  if (!shape) {
    patch_op_fail(result, op, "GPH004", "renameField owner shape was not found", "single owning Shape node", node->id);
    return false;
  }
  patch_replace_text(&op->actual, node->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "renameField precondition failed", op->expected, op->actual);
    return false;
  }
  if (patch_field_name_exists_in_shape(graph, shape->id, node->id, op->value)) {
    patch_op_fail(result, op, "GPH005", "renameField duplicate field", "field name not already present in shape", op->value);
    return false;
  }
  const char *shape_name = shape->name ? shape->name : "";
  patch_replace_text(&node->name, op->value);
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    ZProgramGraphNode *candidate = &graph->nodes[i];
    if (candidate->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS &&
        patch_text_eq(candidate->name, op->actual) &&
        patch_field_access_belongs_to_shape(graph, candidate, shape_name)) {
      patch_replace_text(&candidate->name, op->value);
    } else if (candidate->kind == Z_PROGRAM_GRAPH_NODE_FIELD_INIT && patch_text_eq(candidate->name, op->actual)) {
      ZProgramGraphNode *literal = patch_field_init_owner_shape_literal(graph, candidate->id);
      if (literal && patch_type_names_shape(literal->name, shape_name)) patch_replace_text(&candidate->name, op->value);
    }
  }
  op->ok = true;
  return true;
}

static bool patch_apply_change_field_type(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "changeFieldType target field was not found", op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_FIELD) {
    patch_op_fail(result, op, "GPH003", "changeFieldType target must be a Field node", "Field node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (!patch_type_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "changeFieldType value must be valid Zero type syntax", "Zero type syntax", op->value);
    return false;
  }
  ZProgramGraphNode *shape = patch_field_owner_shape(graph, node->id);
  if (!shape) {
    patch_op_fail(result, op, "GPH004", "changeFieldType owner shape was not found", "single owning Shape node", node->id);
    return false;
  }
  ZProgramGraphNode *field_type = patch_ordered_node(graph, node->id, "type", 0);
  if (!field_type || field_type->kind != Z_PROGRAM_GRAPH_NODE_TYPE_REF) {
    patch_op_fail(result, op, "GPH004", "changeFieldType type node was not found", "type TypeRef node", node->id);
    return false;
  }
  patch_replace_text(&op->actual, node->type);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "changeFieldType precondition failed", op->expected, op->actual);
    return false;
  }
  if (!patch_text_eq(field_type->type, node->type)) {
    patch_op_fail(result, op, "GPH005", "changeFieldType graph facts disagree", node->type, field_type->type);
    return false;
  }
  const char *shape_name = shape->name ? shape->name : "";
  const char *field_name = node->name ? node->name : "";
  patch_replace_text(&node->type, op->value);
  patch_replace_text(&field_type->type, op->value);
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    ZProgramGraphNode *candidate = &graph->nodes[i];
    if (candidate->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS &&
        patch_text_eq(candidate->name, field_name) &&
        patch_text_eq(candidate->type, op->actual) &&
        patch_field_access_belongs_to_shape(graph, candidate, shape_name)) {
      patch_replace_text(&candidate->type, op->value);
    } else if (candidate->kind == Z_PROGRAM_GRAPH_NODE_FIELD_INIT && patch_text_eq(candidate->name, field_name)) {
      ZProgramGraphNode *literal = patch_field_init_owner_shape_literal(graph, candidate->id);
      ZProgramGraphNode *value = patch_ordered_node(graph, candidate->id, "value", 0);
      if (literal && value && patch_type_names_shape(literal->name, shape_name)) {
        bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
        patch_update_expr_types(graph, value->id, op->actual, op->value, visited);
        free(visited);
      }
    }
  }
  ZProgramGraphNode *default_value = patch_ordered_node(graph, node->id, "default", 0);
  if (default_value) {
    bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
    patch_update_expr_types(graph, default_value->id, op->actual, op->value, visited);
    free(visited);
  }
  op->ok = true;
  return true;
}

static bool patch_subtree_contains_node(ZProgramGraph *graph, const char *root_id, const char *target_id, bool *visited) {
  size_t index = patch_node_index(graph, root_id);
  if (index == (size_t)-1 || !visited || visited[index]) return false;
  visited[index] = true;
  if (patch_text_eq(root_id, target_id)) return true;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, root_id) || !patch_body_expr_walk_edge(edge->kind)) continue;
    if (patch_subtree_contains_node(graph, edge->to, target_id, visited)) return true;
  }
  return false;
}

static ZProgramGraphNode *patch_local_owner_function(ZProgramGraph *graph, const char *let_id) {
  for (size_t i = 0; graph && let_id && i < graph->node_len; i++) {
    ZProgramGraphNode *candidate = &graph->nodes[i];
    if (candidate->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) continue;
    ZProgramGraphNode *body = patch_ordered_node(graph, candidate->id, "body", 0);
    if (!body) continue;
    bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
    bool contains = patch_subtree_contains_node(graph, body->id, let_id, visited);
    free(visited);
    if (contains) return candidate;
  }
  return NULL;
}

static bool patch_local_name_conflicts_in_function(ZProgramGraph *graph, ZProgramGraphNode *function, const char *except_id, const char *name) {
  if (patch_param_name_exists_in_function(graph, function ? function->id : NULL, NULL, name)) return true;
  for (size_t i = 0; graph && function && name && i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    if ((node->kind != Z_PROGRAM_GRAPH_NODE_LET && node->kind != Z_PROGRAM_GRAPH_NODE_FOR) ||
        patch_text_eq(node->id, except_id) ||
        !patch_text_eq(node->name, name)) {
      continue;
    }
    bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
    ZProgramGraphNode *body = patch_ordered_node(graph, function->id, "body", 0);
    bool in_function = body && patch_subtree_contains_node(graph, body->id, node->id, visited);
    free(visited);
    if (in_function) return true;
  }
  return false;
}

static void patch_rename_local_identifiers(ZProgramGraph *graph, const char *node_id, const ZProgramGraphNode *binding, const char *old_name, const char *new_name, bool *visited) {
  size_t index = patch_node_index(graph, node_id);
  if (index == (size_t)-1 || !visited || visited[index]) return;
  visited[index] = true;
  ZProgramGraphNode *node = &graph->nodes[index];
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER &&
      patch_text_eq(node->name, old_name) &&
      (binding->line <= 0 || node->line <= 0 || node->line > binding->line)) {
    patch_replace_text(&node->name, new_name);
  }
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, node_id) || !patch_body_expr_walk_edge(edge->kind)) continue;
    patch_rename_local_identifiers(graph, edge->to, binding, old_name, new_name, visited);
  }
}

static void patch_update_local_identifier_types(ZProgramGraph *graph, const char *node_id, const ZProgramGraphNode *binding, const char *old_type, const char *new_type, bool *visited) {
  size_t index = patch_node_index(graph, node_id);
  if (index == (size_t)-1 || !visited || visited[index]) return;
  visited[index] = true;
  ZProgramGraphNode *node = &graph->nodes[index];
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER &&
      patch_text_eq(node->name, binding->name) &&
      patch_text_eq(node->type, old_type) &&
      (binding->line <= 0 || node->line <= 0 || node->line > binding->line)) {
    patch_replace_text(&node->type, new_type);
  }
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, node_id) || !patch_body_expr_walk_edge(edge->kind)) continue;
    patch_update_local_identifier_types(graph, edge->to, binding, old_type, new_type, visited);
  }
}

static bool patch_apply_rename_param(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "renameParam target parameter was not found", op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_PARAM) {
    patch_op_fail(result, op, "GPH003", "renameParam target must be a Param node", "Param node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (!patch_identifier_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "renameParam value must be a Zero identifier", "identifier", op->value);
    return false;
  }
  ZProgramGraphNode *owner = patch_param_owner_function(graph, node->id);
  if (!owner) {
    patch_op_fail(result, op, "GPH004", "renameParam owner function was not found", "owning Function node", node->id);
    return false;
  }
  patch_replace_text(&op->actual, node->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "renameParam precondition failed", op->expected, op->actual);
    return false;
  }
  if (patch_param_name_exists_in_function(graph, owner->id, node->id, op->value)) {
    patch_op_fail(result, op, "GPH005", "renameParam duplicate parameter", "parameter name not already present", op->value);
    return false;
  }
  patch_replace_text(&node->name, op->value);
  ZProgramGraphNode *body = patch_ordered_node(graph, owner->id, "body", 0);
  bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
  if (body) patch_rename_param_identifiers(graph, body->id, op->actual, op->value, visited);
  free(visited);
  op->ok = true;
  return true;
}

static bool patch_apply_rename_local(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "renameLocal target binding was not found", op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_LET) {
    patch_op_fail(result, op, "GPH003", "renameLocal target must be a Let node", "Let node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (!patch_identifier_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "renameLocal value must be a Zero identifier", "identifier", op->value);
    return false;
  }
  ZProgramGraphNode *owner = patch_local_owner_function(graph, node->id);
  if (!owner) {
    patch_op_fail(result, op, "GPH004", "renameLocal owner function was not found", "owning Function node", node->id);
    return false;
  }
  patch_replace_text(&op->actual, node->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "renameLocal precondition failed", op->expected, op->actual);
    return false;
  }
  if (patch_local_name_conflicts_in_function(graph, owner, node->id, op->value)) {
    patch_op_fail(result, op, "GPH005", "renameLocal duplicate binding", "name not already used by a parameter or local in function", op->value);
    return false;
  }
  if (patch_local_name_conflicts_in_function(graph, owner, node->id, op->actual)) {
    patch_op_fail(result, op, "GPH005", "renameLocal cannot prove shadowing boundary", "single binding with this name in function", op->actual);
    return false;
  }
  ZProgramGraphNode binding_snapshot = *node;
  patch_replace_text(&node->name, op->value);
  ZProgramGraphNode *body = patch_ordered_node(graph, owner->id, "body", 0);
  bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
  if (body) patch_rename_local_identifiers(graph, body->id, &binding_snapshot, op->actual, op->value, visited);
  free(visited);
  op->ok = true;
  return true;
}

static bool patch_apply_change_local_type(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "changeLocalType target binding was not found", op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_LET) {
    patch_op_fail(result, op, "GPH003", "changeLocalType target must be a Let node", "Let node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (!patch_type_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "changeLocalType value must be valid Zero type syntax", "Zero type syntax", op->value);
    return false;
  }
  ZProgramGraphNode *declared_type = patch_ordered_node(graph, node->id, "declaredType", 0);
  if (!declared_type || declared_type->kind != Z_PROGRAM_GRAPH_NODE_TYPE_REF) {
    patch_op_fail(result, op, "GPH004", "changeLocalType declared type node was not found", "declaredType TypeRef node", node->id);
    return false;
  }
  patch_replace_text(&op->actual, node->type);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "changeLocalType precondition failed", op->expected, op->actual);
    return false;
  }
  if (!patch_text_eq(declared_type->type, node->type)) {
    patch_op_fail(result, op, "GPH005", "changeLocalType graph facts disagree", node->type, declared_type->type);
    return false;
  }
  ZProgramGraphNode *owner = patch_local_owner_function(graph, node->id);
  if (!owner) {
    patch_op_fail(result, op, "GPH004", "changeLocalType owner function was not found", "owning Function node", node->id);
    return false;
  }
  ZProgramGraphNode binding_snapshot = *node;
  patch_replace_text(&node->type, op->value);
  patch_replace_text(&declared_type->type, op->value);
  ZProgramGraphNode *expr = patch_ordered_node(graph, node->id, "expr", 0);
  if (expr && patch_text_eq(expr->type, op->actual)) {
    bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
    patch_update_expr_types(graph, expr->id, op->actual, op->value, visited);
    free(visited);
  }
  ZProgramGraphNode *body = patch_ordered_node(graph, owner->id, "body", 0);
  if (body) {
    bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
    patch_update_local_identifier_types(graph, body->id, &binding_snapshot, op->actual, op->value, visited);
    free(visited);
  }
  op->ok = true;
  return true;
}

static bool patch_apply_change_return_type(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "changeReturnType target function was not found", op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) {
    patch_op_fail(result, op, "GPH003", "changeReturnType target must be a Function node", "Function node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (!patch_type_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "changeReturnType value must be valid Zero type syntax", "Zero type syntax", op->value);
    return false;
  }
  ZProgramGraphNode *return_type = patch_ordered_node(graph, node->id, "returnType", 0);
  if (!return_type || return_type->kind != Z_PROGRAM_GRAPH_NODE_TYPE_REF) {
    patch_op_fail(result, op, "GPH004", "changeReturnType return type node was not found", "returnType TypeRef node", node->id);
    return false;
  }
  patch_replace_text(&op->actual, node->type);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "changeReturnType precondition failed", op->expected, op->actual);
    return false;
  }
  if (!patch_text_eq(return_type->type, node->type)) {
    patch_op_fail(result, op, "GPH005", "changeReturnType graph facts disagree", node->type, return_type->type);
    return false;
  }
  patch_replace_text(&node->type, op->value);
  patch_replace_text(&return_type->type, op->value);
  ZProgramGraphNode *body = patch_ordered_node(graph, node->id, "body", 0);
  bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
  if (body) patch_update_return_expr_types(graph, body->id, op->actual, op->value, visited);
  free(visited);
  op->ok = true;
  return true;
}

static bool patch_apply_change_param_type(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "changeParamType target parameter was not found", op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_PARAM) {
    patch_op_fail(result, op, "GPH003", "changeParamType target must be a Param node", "Param node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (!patch_type_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "changeParamType value must be valid Zero type syntax", "Zero type syntax", op->value);
    return false;
  }
  ZProgramGraphNode *param_type = patch_ordered_node(graph, node->id, "type", 0);
  if (!param_type || param_type->kind != Z_PROGRAM_GRAPH_NODE_TYPE_REF) {
    patch_op_fail(result, op, "GPH004", "changeParamType type node was not found", "type TypeRef node", node->id);
    return false;
  }
  patch_replace_text(&op->actual, node->type);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "changeParamType precondition failed", op->expected, op->actual);
    return false;
  }
  if (!patch_text_eq(param_type->type, node->type)) {
    patch_op_fail(result, op, "GPH005", "changeParamType graph facts disagree", node->type, param_type->type);
    return false;
  }
  ZProgramGraphNode *owner = patch_param_owner_function(graph, node->id);
  if (!owner) {
    patch_op_fail(result, op, "GPH004", "changeParamType owner function was not found", "owning Function node", node->id);
    return false;
  }
  patch_replace_text(&node->type, op->value);
  patch_replace_text(&param_type->type, op->value);
  ZProgramGraphNode *body = patch_ordered_node(graph, owner->id, "body", 0);
  bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
  if (body) patch_update_param_identifier_types(graph, body->id, node->name, op->actual, op->value, visited);
  free(visited);
  op->ok = true;
  return true;
}

static void patch_free_edge(ZProgramGraphEdge *edge) {
  free(edge->from);
  free(edge->to);
  free(edge->kind);
}

static void patch_free_node(ZProgramGraphNode *node) {
  free(node->id);
  free(node->name);
  free(node->type);
  free(node->value);
  free(node->path);
  free(node->symbol_id);
  free(node->type_id);
  free(node->effect_id);
  free(node->node_hash);
}

static bool patch_apply_remove_import(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (op->name && !patch_import_module_value_valid(op->name)) {
    patch_op_fail(result, op, "GPH003", "removeImport name must be a Zero module path", "dot-separated module identifiers", op->name);
    return false;
  }
  if (op->value && !patch_identifier_value_valid(op->value)) {
    patch_op_fail(result, op, "GPH003", "removeImport alias must be a Zero identifier", "identifier", op->value);
    return false;
  }
  ZProgramGraphNode *parent = op->parent ? patch_find_node(graph, op->parent) : NULL;
  if (op->parent && (!parent || parent->kind != Z_PROGRAM_GRAPH_NODE_MODULE)) {
    patch_op_fail(result, op, "GPH004", "removeImport parent module was not found", "Module node", op->parent);
    return false;
  }
  ZProgramGraphNode *node = op->node ? patch_find_node(graph, op->node) : NULL;
  if (op->node && !node) {
    patch_op_fail(result, op, "GPH004", "removeImport target import was not found", op->node, "");
    return false;
  }
  if (!node) {
    if (!op->parent) parent = patch_default_module_node(graph);
    if (!parent || parent->kind != Z_PROGRAM_GRAPH_NODE_MODULE) {
      patch_op_fail(result, op, "GPH004", "removeImport parent module was not found", "Module node", "single module graph");
      return false;
    }
    bool ambiguous = false;
    node = patch_find_import(graph, parent->id, op->name, op->value ? op->value : "", op->value != NULL, &ambiguous);
    if (ambiguous) {
      patch_op_fail(result, op, "GPH005", "removeImport matched multiple imports", "one Import node", op->name);
      return false;
    }
  }
  if (!node) {
    patch_op_fail(result, op, "GPH004", "removeImport target import was not found", op->name ? op->name : op->node, "");
    return false;
  }
  if (node->kind != Z_PROGRAM_GRAPH_NODE_IMPORT) {
    patch_op_fail(result, op, "GPH003", "removeImport target must be an Import node", "Import node", z_program_graph_node_kind_name(node->kind));
    return false;
  }
  if (op->parent && !patch_import_has_parent(graph, op->parent, node->id)) {
    patch_op_fail(result, op, "GPH005", "removeImport parent precondition failed", op->parent, node->id);
    return false;
  }
  if (op->name && !patch_text_eq(node->name, op->name)) {
    patch_op_fail(result, op, "GPH005", "removeImport name precondition failed", op->name, node->name);
    return false;
  }
  if (op->value && !patch_text_eq(node->value, op->value)) {
    patch_op_fail(result, op, "GPH005", "removeImport alias precondition failed", op->value, node->value);
    return false;
  }
  patch_replace_text(&op->actual, node->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "removeImport precondition failed", op->expected, op->actual);
    return false;
  }
  patch_replace_text(&op->node, node->id);
  size_t root = patch_node_index(graph, node->id);
  if (root == (size_t)-1) {
    patch_op_fail(result, op, "GPH004", "removeImport target import was not found", op->node, "");
    return false;
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->to, op->node)) continue;
    if (!patch_text_eq(edge->kind, "import")) {
      patch_op_fail(result, op, "GPH005", "removeImport would remove a node referenced outside its import edge", "Import edge only", edge->kind);
      return false;
    }
  }
  size_t write_edge = 0;
  for (size_t i = 0; i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    bool remove = patch_text_eq(edge->from, op->node) || (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && patch_text_eq(edge->to, op->node));
    if (remove) {
      patch_free_edge(edge);
      continue;
    }
    if (write_edge != i) graph->edges[write_edge] = *edge;
    write_edge++;
  }
  for (size_t i = write_edge; i < graph->edge_len; i++) graph->edges[i] = (ZProgramGraphEdge){0};
  graph->edge_len = write_edge;
  patch_free_node(&graph->nodes[root]);
  for (size_t i = root + 1; i < graph->node_len; i++) graph->nodes[i - 1] = graph->nodes[i];
  graph->node_len--;
  graph->nodes[graph->node_len] = (ZProgramGraphNode){0};
  op->ok = true;
  return true;
}
static size_t patch_node_index(const ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (patch_text_eq(graph->nodes[i].id, node_id)) return i;
  }
  return (size_t)-1;
}

static bool patch_node_edge_kind_owns_child(const char *kind) {
  static const char *owned_kinds[] = {
    "alias",
    "arg",
    "arm",
    "body",
    "cImport",
    "case",
    "choice",
    "const",
    "declaredType",
    "default",
    "effect",
    "else",
    "enum",
    "error",
    "expr",
    "field",
    "function",
    "guard",
    "import",
    "interface",
    "left",
    "method",
    "param",
    "rangeEnd",
    "returnType",
    "right",
    "shape",
    "statement",
    "target",
    "then",
    "type",
    "typeArg",
    "typeParam",
    "value",
    NULL,
  };
  for (size_t i = 0; owned_kinds[i]; i++) {
    if (patch_text_eq(kind, owned_kinds[i])) return true;
  }
  return false;
}

static bool patch_edge_owns_child_node(const ZProgramGraphEdge *edge) {
  return edge &&
         edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
         patch_node_edge_kind_owns_child(edge->kind);
}

static bool patch_mark_delete_subtree(const ZProgramGraph *graph, size_t index, bool *marked) {
  if (!graph || index >= graph->node_len || marked[index]) return true;
  marked[index] = true;
  const char *node_id = graph->nodes[index].id;
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!patch_edge_owns_child_node(edge) || !patch_text_eq(edge->from, node_id)) continue;
    size_t child = patch_node_index(graph, edge->to);
    if (child == (size_t)-1) return false;
    if (!patch_mark_delete_subtree(graph, child, marked)) return false;
  }
  return true;
}

static bool patch_edge_targets_marked_node(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, const bool *marked) {
  if (!graph || !edge || !marked) return false;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!marked[i]) continue;
    const ZProgramGraphNode *node = &graph->nodes[i];
    switch (edge->target) {
      case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
        if (patch_text_eq(edge->to, node->id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
        if (node->symbol_id && patch_text_eq(edge->to, node->symbol_id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
        if (node->type_id && patch_text_eq(edge->to, node->type_id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
        if (node->effect_id && patch_text_eq(edge->to, node->effect_id)) return true;
        break;
    }
  }
  return false;
}

static bool patch_domain_id_survives(const ZProgramGraph *graph, const bool *marked, ZProgramGraphEdgeTarget target, const char *id) {
  for (size_t i = 0; graph && marked && id && i < graph->node_len; i++) {
    if (marked[i]) continue;
    const ZProgramGraphNode *node = &graph->nodes[i];
    switch (target) {
      case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
        if (patch_text_eq(node->id, id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
        if (node->symbol_id && patch_text_eq(node->symbol_id, id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
        if (node->type_id && patch_text_eq(node->type_id, id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
        if (node->effect_id && patch_text_eq(node->effect_id, id)) return true;
        break;
    }
  }
  return false;
}

static bool patch_edge_target_removed_by_delete(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, const bool *marked) {
  return patch_edge_targets_marked_node(graph, edge, marked) && !patch_domain_id_survives(graph, marked, edge->target, edge->to);
}

static const ZProgramGraphEdge *patch_delete_root_parent_edge(const ZProgramGraph *graph, const bool *marked, const char *root_id) {
  const ZProgramGraphEdge *parent_edge = NULL;
  size_t parent_count = 0;
  for (size_t i = 0; graph && marked && root_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!patch_edge_owns_child_node(edge) || !patch_text_eq(edge->to, root_id)) continue;
    size_t source = patch_node_index(graph, edge->from);
    if (source != (size_t)-1 && marked[source]) continue;
    parent_edge = edge;
    parent_count++;
  }
  return parent_count == 1 ? parent_edge : NULL;
}

static bool patch_delete_external_reference_allowed(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, const bool *marked, const ZProgramGraphEdge *root_parent_edge) {
  size_t source = patch_node_index(graph, edge->from);
  if (source != (size_t)-1 && marked[source]) return true;
  return edge == root_parent_edge;
}

static bool patch_identifier_name_used(ZProgramGraph *graph, const char *node_id, const char *name, bool *visited) {
  size_t index = patch_node_index(graph, node_id);
  if (index == (size_t)-1 || !visited || visited[index]) return false;
  visited[index] = true;
  ZProgramGraphNode *node = &graph->nodes[index];
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER && patch_text_eq(node->name, name)) return true;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, node_id) || !patch_body_expr_walk_edge(edge->kind)) continue;
    if (patch_identifier_name_used(graph, edge->to, name, visited)) return true;
  }
  return false;
}

static bool patch_param_edge_order(const ZProgramGraph *graph, const char *function_id, const char *param_id, size_t *out_order) {
  for (size_t i = 0; graph && function_id && param_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        patch_text_eq(edge->from, function_id) &&
        patch_text_eq(edge->to, param_id) &&
        patch_text_eq(edge->kind, "param")) {
      if (out_order) *out_order = edge->order;
      return true;
    }
  }
  return false;
}

static bool patch_direct_call_arg_node(const ZProgramGraph *graph, const char *call_id, size_t order, const char **out_arg_id) {
  for (size_t i = 0; graph && call_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        patch_text_eq(edge->from, call_id) &&
        patch_text_eq(edge->kind, "arg") &&
        edge->order == order) {
      if (out_arg_id) *out_arg_id = edge->to;
      return true;
    }
  }
  return false;
}
static bool patch_apply_remove_function(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) { ZProgramGraphNode *function = patch_find_node(graph, op->node); if (!function) { patch_op_fail(result, op, "GPH004", "removeFunction target function was not found", op->node, ""); return false; } if (function->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) { patch_op_fail(result, op, "GPH003", "removeFunction target must be a Function node", "Function node", z_program_graph_node_kind_name(function->kind)); return false; } patch_replace_text(&op->actual, function->name); if (op->has_expected && !patch_text_eq(op->expected, op->actual)) { patch_op_fail(result, op, "GPH005", "removeFunction precondition failed", op->expected, op->actual); return false; } bool *marked = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool)); size_t root = patch_node_index(graph, function->id); if (root == (size_t)-1 || !patch_mark_delete_subtree(graph, root, marked)) { free(marked); patch_op_fail(result, op, "GPH006", "removeFunction subtree references a missing node", "valid function subtree", op->node); return false; } const ZProgramGraphEdge *parent_edge = patch_delete_root_parent_edge(graph, marked, function->id); if (!parent_edge || !patch_text_eq(parent_edge->kind, "function")) { free(marked); patch_op_fail(result, op, "GPH004", "removeFunction owner module function edge was not found", "owning Module function edge", function->id); return false; } char *parent_id = z_strdup(parent_edge->from); size_t removed_order = parent_edge->order; const char *function_name = function->name ? function->name : ""; for (size_t i = 0; graph && i < graph->node_len; i++) { if (marked[i]) continue; ZProgramGraphNode *node = &graph->nodes[i]; if (node->kind == Z_PROGRAM_GRAPH_NODE_CALL && patch_text_eq(node->name, function_name)) { free(parent_id); free(marked); patch_op_fail(result, op, "GPH005", "removeFunction function is still called", "unused function", function_name); return false; } } for (size_t i = 0; graph && i < graph->edge_len; i++) { ZProgramGraphEdge *edge = &graph->edges[i]; if (!patch_edge_target_removed_by_delete(graph, edge, marked)) continue; if (!patch_delete_external_reference_allowed(graph, edge, marked, parent_edge)) { free(parent_id); free(marked); patch_op_fail(result, op, "GPH005", "removeFunction would remove a node referenced outside its owned function subtree", "owned function subtree", edge->to); return false; } } size_t write_edge = 0; for (size_t i = 0; i < graph->edge_len; i++) { ZProgramGraphEdge *edge = &graph->edges[i]; size_t source = patch_node_index(graph, edge->from); bool remove = (source != (size_t)-1 && marked[source]) || patch_edge_target_removed_by_delete(graph, edge, marked); if (remove) { patch_free_edge(edge); continue; } if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->order > removed_order && patch_text_eq(edge->from, parent_id) && patch_text_eq(edge->kind, "function")) edge->order--; if (write_edge != i) graph->edges[write_edge] = *edge; write_edge++; } for (size_t i = write_edge; i < graph->edge_len; i++) graph->edges[i] = (ZProgramGraphEdge){0}; graph->edge_len = write_edge; size_t write_node = 0; for (size_t i = 0; i < graph->node_len; i++) { ZProgramGraphNode *node = &graph->nodes[i]; if (marked[i]) { patch_free_node(node); continue; } if (write_node != i) graph->nodes[write_node] = *node; write_node++; } for (size_t i = write_node; i < graph->node_len; i++) graph->nodes[i] = (ZProgramGraphNode){0}; graph->node_len = write_node; free(parent_id); free(marked); op->ok = true; return true; }
static bool patch_remove_param_external_reference_allowed(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, const bool *marked, const char *function_id, const char *param_id, const char *function_name, size_t order) {
  size_t source = patch_node_index(graph, edge->from);
  if (source != (size_t)-1 && marked[source]) return true;
  if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) return false;
  if (patch_text_eq(edge->kind, "param") && patch_text_eq(edge->from, function_id) && patch_text_eq(edge->to, param_id)) return true;
  if (patch_text_eq(edge->kind, "arg") && edge->order == order) {
    const ZProgramGraphNode *call = patch_find_node_const(graph, edge->from);
    if (call && call->kind == Z_PROGRAM_GRAPH_NODE_CALL && patch_text_eq(call->name, function_name)) return true;
  }
  return false;
}

static bool patch_apply_remove_param(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *param = patch_find_node(graph, op->node);
  if (!param) {
    patch_op_fail(result, op, "GPH004", "removeParam target parameter was not found", op->node, "");
    return false;
  }
  if (param->kind != Z_PROGRAM_GRAPH_NODE_PARAM) {
    patch_op_fail(result, op, "GPH003", "removeParam target must be a Param node", "Param node", z_program_graph_node_kind_name(param->kind));
    return false;
  }
  ZProgramGraphNode *owner = patch_param_owner_function(graph, param->id);
  if (!owner) {
    patch_op_fail(result, op, "GPH004", "removeParam owner function was not found", "owning Function node", param->id);
    return false;
  }
  size_t order = 0;
  if (!patch_param_edge_order(graph, owner->id, param->id, &order)) {
    patch_op_fail(result, op, "GPH004", "removeParam parameter edge was not found", "Function param edge", param->id);
    return false;
  }
  patch_replace_text(&op->actual, param->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "removeParam precondition failed", op->expected, op->actual);
    return false;
  }
  ZProgramGraphNode *body = patch_ordered_node(graph, owner->id, "body", 0);
  if (body) {
    bool *visited = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
    bool used = patch_identifier_name_used(graph, body->id, param->name, visited);
    free(visited);
    if (used) {
      patch_op_fail(result, op, "GPH005", "removeParam parameter is still used in the function body", "unused parameter", param->name);
      return false;
    }
  }
  bool *marked = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
  size_t param_index = patch_node_index(graph, param->id);
  if (param_index == (size_t)-1 || !patch_mark_delete_subtree(graph, param_index, marked)) {
    free(marked);
    patch_op_fail(result, op, "GPH006", "removeParam subtree references a missing node", "valid parameter subtree", param->id);
    return false;
  }
  const char *function_name = owner->name ? owner->name : "";
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    ZProgramGraphNode *call = &graph->nodes[i];
    if (call->kind != Z_PROGRAM_GRAPH_NODE_CALL || !patch_text_eq(call->name, function_name)) continue;
    const char *arg_id = NULL;
    if (!patch_direct_call_arg_node(graph, call->id, order, &arg_id)) {
      free(marked);
      patch_op_fail(result, op, "GPH005", "removeParam direct call is missing the removed argument", "call arg at removed parameter order", call->id);
      return false;
    }
    size_t arg_index = patch_node_index(graph, arg_id);
    if (arg_index == (size_t)-1 || !patch_mark_delete_subtree(graph, arg_index, marked)) {
      free(marked);
      patch_op_fail(result, op, "GPH006", "removeParam argument subtree references a missing node", "valid call argument subtree", arg_id);
      return false;
    }
  }
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (!patch_edge_target_removed_by_delete(graph, edge, marked)) continue;
    if (!patch_remove_param_external_reference_allowed(graph, edge, marked, owner->id, param->id, function_name, order)) {
      free(marked);
      patch_op_fail(result, op, "GPH005", "removeParam would remove a node referenced outside its owned parameter/call argument edge", "owned parameter and direct call argument edges only", edge->kind);
      return false;
    }
  }
  size_t write_edge = 0;
  for (size_t i = 0; i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    size_t source = patch_node_index(graph, edge->from);
    bool remove = (source != (size_t)-1 && marked[source]) || patch_edge_target_removed_by_delete(graph, edge, marked);
    if (remove) {
      patch_free_edge(edge);
      continue;
    }
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->order > order) {
      if (patch_text_eq(edge->from, owner->id) && patch_text_eq(edge->kind, "param")) edge->order--;
      else {
        const ZProgramGraphNode *call = patch_find_node_const(graph, edge->from);
        if (call && call->kind == Z_PROGRAM_GRAPH_NODE_CALL && patch_text_eq(call->name, function_name) && patch_text_eq(edge->kind, "arg")) edge->order--;
      }
    }
    if (write_edge != i) graph->edges[write_edge] = *edge;
    write_edge++;
  }
  for (size_t i = write_edge; i < graph->edge_len; i++) graph->edges[i] = (ZProgramGraphEdge){0};
  graph->edge_len = write_edge;
  size_t write_node = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    if (marked[i]) {
      patch_free_node(node);
      continue;
    }
    if (write_node != i) graph->nodes[write_node] = *node;
    write_node++;
  }
  for (size_t i = write_node; i < graph->node_len; i++) graph->nodes[i] = (ZProgramGraphNode){0};
  graph->node_len = write_node;
  free(marked);
  op->ok = true;
  return true;
}

static bool patch_apply_delete(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  size_t root = patch_node_index(graph, op->node);
  if (root == (size_t)-1) {
    patch_op_fail(result, op, "GPH004", "patch node was not found", op->node, "");
    return false;
  }
  if (graph->nodes[root].kind == Z_PROGRAM_GRAPH_NODE_MODULE) {
    patch_op_fail(result, op, "GPH003", "patch cannot delete the module root", "non-module node", op->node);
    return false;
  }
  patch_replace_text(&op->actual, graph->nodes[root].node_hash);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "patch node hash precondition failed", op->expected, op->actual);
    return false;
  }
  bool *marked = z_checked_calloc(graph->node_len, sizeof(bool));
  if (!patch_mark_delete_subtree(graph, root, marked)) {
    free(marked);
    patch_op_fail(result, op, "GPH006", "delete subtree references a missing node", "valid child edge target", op->node);
    return false;
  }
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!marked[i] || graph->nodes[i].kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    free(marked);
    patch_op_fail(result, op, "GPH003", "patch delete subtree cannot include the module root", "non-module owned subtree", graph->nodes[i].id);
    return false;
  }
  const ZProgramGraphEdge *root_parent_edge = patch_delete_root_parent_edge(graph, marked, op->node);
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!patch_edge_target_removed_by_delete(graph, edge, marked)) continue;
    if (!patch_delete_external_reference_allowed(graph, edge, marked, root_parent_edge)) {
      free(marked);
      patch_op_fail(result, op, "GPH005", "delete would remove a node referenced outside its subtree", "owned subtree", edge->to);
      return false;
    }
  }
  size_t write_edge = 0;
  for (size_t i = 0; i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    size_t source = patch_node_index(graph, edge->from);
    bool remove = (source != (size_t)-1 && marked[source]) || patch_edge_target_removed_by_delete(graph, edge, marked);
    if (remove) {
      free(edge->from);
      free(edge->to);
      free(edge->kind);
      continue;
    }
    if (write_edge != i) graph->edges[write_edge] = *edge;
    write_edge++;
  }
  for (size_t i = write_edge; i < graph->edge_len; i++) graph->edges[i] = (ZProgramGraphEdge){0};
  graph->edge_len = write_edge;

  size_t write_node = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    if (marked[i]) {
      free(node->id);
      free(node->name);
      free(node->type);
      free(node->value);
      free(node->path);
      free(node->symbol_id);
      free(node->type_id);
      free(node->effect_id);
      free(node->node_hash);
      continue;
    }
    if (write_node != i) graph->nodes[write_node] = *node;
    write_node++;
  }
  for (size_t i = write_node; i < graph->node_len; i++) graph->nodes[i] = (ZProgramGraphNode){0};
  graph->node_len = write_node;
  free(marked);
  op->ok = true;
  return true;
}

bool z_program_graph_patch_apply_operation(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (patch_text_eq(op->op, "insert")) return patch_apply_insert(graph, result, op);
  if (patch_text_eq(op->op, "insertEdge")) return patch_apply_insert_edge(graph, result, op);
  if (patch_text_eq(op->op, "replace")) return patch_apply_replace(graph, result, op);
  if (patch_text_eq(op->op, "delete")) return patch_apply_delete(graph, result, op);
  if (patch_text_eq(op->op, "rename")) return patch_apply_rename(graph, result, op);
  if (patch_text_eq(op->op, "renameSymbol")) return patch_apply_rename_symbol(graph, result, op);
  if (patch_text_eq(op->op, "renameParam")) return patch_apply_rename_param(graph, result, op);
  if (patch_text_eq(op->op, "renameLocal")) return patch_apply_rename_local(graph, result, op);
  if (patch_text_eq(op->op, "renameField")) return patch_apply_rename_field(graph, result, op);
  if (patch_text_eq(op->op, "replaceCallee")) return patch_apply_replace_callee(graph, result, op);
  if (patch_text_eq(op->op, "addImport")) return patch_apply_add_import(graph, result, op);
  if (patch_text_eq(op->op, "addFunction")) return patch_apply_add_function(graph, result, op);
  if (patch_text_eq(op->op, "addParam")) return patch_apply_add_param(graph, result, op);
  if (patch_text_eq(op->op, "removeParam")) return patch_apply_remove_param(graph, result, op);
  if (patch_text_eq(op->op, "removeFunction")) return patch_apply_remove_function(graph, result, op);
  if (patch_text_eq(op->op, "removeImport")) return patch_apply_remove_import(graph, result, op);
  if (patch_text_eq(op->op, "replaceImport")) return patch_apply_replace_import(graph, result, op);
  if (patch_text_eq(op->op, "renameImportAlias")) return patch_apply_rename_import_alias(graph, result, op);
  if (patch_text_eq(op->op, "changeReturnType")) return patch_apply_change_return_type(graph, result, op);
  if (patch_text_eq(op->op, "changeParamType")) return patch_apply_change_param_type(graph, result, op);
  if (patch_text_eq(op->op, "changeFieldType")) return patch_apply_change_field_type(graph, result, op);
  if (patch_text_eq(op->op, "changeLocalType")) return patch_apply_change_local_type(graph, result, op);

  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "patch node was not found", op->node, "");
    return false;
  }

  char **text_slot = patch_node_text_field(node, op->field);
  if (text_slot) {
    patch_replace_text(&op->actual, *text_slot);
    if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
      patch_op_fail(result, op, "GPH005", "patch field precondition failed", op->expected, op->actual);
      return false;
    }
    if (!patch_validate_text_value(node, result, op)) return false;
    patch_replace_text(text_slot, op->value);
    op->ok = true;
    return true;
  }

  bool *bool_slot = patch_node_bool_field(node, op->field);
  if (bool_slot) {
    bool next = false;
    if (!patch_parse_bool(op->value, &next)) {
      patch_op_fail(result, op, "GPH003", "patch flag value must be true or false", "true or false", op->value);
      return false;
    }
    const char *actual = *bool_slot ? "true" : "false";
    patch_replace_text(&op->actual, actual);
    if (op->has_expected && !patch_text_eq(op->expected, actual)) {
      patch_op_fail(result, op, "GPH005", "patch field precondition failed", op->expected, actual);
      return false;
    }
    *bool_slot = next;
    op->ok = true;
    return true;
  }

  patch_op_fail(result, op, "GPH003", "patch field is not editable", "name, type, value, public, mutable, static, fallible, or exportC", op->field);
  return false;
}
