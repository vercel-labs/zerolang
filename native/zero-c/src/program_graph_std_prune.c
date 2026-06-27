#include "program_graph_std_prune.h"

#include "program_graph_adjacency.h"
#include "program_graph_order.h"
#include "program_graph_string_map.h"
#include "std_source.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool std_prune_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
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

typedef struct {
  const char **paths;
  const ZStdSourceModule **modules;
  size_t len;
} StdPruneEmbeddedPaths;

static const ZStdSourceModule *std_prune_declared_std_module(const ZProgramGraphNode *node) {
  if (!node || node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) return NULL;
  const ZStdSourceModule *module = z_std_source_module_for_path(node->path);
  return module && std_prune_text_eq(node->name, module->module) ? module : NULL;
}

static bool std_prune_embedded_module_present(const StdPruneEmbeddedPaths *embedded, const ZStdSourceModule *module) {
  for (size_t i = 0; embedded && module && i < embedded->len; i++) {
    if (embedded->modules[i] == module) return true;
  }
  return false;
}

static bool std_prune_path_has_conflicting_module(const ZProgramGraph *graph, const char *path, const ZStdSourceModule *module) {
  for (size_t i = 0; graph && path && module && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE || !std_prune_text_eq(node->path, path)) continue;
    if (std_prune_declared_std_module(node) != module) return true;
  }
  return false;
}

static void std_prune_embedded_paths_add(StdPruneEmbeddedPaths *embedded, const char *path, const ZStdSourceModule *module) {
  if (!embedded || !path || !module) return;
  for (size_t i = 0; i < embedded->len; i++) {
    if (std_prune_text_eq(embedded->paths[i], path)) return;
  }
  embedded->paths[embedded->len] = path;
  embedded->modules[embedded->len] = module;
  embedded->len++;
}

static void std_prune_collect_embedded_paths(const ZProgramGraph *graph, StdPruneEmbeddedPaths *embedded) {
  if (!embedded) return;
  embedded->paths = z_checked_calloc(graph && graph->node_len ? graph->node_len : 1, sizeof(const char *));
  embedded->modules = z_checked_calloc(graph && graph->node_len ? graph->node_len : 1, sizeof(const ZStdSourceModule *));
  embedded->len = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    const ZStdSourceModule *module = std_prune_declared_std_module(node);
    if (!module) continue;
    std_prune_embedded_paths_add(embedded, node->path, module);
  }
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    const ZStdSourceModule *module = z_std_source_module_for_path(node->path);
    if (!std_prune_embedded_module_present(embedded, module)) continue;
    if (std_prune_path_has_conflicting_module(graph, node->path, module)) continue;
    std_prune_embedded_paths_add(embedded, node->path, module);
  }
}

static void std_prune_embedded_paths_free(StdPruneEmbeddedPaths *embedded) {
  if (!embedded) return;
  free(embedded->paths);
  free(embedded->modules);
  *embedded = (StdPruneEmbeddedPaths){0};
}

static const ZStdSourceModule *std_prune_embedded_std_module_for_path(const StdPruneEmbeddedPaths *embedded, const char *path) {
  for (size_t i = 0; embedded && path && i < embedded->len; i++) {
    if (std_prune_text_eq(embedded->paths[i], path)) return embedded->modules[i];
  }
  return NULL;
}

static bool std_prune_node_is_embedded_std(const StdPruneEmbeddedPaths *embedded, const ZProgramGraphNode *node) {
  return node && std_prune_embedded_std_module_for_path(embedded, node->path) != NULL;
}

static bool std_prune_node_is_embedded_std_function(const StdPruneEmbeddedPaths *embedded, const ZProgramGraphNode *node) {
  return node &&
         node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION &&
         std_prune_node_is_embedded_std(embedded, node);
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

static char *std_prune_function_key(const char *module, const char *name) {
  ZBuf key;
  zbuf_init(&key);
  zbuf_append(&key, module ? module : "");
  zbuf_append_char(&key, '\x1f');
  zbuf_append(&key, name ? name : "");
  return key.data ? key.data : z_strdup("");
}

static void std_prune_build_function_index(const ZProgramGraph *graph, const StdPruneEmbeddedPaths *embedded, ZProgramGraphStringMap *index) {
  z_program_graph_string_map_init(index, graph ? graph->node_len : 0);
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!std_prune_node_is_embedded_std_function(embedded, node)) continue;
    const ZStdSourceModule *module = std_prune_embedded_std_module_for_path(embedded, node->path);
    if (!module) continue;
    char *key = std_prune_function_key(module->module, node->name);
    if (!z_program_graph_string_map_find(index, key)) z_program_graph_string_map_put(index, key, i);
    free(key);
  }
}

static size_t std_prune_find_std_function_index(const ZProgramGraphStringMap *function_index, const ZStdSourceModule *module, const char *name) {
  if (!function_index || !module || !name) return SIZE_MAX;
  char *key = std_prune_function_key(module->module, name);
  ZProgramGraphStringMapEntry *entry = z_program_graph_string_map_find(function_index, key);
  free(key);
  return entry ? entry->value : SIZE_MAX;
}

static void std_prune_queue_function(size_t index, bool *keep, bool *queued, size_t *queue, size_t *queue_len) {
  if (index == SIZE_MAX || queued[index]) return;
  keep[index] = true;
  queued[index] = true;
  queue[(*queue_len)++] = index;
}

