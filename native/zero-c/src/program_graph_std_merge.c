#include "program_graph_build.h"
#include "std_source.h"

#include <stdlib.h>
#include <string.h>

static bool std_merge_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool std_merge_starts_with(const char *text, const char *prefix) {
  size_t len = prefix ? strlen(prefix) : 0;
  return text && prefix && strncmp(text, prefix, len) == 0;
}

static char *std_merge_strdup(const char *text) { return text ? z_strdup(text) : NULL; }

static const char *std_merge_basename(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}

static const ZProgramGraphNode *std_merge_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (std_merge_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static bool std_merge_has_node_id(const ZProgramGraph *graph, const char *id) {
  return std_merge_find_node(graph, id) != NULL;
}

static bool std_merge_node_payload_equal(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  return left && right &&
         left->kind == right->kind &&
         left->line == right->line &&
         left->column == right->column &&
         left->is_public == right->is_public &&
         left->is_mutable == right->is_mutable &&
         left->is_static == right->is_static &&
         left->fallible == right->fallible &&
         left->export_c == right->export_c &&
         std_merge_text_eq(left->name, right->name) &&
         std_merge_text_eq(left->type, right->type) &&
         std_merge_text_eq(left->value, right->value) &&
         std_merge_text_eq(left->path, right->path) &&
         std_merge_text_eq(left->node_hash, right->node_hash);
}

static bool std_merge_edge_equal(const ZProgramGraphEdge *left, const ZProgramGraphEdge *right) {
  return left && right &&
         left->target == right->target &&
         left->order == right->order &&
         std_merge_text_eq(left->from, right->from) &&
         std_merge_text_eq(left->to, right->to) &&
         std_merge_text_eq(left->kind, right->kind);
}

static bool std_merge_has_edge(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  for (size_t i = 0; graph && edge && i < graph->edge_len; i++) {
    if (std_merge_edge_equal(&graph->edges[i], edge)) return true;
  }
  return false;
}

static bool std_merge_has_ordered_edge(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  for (size_t i = 0; graph && edge && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *existing = &graph->edges[i];
    if (existing->target == edge->target &&
        existing->order == edge->order &&
        std_merge_text_eq(existing->from, edge->from) &&
        std_merge_text_eq(existing->kind, edge->kind)) return true;
  }
  return false;
}

static size_t std_merge_next_edge_order(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  size_t order = 0;
  for (size_t i = 0; graph && edge && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *existing = &graph->edges[i];
    if (existing->target == edge->target &&
        std_merge_text_eq(existing->from, edge->from) &&
        std_merge_text_eq(existing->kind, edge->kind) &&
        existing->order >= order) order = existing->order + 1;
  }
  return order;
}

static bool std_merge_edge_can_append(const char *kind) {
  static const char *const append_kinds[] = {
    "alias",
    "cImport",
    "choice",
    "const",
    "enum",
    "function",
    "import",
    "interface",
    "method",
    "shape",
  };
  for (size_t i = 0; i < sizeof(append_kinds) / sizeof(append_kinds[0]); i++) {
    if (std_merge_text_eq(kind, append_kinds[i])) return true;
  }
  return false;
}

static void std_merge_copy_node(const ZProgramGraphNode *from, ZProgramGraphNode *to) {
  *to = (ZProgramGraphNode){
    .id = std_merge_strdup(from->id),
    .kind = from->kind,
    .name = std_merge_strdup(from->name),
    .type = std_merge_strdup(from->type),
    .value = std_merge_strdup(from->value),
    .path = std_merge_strdup(from->path),
    .symbol_id = std_merge_strdup(from->symbol_id),
    .type_id = std_merge_strdup(from->type_id),
    .effect_id = std_merge_strdup(from->effect_id),
    .node_hash = std_merge_strdup(from->node_hash),
    .line = from->line,
    .column = from->column,
    .is_public = from->is_public,
    .is_mutable = from->is_mutable,
    .is_static = from->is_static,
    .fallible = from->fallible,
    .export_c = from->export_c,
  };
}

static void std_merge_copy_edge(const ZProgramGraphEdge *from, ZProgramGraphEdge *to) {
  *to = (ZProgramGraphEdge){
    .from = std_merge_strdup(from->from),
    .to = std_merge_strdup(from->to),
    .kind = std_merge_strdup(from->kind),
    .target = from->target,
    .order = from->order,
  };
}

static void std_merge_free_node_fields(ZProgramGraphNode *node) {
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

static void std_merge_free_edge_fields(ZProgramGraphEdge *edge) {
  if (!edge) return;
  free(edge->from);
  free(edge->to);
  free(edge->kind);
  *edge = (ZProgramGraphEdge){0};
}

static bool std_merge_id_in_list(char **ids, size_t len, const char *id) {
  for (size_t i = 0; id && i < len; i++) {
    if (std_merge_text_eq(ids[i], id)) return true;
  }
  return false;
}

static bool std_merge_path_matches_module(const char *path, const ZStdSourceModule *module) {
  return module && path &&
         (std_merge_text_eq(path, module->path) ||
          std_merge_text_eq(std_merge_basename(path), std_merge_basename(module->path)));
}

static void std_merge_remove_module_path_nodes(ZProgramGraph *graph, const ZStdSourceModule *module) {
  if (!graph || !module || graph->node_len == 0) return;
  bool *remove = z_checked_calloc(graph->node_len, sizeof(bool));
  char **removed_ids = z_checked_calloc(graph->node_len, sizeof(char *));
  size_t removed_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!std_merge_path_matches_module(graph->nodes[i].path, module)) continue;
    remove[i] = true;
    removed_ids[removed_len++] = std_merge_strdup(graph->nodes[i].id);
  }
  if (removed_len > 0) {
    size_t edge_write = 0;
    for (size_t i = 0; i < graph->edge_len; i++) {
      bool drop = std_merge_id_in_list(removed_ids, removed_len, graph->edges[i].from) ||
                  (graph->edges[i].target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
                   std_merge_id_in_list(removed_ids, removed_len, graph->edges[i].to));
      if (drop) {
        std_merge_free_edge_fields(&graph->edges[i]);
        continue;
      }
      if (edge_write != i) {
        graph->edges[edge_write] = graph->edges[i];
        graph->edges[i] = (ZProgramGraphEdge){0};
      }
      edge_write++;
    }
    graph->edge_len = edge_write;

    size_t node_write = 0;
    for (size_t i = 0; i < graph->node_len; i++) {
      if (remove[i]) {
        std_merge_free_node_fields(&graph->nodes[i]);
        continue;
      }
      if (node_write != i) {
        graph->nodes[node_write] = graph->nodes[i];
        graph->nodes[i] = (ZProgramGraphNode){0};
      }
      node_write++;
    }
    graph->node_len = node_write;
  }
  for (size_t i = 0; i < removed_len; i++) free(removed_ids[i]);
  free(removed_ids);
  free(remove);
}

