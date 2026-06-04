#include "program_graph_compare.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_text_cmp(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "");
}

static bool compare_text_eq(const char *left, const char *right) {
  return compare_text_cmp(left, right) == 0;
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

static int compare_size_t_value(size_t left, size_t right) {
  if (left < right) return -1;
  if (left > right) return 1;
  return 0;
}

static bool compare_edge_kind(const ZProgramGraphEdge *edge, const char *kind) {
  return edge && compare_text_eq(edge->kind, kind);
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

static size_t compare_owned_edge_kind_order(const char *kind) {
  for (size_t i = 0; compare_owned_edge_order[i]; i++) {
    if (compare_text_eq(kind, compare_owned_edge_order[i])) return i;
  }
  return compare_missing_index();
}

typedef struct {
  const char *id;
  size_t index;
} CompareIdEntry;

typedef struct {
  size_t from;
  size_t child;
  size_t kind_order;
  size_t order;
} CompareOwnedEdge;

typedef struct {
  const ZProgramGraph *graph;
  CompareIdEntry *node_ids;
  CompareIdEntry *symbol_ids;
  CompareIdEntry *type_ids;
  CompareIdEntry *effect_ids;
  size_t node_id_len;
  size_t symbol_id_len;
  size_t type_id_len;
  size_t effect_id_len;
  bool *declared_type_ref;
  bool *skip_node;
  CompareOwnedEdge *owned_edges;
  size_t owned_edge_len;
  size_t *node_order;
  size_t node_order_len;
  size_t *rank;
  size_t *semantic_edges;
  size_t semantic_edge_len;
} CompareIndex;

typedef struct {
  size_t raw_index;
  size_t from_rank;
  size_t to_rank;
  ZProgramGraphEdgeTarget target;
  size_t order;
  const char *kind;
} CompareEdgeFact;

typedef const char *(*CompareNodeIdGetter)(const ZProgramGraphNode *);

static const char *compare_node_id_value(const ZProgramGraphNode *node) {
  return node ? node->id : NULL;
}

static const char *compare_node_symbol_id_value(const ZProgramGraphNode *node) {
  return node ? node->symbol_id : NULL;
}

static const char *compare_node_type_id_value(const ZProgramGraphNode *node) {
  return node ? node->type_id : NULL;
}

static const char *compare_node_effect_id_value(const ZProgramGraphNode *node) {
  return node ? node->effect_id : NULL;
}

static int compare_id_entry_cmp(const void *left_ptr, const void *right_ptr) {
  const CompareIdEntry *left = left_ptr;
  const CompareIdEntry *right = right_ptr;
  int text = compare_text_cmp(left->id, right->id);
  if (text != 0) return text;
  if (left->index < right->index) return -1;
  if (left->index > right->index) return 1;
  return 0;
}

static CompareIdEntry *compare_build_id_index(const ZProgramGraph *graph, CompareNodeIdGetter getter, size_t *out_len) {
  size_t len = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const char *id = getter(&graph->nodes[i]);
    if (id && id[0] != '\0') len++;
  }
  CompareIdEntry *entries = z_checked_calloc(len ? len : 1, sizeof(CompareIdEntry));
  size_t write = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const char *id = getter(&graph->nodes[i]);
    if (!id || id[0] == '\0') continue;
    entries[write++] = (CompareIdEntry){.id = id, .index = i};
  }
  if (write > 1) qsort(entries, write, sizeof(CompareIdEntry), compare_id_entry_cmp);
  if (out_len) *out_len = write;
  return entries;
}

