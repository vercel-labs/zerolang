#include "program_graph_std_prune.h"

#include "program_graph_adjacency.h"
#include "program_graph_order.h"
#include "std_source.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool std_prune_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool std_prune_starts_with(const char *text, const char *prefix) {
  size_t len = prefix ? strlen(prefix) : 0;
  return text && prefix && strncmp(text, prefix, len) == 0;
}

static void std_prune_free_node_fields(ZProgramGraphNode *node) {
  if (!node) return;
  free(node->id);
  free(node->name);
  free(node->type);
  free(node->value);
  free(node->path);
  free(node->symbol_id);
  free(node->type_id);
  free(node->effect_id);
  free(node->node_hash);
  *node = (ZProgramGraphNode){0};
}

static void std_prune_free_edge_fields(ZProgramGraphEdge *edge) {
  if (!edge) return;
  free(edge->from);
  free(edge->to);
  free(edge->kind);
  *edge = (ZProgramGraphEdge){0};
}

static bool std_prune_node_is_embedded_std(const ZProgramGraphNode *node) {
  return node && z_std_source_module_for_path(node->path) != NULL;
}

static bool std_prune_node_is_embedded_std_function(const ZProgramGraphNode *node) {
  return node &&
         node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION &&
         std_prune_node_is_embedded_std(node) &&
         std_prune_starts_with(node->name, "__zero_std_");
}

static const ZProgramGraphNode *std_prune_ordered_node(const ZProgramGraphAdjacency *adjacency, const char *from, const char *kind, size_t order) {
  size_t start = 0;
  size_t len = 0;
  if (!kind) return NULL;
  z_program_graph_adjacency_owner_run(adjacency, from, kind, &start, &len);
  for (size_t i = start; i < start + len; i++) {
    const ZProgramGraphEdge *edge = z_program_graph_adjacency_owner_edge_at(adjacency, i);
    if (edge && edge->order == order) return z_program_graph_adjacency_node(adjacency, edge->to);
  }
  return NULL;
}

static bool std_prune_expr_name_into(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *node, ZBuf *out) {
  if (!node || !out) return false;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER || node->kind == Z_PROGRAM_GRAPH_NODE_CALL) {
    zbuf_append(out, node->name ? node->name : "");
    return true;
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) {
    const ZProgramGraphNode *left = std_prune_ordered_node(adjacency, node->id, "left", 0);
    if (!std_prune_expr_name_into(adjacency, left, out)) return false;
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS ||
        !left || left->kind != Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS || !std_prune_text_eq(left->name, node->name)) {
      zbuf_append_char(out, '.');
      zbuf_append(out, node->name ? node->name : "");
    }
    return true;
  }
  return false;
}

static char *std_prune_expr_name(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *node) {
  ZBuf name;
  zbuf_init(&name);
  if (!std_prune_expr_name_into(adjacency, node, &name)) {
    zbuf_free(&name);
    return NULL;
  }
  return name.data;
}

static bool std_prune_node_belongs_to_module(const ZProgramGraphNode *node, const ZStdSourceModule *module) {
  if (!node || !module) return false;
  const ZStdSourceModule *node_module = z_std_source_module_for_path(node->path);
  return node_module && std_prune_text_eq(node_module->module, module->module);
}

static size_t std_prune_find_std_function_index(const ZProgramGraph *graph, const ZStdSourceModule *module, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!std_prune_node_is_embedded_std_function(node) || !std_prune_text_eq(node->name, name)) continue;
    if (!module || std_prune_node_belongs_to_module(node, module)) return i;
  }
  return SIZE_MAX;
}

static void std_prune_queue_function(size_t index, bool *keep, bool *queued, size_t *queue, size_t *queue_len) {
  if (index == SIZE_MAX || queued[index]) return;
  keep[index] = true;
  queued[index] = true;
  queue[(*queue_len)++] = index;
}

