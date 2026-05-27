#include "program_graph_compare.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool compare_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool compare_fail(
  ZProgramGraphCompare *out,
  const char *code,
  const char *message,
  const char *field,
  size_t left_index,
  size_t right_index,
  size_t left_count,
  size_t right_count
) {
  if (out) {
    out->ok = false;
    snprintf(out->code, sizeof(out->code), "%s", code ? code : "GRC000");
    snprintf(out->message, sizeof(out->message), "%s", message ? message : "program graphs differ");
    snprintf(out->field, sizeof(out->field), "%s", field ? field : "");
    out->left_index = left_index;
    out->right_index = right_index;
    out->left_count = left_count;
    out->right_count = right_count;
  }
  return false;
}

static size_t compare_missing_index(void) {
  return (size_t)-1;
}

static bool compare_edge_kind(const ZProgramGraphEdge *edge, const char *kind) {
  return edge && compare_text_eq(edge->kind, kind);
}

static bool compare_is_declared_type_ref(const ZProgramGraph *graph, size_t index) {
  if (!graph || index >= graph->node_len) return false;
  const ZProgramGraphNode *node = &graph->nodes[index];
  if (node->kind != Z_PROGRAM_GRAPH_NODE_TYPE_REF) return false;
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        compare_edge_kind(edge, "declaredType") &&
        compare_text_eq(edge->to, node->id)) {
      return true;
    }
  }
  return false;
}

static bool compare_has_module_named(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && compare_text_eq(node->name, name)) return true;
  }
  return false;
}

static bool compare_is_local_import(const ZProgramGraph *graph, size_t index) {
  if (!graph || index >= graph->node_len) return false;
  const ZProgramGraphNode *node = &graph->nodes[index];
  if (node->name && strncmp(node->name, "std.", strlen("std.")) == 0) return false;
  return node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT && compare_has_module_named(graph, node->name);
}

static bool compare_skip_node(const ZProgramGraph *graph, size_t index) {
  return compare_is_declared_type_ref(graph, index) || compare_is_local_import(graph, index);
}

static bool compare_skip_edge(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  if (!graph || !edge) return false;
  size_t source = compare_missing_index();
  for (size_t i = 0; i < graph->node_len; i++) {
    if (compare_text_eq(graph->nodes[i].id, edge->from)) {
      source = i;
      break;
    }
  }
  if (source != compare_missing_index() && compare_skip_node(graph, source)) return true;
  if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) return false;
  size_t target = compare_missing_index();
  for (size_t i = 0; i < graph->node_len; i++) {
    if (compare_text_eq(graph->nodes[i].id, edge->to)) {
      target = i;
      break;
    }
  }
  return target != compare_missing_index() && compare_skip_node(graph, target);
}

static bool compare_node_edge_kind_owns_child(const char *kind) {
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
    if (compare_text_eq(kind, owned_kinds[i])) return true;
  }
  return false;
}

static bool compare_edge_owns_child_node(const ZProgramGraphEdge *edge) {
  return edge &&
         edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
         compare_node_edge_kind_owns_child(edge->kind);
}

static const char *const compare_owned_edge_order[] = {
  "cImport",
  "import",
  "const",
  "alias",
  "shape",
  "interface",
  "enum",
  "choice",
  "function",
  "typeParam",
  "param",
  "type",
  "returnType",
  "effect",
  "error",
  "field",
  "method",
  "case",
  "target",
  "declaredType",
  "default",
  "value",
  "expr",
  "left",
  "right",
  "arg",
  "rangeEnd",
  "then",
  "else",
  "guard",
  "arm",
  "body",
  "statement",
  NULL,
};

static size_t compare_semantic_node_count(const ZProgramGraph *graph) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (!compare_skip_node(graph, i)) count++;
  }
  return count;
}

static size_t compare_semantic_edge_count(const ZProgramGraph *graph) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    if (!compare_skip_edge(graph, &graph->edges[i])) count++;
  }
  return count;
}

static size_t compare_nth_semantic_edge(const ZProgramGraph *graph, size_t semantic_index) {
  size_t seen = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    if (compare_skip_edge(graph, &graph->edges[i])) continue;
    if (seen == semantic_index) return i;
    seen++;
  }
  return compare_missing_index();
}

static size_t compare_node_index_by_id(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (compare_text_eq(graph->nodes[i].id, id)) return i;
  }
  return compare_missing_index();
}

static size_t compare_node_index_by_symbol_id(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (compare_text_eq(graph->nodes[i].symbol_id, id)) return i;
  }
  return compare_missing_index();
}

static size_t compare_node_index_by_type_id(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (compare_text_eq(graph->nodes[i].type_id, id)) return i;
  }
  return compare_missing_index();
}

static size_t compare_node_index_by_effect_id(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (compare_text_eq(graph->nodes[i].effect_id, id)) return i;
  }
  return compare_missing_index();
}

