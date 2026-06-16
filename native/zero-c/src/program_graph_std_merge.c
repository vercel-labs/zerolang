#include "program_graph_build.h"
#include "program_graph_std_deps.h"
#include "program_graph_std_prune.h"
#include "std_source.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static long long std_merge_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

static bool std_merge_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static char *std_merge_strdup(const char *text) { return text ? z_strdup(text) : NULL; }

typedef struct {
  bool present;
  uint64_t stdlib_fingerprint;
  char *input_graph_hash;
  char *merged_graph_hash;
  ZProgramGraph graph;
  size_t modules_merged;
  size_t nodes_merged;
  size_t edges_merged;
} ZStdMergeCache;

static ZStdMergeCache std_merge_cache;

static bool std_merge_cache_hash_matches(const char *hash) {
  if (!hash || !hash[0] || !std_merge_cache.present) return false;
  return std_merge_text_eq(hash, std_merge_cache.input_graph_hash) ||
         std_merge_text_eq(hash, std_merge_cache.merged_graph_hash);
}

static bool std_merge_cache_try_load(ZProgramGraph *graph, SourceInput *profile) {
  if (!graph || !std_merge_cache_hash_matches(graph->graph_hash)) return false;
  if (std_merge_cache.stdlib_fingerprint != z_std_source_graph_fingerprint()) return false;
  ZProgramGraph clone = {0};
  if (!z_program_graph_clone(&std_merge_cache.graph, &clone)) return false;
  z_program_graph_free(graph);
  *graph = clone;
  if (profile) {
    profile->graph_stdlib_merge_cache_hit = true;
    profile->graph_stdlib_modules_merged = std_merge_cache.modules_merged;
    profile->graph_stdlib_nodes_merged = std_merge_cache.nodes_merged;
    profile->graph_stdlib_edges_merged = std_merge_cache.edges_merged;
  }
  return true;
}

static bool std_merge_cache_store(const char *input_graph_hash, const ZProgramGraph *graph, const SourceInput *profile) {
  if (!input_graph_hash || !input_graph_hash[0] || !graph) return false;
  ZProgramGraph clone = {0};
  if (!z_program_graph_clone(graph, &clone)) return false;
  free(std_merge_cache.input_graph_hash);
  free(std_merge_cache.merged_graph_hash);
  z_program_graph_free(&std_merge_cache.graph);
  std_merge_cache = (ZStdMergeCache){
    .present = true,
    .stdlib_fingerprint = z_std_source_graph_fingerprint(),
    .input_graph_hash = std_merge_strdup(input_graph_hash),
    .merged_graph_hash = std_merge_strdup(graph->graph_hash),
    .graph = clone,
    .modules_merged = profile ? profile->graph_stdlib_modules_merged : 0,
    .nodes_merged = profile ? profile->graph_stdlib_nodes_merged : 0,
    .edges_merged = profile ? profile->graph_stdlib_edges_merged : 0,
  };
  return true;
}

static const char *std_merge_basename(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}

/*
 * Open-addressing string map used to keep the merge linear. Node and edge
 * membership checks previously scanned the whole merged graph per lookup,
 * which made merging the embedded stdlib quadratic in graph size.
 */
typedef struct {
  uint64_t hash;
  char *key;
  size_t value;
  bool used;
} StdMergeMapEntry;

typedef struct {
  StdMergeMapEntry *entries;
  size_t cap;
  size_t len;
} StdMergeMap;

static uint64_t std_merge_hash_text(const char *text) {
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
    hash ^= *p;
    hash *= 1099511628211ULL;
  }
  return hash;
}

static void std_merge_map_init(StdMergeMap *map, size_t expected) {
  size_t cap = 16;
  while (cap < expected * 2) cap *= 2;
  map->entries = z_checked_calloc(cap, sizeof(StdMergeMapEntry));
  map->cap = cap;
  map->len = 0;
}

static void std_merge_map_free(StdMergeMap *map) {
  for (size_t i = 0; map->entries && i < map->cap; i++) {
    if (map->entries[i].used) free(map->entries[i].key);
  }
  free(map->entries);
  *map = (StdMergeMap){0};
}

static StdMergeMapEntry *std_merge_map_slot(const StdMergeMap *map, uint64_t hash, const char *key) {
  size_t index = (size_t)(hash & (map->cap - 1));
  for (;;) {
    StdMergeMapEntry *entry = &map->entries[index];
    if (!entry->used) return entry;
    if (entry->hash == hash && std_merge_text_eq(entry->key, key)) return entry;
    index = (index + 1) & (map->cap - 1);
  }
}

static StdMergeMapEntry *std_merge_map_find(const StdMergeMap *map, const char *key) {
  if (!map->entries || map->len == 0) return NULL;
  StdMergeMapEntry *entry = std_merge_map_slot(map, std_merge_hash_text(key), key);
  return entry->used ? entry : NULL;
}

