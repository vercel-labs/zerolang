#include "program_graph_store_prune.h"

#include "std_source.h"
#include "zero.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static bool prune_text_eq(const char *left, const char *right) { return strcmp(left ? left : "", right ? right : "") == 0; }

static bool prune_starts_with(const char *text, const char *prefix) {
  size_t len = prefix ? strlen(prefix) : 0;
  return text && prefix && strncmp(text, prefix, len) == 0;
}

static const char *prune_basename(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}

static const ZStdSourceModule *prune_std_source_module_for_path(const char *path) {
  for (size_t i = 0; path && i < z_std_source_module_count(); i++) {
    const ZStdSourceModule *module = z_std_source_module_at(i);
    if (module &&
        (prune_text_eq(module->path, path) ||
         prune_text_eq(prune_basename(module->path), prune_basename(path)))) return module;
  }
  return NULL;
}

static bool prune_path_is_embedded_std(const ZProgramGraph *graph, const char *path) {
  const ZStdSourceModule *module = prune_std_source_module_for_path(path);
  if (!graph || !module) return false;
  bool has_module_node = false;
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!prune_text_eq(node->path, path)) continue;
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && prune_text_eq(node->name, module->module)) has_module_node = true;
  }
  return has_module_node;
}

static bool prune_node_is_appended_std_helper(const ZProgramGraphNode *node) {
  return node &&
         node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION &&
         prune_starts_with(node->name, "__zero_std_");
}

static void prune_free_node_fields(ZProgramGraphNode *node) {
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

static void prune_free_edge_fields(ZProgramGraphEdge *edge) {
  if (!edge) return;
  free(edge->from);
  free(edge->to);
  free(edge->kind);
  *edge = (ZProgramGraphEdge){0};
}

static bool prune_id_removed(char **removed_ids, size_t len, const char *id) {
  for (size_t i = 0; id && i < len; i++) {
    if (prune_text_eq(removed_ids[i], id)) return true;
  }
  return false;
}

static ssize_t prune_node_index_for_id(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (prune_text_eq(graph->nodes[i].id, id)) return (ssize_t)i;
  }
  return -1;
}

static void prune_mark_removed(char **removed_ids, size_t *removed_len, const ZProgramGraphNode *node) {
  if (!removed_ids || !removed_len || !node) return;
  removed_ids[(*removed_len)++] = z_strdup(node->id ? node->id : "");
}

static void prune_expand_removed_subgraphs(ZProgramGraph *graph, bool *remove, char **removed_ids, size_t *removed_len) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; graph && i < graph->edge_len; i++) {
      ZProgramGraphEdge *edge = &graph->edges[i];
      if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
      ssize_t from = prune_node_index_for_id(graph, edge->from);
      ssize_t to = prune_node_index_for_id(graph, edge->to);
      if (from < 0 || to < 0) continue;
      if (remove[from] && !remove[to]) {
        remove[to] = true;
        prune_mark_removed(removed_ids, removed_len, &graph->nodes[to]);
        changed = true;
      }
    }
  }
}

static void prune_removed_edges(ZProgramGraph *graph, char **removed_ids, size_t removed_len) {
  size_t write = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    bool drop = prune_id_removed(removed_ids, removed_len, edge->from) ||
                (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && prune_id_removed(removed_ids, removed_len, edge->to));
    if (drop) {
      prune_free_edge_fields(edge);
      continue;
    }
    if (write != i) {
      graph->edges[write] = graph->edges[i];
      graph->edges[i] = (ZProgramGraphEdge){0};
    }
    write++;
  }
  if (graph) graph->edge_len = write;
}

void z_program_graph_prune_embedded_std_source_nodes(ZProgramGraph *graph) {
  if (!graph || graph->node_len == 0) return;
  bool *remove = z_checked_calloc(graph->node_len, sizeof(bool));
  char **removed_ids = z_checked_calloc(graph->node_len, sizeof(char *));
  bool has_local_source = false;
  size_t removed_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    bool embedded_std = prune_path_is_embedded_std(graph, graph->nodes[i].path);
    bool appended_std_helper = prune_node_is_appended_std_helper(&graph->nodes[i]);
    if (embedded_std) {
      remove[i] = true;
      prune_mark_removed(removed_ids, &removed_len, &graph->nodes[i]);
    } else if (appended_std_helper) {
      remove[i] = true;
      prune_mark_removed(removed_ids, &removed_len, &graph->nodes[i]);
    } else {
      has_local_source = true;
    }
  }
  prune_expand_removed_subgraphs(graph, remove, removed_ids, &removed_len);
  if (has_local_source && removed_len > 0) {
    prune_removed_edges(graph, removed_ids, removed_len);
    size_t write = 0;
    for (size_t i = 0; i < graph->node_len; i++) {
      if (remove[i]) {
        prune_free_node_fields(&graph->nodes[i]);
        continue;
      }
      if (write != i) {
        graph->nodes[write] = graph->nodes[i];
        graph->nodes[i] = (ZProgramGraphNode){0};
      }
      write++;
    }
    graph->node_len = write;
  }
  for (size_t i = 0; i < removed_len; i++) free(removed_ids[i]);
  free(removed_ids);
  free(remove);
}