static const ZProgramGraphEdge *compare_next_owned_edge_by_order(const ZProgramGraph *graph, const char *from, const char *kind, const bool *visited, bool have_last, size_t last_order) {
  const ZProgramGraphEdge *best = NULL;
  size_t best_child = compare_missing_index();
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!compare_edge_owns_child_node(edge) ||
        !compare_text_eq(edge->from, from) ||
        !compare_text_eq(edge->kind, kind) ||
        (have_last && edge->order <= last_order)) {
      continue;
    }
    size_t child = compare_node_index_by_id(graph, edge->to);
    if (child == compare_missing_index() || (visited && visited[child])) continue;
    if (!best || edge->order < best->order || (edge->order == best->order && child < best_child)) {
      best = edge;
      best_child = child;
    }
  }
  return best;
}

static void compare_push_semantic_node(const ZProgramGraph *graph, size_t index, bool *visited, size_t *order, size_t *len) {
  if (!graph || index >= graph->node_len || visited[index]) return;
  visited[index] = true;
  if (!compare_skip_node(graph, index)) order[(*len)++] = index;
  const char *node_id = graph->nodes[index].id;
  for (size_t i = 0; compare_owned_edge_order[i]; i++) {
    bool have_last = false;
    size_t last_order = 0;
    for (;;) {
      const ZProgramGraphEdge *edge = compare_next_owned_edge_by_order(graph, node_id, compare_owned_edge_order[i], visited, have_last, last_order);
      if (!edge) break;
      size_t child = compare_node_index_by_id(graph, edge->to);
      last_order = edge->order;
      have_last = true;
      compare_push_semantic_node(graph, child, visited, order, len);
    }
  }
}

static size_t *compare_semantic_node_order(const ZProgramGraph *graph, size_t *out_len) {
  size_t len = 0;
  size_t *order = z_checked_calloc(graph && graph->node_len ? graph->node_len : 1, sizeof(size_t));
  bool *visited = z_checked_calloc(graph && graph->node_len ? graph->node_len : 1, sizeof(bool));
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_MODULE) compare_push_semantic_node(graph, i, visited, order, &len);
  }
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (!visited[i]) compare_push_semantic_node(graph, i, visited, order, &len);
  }
  free(visited);
  if (out_len) *out_len = len;
  return order;
}

static size_t *compare_semantic_rank_map(const ZProgramGraph *graph, const size_t *order, size_t order_len) {
  size_t *rank = z_checked_calloc(graph && graph->node_len ? graph->node_len : 1, sizeof(size_t));
  for (size_t i = 0; graph && i < graph->node_len; i++) rank[i] = compare_missing_index();
  for (size_t i = 0; i < order_len; i++) {
    if (order[i] < (graph ? graph->node_len : 0)) rank[order[i]] = i;
  }
  return rank;
}

static size_t compare_semantic_rank_for_raw_node(const ZProgramGraph *graph, const size_t *rank, size_t raw_index) {
  if (!graph || !rank || raw_index >= graph->node_len || compare_skip_node(graph, raw_index)) return compare_missing_index();
  return rank[raw_index];
}

static size_t compare_target_index(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, const size_t *rank) {
  if (!edge) return compare_missing_index();
  size_t raw_index = compare_missing_index();
  switch (edge->target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
      raw_index = compare_node_index_by_id(graph, edge->to);
      break;
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
      raw_index = compare_node_index_by_symbol_id(graph, edge->to);
      break;
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
      raw_index = compare_node_index_by_type_id(graph, edge->to);
      break;
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
      raw_index = compare_node_index_by_effect_id(graph, edge->to);
      break;
  }
  return compare_semantic_rank_for_raw_node(graph, rank, raw_index);
}

static bool compare_node_text_field(const char *field, const char *left, const char *right, size_t left_index, size_t right_index, ZProgramGraphCompare *out) {
  if (compare_text_eq(left, right)) return true;
  return compare_fail(out, "GRC004", "node semantic field differs", field, left_index, right_index, 0, 0);
}

static bool compare_node_bool_field(const char *field, bool left, bool right, size_t left_index, size_t right_index, ZProgramGraphCompare *out) {
  if (left == right) return true;
  return compare_fail(out, "GRC005", "node flag differs", field, left_index, right_index, 0, 0);
}