static size_t compare_lookup_id(const CompareIdEntry *entries, size_t len, const char *id) {
  if (!entries || !id || id[0] == '\0') return compare_missing_index();
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    int cmp = compare_text_cmp(entries[mid].id, id);
    if (cmp < 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  if (low < len && compare_text_eq(entries[low].id, id)) return entries[low].index;
  return compare_missing_index();
}

static size_t compare_index_node_by_id(const CompareIndex *index, const char *id) {
  return compare_lookup_id(index ? index->node_ids : NULL, index ? index->node_id_len : 0, id);
}

static size_t compare_index_node_by_symbol_id(const CompareIndex *index, const char *id) {
  return compare_lookup_id(index ? index->symbol_ids : NULL, index ? index->symbol_id_len : 0, id);
}

static size_t compare_index_node_by_type_id(const CompareIndex *index, const char *id) {
  return compare_lookup_id(index ? index->type_ids : NULL, index ? index->type_id_len : 0, id);
}

static size_t compare_index_node_by_effect_id(const CompareIndex *index, const char *id) {
  return compare_lookup_id(index ? index->effect_ids : NULL, index ? index->effect_id_len : 0, id);
}

static bool compare_index_has_module_named(const CompareIndex *index, const char *name) {
  const ZProgramGraph *graph = index ? index->graph : NULL;
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && compare_text_eq(node->name, name)) return true;
  }
  return false;
}

static void compare_index_build_skip_nodes(CompareIndex *index) {
  const ZProgramGraph *graph = index ? index->graph : NULL;
  size_t len = graph && graph->node_len ? graph->node_len : 1;
  index->declared_type_ref = z_checked_calloc(len, sizeof(bool));
  index->skip_node = z_checked_calloc(len, sizeof(bool));
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !compare_edge_kind(edge, "declaredType")) continue;
    size_t target = compare_index_node_by_id(index, edge->to);
    if (target != compare_missing_index()) index->declared_type_ref[target] = true;
  }
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    bool declared_type_ref = node->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF && index->declared_type_ref[i];
    bool std_import = node->name && strncmp(node->name, "std.", strlen("std.")) == 0;
    bool local_import = node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT && !std_import && compare_index_has_module_named(index, node->name);
    index->skip_node[i] = declared_type_ref || local_import;
  }
}

static bool compare_index_skip_node(const CompareIndex *index, size_t node_index) {
  const ZProgramGraph *graph = index ? index->graph : NULL;
  return graph && node_index < graph->node_len && index->skip_node && index->skip_node[node_index];
}

static bool compare_index_skip_edge(const CompareIndex *index, const ZProgramGraphEdge *edge) {
  if (!index || !index->graph || !edge) return false;
  size_t source = compare_index_node_by_id(index, edge->from);
  if (source != compare_missing_index() && compare_index_skip_node(index, source)) return true;
  if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) return false;
  size_t target = compare_index_node_by_id(index, edge->to);
  return target != compare_missing_index() && compare_index_skip_node(index, target);
}

static int compare_owned_edge_cmp(const void *left_ptr, const void *right_ptr) {
  const CompareOwnedEdge *left = left_ptr;
  const CompareOwnedEdge *right = right_ptr;
  int cmp = compare_size_t_value(left->from, right->from);
  if (cmp != 0) return cmp;
  cmp = compare_size_t_value(left->kind_order, right->kind_order);
  if (cmp != 0) return cmp;
  cmp = compare_size_t_value(left->order, right->order);
  if (cmp != 0) return cmp;
  return compare_size_t_value(left->child, right->child);
}

static void compare_index_build_owned_edges(CompareIndex *index) {
  const ZProgramGraph *graph = index ? index->graph : NULL;
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || compare_owned_edge_kind_order(edge->kind) == compare_missing_index()) continue;
    if (compare_index_node_by_id(index, edge->from) == compare_missing_index()) continue;
    if (compare_index_node_by_id(index, edge->to) == compare_missing_index()) continue;
    count++;
  }
  index->owned_edges = z_checked_calloc(count ? count : 1, sizeof(CompareOwnedEdge));
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
    size_t kind_order = compare_owned_edge_kind_order(edge->kind);
    if (kind_order == compare_missing_index()) continue;
    size_t from = compare_index_node_by_id(index, edge->from);
    size_t child = compare_index_node_by_id(index, edge->to);
    if (from == compare_missing_index() || child == compare_missing_index()) continue;
    index->owned_edges[index->owned_edge_len++] = (CompareOwnedEdge){
      .from = from,
      .child = child,
      .kind_order = kind_order,
      .order = edge->order,
    };
  }
  if (index->owned_edge_len > 1) qsort(index->owned_edges, index->owned_edge_len, sizeof(CompareOwnedEdge), compare_owned_edge_cmp);
}