static void std_prune_mark_call_target(const ZProgramGraphStringMap *function_index, const ZProgramGraphAdjacency *adjacency, const ZStdSourceModule *current_module, const ZProgramGraphNode *call, bool *keep, bool *queued, size_t *queue, size_t *queue_len) {
  if (!call || (call->kind != Z_PROGRAM_GRAPH_NODE_CALL && call->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) return;
  char *qualified = std_prune_expr_name(adjacency, call);
  const char *callee = qualified && qualified[0] ? qualified : call->name;
  const ZStdSourceModule *public_module = z_std_source_module_for_public_call(callee);
  const char *public_target = z_std_source_target_for_public_call(callee);
  if (public_target && public_module) {
    std_prune_queue_function(std_prune_find_std_function_index(function_index, public_module, public_target), keep, queued, queue, queue_len);
  } else if (current_module) {
    std_prune_queue_function(std_prune_find_std_function_index(function_index, current_module, call->name), keep, queued, queue, queue_len);
  }
  free(qualified);
}

static void std_prune_scan_reachable_std_calls(const ZProgramGraph *graph, const ZProgramGraphAdjacency *adjacency, const ZProgramGraphStringMap *function_index, const ZStdSourceModule *current_module, size_t node_index, const size_t *head, const size_t *next, const size_t *edge_to, bool *visited, bool *keep, bool *queued, size_t *queue, size_t *queue_len, unsigned depth) {
  if (!graph || node_index == SIZE_MAX || depth > 256) return;
  if (visited[node_index]) return;
  visited[node_index] = true;
  const ZProgramGraphNode *node = &graph->nodes[node_index];
  std_prune_mark_call_target(function_index, adjacency, current_module, node, keep, queued, queue, queue_len);
  for (size_t edge_index = head[node_index]; edge_index != SIZE_MAX; edge_index = next[edge_index]) {
    std_prune_scan_reachable_std_calls(graph, adjacency, function_index, current_module, edge_to[edge_index], head, next, edge_to, visited, keep, queued, queue, queue_len, depth + 1);
  }
}

static void std_prune_build_edge_index(const ZProgramGraph *graph, const ZProgramGraphAdjacency *adjacency, size_t *edge_from, size_t *edge_to, size_t *head, size_t *next) {
  if (!graph) return;
  for (size_t i = 0; i < graph->node_len; i++) head[i] = SIZE_MAX;
  for (size_t i = 0; i < graph->edge_len; i++) {
    edge_from[i] = z_program_graph_adjacency_node_index(adjacency, graph->edges[i].from);
    edge_to[i] = graph->edges[i].target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE
      ? z_program_graph_adjacency_node_index(adjacency, graph->edges[i].to)
      : SIZE_MAX;
    next[i] = SIZE_MAX;
    size_t from = edge_from[i];
    if (from == SIZE_MAX || edge_to[i] == SIZE_MAX) continue;
    next[i] = head[from];
    head[from] = i;
  }
}

static void std_prune_expand_removed_subgraphs(const ZProgramGraph *graph, bool *remove, const size_t *edge_to, const size_t *head, const size_t *next) {
  if (!graph) return;
  size_t *queue = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(size_t));
  size_t queue_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (remove[i]) queue[queue_len++] = i;
  }
  for (size_t cursor = 0; cursor < queue_len; cursor++) {
    size_t from = queue[cursor];
    for (size_t edge_index = head[from]; edge_index != SIZE_MAX; edge_index = next[edge_index]) {
      size_t to = edge_to[edge_index];
      if (to == SIZE_MAX || remove[to]) continue;
      remove[to] = true;
      queue[queue_len++] = to;
    }
  }
  free(queue);
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
  ZProgramGraphAdjacency adjacency; z_program_graph_adjacency_init(&adjacency, graph);
  StdPruneEmbeddedPaths embedded = {0};
  std_prune_collect_embedded_paths(graph, &embedded);
  ZProgramGraphStringMap function_index; std_prune_build_function_index(graph, &embedded, &function_index);
  bool *keep = z_checked_calloc(graph->node_len, sizeof(bool)), *queued = z_checked_calloc(graph->node_len, sizeof(bool)), *visited = z_checked_calloc(graph->node_len, sizeof(bool));
  size_t *queue = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(size_t));
  size_t *edge_from = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t));
  size_t *edge_to = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t));
  size_t *head = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(size_t));
  size_t *next = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t));
  std_prune_build_edge_index(graph, &adjacency, edge_from, edge_to, head, next);
  size_t queue_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && !std_prune_node_is_embedded_std(&embedded, node)) {
      queued[i] = true;
      queue[queue_len++] = i;
    }
  }
  for (size_t cursor = 0; cursor < queue_len; cursor++) {
    const ZProgramGraphNode *function = &graph->nodes[queue[cursor]];
    const ZStdSourceModule *current_module = std_prune_embedded_std_module_for_path(&embedded, function->path);
    std_prune_scan_reachable_std_calls(graph, &adjacency, &function_index, current_module, queue[cursor], head, next, edge_to, visited, keep, queued, queue, &queue_len, 0);
  }
  z_program_graph_adjacency_free(&adjacency); z_program_graph_string_map_free(&function_index);

  bool *remove = z_checked_calloc(graph->node_len, sizeof(bool));
  size_t removed_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (std_prune_node_is_embedded_std_function(&embedded, &graph->nodes[i]) && !keep[i]) {
      remove[i] = true;
      removed_len++;
    }
  }
  free(queue); free(queued); free(keep);
  if (removed_len == 0) {
    free(next); free(head); free(edge_to); free(edge_from); free(visited);
    std_prune_embedded_paths_free(&embedded);
    free(remove);
    return;
  }

  std_prune_expand_removed_subgraphs(graph, remove, edge_to, head, next);
  std_prune_remove_marked_nodes(graph, remove, edge_from, edge_to);
  free(next); free(head); free(edge_to); free(edge_from); free(visited); std_prune_embedded_paths_free(&embedded); free(remove);
}
