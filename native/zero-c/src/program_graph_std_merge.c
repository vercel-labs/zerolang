#include "program_graph_build.h"
#include "program_graph_std_deps.h"
#include "program_graph_std_prune.h"
#include "program_graph_string_map.h"
#include "std_source.h"

#include <stdint.h>
#include <stdio.h>
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

typedef ZProgramGraphStringMapEntry StdMergeMapEntry;
typedef ZProgramGraphStringMap StdMergeMap;

static void std_merge_map_init(StdMergeMap *map, size_t expected) { z_program_graph_string_map_init(map, expected); }
static void std_merge_map_free(StdMergeMap *map) { z_program_graph_string_map_free(map); }
static StdMergeMapEntry *std_merge_map_find(const StdMergeMap *map, const char *key) { return z_program_graph_string_map_find(map, key); }
static StdMergeMapEntry *std_merge_map_put(StdMergeMap *map, const char *key, size_t value) {
  z_program_graph_string_map_put(map, key, value);
  return z_program_graph_string_map_find(map, key);
}

typedef enum {
  STD_MERGE_EDGE_KEY_FULL,
  STD_MERGE_EDGE_KEY_SLOT,
  STD_MERGE_EDGE_KEY_ORDER,
} StdMergeEdgeKeyMode;

typedef struct {
  uint64_t hash;
  char *from, *to, *kind;
  ZProgramGraphEdgeTarget target;
  size_t order;
  size_t value;
  bool used;
} StdMergeEdgeMapEntry;

typedef struct {
  StdMergeEdgeMapEntry *entries;
  size_t cap;
  size_t len;
  StdMergeEdgeKeyMode mode;
} StdMergeEdgeMap;

typedef struct {
  StdMergeEdgeMap full;
  StdMergeEdgeMap slot;
  StdMergeEdgeMap next_order;
} StdMergeEdgeIndex;