static size_t compare_index_first_owned_edge(const CompareIndex *index, size_t from, size_t kind_order) {
  size_t low = 0;
  size_t high = index ? index->owned_edge_len : 0;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    const CompareOwnedEdge *edge = &index->owned_edges[mid];
    if (edge->from < from || (edge->from == from && edge->kind_order < kind_order)) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

static void compare_index_push_semantic_node(const CompareIndex *index, size_t node_index, bool *visited, size_t *order, size_t *len) {
  const ZProgramGraph *graph = index ? index->graph : NULL;
  if (!graph || node_index >= graph->node_len || visited[node_index]) return;
  visited[node_index] = true;
  if (!compare_index_skip_node(index, node_index)) order[(*len)++] = node_index;
  for (size_t kind_order = 0; compare_owned_edge_order[kind_order]; kind_order++) {
    size_t edge_index = compare_index_first_owned_edge(index, node_index, kind_order);
    while (index &&
           edge_index < index->owned_edge_len &&
           index->owned_edges[edge_index].from == node_index &&
           index->owned_edges[edge_index].kind_order == kind_order) {
      size_t child = index->owned_edges[edge_index].child;
      if (!visited[child]) compare_index_push_semantic_node(index, child, visited, order, len);
      edge_index++;
    }
  }
}

static void compare_index_build_node_order(CompareIndex *index) {
  const ZProgramGraph *graph = index ? index->graph : NULL;
  size_t node_len = graph && graph->node_len ? graph->node_len : 1;
  index->node_order = z_checked_calloc(node_len, sizeof(size_t));
  index->rank = z_checked_calloc(node_len, sizeof(size_t));
  bool *visited = z_checked_calloc(node_len, sizeof(bool));
  for (size_t i = 0; graph && i < graph->node_len; i++) index->rank[i] = compare_missing_index();
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_MODULE) compare_index_push_semantic_node(index, i, visited, index->node_order, &index->node_order_len);
  }
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (!visited[i]) compare_index_push_semantic_node(index, i, visited, index->node_order, &index->node_order_len);
  }
  for (size_t i = 0; i < index->node_order_len; i++) {
    if (index->node_order[i] < (graph ? graph->node_len : 0)) index->rank[index->node_order[i]] = i;
  }
  free(visited);
}

static size_t compare_index_semantic_rank_for_raw_node(const CompareIndex *index, size_t raw_index) {
  const ZProgramGraph *graph = index ? index->graph : NULL;
  if (!graph || !index->rank || raw_index >= graph->node_len || compare_index_skip_node(index, raw_index)) return compare_missing_index();
  return index->rank[raw_index];
}

static void compare_index_build_semantic_edges(CompareIndex *index) {
  const ZProgramGraph *graph = index ? index->graph : NULL;
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    if (!compare_index_skip_edge(index, &graph->edges[i])) count++;
  }
  index->semantic_edges = z_checked_calloc(count ? count : 1, sizeof(size_t));
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    if (!compare_index_skip_edge(index, &graph->edges[i])) index->semantic_edges[index->semantic_edge_len++] = i;
  }
}

static size_t compare_index_target_rank(const CompareIndex *index, const ZProgramGraphEdge *edge) {
  if (!edge) return compare_missing_index();
  size_t raw_index = compare_missing_index();
  switch (edge->target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
      raw_index = compare_index_node_by_id(index, edge->to);
      break;
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
      raw_index = compare_index_node_by_symbol_id(index, edge->to);
      break;
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
      raw_index = compare_index_node_by_type_id(index, edge->to);
      break;
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
      raw_index = compare_index_node_by_effect_id(index, edge->to);
      break;
  }
  return compare_index_semantic_rank_for_raw_node(index, raw_index);
}