static void std_prune_mark_call_target(const ZProgramGraph *graph, const ZProgramGraphAdjacency *adjacency, const ZStdSourceModule *current_module, const ZProgramGraphNode *call, bool *keep, bool *queued, size_t *queue, size_t *queue_len) {
  if (!graph || !call || (call->kind != Z_PROGRAM_GRAPH_NODE_CALL && call->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) return;
  char *qualified = std_prune_expr_name(adjacency, call);
  const char *callee = qualified && qualified[0] ? qualified : call->name;
  const ZStdSourceModule *public_module = z_std_source_module_for_public_call(callee);
  const char *public_target = z_std_source_target_for_public_call(callee);
  if (public_target && public_module) {
    std_prune_queue_function(std_prune_find_std_function_index(graph, public_module, public_target), keep, queued, queue, queue_len);
  } else if (current_module) {
    std_prune_queue_function(std_prune_find_std_function_index(graph, current_module, call->name), keep, queued, queue, queue_len);
  }
  free(qualified);
}

static void std_prune_scan_reachable_std_calls(const ZProgramGraph *graph, const ZProgramGraphAdjacency *adjacency, const ZStdSourceModule *current_module, const char *node_id, bool *keep, bool *queued, size_t *queue, size_t *queue_len, unsigned depth) {
  if (!graph || !node_id || depth > 256) return;
  const ZProgramGraphNode *node = z_program_graph_adjacency_node(adjacency, node_id);
  if (!node) return;
  std_prune_mark_call_target(graph, adjacency, current_module, node, keep, queued, queue, queue_len);
  size_t start = 0;
  size_t len = 0;
  z_program_graph_adjacency_owner_run(adjacency, node_id, NULL, &start, &len);
  for (size_t i = start; i < start + len; i++) {
    const ZProgramGraphEdge *edge = z_program_graph_adjacency_owner_edge_at(adjacency, i);
    if (!edge || edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
    std_prune_scan_reachable_std_calls(graph, adjacency, current_module, edge->to, keep, queued, queue, queue_len, depth + 1);
  }
}

static void std_prune_expand_removed_subgraphs(const ZProgramGraph *graph, bool *remove, const size_t *edge_from, const size_t *edge_to) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; graph && i < graph->edge_len; i++) {
      if (graph->edges[i].target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
      size_t from = edge_from[i];
      size_t to = edge_to[i];
      if (from == SIZE_MAX || to == SIZE_MAX) continue;
      if (remove[from] && !remove[to]) {
        remove[to] = true;
        changed = true;
      }
    }
  }
}

static void std_prune_remove_marked_nodes(ZProgramGraph *graph, const bool *remove, const size_t *edge_from, const size_t *edge_to) {
  size_t edge_write = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    bool drop = (edge_from[i] != SIZE_MAX && remove[edge_from[i]]) ||
                (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge_to[i] != SIZE_MAX && remove[edge_to[i]]);
    if (drop) {
      std_prune_free_edge_fields(edge);
      continue;
    }
    if (edge_write != i) {
      graph->edges[edge_write] = graph->edges[i];
      graph->edges[i] = (ZProgramGraphEdge){0};
    }
    edge_write++;
  }
  if (graph) graph->edge_len = edge_write;

  size_t node_write = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (remove[i]) {
      std_prune_free_node_fields(&graph->nodes[i]);
      continue;
    }
    if (node_write != i) {
      graph->nodes[node_write] = graph->nodes[i];
      graph->nodes[i] = (ZProgramGraphNode){0};
    }
    node_write++;
  }
  if (graph) graph->node_len = node_write;
  z_program_graph_compact_ordered_edges(graph);
}

void z_program_graph_prune_unreachable_std_source_functions(ZProgramGraph *graph) {
  if (!graph || graph->node_len == 0) return;
  ZProgramGraphAdjacency adjacency;
  z_program_graph_adjacency_init(&adjacency, graph);
  bool *keep = z_checked_calloc(graph->node_len, sizeof(bool));
  bool *queued = z_checked_calloc(graph->node_len, sizeof(bool));
  size_t *queue = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(size_t));
  size_t queue_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && !std_prune_node_is_embedded_std(node)) {
      queued[i] = true;
      queue[queue_len++] = i;
    }
  }
  for (size_t cursor = 0; cursor < queue_len; cursor++) {
    const ZProgramGraphNode *function = &graph->nodes[queue[cursor]];
    const ZStdSourceModule *current_module = z_std_source_module_for_path(function->path);
    std_prune_scan_reachable_std_calls(graph, &adjacency, current_module, function->id, keep, queued, queue, &queue_len, 0);
  }
  z_program_graph_adjacency_free(&adjacency);

  bool *remove = z_checked_calloc(graph->node_len, sizeof(bool));
  size_t removed_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (std_prune_node_is_embedded_std_function(&graph->nodes[i]) && !keep[i]) {
      remove[i] = true;
      removed_len++;
    }
  }
  free(queue);
  free(queued);
  free(keep);
  if (removed_len == 0) {
    free(remove);
    return;
  }

  const char **node_ids = z_checked_calloc(graph->node_len, sizeof(const char *));
  for (size_t i = 0; i < graph->node_len; i++) node_ids[i] = graph->nodes[i].id;
  ZProgramGraphAdjacencyNodeEntry *id_index = z_program_graph_id_index_build(node_ids, graph->node_len);
  size_t *edge_from = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t));
  size_t *edge_to = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t));
  for (size_t i = 0; i < graph->edge_len; i++) {
    edge_from[i] = z_program_graph_id_index_find(id_index, graph->node_len, graph->edges[i].from);
    edge_to[i] = graph->edges[i].target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE
      ? z_program_graph_id_index_find(id_index, graph->node_len, graph->edges[i].to)
      : SIZE_MAX;
  }
  std_prune_expand_removed_subgraphs(graph, remove, edge_from, edge_to);
  std_prune_remove_marked_nodes(graph, remove, edge_from, edge_to);
  free(edge_to);
  free(edge_from);
  free(id_index);
  free(node_ids);
  free(remove);
}