static bool compare_nodes(const ZProgramGraph *left, const ZProgramGraph *right, ZProgramGraphCompare *out) {
  size_t left_count = 0;
  size_t right_count = 0;
  size_t *left_order = compare_semantic_node_order(left, &left_count);
  size_t *right_order = compare_semantic_node_order(right, &right_count);
  if (left_count != right_count) {
    free(left_order);
    free(right_order);
    return compare_fail(out, "GRC002", "node count differs", "nodes", 0, 0, left_count, right_count);
  }
  for (size_t i = 0; i < left_count; i++) {
    size_t left_index = left_order[i];
    size_t right_index = right_order[i];
    const ZProgramGraphNode *left_node = &left->nodes[left_index];
    const ZProgramGraphNode *right_node = &right->nodes[right_index];
    if (left_node->kind != right_node->kind) {
      free(left_order);
      free(right_order);
      return compare_fail(out, "GRC003", "node kind differs", "kind", left_index, right_index, 0, 0);
    }
    if (!compare_node_text_field("name", left_node->name, right_node->name, left_index, right_index, out) ||
        !compare_node_text_field("type", left_node->type, right_node->type, left_index, right_index, out) ||
        !compare_node_text_field("value", left_node->value, right_node->value, left_index, right_index, out) ||
        !compare_node_bool_field("public", left_node->is_public, right_node->is_public, left_index, right_index, out) ||
        !compare_node_bool_field("mutable", left_node->is_mutable, right_node->is_mutable, left_index, right_index, out) ||
        !compare_node_bool_field("static", left_node->is_static, right_node->is_static, left_index, right_index, out) ||
        !compare_node_bool_field("fallible", left_node->fallible, right_node->fallible, left_index, right_index, out) ||
        !compare_node_bool_field("exportC", left_node->export_c, right_node->export_c, left_index, right_index, out)) {
      free(left_order);
      free(right_order);
      return false;
    }
  }
  free(left_order);
  free(right_order);
  return true;
}

static bool compare_edges(const ZProgramGraph *left, const ZProgramGraph *right, ZProgramGraphCompare *out) {
  size_t left_count = compare_semantic_edge_count(left);
  size_t right_count = compare_semantic_edge_count(right);
  if (left_count != right_count) {
    return compare_fail(out, "GRC006", "edge count differs", "edges", 0, 0, left_count, right_count);
  }
  size_t left_order_len = 0;
  size_t right_order_len = 0;
  size_t *left_order = compare_semantic_node_order(left, &left_order_len);
  size_t *right_order = compare_semantic_node_order(right, &right_order_len);
  size_t *left_rank = compare_semantic_rank_map(left, left_order, left_order_len);
  size_t *right_rank = compare_semantic_rank_map(right, right_order, right_order_len);
  free(left_order);
  free(right_order);
  bool *matched = z_checked_calloc(right_count ? right_count : 1, sizeof(bool));
  for (size_t i = 0; i < left_count; i++) {
    size_t left_index = compare_nth_semantic_edge(left, i);
    const ZProgramGraphEdge *left_edge = &left->edges[left_index];
    size_t left_from = compare_semantic_rank_for_raw_node(left, left_rank, compare_node_index_by_id(left, left_edge->from));
    size_t left_to = compare_target_index(left, left_edge, left_rank);
    bool found = false;
    for (size_t j = 0; j < right_count; j++) {
      if (matched[j]) continue;
      size_t right_index = compare_nth_semantic_edge(right, j);
      const ZProgramGraphEdge *right_edge = &right->edges[right_index];
      size_t right_from = compare_semantic_rank_for_raw_node(right, right_rank, compare_node_index_by_id(right, right_edge->from));
      size_t right_to = compare_target_index(right, right_edge, right_rank);
      if (left_from != right_from ||
          left_to != right_to ||
          left_edge->target != right_edge->target ||
          left_edge->order != right_edge->order ||
          !compare_text_eq(left_edge->kind, right_edge->kind)) {
        continue;
      }
      matched[j] = true;
      found = true;
      break;
    }
    if (!found) {
      free(matched);
      free(left_rank);
      free(right_rank);
      return compare_fail(out, "GRC007", "edge semantic fact is missing", "edge", left_index, compare_missing_index(), left_from, left_to);
    }
  }
  free(matched);
  free(left_rank);
  free(right_rank);
  return true;
}

bool z_program_graph_semantic_compare(const ZProgramGraph *left, const ZProgramGraph *right, ZProgramGraphCompare *out) {
  if (out) *out = (ZProgramGraphCompare){.ok = true};
  if (!left || !right) return compare_fail(out, "GRC001", "program graph is missing", "graph", 0, 0, left ? 1 : 0, right ? 1 : 0);
  if (out) {
    out->left_semantic_nodes = compare_semantic_node_count(left);
    out->right_semantic_nodes = compare_semantic_node_count(right);
    out->left_semantic_edges = compare_semantic_edge_count(left);
    out->right_semantic_edges = compare_semantic_edge_count(right);
  }
  if (left->schema_version != right->schema_version) {
    return compare_fail(out, "GRC012", "schema version differs", "schemaVersion", 0, 0, left->schema_version, right->schema_version);
  }
  if (!compare_text_eq(left->module_identity, right->module_identity)) {
    return compare_fail(out, "GRC013", "module identity differs", "moduleIdentity", 0, 0, 0, 0);
  }
  if (!compare_nodes(left, right, out)) return false;
  if (!compare_edges(left, right, out)) return false;
  if (out) out->ok = true;
  return true;
}