static uint64_t std_merge_hash_bytes(uint64_t hash, const void *bytes, size_t len) {
  const unsigned char *p = (const unsigned char *)bytes;
  for (size_t i = 0; p && i < len; i++) {
    hash ^= (uint64_t)p[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static uint64_t std_merge_hash_text(uint64_t hash, const char *text) {
  const unsigned char *p = (const unsigned char *)(text ? text : "");
  while (*p) {
    hash ^= (uint64_t)*p++;
    hash *= 1099511628211ULL;
  }
  hash ^= 0xffu;
  hash *= 1099511628211ULL;
  return hash;
}

static uint64_t std_merge_edge_hash(const ZProgramGraphEdge *edge, StdMergeEdgeKeyMode mode) {
  uint64_t hash = 1469598103934665603ULL;
  size_t target = edge ? (size_t)edge->target : 0;
  size_t order = edge ? edge->order : 0;
  hash = std_merge_hash_bytes(hash, &target, sizeof(target));
  hash = std_merge_hash_text(hash, edge ? edge->from : "");
  hash = std_merge_hash_text(hash, edge ? edge->kind : "");
  if (mode == STD_MERGE_EDGE_KEY_FULL) hash = std_merge_hash_text(hash, edge ? edge->to : "");
  if (mode == STD_MERGE_EDGE_KEY_FULL || mode == STD_MERGE_EDGE_KEY_SLOT) {
    hash = std_merge_hash_bytes(hash, &order, sizeof(order));
  }
  return hash ? hash : 1;
}

static bool std_merge_edge_map_entry_matches(const StdMergeEdgeMapEntry *entry, const ZProgramGraphEdge *edge, StdMergeEdgeKeyMode mode, uint64_t hash) {
  if (!entry || !entry->used || entry->hash != hash || !edge) return false;
  if (entry->target != edge->target) return false;
  if (!std_merge_text_eq(entry->from, edge->from) || !std_merge_text_eq(entry->kind, edge->kind)) return false;
  if (mode == STD_MERGE_EDGE_KEY_FULL && !std_merge_text_eq(entry->to, edge->to)) return false;
  if ((mode == STD_MERGE_EDGE_KEY_FULL || mode == STD_MERGE_EDGE_KEY_SLOT) && entry->order != edge->order) return false;
  return true;
}

static void std_merge_edge_map_init(StdMergeEdgeMap *map, size_t expected, StdMergeEdgeKeyMode mode) {
  size_t cap = 16;
  while (cap < expected * 2) cap *= 2;
  map->entries = z_checked_calloc(cap, sizeof(StdMergeEdgeMapEntry));
  map->cap = cap;
  map->len = 0;
  map->mode = mode;
}

static void std_merge_edge_map_free(StdMergeEdgeMap *map) {
  for (size_t i = 0; map->entries && i < map->cap; i++) {
    if (!map->entries[i].used) continue;
    free(map->entries[i].from);
    free(map->entries[i].to);
    free(map->entries[i].kind);
  }
  free(map->entries);
  *map = (StdMergeEdgeMap){0};
}

static StdMergeEdgeMapEntry *std_merge_edge_map_slot(const StdMergeEdgeMap *map, const ZProgramGraphEdge *edge, uint64_t hash) {
  size_t index = (size_t)(hash & (map->cap - 1));
  for (;;) {
    StdMergeEdgeMapEntry *entry = &map->entries[index];
    if (!entry->used || std_merge_edge_map_entry_matches(entry, edge, map->mode, hash)) return entry;
    index = (index + 1) & (map->cap - 1);
  }
}

static StdMergeEdgeMapEntry *std_merge_edge_map_find(const StdMergeEdgeMap *map, const ZProgramGraphEdge *edge) {
  if (!map || !map->entries || map->len == 0) return NULL;
  uint64_t hash = std_merge_edge_hash(edge, map->mode);
  StdMergeEdgeMapEntry *entry = std_merge_edge_map_slot(map, edge, hash);
  return entry->used ? entry : NULL;
}

static void std_merge_edge_map_grow(StdMergeEdgeMap *map) {
  StdMergeEdgeMap grown;
  std_merge_edge_map_init(&grown, map->cap, map->mode);
  for (size_t i = 0; i < map->cap; i++) {
    if (!map->entries[i].used) continue;
    size_t index = (size_t)(map->entries[i].hash & (grown.cap - 1));
    while (grown.entries[index].used) index = (index + 1) & (grown.cap - 1);
    grown.entries[index] = map->entries[i];
    grown.len++;
  }
  free(map->entries);
  *map = grown;
}

static void std_merge_edge_map_entry_set(StdMergeEdgeMapEntry *entry, const ZProgramGraphEdge *edge, uint64_t hash) {
  *entry = (StdMergeEdgeMapEntry){
    .hash = hash,
    .from = std_merge_strdup(edge ? edge->from : NULL),
    .to = std_merge_strdup(edge ? edge->to : NULL),
    .kind = std_merge_strdup(edge ? edge->kind : NULL),
    .target = edge ? edge->target : Z_PROGRAM_GRAPH_EDGE_TARGET_NODE,
    .order = edge ? edge->order : 0,
    .used = true,
  };
}

static StdMergeEdgeMapEntry *std_merge_edge_map_put(StdMergeEdgeMap *map, const ZProgramGraphEdge *edge, size_t value) {
  if (map->len * 2 >= map->cap) std_merge_edge_map_grow(map);
  uint64_t hash = std_merge_edge_hash(edge, map->mode);
  StdMergeEdgeMapEntry *entry = std_merge_edge_map_slot(map, edge, hash);
  if (!entry->used) {
    std_merge_edge_map_entry_set(entry, edge, hash);
    map->len++;
  }
  entry->value = value;
  return entry;
}

static void std_merge_edge_map_put_max(StdMergeEdgeMap *map, const ZProgramGraphEdge *edge, size_t value) {
  if (map->len * 2 >= map->cap) std_merge_edge_map_grow(map);
  uint64_t hash = std_merge_edge_hash(edge, map->mode);
  StdMergeEdgeMapEntry *entry = std_merge_edge_map_slot(map, edge, hash);
  if (!entry->used) {
    std_merge_edge_map_entry_set(entry, edge, hash);
    map->len++;
  }
  if (entry->value < value) entry->value = value;
}

static void std_merge_edge_index_add(StdMergeEdgeIndex *index, const ZProgramGraphEdge *edge) {
  std_merge_edge_map_put(&index->full, edge, 0);
  std_merge_edge_map_put(&index->slot, edge, 0);
  std_merge_edge_map_put_max(&index->next_order, edge, edge->order + 1);
}

static void std_merge_edge_index_init(StdMergeEdgeIndex *index, const ZProgramGraph *graph) {
  size_t edge_len = graph ? graph->edge_len : 0;
  std_merge_edge_map_init(&index->full, edge_len, STD_MERGE_EDGE_KEY_FULL);
  std_merge_edge_map_init(&index->slot, edge_len, STD_MERGE_EDGE_KEY_SLOT);
  std_merge_edge_map_init(&index->next_order, edge_len, STD_MERGE_EDGE_KEY_ORDER);
  for (size_t i = 0; i < edge_len; i++) std_merge_edge_index_add(index, &graph->edges[i]);
}

static void std_merge_edge_index_free(StdMergeEdgeIndex *index) {
  std_merge_edge_map_free(&index->full);
  std_merge_edge_map_free(&index->slot);
  std_merge_edge_map_free(&index->next_order);
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

static bool std_merge_path_matches_module_loose(const char *path, const ZStdSourceModule *module) {
  return module && z_std_source_module_for_path(path) == module;
}

static const ZStdSourceModule *std_merge_declared_std_module(const ZProgramGraphNode *node) {
  if (!node || node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) return NULL;
  const ZStdSourceModule *module = z_std_source_module_for_path(node->path);
  return module && std_merge_text_eq(node->name, module->module) ? module : NULL;
}

static bool std_merge_declared_std_module_present(const ZProgramGraph *graph, const ZStdSourceModule *module) {
  for (size_t i = 0; graph && module && i < graph->node_len; i++) {
    if (std_merge_declared_std_module(&graph->nodes[i]) == module) return true;
  }
  return false;
}

static bool std_merge_path_has_conflicting_module(const ZProgramGraph *graph, const char *path, const ZStdSourceModule *module) {
  for (size_t i = 0; graph && path && module && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE || !std_merge_text_eq(node->path, path)) continue;
    if (std_merge_declared_std_module(node) != module) return true;
  }
  return false;
}

static bool std_merge_path_belongs_to_declared_std_module(const ZProgramGraph *graph, const char *path, const ZStdSourceModule *module) {
  return module &&
         z_std_source_module_for_path(path) == module &&
         std_merge_declared_std_module_present(graph, module) &&
         !std_merge_path_has_conflicting_module(graph, path, module);
}

static void std_merge_remove_module_path_nodes(ZProgramGraph *graph, const ZStdSourceModule *module) {
  if (!graph || !module || graph->node_len == 0) return;
  bool *remove = z_checked_calloc(graph->node_len, sizeof(bool));
  char **removed_ids = z_checked_calloc(graph->node_len, sizeof(char *));
  size_t removed_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!std_merge_path_belongs_to_declared_std_module(graph, graph->nodes[i].path, module)) continue;
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
    if (!std_merge_path_matches_module_loose(node->path, module)) continue;
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

static ZProgramGraphEdge *std_merge_append_edge_copy(ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  if (graph->edge_len + 1 > graph->edge_cap) {
    graph->edge_cap = z_grow_capacity(graph->edge_cap, graph->edge_len + 1, 64);
    graph->edges = z_checked_reallocarray(graph->edges, graph->edge_cap, sizeof(ZProgramGraphEdge));
  }
  std_merge_copy_edge(edge, &graph->edges[graph->edge_len++]);
  return &graph->edges[graph->edge_len - 1];
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
    const char *mapped_from = std_merge_mapped_node_id(&mapped_index, mapped_ids, edge->from);
    const char *mapped_to = edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE
      ? std_merge_mapped_node_id(&mapped_index, mapped_ids, edge->to)
      : edge->to;
    ZProgramGraphEdge mapped = {
      .from = (char *)mapped_from,
      .to = (char *)mapped_to,
      .kind = (char *)edge->kind,
      .target = edge->target,
      .order = edge->order,
    };
    if (std_merge_edge_map_find(&edge_index.full, &mapped)) continue;
    bool slot_taken = std_merge_edge_map_find(&edge_index.slot, &mapped) != NULL;
    if (slot_taken) {
      if (!std_merge_edge_can_append(mapped.kind)) continue;
      const StdMergeEdgeMapEntry *order_entry = std_merge_edge_map_find(&edge_index.next_order, &mapped);
      mapped.order = order_entry ? order_entry->value : 0;
    }
    ZProgramGraphEdge *stored = std_merge_append_edge_copy(graph, &mapped);
    std_merge_edge_index_add(&edge_index, stored);
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

static size_t std_merge_module_index_for_pointer(const ZStdSourceModule *module) {
  for (size_t i = 0; module && i < z_std_source_module_count(); i++) {
    if (z_std_source_module_at(i) == module) return i;
  }
  return SIZE_MAX;
}

static void std_merge_mark_initial_std_modules(const ZProgramGraph *graph, bool *present) {
  for (size_t i = 0; graph && present && i < graph->node_len; i++) {
    size_t module_index = std_merge_module_index_for_pointer(std_merge_declared_std_module(&graph->nodes[i]));
    if (module_index != SIZE_MAX) present[module_index] = true;
  }
}

static bool std_merge_embedded_std_graph_modules_impl(ZProgramGraph *graph, const SourceInput *input, SourceInput *profile, ZDiag *diag) {
  char *input_graph_hash = std_merge_strdup(graph ? graph->graph_hash : NULL);
  if (std_merge_cache_try_load(graph, profile)) {
    size_t cached_node_len = graph ? graph->node_len : 0;
    size_t cached_edge_len = graph ? graph->edge_len : 0;
    long long prune_started = std_merge_now_ms();
    z_program_graph_prune_unreachable_std_source_functions(graph);
    if (profile) profile->graph_stdlib_prune_ms += std_merge_now_ms() - prune_started;
    if (graph && (graph->node_len != cached_node_len || graph->edge_len != cached_edge_len)) {
      long long identity_started = std_merge_now_ms();
      z_program_graph_finalize_identities(graph);
      if (profile) profile->graph_stdlib_identity_ms += std_merge_now_ms() - identity_started;
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
  bool *initial_module_present = z_checked_calloc(z_std_source_module_count() ? z_std_source_module_count() : 1, sizeof(bool));
  std_merge_mark_initial_std_modules(graph, initial_module_present);
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
      if (initial_module_present[i]) {
        long long cleanup_started = std_merge_now_ms();
        std_merge_remove_module_path_nodes(graph, module);
        if (profile) profile->graph_stdlib_cleanup_ms += std_merge_now_ms() - cleanup_started;
      }
      ZProgramGraph module_graph = {0};
      long long load_started = std_merge_now_ms();
      bool ok = z_std_source_module_load_graph(module, &module_graph, diag);
      if (profile) profile->graph_stdlib_module_load_ms += std_merge_now_ms() - load_started;
      if (!ok) {
        if (diag && !diag->path) diag->path = module->path;
        for (size_t j = 0; j < merged_module_len; j++) free(merged_modules[j]);
        free(merged_modules);
        free(graph_referenced);
        free(initial_module_present);
        free(input_graph_hash);
        return false;
      }
      std_merge_canonicalize_module_root(&module_graph, module);
      std_merge_graph(graph, &module_graph, profile);
      z_program_graph_free(&module_graph);
      merged = true;
      pass_merged = true;
    }
    if (pass_merged) {
      long long prune_started = std_merge_now_ms();
      z_program_graph_prune_unreachable_std_source_functions(graph);
      if (profile) profile->graph_stdlib_prune_ms += std_merge_now_ms() - prune_started;
    }
  }
  for (size_t i = 0; i < merged_module_len; i++) free(merged_modules[i]);
  free(merged_modules);
  free(graph_referenced);
  free(initial_module_present);
  if (merged) {
    long long finalize_started = std_merge_now_ms(), prune_started = std_merge_now_ms();
    z_program_graph_prune_unreachable_std_source_functions(graph);
    if (profile) profile->graph_stdlib_prune_ms += std_merge_now_ms() - prune_started;
    long long identity_started = std_merge_now_ms(); z_program_graph_finalize_identities(graph);
    if (profile) profile->graph_stdlib_identity_ms += std_merge_now_ms() - identity_started;
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