static void compare_index_init(CompareIndex *index, const ZProgramGraph *graph) {
  *index = (CompareIndex){.graph = graph};
  index->node_ids = compare_build_id_index(graph, compare_node_id_value, &index->node_id_len);
  index->symbol_ids = compare_build_id_index(graph, compare_node_symbol_id_value, &index->symbol_id_len);
  index->type_ids = compare_build_id_index(graph, compare_node_type_id_value, &index->type_id_len);
  index->effect_ids = compare_build_id_index(graph, compare_node_effect_id_value, &index->effect_id_len);
  compare_index_build_skip_nodes(index);
  compare_index_build_owned_edges(index);
  compare_index_build_node_order(index);
  compare_index_build_semantic_edges(index);
}

static void compare_index_free(CompareIndex *index) {
  if (!index) return;
  free(index->node_ids);
  free(index->symbol_ids);
  free(index->type_ids);
  free(index->effect_ids);
  free(index->declared_type_ref);
  free(index->skip_node);
  free(index->owned_edges);
  free(index->node_order);
  free(index->rank);
  free(index->semantic_edges);
  *index = (CompareIndex){0};
}

static int compare_edge_fact_cmp(const void *left_ptr, const void *right_ptr) {
  const CompareEdgeFact *left = left_ptr;
  const CompareEdgeFact *right = right_ptr;
  int cmp = compare_size_t_value(left->from_rank, right->from_rank);
  if (cmp != 0) return cmp;
  cmp = compare_size_t_value(left->to_rank, right->to_rank);
  if (cmp != 0) return cmp;
  cmp = compare_size_t_value((size_t)left->target, (size_t)right->target);
  if (cmp != 0) return cmp;
  cmp = compare_size_t_value(left->order, right->order);
  if (cmp != 0) return cmp;
  cmp = compare_text_cmp(left->kind, right->kind);
  if (cmp != 0) return cmp;
  return compare_size_t_value(left->raw_index, right->raw_index);
}

static bool compare_edge_fact_eq(const CompareEdgeFact *left, const CompareEdgeFact *right) {
  return left &&
         right &&
         left->from_rank == right->from_rank &&
         left->to_rank == right->to_rank &&
         left->target == right->target &&
         left->order == right->order &&
         compare_text_eq(left->kind, right->kind);
}

static CompareEdgeFact *compare_build_edge_facts(const CompareIndex *index) {
  const ZProgramGraph *graph = index ? index->graph : NULL;
  size_t count = index ? index->semantic_edge_len : 0;
  CompareEdgeFact *facts = z_checked_calloc(count ? count : 1, sizeof(CompareEdgeFact));
  for (size_t i = 0; graph && index && i < count; i++) {
    size_t raw_index = index->semantic_edges[i];
    const ZProgramGraphEdge *edge = &graph->edges[raw_index];
    size_t from_raw = compare_index_node_by_id(index, edge->from);
    facts[i] = (CompareEdgeFact){
      .raw_index = raw_index,
      .from_rank = compare_index_semantic_rank_for_raw_node(index, from_raw),
      .to_rank = compare_index_target_rank(index, edge),
      .target = edge->target,
      .order = edge->order,
      .kind = edge->kind,
    };
  }
  if (count > 1) qsort(facts, count, sizeof(CompareEdgeFact), compare_edge_fact_cmp);
  return facts;
}

static bool compare_node_text_field(const char *field, const char *left, const char *right, size_t left_index, size_t right_index, ZProgramGraphCompare *out) {
  if (compare_text_eq(left, right)) return true;
  return compare_fail(out, "GRC004", "node semantic field differs", field, left_index, right_index, 0, 0);
}

static bool compare_node_bool_field(const char *field, bool left, bool right, size_t left_index, size_t right_index, ZProgramGraphCompare *out) {
  if (left == right) return true;
  return compare_fail(out, "GRC005", "node flag differs", field, left_index, right_index, 0, 0);
}