static void std_merge_map_grow(StdMergeMap *map) {
  StdMergeMap grown;
  std_merge_map_init(&grown, map->cap);
  for (size_t i = 0; i < map->cap; i++) {
    if (!map->entries[i].used) continue;
    StdMergeMapEntry *slot = std_merge_map_slot(&grown, map->entries[i].hash, map->entries[i].key);
    *slot = map->entries[i];
    grown.len++;
  }
  free(map->entries);
  *map = grown;
}

/* Inserts or updates; the map owns a copy of the key. */
static StdMergeMapEntry *std_merge_map_put(StdMergeMap *map, const char *key, size_t value) {
  if (map->len * 2 >= map->cap) std_merge_map_grow(map);
  uint64_t hash = std_merge_hash_text(key);
  StdMergeMapEntry *entry = std_merge_map_slot(map, hash, key);
  if (!entry->used) {
    *entry = (StdMergeMapEntry){.hash = hash, .key = z_strdup(key ? key : ""), .used = true};
    map->len++;
  }
  entry->value = value;
  return entry;
}

static char *std_merge_edge_identity_key(const ZProgramGraphEdge *edge, bool include_to, bool include_order) {
  ZBuf key;
  zbuf_init(&key);
  zbuf_appendf(&key, "%d\x1f%s\x1f%s", (int)edge->target, edge->from ? edge->from : "", edge->kind ? edge->kind : "");
  if (include_to) zbuf_appendf(&key, "\x1f%s", edge->to ? edge->to : "");
  if (include_order) zbuf_appendf(&key, "\x1f%zu", edge->order);
  return key.data ? key.data : z_strdup("");
}

/*
 * Edge membership index: full identity for exact duplicates, slot identity
 * (target+from+kind+order) for occupied-order checks, and a max-order map
 * keyed by target+from+kind for append ordering.
 */
typedef struct {
  StdMergeMap full;
  StdMergeMap slot;
  StdMergeMap next_order;
} StdMergeEdgeIndex;

static void std_merge_edge_index_add(StdMergeEdgeIndex *index, const ZProgramGraphEdge *edge) {
  char *full_key = std_merge_edge_identity_key(edge, true, true);
  char *slot_key = std_merge_edge_identity_key(edge, false, true);
  char *order_key = std_merge_edge_identity_key(edge, false, false);
  std_merge_map_put(&index->full, full_key, 0);
  std_merge_map_put(&index->slot, slot_key, 0);
  StdMergeMapEntry *order_entry = std_merge_map_find(&index->next_order, order_key);
  if (!order_entry || order_entry->value <= edge->order) std_merge_map_put(&index->next_order, order_key, edge->order + 1);
  free(full_key);
  free(slot_key);
  free(order_key);
}

static void std_merge_edge_index_init(StdMergeEdgeIndex *index, const ZProgramGraph *graph) {
  size_t edge_len = graph ? graph->edge_len : 0;
  std_merge_map_init(&index->full, edge_len);
  std_merge_map_init(&index->slot, edge_len);
  std_merge_map_init(&index->next_order, edge_len);
  for (size_t i = 0; i < edge_len; i++) std_merge_edge_index_add(index, &graph->edges[i]);
}