static void std_merge_append_node_copy(ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (graph->node_len + 1 > graph->node_cap) {
    graph->node_cap = z_grow_capacity(graph->node_cap, graph->node_len + 1, 64);
    graph->nodes = z_checked_reallocarray(graph->nodes, graph->node_cap, sizeof(ZProgramGraphNode));
  }
  std_merge_copy_node(node, &graph->nodes[graph->node_len++]);
}

static void std_merge_append_node_copy_with_id(ZProgramGraph *graph, const ZProgramGraphNode *node, const char *id) {
  std_merge_append_node_copy(graph, node);
  ZProgramGraphNode *copy = &graph->nodes[graph->node_len - 1];
  free(copy->id);
  copy->id = std_merge_strdup(id);
}

static void std_merge_append_edge_copy(ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  if (graph->edge_len + 1 > graph->edge_cap) {
    graph->edge_cap = z_grow_capacity(graph->edge_cap, graph->edge_len + 1, 64);
    graph->edges = z_checked_reallocarray(graph->edges, graph->edge_cap, sizeof(ZProgramGraphEdge));
  }
  std_merge_copy_edge(edge, &graph->edges[graph->edge_len++]);
}

static char *std_merge_unique_node_id(const ZProgramGraph *graph, const char *base_id) {
  for (size_t suffix = 1; suffix < 1000000; suffix++) {
    ZBuf candidate;
    zbuf_init(&candidate);
    zbuf_append(&candidate, base_id && base_id[0] ? base_id : "#node");
    zbuf_appendf(&candidate, "-merge%zu", suffix);
    if (!std_merge_has_node_id(graph, candidate.data)) return candidate.data ? candidate.data : std_merge_strdup("#node-merge");
    zbuf_free(&candidate);
  }
  return std_merge_strdup(base_id && base_id[0] ? base_id : "#node");
}