static bool compare_nodes(const CompareIndex *left, const CompareIndex *right, ZProgramGraphCompare *out) {
  const ZProgramGraph *left_graph = left ? left->graph : NULL;
  const ZProgramGraph *right_graph = right ? right->graph : NULL;
  size_t left_count = left ? left->node_order_len : 0;
  size_t right_count = right ? right->node_order_len : 0;
  if (left_count != right_count) {
    return compare_fail(out, "GRC002", "node count differs", "nodes", 0, 0, left_count, right_count);
  }
  for (size_t i = 0; i < left_count; i++) {
    size_t left_index = left->node_order[i];
    size_t right_index = right->node_order[i];
    const ZProgramGraphNode *left_node = &left_graph->nodes[left_index];
    const ZProgramGraphNode *right_node = &right_graph->nodes[right_index];
    if (left_node->kind != right_node->kind) {
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
      return false;
    }
  }
  return true;
}

static bool compare_edges(const CompareIndex *left, const CompareIndex *right, ZProgramGraphCompare *out) {
  size_t left_count = left ? left->semantic_edge_len : 0;
  size_t right_count = right ? right->semantic_edge_len : 0;
  if (left_count != right_count) {
    return compare_fail(out, "GRC006", "edge count differs", "edges", 0, 0, left_count, right_count);
  }
  CompareEdgeFact *left_facts = compare_build_edge_facts(left);
  CompareEdgeFact *right_facts = compare_build_edge_facts(right);
  for (size_t i = 0; i < left_count; i++) {
    if (!compare_edge_fact_eq(&left_facts[i], &right_facts[i])) {
      size_t left_index = left_facts[i].raw_index;
      size_t right_index = right_facts[i].raw_index;
      size_t left_from = left_facts[i].from_rank;
      size_t left_to = left_facts[i].to_rank;
      free(left_facts);
      free(right_facts);
      return compare_fail(out, "GRC007", "edge semantic fact differs", "edge", left_index, right_index, left_from, left_to);
    }
  }
  free(left_facts);
  free(right_facts);
  return true;
}

bool z_program_graph_semantic_compare(const ZProgramGraph *left, const ZProgramGraph *right, ZProgramGraphCompare *out) {
  if (out) *out = (ZProgramGraphCompare){.ok = true};
  if (!left || !right) return compare_fail(out, "GRC001", "program graph is missing", "graph", 0, 0, left ? 1 : 0, right ? 1 : 0);
  CompareIndex left_index;
  CompareIndex right_index;
  compare_index_init(&left_index, left);
  compare_index_init(&right_index, right);
  if (out) {
    out->left_semantic_nodes = left_index.node_order_len;
    out->right_semantic_nodes = right_index.node_order_len;
    out->left_semantic_edges = left_index.semantic_edge_len;
    out->right_semantic_edges = right_index.semantic_edge_len;
  }
  bool ok = true;
  if (left->schema_version != right->schema_version) {
    ok = compare_fail(out, "GRC012", "schema version differs", "schemaVersion", 0, 0, left->schema_version, right->schema_version);
  } else if (!compare_text_eq(left->module_identity, right->module_identity)) {
    ok = compare_fail(out, "GRC013", "module identity differs", "moduleIdentity", 0, 0, 0, 0);
  } else if (!compare_nodes(&left_index, &right_index, out)) {
    ok = false;
  } else if (!compare_edges(&left_index, &right_index, out)) {
    ok = false;
  }
  compare_index_free(&left_index);
  compare_index_free(&right_index);
  if (ok && out) out->ok = true;
  return ok;
}

static void diff_append_entry(ZProgramGraphDiff *diff, ZProgramGraphDiffEntry entry) {
  if (!diff) return;
  if (diff->diff_len >= diff->diff_cap) {
    size_t new_cap = diff->diff_cap ? diff->diff_cap * 2 : 4;
    ZProgramGraphDiffEntry *new_entries = z_checked_reallocarray(diff->diffs, new_cap, sizeof(ZProgramGraphDiffEntry));
    diff->diffs = new_entries;
    diff->diff_cap = new_cap;
  }
  diff->diffs[diff->diff_len++] = entry;
}

