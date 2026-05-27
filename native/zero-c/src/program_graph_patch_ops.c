#include "program_graph_patch.h"
#include "type_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void patch_replace_text(char **slot, const char *value) {
  if (!slot) return;
  free(*slot);
  *slot = z_strdup(value ? value : "");
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
  if (!text || strncmp(text, "node:", strlen("node:")) != 0 || !text[strlen("node:")]) return false;
  for (const char *cursor = text + strlen("node:"); *cursor; cursor++) {
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
    patch_op_fail(result, op, "GPH003", "insert node id must be a ProgramGraph node id", "node:<id>", op->node);
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