static void std_merge_edge_index_free(StdMergeEdgeIndex *index) {
  std_merge_map_free(&index->full);
  std_merge_map_free(&index->slot);
  std_merge_map_free(&index->next_order);
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

static void std_merge_replace_text(char **field, const char *value) {
  if (!field || std_merge_text_eq(*field, value)) return;
  free(*field);
  *field = std_merge_strdup(value);
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

static void std_merge_canonicalize_module_root(ZProgramGraph *graph, const ZStdSourceModule *module) {
  for (size_t i = 0; graph && module && i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    if (!std_merge_path_matches_module(node->path, module)) continue;
    std_merge_replace_text(&node->path, module->path);
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE) std_merge_replace_text(&node->name, module->module);
  }
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

static char *std_merge_unique_node_id(const StdMergeMap *node_index, const char *base_id) {
  for (size_t suffix = 1; suffix < 1000000; suffix++) {
    ZBuf candidate;
    zbuf_init(&candidate);
    zbuf_append(&candidate, base_id && base_id[0] ? base_id : "#node");
    zbuf_appendf(&candidate, "-merge%zu", suffix);
    if (!std_merge_map_find(node_index, candidate.data)) return candidate.data ? candidate.data : std_merge_strdup("#node-merge");
    zbuf_free(&candidate);
  }
  return std_merge_strdup(base_id && base_id[0] ? base_id : "#node");
}

static const char *std_merge_mapped_node_id(const StdMergeMap *mapped_index, char **mapped_ids, const char *id) {
  const StdMergeMapEntry *entry = id ? std_merge_map_find(mapped_index, id) : NULL;
  if (!entry) return id;
  return mapped_ids[entry->value] ? mapped_ids[entry->value] : id;
}

static void std_merge_graph(ZProgramGraph *graph, const ZProgramGraph *module_graph, SourceInput *profile) {
  size_t module_node_len = module_graph ? module_graph->node_len : 0;
  char **mapped_ids = z_checked_calloc(module_node_len ? module_node_len : 1, sizeof(char *));
  StdMergeMap node_index;
  StdMergeMap mapped_index;
  std_merge_map_init(&node_index, (graph ? graph->node_len : 0) + module_node_len);
  std_merge_map_init(&mapped_index, module_node_len);
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    /* keep the first occurrence to mirror the previous linear scan */
    if (!std_merge_map_find(&node_index, graph->nodes[i].id)) std_merge_map_put(&node_index, graph->nodes[i].id, i);
  }
  long long node_started = std_merge_now_ms();
  for (size_t i = 0; graph && module_graph && i < module_graph->node_len; i++) {
    const ZProgramGraphNode *node = &module_graph->nodes[i];
    if (!std_merge_map_find(&mapped_index, node->id)) std_merge_map_put(&mapped_index, node->id, i);
    const StdMergeMapEntry *existing_entry = std_merge_map_find(&node_index, node->id);
    const ZProgramGraphNode *existing = existing_entry ? &graph->nodes[existing_entry->value] : NULL;
    if (existing && std_merge_node_payload_equal(existing, node)) {
      mapped_ids[i] = std_merge_strdup(existing->id);
      continue;
    }
    if (existing) {
      mapped_ids[i] = std_merge_unique_node_id(&node_index, node->id);
      std_merge_append_node_copy_with_id(graph, node, mapped_ids[i]);
      std_merge_map_put(&node_index, mapped_ids[i], graph->node_len - 1);
      continue;
    }
    mapped_ids[i] = std_merge_strdup(node->id);
    std_merge_append_node_copy(graph, node);
    std_merge_map_put(&node_index, node->id, graph->node_len - 1);
  }
  if (profile) {
    profile->graph_stdlib_node_merge_ms += std_merge_now_ms() - node_started;
    profile->graph_stdlib_nodes_merged += module_graph ? module_graph->node_len : 0;
  }
  long long edge_started = std_merge_now_ms();
  StdMergeEdgeIndex edge_index;
  std_merge_edge_index_init(&edge_index, graph);
  for (size_t i = 0; graph && module_graph && i < module_graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &module_graph->edges[i];
    ZProgramGraphEdge mapped = {
      .from = std_merge_strdup(std_merge_mapped_node_id(&mapped_index, mapped_ids, edge->from)),
      .to = edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE ?
              std_merge_strdup(std_merge_mapped_node_id(&mapped_index, mapped_ids, edge->to)) :
              std_merge_strdup(edge->to),
      .kind = std_merge_strdup(edge->kind),
      .target = edge->target,
      .order = edge->order,
    };
    char *full_key = std_merge_edge_identity_key(&mapped, true, true);
    if (std_merge_map_find(&edge_index.full, full_key)) {
      free(full_key);
      std_merge_free_edge_fields(&mapped);
      continue;
    }
    free(full_key);
    char *slot_key = std_merge_edge_identity_key(&mapped, false, true);
    bool slot_taken = std_merge_map_find(&edge_index.slot, slot_key) != NULL;
    free(slot_key);
    if (slot_taken) {
      if (!std_merge_edge_can_append(mapped.kind)) {
        std_merge_free_edge_fields(&mapped);
        continue;
      }
      char *order_key = std_merge_edge_identity_key(&mapped, false, false);
      const StdMergeMapEntry *order_entry = std_merge_map_find(&edge_index.next_order, order_key);
      free(order_key);
      mapped.order = order_entry ? order_entry->value : 0;
    }
    std_merge_edge_index_add(&edge_index, &mapped);
    std_merge_append_edge_copy(graph, &mapped);
    std_merge_free_edge_fields(&mapped);
  }
  std_merge_edge_index_free(&edge_index);
  if (profile) {
    profile->graph_stdlib_edge_merge_ms += std_merge_now_ms() - edge_started;
    profile->graph_stdlib_edges_merged += module_graph ? module_graph->edge_len : 0;
  }
  std_merge_map_free(&node_index);
  std_merge_map_free(&mapped_index);
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

static bool std_merge_embedded_std_graph_modules_impl(ZProgramGraph *graph, const SourceInput *input, SourceInput *profile, ZDiag *diag) {
  char *input_graph_hash = std_merge_strdup(graph ? graph->graph_hash : NULL);
  if (std_merge_cache_try_load(graph, profile)) {
    size_t cached_node_len = graph ? graph->node_len : 0;
    size_t cached_edge_len = graph ? graph->edge_len : 0;
    z_program_graph_prune_unreachable_std_source_functions(graph);
    if (graph && (graph->node_len != cached_node_len || graph->edge_len != cached_edge_len)) {
      z_program_graph_finalize_identities(graph);
    }
    free(input_graph_hash);
    return true;
  }
  SourceInput profile_start = {0};
  if (profile) {
    profile_start.graph_stdlib_modules_merged = profile->graph_stdlib_modules_merged;
    profile_start.graph_stdlib_nodes_merged = profile->graph_stdlib_nodes_merged;
    profile_start.graph_stdlib_edges_merged = profile->graph_stdlib_edges_merged;
  }
  bool merged = false;
  char **merged_modules = z_checked_calloc(z_std_source_module_count() ? z_std_source_module_count() : 1, sizeof(char *));
  bool *graph_referenced = z_checked_calloc(z_std_source_module_count() ? z_std_source_module_count() : 1, sizeof(bool));
  size_t merged_module_len = 0;
  bool pass_merged = true;
  while (pass_merged) {
    pass_merged = false;
    long long reference_started = std_merge_now_ms();
    z_program_graph_collect_std_module_references(graph, graph_referenced);
    if (profile) profile->graph_stdlib_reference_scan_ms += std_merge_now_ms() - reference_started;
    for (size_t i = 0; i < z_std_source_module_count(); i++) {
      const ZStdSourceModule *module = z_std_source_module_at(i);
      if (!module) continue;
      bool source_imports = std_merge_source_imports_module(input, module->module);
      if (!source_imports && !graph_referenced[i]) continue;
      if (std_merge_module_name_seen(merged_modules, merged_module_len, module->module)) continue;
      merged_modules[merged_module_len++] = std_merge_strdup(module->module);
      if (profile) profile->graph_stdlib_modules_merged++;
      long long cleanup_started = std_merge_now_ms();
      std_merge_remove_module_path_nodes(graph, module);
      if (profile) profile->graph_stdlib_cleanup_ms += std_merge_now_ms() - cleanup_started;
      ZProgramGraph module_graph = {0};
      long long load_started = std_merge_now_ms();
      bool ok = z_std_source_module_load_graph(module, &module_graph, diag);
      if (profile) profile->graph_stdlib_module_load_ms += std_merge_now_ms() - load_started;
      if (!ok) {
        if (diag && !diag->path) diag->path = module->path;
        for (size_t j = 0; j < merged_module_len; j++) free(merged_modules[j]);
        free(merged_modules);
        free(graph_referenced);
        free(input_graph_hash);
        return false;
      }
      std_merge_canonicalize_module_root(&module_graph, module);
      std_merge_graph(graph, &module_graph, profile);
      z_program_graph_free(&module_graph);
      z_program_graph_prune_unreachable_std_source_functions(graph);
      merged = true;
      pass_merged = true;
    }
  }
  for (size_t i = 0; i < merged_module_len; i++) free(merged_modules[i]);
  free(merged_modules);
  free(graph_referenced);
  if (merged) {
    long long finalize_started = std_merge_now_ms();
    z_program_graph_finalize_identities(graph);
    if (profile) profile->graph_stdlib_finalize_ms += std_merge_now_ms() - finalize_started;
    SourceInput cache_profile = {0};
    if (profile) {
      cache_profile.graph_stdlib_modules_merged = profile->graph_stdlib_modules_merged - profile_start.graph_stdlib_modules_merged;
      cache_profile.graph_stdlib_nodes_merged = profile->graph_stdlib_nodes_merged - profile_start.graph_stdlib_nodes_merged;
      cache_profile.graph_stdlib_edges_merged = profile->graph_stdlib_edges_merged - profile_start.graph_stdlib_edges_merged;
    }
    bool stored = std_merge_cache_store(input_graph_hash, graph, profile ? &cache_profile : NULL);
    if (profile) profile->graph_stdlib_merge_cache_stored = stored;
  }
  free(input_graph_hash);
  return true;
}

bool z_program_graph_merge_embedded_std_graph_modules(ZProgramGraph *graph, const SourceInput *input, ZDiag *diag) {
  return std_merge_embedded_std_graph_modules_impl(graph, input, NULL, diag);
}

bool z_program_graph_merge_embedded_std_graph_modules_timed(ZProgramGraph *graph, SourceInput *input, ZDiag *diag) {
  return std_merge_embedded_std_graph_modules_impl(graph, input, input, diag);
}