static void diff_emit_modified(ZProgramGraphDiff *diff, const char *node_id, const char *node_kind, const char *field, const char *left_value, const char *right_value) {
  if (!diff) return;
  ZProgramGraphDiffEntry entry = {
    .kind = Z_PROGRAM_GRAPH_DIFF_KIND_MODIFIED,
  };
  if (node_id) snprintf(entry.node_id, sizeof(entry.node_id), "%s", node_id);
  if (node_kind) snprintf(entry.node_kind, sizeof(entry.node_kind), "%s", node_kind);
  if (field) snprintf(entry.field, sizeof(entry.field), "%s", field);
  if (left_value) snprintf(entry.left_value, sizeof(entry.left_value), "%s", left_value);
  if (right_value) snprintf(entry.right_value, sizeof(entry.right_value), "%s", right_value);
  diff_append_entry(diff, entry);
}

static void diff_emit_added(ZProgramGraphDiff *diff, const char *node_id, const char *node_kind, const char *name) {
  if (!diff) return;
  ZProgramGraphDiffEntry entry = {
    .kind = Z_PROGRAM_GRAPH_DIFF_KIND_ADDED,
  };
  if (node_id) snprintf(entry.node_id, sizeof(entry.node_id), "%s", node_id);
  if (node_kind) snprintf(entry.node_kind, sizeof(entry.node_kind), "%s", node_kind);
  if (name) snprintf(entry.field, sizeof(entry.field), "%s", "name");
  if (name) snprintf(entry.left_value, sizeof(entry.left_value), "%s", "");
  if (name) snprintf(entry.right_value, sizeof(entry.right_value), "%s", name);
  diff_append_entry(diff, entry);
}

static void diff_emit_removed(ZProgramGraphDiff *diff, const char *node_id, const char *node_kind, const char *name) {
  if (!diff) return;
  ZProgramGraphDiffEntry entry = {
    .kind = Z_PROGRAM_GRAPH_DIFF_KIND_REMOVED,
  };
  if (node_id) snprintf(entry.node_id, sizeof(entry.node_id), "%s", node_id);
  if (node_kind) snprintf(entry.node_kind, sizeof(entry.node_kind), "%s", node_kind);
  if (name) snprintf(entry.field, sizeof(entry.field), "%s", "name");
  if (name) snprintf(entry.left_value, sizeof(entry.left_value), "%s", name);
  if (name) snprintf(entry.right_value, sizeof(entry.right_value), "%s", "");
  diff_append_entry(diff, entry);
}

static void diff_compare_node_fields(
  ZProgramGraphDiff *diff,
  const ZProgramGraphNode *left_node,
  const ZProgramGraphNode *right_node,
  const char *node_id,
  const char *node_kind
) {
  if (!left_node || !right_node) return;
  if (!compare_text_eq(left_node->name, right_node->name))
    diff_emit_modified(diff, node_id, node_kind, "name", left_node->name, right_node->name);
  if (!compare_text_eq(left_node->type, right_node->type))
    diff_emit_modified(diff, node_id, node_kind, "type", left_node->type, right_node->type);
  if (!compare_text_eq(left_node->value, right_node->value))
    diff_emit_modified(diff, node_id, node_kind, "value", left_node->value, right_node->value);
  if (left_node->is_public != right_node->is_public)
    diff_emit_modified(diff, node_id, node_kind, "public", left_node->is_public ? "true" : "false", right_node->is_public ? "true" : "false");
  if (left_node->is_mutable != right_node->is_mutable)
    diff_emit_modified(diff, node_id, node_kind, "mutable", left_node->is_mutable ? "true" : "false", right_node->is_mutable ? "true" : "false");
  if (left_node->is_static != right_node->is_static)
    diff_emit_modified(diff, node_id, node_kind, "static", left_node->is_static ? "true" : "false", right_node->is_static ? "true" : "false");
  if (left_node->fallible != right_node->fallible)
    diff_emit_modified(diff, node_id, node_kind, "fallible", left_node->fallible ? "true" : "false", right_node->fallible ? "true" : "false");
  if (left_node->export_c != right_node->export_c)
    diff_emit_modified(diff, node_id, node_kind, "exportC", left_node->export_c ? "true" : "false", right_node->export_c ? "true" : "false");
}