static const char *std_merge_mapped_node_id(const ZProgramGraph *module_graph, char **mapped_ids, const char *id) {
  for (size_t i = 0; module_graph && mapped_ids && id && i < module_graph->node_len; i++) {
    if (std_merge_text_eq(module_graph->nodes[i].id, id)) return mapped_ids[i] ? mapped_ids[i] : id;
  }
  return id;
}

static void std_merge_graph(ZProgramGraph *graph, const ZProgramGraph *module_graph) {
  char **mapped_ids = z_checked_calloc(module_graph && module_graph->node_len ? module_graph->node_len : 1, sizeof(char *));
  for (size_t i = 0; graph && module_graph && i < module_graph->node_len; i++) {
    const ZProgramGraphNode *node = &module_graph->nodes[i];
    const ZProgramGraphNode *existing = std_merge_find_node(graph, node->id);
    if (existing && std_merge_node_payload_equal(existing, node)) {
      mapped_ids[i] = std_merge_strdup(existing->id);
      continue;
    }
    if (existing) {
      mapped_ids[i] = std_merge_unique_node_id(graph, node->id);
      std_merge_append_node_copy_with_id(graph, node, mapped_ids[i]);
      continue;
    }
    mapped_ids[i] = std_merge_strdup(node->id);
    std_merge_append_node_copy(graph, node);
  }
  for (size_t i = 0; graph && module_graph && i < module_graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &module_graph->edges[i];
    ZProgramGraphEdge mapped = {
      .from = std_merge_strdup(std_merge_mapped_node_id(module_graph, mapped_ids, edge->from)),
      .to = edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE ?
              std_merge_strdup(std_merge_mapped_node_id(module_graph, mapped_ids, edge->to)) :
              std_merge_strdup(edge->to),
      .kind = std_merge_strdup(edge->kind),
      .target = edge->target,
      .order = edge->order,
    };
    if (std_merge_has_edge(graph, &mapped)) {
      std_merge_free_edge_fields(&mapped);
      continue;
    }
    if (std_merge_has_ordered_edge(graph, &mapped)) {
      if (!std_merge_edge_can_append(mapped.kind)) {
        std_merge_free_edge_fields(&mapped);
        continue;
      }
      mapped.order = std_merge_next_edge_order(graph, &mapped);
    }
    std_merge_append_edge_copy(graph, &mapped);
    std_merge_free_edge_fields(&mapped);
  }
  for (size_t i = 0; module_graph && i < module_graph->node_len; i++) free(mapped_ids[i]);
  free(mapped_ids);
}

static bool std_merge_source_imports_module(const SourceInput *input, const char *module) {
  for (size_t i = 0; input && module && i < input->module_count; i++) {
    if (std_merge_text_eq(input->module_names[i], module)) return true;
  }
  return false;
}

static bool std_merge_module_name_seen(char **items, size_t len, const char *module) {
  for (size_t i = 0; module && i < len; i++) {
    if (std_merge_text_eq(items[i], module)) return true;
  }
  return false;
}

bool z_program_graph_merge_embedded_std_graph_modules(ZProgramGraph *graph, const SourceInput *input, ZDiag *diag) {
  bool merged = false;
  char **merged_modules = z_checked_calloc(input && input->module_count ? input->module_count : 1, sizeof(char *));
  size_t merged_module_len = 0;
  for (size_t i = 0; input && i < input->module_count; i++) {
    const char *module_name = input->module_names[i];
    if (!std_merge_starts_with(module_name, "std.")) continue;
    const ZStdSourceModule *module = z_std_source_module_for_name(module_name);
    if (!module || !std_merge_source_imports_module(input, module->module)) continue;
    if (std_merge_module_name_seen(merged_modules, merged_module_len, module->module)) continue;
    merged_modules[merged_module_len++] = std_merge_strdup(module->module);
    std_merge_remove_module_path_nodes(graph, module);
    ZProgramGraph module_graph = {0};
    bool ok = z_std_source_module_load_graph(module, &module_graph, diag);
    if (!ok) {
      if (diag && !diag->path) diag->path = module->path;
      for (size_t j = 0; j < merged_module_len; j++) free(merged_modules[j]);
      free(merged_modules);
      return false;
    }
    std_merge_graph(graph, &module_graph);
    z_program_graph_free(&module_graph);
    merged = true;
  }
  for (size_t i = 0; i < merged_module_len; i++) free(merged_modules[i]);
  free(merged_modules);
  if (merged) z_program_graph_finalize_identities(graph);
  return true;
}