static void diff_build_from_indices(
  ZProgramGraphDiff *diff,
  const CompareIndex *left_index,
  const CompareIndex *right_index
) {
  const ZProgramGraph *left_graph = left_index ? left_index->graph : NULL;
  const ZProgramGraph *right_graph = right_index ? right_index->graph : NULL;

  diff->left_node_count = left_index ? left_index->node_order_len : 0;
  diff->right_node_count = right_index ? right_index->node_order_len : 0;

  if (!left_graph || !right_graph) return;

  typedef struct {
    size_t left_raw;
    size_t right_raw;
    bool matched;
  } NodePair;

  NodePair *pairs = z_checked_calloc(left_graph->node_len + right_graph->node_len + 1, sizeof(NodePair));
  size_t pair_count = 0;

  for (size_t li = 0; left_index && li < left_index->node_order_len; li++) {
    size_t left_raw = left_index->node_order[li];
    if (compare_index_skip_node(left_index, left_raw)) continue;

    size_t right_raw = compare_index_node_by_id(right_index, left_graph->nodes[left_raw].id);
    if (right_raw != compare_missing_index()) {
      pairs[pair_count].left_raw = left_raw;
      pairs[pair_count].right_raw = right_raw;
      pairs[pair_count].matched = true;
      pair_count++;
    }
  }

  for (size_t ri = 0; right_index && ri < right_index->node_order_len; ri++) {
    size_t right_raw = right_index->node_order[ri];
    if (compare_index_skip_node(right_index, right_raw)) continue;

    bool found = false;
    for (size_t p = 0; p < pair_count; p++) {
      if (pairs[p].matched && pairs[p].right_raw == right_raw) {
        found = true;
        break;
      }
    }
    if (!found) {
      const ZProgramGraphNode *rnode = &right_graph->nodes[right_raw];
      diff_emit_added(diff, rnode->id, z_program_graph_node_kind_name(rnode->kind), rnode->name);
    }
  }

  for (size_t li = 0; left_index && li < left_index->node_order_len; li++) {
    size_t left_raw = left_index->node_order[li];
    if (compare_index_skip_node(left_index, left_raw)) continue;

    bool found = false;
    size_t right_raw = compare_missing_index();
    for (size_t p = 0; p < pair_count; p++) {
      if (pairs[p].matched && pairs[p].left_raw == left_raw) {
        found = true;
        right_raw = pairs[p].right_raw;
        break;
      }
    }
    if (!found) {
      const ZProgramGraphNode *lnode = &left_graph->nodes[left_raw];
      diff_emit_removed(diff, lnode->id, z_program_graph_node_kind_name(lnode->kind), lnode->name);
    } else {
      const ZProgramGraphNode *lnode = &left_graph->nodes[left_raw];
      const ZProgramGraphNode *rnode = &right_graph->nodes[right_raw];
      diff_compare_node_fields(diff, lnode, rnode, lnode->id, z_program_graph_node_kind_name(lnode->kind));
    }
  }

  free(pairs);
}

bool z_program_graph_diff(const ZProgramGraph *left, const ZProgramGraph *right, ZProgramGraphDiff *out) {
  if (out) *out = (ZProgramGraphDiff){.ok = true};
  if (!left || !right) {
    if (out) out->ok = false;
    return false;
  }

  CompareIndex left_index;
  CompareIndex right_index;
  compare_index_init(&left_index, left);
  compare_index_init(&right_index, right);

  diff_build_from_indices(out, &left_index, &right_index);

  compare_index_free(&left_index);
  compare_index_free(&right_index);

  if (out) out->ok = true;
  return true;
}

void z_program_graph_diff_free(ZProgramGraphDiff *diff) {
  if (!diff) return;
  free(diff->diffs);
  *diff = (ZProgramGraphDiff){0};
}
