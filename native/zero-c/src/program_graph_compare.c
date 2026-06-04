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

static bool compare_edge_kind_is_commutative_list(const char *kind) {
  static const char *const commutative_kinds[] = {
    "import",
    "cImport",
    "param",
    "field",
    "arg",
    NULL,
  };
  for (size_t i = 0; commutative_kinds[i]; i++) {
    if (compare_text_eq(kind, commutative_kinds[i])) return true;
  }
  return false;
}

static bool node_id_is_ancestor_of(const ZProgramGraph *graph, const char *ancestor_id, const char *descendant_id);
static void merge_append_conflict(ZProgramGraphMergeReport *report, ZProgramGraphMergeConflictEntry entry);

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

typedef struct {
  const char *node_id;
  const char *base_symbol_id;
  const char *new_symbol_id;
  const char *base_name;
  const char *new_name;
} MoveInfo;

typedef struct {
  MoveInfo *moves;
  size_t move_len;
  size_t move_cap;
} MoveTracker;

static bool node_id_is_ancestor_of(const ZProgramGraph *graph, const char *ancestor_id, const char *descendant_id);
static void merge_append_conflict(ZProgramGraphMergeReport *report, ZProgramGraphMergeConflictEntry entry);

static void move_tracker_init(MoveTracker *tracker) {
  if (!tracker) return;
  tracker->moves = NULL;
  tracker->move_len = 0;
  tracker->move_cap = 0;
}

static void move_tracker_free(MoveTracker *tracker) {
  if (!tracker) return;
  free(tracker->moves);
  tracker->moves = NULL;
  tracker->move_len = 0;
  tracker->move_cap = 0;
}

static void move_tracker_append(MoveTracker *tracker, const MoveInfo *info) {
  if (!tracker || !info) return;
  if (tracker->move_len >= tracker->move_cap) {
    size_t new_cap = tracker->move_cap ? tracker->move_cap * 2 : 4;
    MoveInfo *new_moves = z_checked_reallocarray(tracker->moves, new_cap, sizeof(MoveInfo));
    tracker->moves = new_moves;
    tracker->move_cap = new_cap;
  }
  tracker->moves[tracker->move_len++] = *info;
}

static const MoveInfo *move_tracker_find_by_base_symbol_id(const MoveTracker *tracker, const char *base_symbol_id) {
  if (!tracker || !base_symbol_id) return NULL;
  for (size_t i = 0; i < tracker->move_len; i++) {
    if (compare_text_eq(tracker->moves[i].base_symbol_id, base_symbol_id)) {
      return &tracker->moves[i];
    }
  }
  return NULL;
}

static const MoveInfo *move_tracker_find_by_new_symbol_id(const MoveTracker *tracker, const char *new_symbol_id) {
  if (!tracker || !new_symbol_id) return NULL;
  for (size_t i = 0; i < tracker->move_len; i++) {
    if (compare_text_eq(tracker->moves[i].new_symbol_id, new_symbol_id)) {
      return &tracker->moves[i];
    }
  }
  return NULL;
}

static void move_tracker_build(MoveTracker *tracker, const ZProgramGraph *base, const ZProgramGraph *derived) {
  if (!tracker || !base || !derived) return;
  move_tracker_init(tracker);

  for (size_t i = 0; i < derived->node_len; i++) {
    const ZProgramGraphNode *derived_node = &derived->nodes[i];
    if (!derived_node->id || !derived_node->symbol_id) continue;

    size_t base_idx = compare_missing_index();
    for (size_t j = 0; j < base->node_len; j++) {
      if (compare_text_eq(base->nodes[j].id, derived_node->id)) {
        base_idx = j;
        break;
      }
    }

    if (base_idx == compare_missing_index()) continue;

    const ZProgramGraphNode *base_node = &base->nodes[base_idx];
    if (!compare_text_eq(base_node->symbol_id, derived_node->symbol_id)) {
      MoveInfo info = {
        .node_id = derived_node->id,
        .base_symbol_id = base_node->symbol_id,
        .new_symbol_id = derived_node->symbol_id,
        .base_name = base_node->name,
        .new_name = derived_node->name,
      };
      move_tracker_append(tracker, &info);
    }
  }
}

static const char *graph_get_symbol_id_at_node(const ZProgramGraph *graph, const char *node_id) {
  if (!graph || !node_id) return NULL;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (compare_text_eq(graph->nodes[i].id, node_id)) {
      return graph->nodes[i].symbol_id;
    }
  }
  return NULL;
}

static bool check_parent_moved_child_edited(
  const ZProgramGraph *base,
  const ZProgramGraph *ours,
  const ZProgramGraph *theirs,
  const ZProgramGraphDiffEntry *our_entry,
  const ZProgramGraphDiffEntry *their_entry,
  MoveTracker *our_moves,
  MoveTracker *their_moves
) {
  if (!base || !ours || !theirs || !our_entry || !their_entry) return false;

  if (our_entry->kind != Z_PROGRAM_GRAPH_DIFF_KIND_MODIFIED || their_entry->kind != Z_PROGRAM_GRAPH_DIFF_KIND_MODIFIED) {
    return false;
  }

  if (compare_text_eq(our_entry->node_id, their_entry->node_id)) {
    return false;
  }

  for (size_t i = 0; i < theirs->node_len; i++) {
    const ZProgramGraphNode *theirs_node = &theirs->nodes[i];
    if (!compare_text_eq(theirs_node->id, our_entry->node_id)) continue;

    const char *theirs_base_symbol_id = graph_get_symbol_id_at_node(base, our_entry->node_id);
    if (!theirs_base_symbol_id) continue;

    const MoveInfo *their_move = move_tracker_find_by_base_symbol_id(their_moves, theirs_base_symbol_id);
    if (!their_move) continue;

    if (node_id_is_ancestor_of(ours, their_move->node_id, our_entry->node_id)) {
      return true;
    }
  }

  for (size_t i = 0; i < ours->node_len; i++) {
    const ZProgramGraphNode *ours_node = &ours->nodes[i];
    if (!compare_text_eq(ours_node->id, their_entry->node_id)) continue;

    const char *ours_base_symbol_id = graph_get_symbol_id_at_node(base, their_entry->node_id);
    if (!ours_base_symbol_id) continue;

    const MoveInfo *our_move = move_tracker_find_by_base_symbol_id(our_moves, ours_base_symbol_id);
    if (!our_move) continue;

    if (node_id_is_ancestor_of(theirs, our_move->node_id, their_entry->node_id)) {
      return true;
    }
  }

  return false;
}

static void check_move_move_cycle_and_add_conflicts(
  const ZProgramGraph *base,
  const ZProgramGraph *ours,
  const ZProgramGraph *theirs,
  MoveTracker *our_moves,
  MoveTracker *their_moves,
  ZProgramGraphMergeReport *report
) {
  if (!base || !ours || !theirs || !our_moves || !their_moves) return;

  for (size_t i = 0; i < our_moves->move_len; i++) {
    const MoveInfo *our_move = &our_moves->moves[i];

    const MoveInfo *their_move = move_tracker_find_by_base_symbol_id(their_moves, our_move->base_symbol_id);
    if (!their_move) continue;

    const MoveInfo *reverse_our_move = move_tracker_find_by_new_symbol_id(our_moves, their_move->new_symbol_id);
    const MoveInfo *reverse_their_move = move_tracker_find_by_new_symbol_id(their_moves, our_move->new_symbol_id);

    if (reverse_our_move && reverse_their_move) {
      ZProgramGraphMergeConflictEntry conflict = {
        .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_MOVE_MOVE_CYCLE,
      };
      snprintf(conflict.code, sizeof(conflict.code), "GMG006");
      snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", our_move->node_id);
      snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", "");
      snprintf(conflict.name, sizeof(conflict.name), "%s", our_move->new_name ? our_move->new_name : "");
      snprintf(conflict.field, sizeof(conflict.field), "%s", "symbol_id");
      snprintf(conflict.our_move, sizeof(conflict.our_move), "%s->%s",
               our_move->base_symbol_id ? our_move->base_symbol_id : "",
               our_move->new_symbol_id ? our_move->new_symbol_id : "");
      snprintf(conflict.their_move, sizeof(conflict.their_move), "%s->%s",
               their_move->base_symbol_id ? their_move->base_symbol_id : "",
               their_move->new_symbol_id ? their_move->new_symbol_id : "");
      snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", our_move->new_symbol_id ? our_move->new_symbol_id : "");
      snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", their_move->new_symbol_id ? their_move->new_symbol_id : "");
      snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", our_move->base_symbol_id ? our_move->base_symbol_id : "");
      merge_append_conflict(report, conflict);
    }
  }
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

static void merge_append_conflict(ZProgramGraphMergeReport *report, ZProgramGraphMergeConflictEntry entry) {
  if (!report) return;
  if (report->conflict_len >= report->conflict_cap) {
    size_t new_cap = report->conflict_cap ? report->conflict_cap * 2 : 4;
    ZProgramGraphMergeConflictEntry *new_entries = z_checked_reallocarray(report->conflicts, new_cap, sizeof(ZProgramGraphMergeConflictEntry));
    report->conflicts = new_entries;
    report->conflict_cap = new_cap;
  }
  report->conflicts[report->conflict_len++] = entry;
  if (entry.kind == Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_AUTO_RESOLVED) {
    report->auto_resolved_count++;
  } else {
    report->has_conflicts = true;
  }
}

static ZProgramGraphDiffEntry *diff_find_by_node_id(const ZProgramGraphDiff *diff, const char *node_id) {
  if (!diff || !node_id) return NULL;
  for (size_t i = 0; i < diff->diff_len; i++) {
    if (compare_text_eq(diff->diffs[i].node_id, node_id)) {
      return &diff->diffs[i];
    }
  }
  return NULL;
}

static bool node_id_is_ancestor_of_with_visited(const ZProgramGraph *graph, const char *ancestor_id, const char *descendant_id, const char **visited_ids, size_t visited_cap, size_t visited_len);

static bool node_id_is_ancestor_of(const ZProgramGraph *graph, const char *ancestor_id, const char *descendant_id) {
  if (!graph || !ancestor_id || !descendant_id || compare_text_eq(ancestor_id, descendant_id)) return false;

  const size_t max_depth = graph->node_len > 0 ? graph->node_len : 1;
  const char **visited_ids = z_checked_calloc(max_depth, sizeof(const char *));
  bool result = node_id_is_ancestor_of_with_visited(graph, ancestor_id, descendant_id, visited_ids, max_depth, 0);
  free(visited_ids);
  return result;
}

static bool node_id_is_ancestor_of_with_visited(const ZProgramGraph *graph, const char *ancestor_id, const char *descendant_id, const char **visited_ids, size_t visited_cap, size_t visited_len) {
  if (!graph || !ancestor_id || !descendant_id || compare_text_eq(ancestor_id, descendant_id)) return false;

  for (size_t v = 0; v < visited_len; v++) {
    if (compare_text_eq(visited_ids[v], ancestor_id)) return false;
  }

  if (visited_len >= visited_cap) return false;
  visited_ids[visited_len++] = ancestor_id;

  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
    if (compare_text_eq(edge->from, ancestor_id)) {
      if (compare_text_eq(edge->to, descendant_id)) return true;
      if (node_id_is_ancestor_of_with_visited(graph, edge->to, descendant_id, visited_ids, visited_cap, visited_len)) return true;
    }
  }
  return false;
}

bool z_program_graph_merge_detect(const ZProgramGraph *base, const ZProgramGraph *ours, const ZProgramGraph *theirs, ZProgramGraphMergeReport *out) {
  if (out) *out = (ZProgramGraphMergeReport){.ok = true};

  if (!base || !ours || !theirs) {
    if (out) out->ok = false;
    return false;
  }

  ZProgramGraphDiff ours_diff = {0};
  ZProgramGraphDiff theirs_diff = {0};

  if (!z_program_graph_diff(base, ours, &ours_diff) || !ours_diff.ok) {
    if (out) out->ok = false;
    z_program_graph_diff_free(&ours_diff);
    z_program_graph_diff_free(&theirs_diff);
    return false;
  }

  if (!z_program_graph_diff(base, theirs, &theirs_diff) || !theirs_diff.ok) {
    if (out) out->ok = false;
    z_program_graph_diff_free(&ours_diff);
    z_program_graph_diff_free(&theirs_diff);
    return false;
  }

  if (out) {
    out->left_changes = ours_diff.diff_len;
    out->right_changes = theirs_diff.diff_len;
  }

  MoveTracker our_moves = {0};
  MoveTracker their_moves = {0};
  move_tracker_build(&our_moves, base, ours);
  move_tracker_build(&their_moves, base, theirs);

  check_move_move_cycle_and_add_conflicts(base, ours, theirs, &our_moves, &their_moves, out);

  for (size_t i = 0; i < ours_diff.diff_len; i++) {
    ZProgramGraphDiffEntry *our_entry = &ours_diff.diffs[i];
    ZProgramGraphDiffEntry *their_entry = diff_find_by_node_id(&theirs_diff, our_entry->node_id);

    if (!their_entry) {
      continue;
    }

    if (our_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_MODIFIED && their_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_MODIFIED) {
      if (compare_text_eq(our_entry->field, their_entry->field)) {
        if (compare_text_eq(our_entry->right_value, their_entry->right_value)) {
          // Identical edits - both changed same field to same value -> auto-resolved
          ZProgramGraphMergeConflictEntry conflict = {
            .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_AUTO_RESOLVED,
          };
          snprintf(conflict.code, sizeof(conflict.code), "GMG101");
          snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", our_entry->node_id);
          snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", our_entry->node_kind);
          snprintf(conflict.name, sizeof(conflict.name), "%s", "");
          snprintf(conflict.field, sizeof(conflict.field), "%s", our_entry->field);
          snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", our_entry->left_value);
          snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", our_entry->right_value);
          snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", our_entry->left_value);
          merge_append_conflict(out, conflict);
        } else {
          ZProgramGraphMergeConflictEntry conflict = {
            .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_EDIT_EDIT,
          };
          snprintf(conflict.code, sizeof(conflict.code), "GMG001");
          snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", our_entry->node_id);
          snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", our_entry->node_kind);
          snprintf(conflict.name, sizeof(conflict.name), "%s", "");
          snprintf(conflict.field, sizeof(conflict.field), "%s", our_entry->field);
          snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", our_entry->left_value);
          snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", our_entry->right_value);
          snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", our_entry->left_value);
          merge_append_conflict(out, conflict);
        }
      } else {
        /* Different fields on same node - not a conflict, apply both changes.
         * Only check for ancestor coupling when nodes are actually different. */
        if (compare_text_eq(our_entry->node_id, their_entry->node_id)) {
          /* Same node, different fields - field-level disjointness applies, no conflict */
        } else if (check_parent_moved_child_edited(base, ours, theirs, our_entry, their_entry, &our_moves, &their_moves)) {
          ZProgramGraphMergeConflictEntry conflict = {
            .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_PARENT_MOVED_CHILD_EDITED,
          };
          snprintf(conflict.code, sizeof(conflict.code), "GMG005");
          snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", our_entry->node_id);
          snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", our_entry->node_kind);
          snprintf(conflict.name, sizeof(conflict.name), "%s", "");
          snprintf(conflict.field, sizeof(conflict.field), "%s", our_entry->field);
          snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", our_entry->left_value);
          snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", our_entry->right_value);
          snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", our_entry->left_value);
          merge_append_conflict(out, conflict);
        } else if (node_id_is_ancestor_of(ours, our_entry->node_id, their_entry->node_id) ||
                   node_id_is_ancestor_of(theirs, their_entry->node_id, our_entry->node_id)) {
          ZProgramGraphMergeConflictEntry conflict = {
            .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_ANCESTOR_COUPLING,
          };
          snprintf(conflict.code, sizeof(conflict.code), "GMG004");
          snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", our_entry->node_id);
          snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", our_entry->node_kind);
          snprintf(conflict.name, sizeof(conflict.name), "%s", "");
          snprintf(conflict.field, sizeof(conflict.field), "%s", "");
          snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", our_entry->left_value);
          snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", our_entry->right_value);
          snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", our_entry->left_value);
          merge_append_conflict(out, conflict);
        }
      }
    } else if (our_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_MODIFIED && their_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_REMOVED) {
      ZProgramGraphMergeConflictEntry conflict = {
        .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_EDIT_DELETE,
      };
      snprintf(conflict.code, sizeof(conflict.code), "GMG002");
      snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", our_entry->node_id);
      snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", our_entry->node_kind);
      snprintf(conflict.name, sizeof(conflict.name), "%s", "");
      snprintf(conflict.field, sizeof(conflict.field), "%s", our_entry->field);
      snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", our_entry->left_value);
      snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", "[deleted]");
      snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", our_entry->left_value);
      merge_append_conflict(out, conflict);
    } else if (our_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_REMOVED && their_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_MODIFIED) {
      ZProgramGraphMergeConflictEntry conflict = {
        .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_EDIT_DELETE,
      };
      snprintf(conflict.code, sizeof(conflict.code), "GMG002");
      snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", our_entry->node_id);
      snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", our_entry->node_kind);
      snprintf(conflict.name, sizeof(conflict.name), "%s", "");
      snprintf(conflict.field, sizeof(conflict.field), "%s", their_entry->field);
      snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", "[deleted]");
      snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", their_entry->right_value);
      snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", our_entry->left_value);
      merge_append_conflict(out, conflict);
    } else if (our_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_ADDED && their_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_ADDED) {
      if (compare_text_eq(our_entry->node_id, their_entry->node_id)) {
        if (compare_text_eq(our_entry->right_value, their_entry->right_value)) {
          // Identical add/add: both added same node with same name -> auto-resolved
          ZProgramGraphMergeConflictEntry conflict = {
            .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_AUTO_RESOLVED,
          };
          snprintf(conflict.code, sizeof(conflict.code), "GMG103");
          snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", our_entry->node_id);
          snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", our_entry->node_kind);
          snprintf(conflict.name, sizeof(conflict.name), "%s", our_entry->right_value);
          snprintf(conflict.field, sizeof(conflict.field), "%s", "name");
          snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", "");
          snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", our_entry->right_value);
          snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", "");
          merge_append_conflict(out, conflict);
        } else {
          ZProgramGraphMergeConflictEntry conflict = {
            .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_ADD_ADD,
          };
          snprintf(conflict.code, sizeof(conflict.code), "GMG003");
          snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", our_entry->node_id);
          snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", our_entry->node_kind);
          snprintf(conflict.name, sizeof(conflict.name), "%s", our_entry->right_value);
          snprintf(conflict.field, sizeof(conflict.field), "%s", "name");
          snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", "");
          snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", our_entry->right_value);
          snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", "");
          merge_append_conflict(out, conflict);
        }
      }
    }
  }

  for (size_t i = 0; i < theirs_diff.diff_len; i++) {
    ZProgramGraphDiffEntry *their_entry = &theirs_diff.diffs[i];
    ZProgramGraphDiffEntry *our_entry = diff_find_by_node_id(&ours_diff, their_entry->node_id);

    // Detect delete/delete: both deleted the same node -> auto-resolved
    if (our_entry &&
        our_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_REMOVED &&
        their_entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_REMOVED) {
      ZProgramGraphMergeConflictEntry conflict = {
        .kind = Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_AUTO_RESOLVED,
      };
      snprintf(conflict.code, sizeof(conflict.code), "GMG102");
      snprintf(conflict.node_id, sizeof(conflict.node_id), "%s", their_entry->node_id);
      snprintf(conflict.node_kind, sizeof(conflict.node_kind), "%s", their_entry->node_kind);
      snprintf(conflict.name, sizeof(conflict.name), "%s", their_entry->left_value);
      snprintf(conflict.field, sizeof(conflict.field), "%s", "name");
      snprintf(conflict.left_value, sizeof(conflict.left_value), "%s", their_entry->left_value);
      snprintf(conflict.right_value, sizeof(conflict.right_value), "%s", "");
      snprintf(conflict.ancestor_value, sizeof(conflict.ancestor_value), "%s", their_entry->left_value);
      merge_append_conflict(out, conflict);
    }
  }

  z_program_graph_diff_free(&ours_diff);
  z_program_graph_diff_free(&theirs_diff);
  move_tracker_free(&our_moves);
  move_tracker_free(&their_moves);

  if (out) out->ok = true;
  return true;
}

void z_program_graph_merge_report_free(ZProgramGraphMergeReport *report) {
  if (!report) return;
  free(report->conflicts);
  *report = (ZProgramGraphMergeReport){0};
}

static ZProgramGraphNode *merge_copy_node(const ZProgramGraphNode *src) {
  if (!src) return NULL;
  ZProgramGraphNode *node = z_checked_calloc(1, sizeof(ZProgramGraphNode));
  node->id = z_strdup(src->id);
  node->kind = src->kind;
  node->name = z_strdup(src->name);
  node->type = z_strdup(src->type);
  node->value = z_strdup(src->value);
  node->path = z_strdup(src->path);
  node->symbol_id = z_strdup(src->symbol_id);
  node->type_id = z_strdup(src->type_id);
  node->effect_id = z_strdup(src->effect_id);
  node->node_hash = z_strdup(src->node_hash);
  node->line = src->line;
  node->column = src->column;
  node->is_public = src->is_public;
  node->is_mutable = src->is_mutable;
  node->is_static = src->is_static;
  node->fallible = src->fallible;
  node->export_c = src->export_c;
  return node;
}

static ZProgramGraphEdge *merge_copy_edge(const ZProgramGraphEdge *src) {
  if (!src) return NULL;
  ZProgramGraphEdge *edge = z_checked_calloc(1, sizeof(ZProgramGraphEdge));
  edge->from = z_strdup(src->from);
  edge->to = z_strdup(src->to);
  edge->kind = z_strdup(src->kind);
  edge->target = src->target;
  edge->order = src->order;
  return edge;
}

static void merge_init_graph(ZProgramGraph *graph, const ZProgramGraph *src) {
  if (!graph || !src) return;
  graph->schema_version = src->schema_version;
  graph->validation_state = src->validation_state;
  graph->module_identity = z_strdup(src->module_identity);
  graph->graph_hash = z_strdup(src->graph_hash);
  graph->canonical_source = src->canonical_source;
  graph->next_id = src->next_id;
}

static ZProgramGraphNode *merge_find_node_by_id(const ZProgramGraph *graph, const char *id) {
  if (!graph || !id) return NULL;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].id && strcmp(graph->nodes[i].id, id) == 0) {
      return &graph->nodes[i];
    }
  }
  return NULL;
}

static bool merge_node_set_field(ZProgramGraphNode *node, const char *field, const char *value) {
  if (!node || !field) return false;
  if (strcmp(field, "name") == 0) {
    free(node->name);
    node->name = z_strdup(value ? value : "");
  } else if (strcmp(field, "type") == 0) {
    free(node->type);
    node->type = z_strdup(value ? value : "");
  } else if (strcmp(field, "value") == 0) {
    free(node->value);
    node->value = z_strdup(value ? value : "");
  } else if (strcmp(field, "public") == 0) {
    node->is_public = (value && strcmp(value, "true") == 0);
  } else if (strcmp(field, "mutable") == 0) {
    node->is_mutable = (value && strcmp(value, "true") == 0);
  } else if (strcmp(field, "static") == 0) {
    node->is_static = (value && strcmp(value, "true") == 0);
  } else if (strcmp(field, "fallible") == 0) {
    node->fallible = (value && strcmp(value, "true") == 0);
  } else if (strcmp(field, "exportC") == 0) {
    node->export_c = (value && strcmp(value, "true") == 0);
  } else {
    return false;
  }
  return true;
}

static bool merge_graph_add_node(ZProgramGraph *graph, const ZProgramGraphNode *src_node) {
  if (!graph || !src_node) return false;
  if (graph->node_len >= graph->node_cap) {
    size_t new_cap = graph->node_cap ? graph->node_cap * 2 : 4;
    ZProgramGraphNode *new_nodes = z_checked_reallocarray(graph->nodes, new_cap, sizeof(ZProgramGraphNode));
    graph->nodes = new_nodes;
    graph->node_cap = new_cap;
  }
  ZProgramGraphNode *node = &graph->nodes[graph->node_len++];
  node->id = z_strdup(src_node->id);
  node->kind = src_node->kind;
  node->name = z_strdup(src_node->name);
  node->type = z_strdup(src_node->type);
  node->value = z_strdup(src_node->value);
  node->path = z_strdup(src_node->path);
  node->symbol_id = z_strdup(src_node->symbol_id);
  node->type_id = z_strdup(src_node->type_id);
  node->effect_id = z_strdup(src_node->effect_id);
  node->node_hash = z_strdup(src_node->node_hash);
  node->line = src_node->line;
  node->column = src_node->column;
  node->is_public = src_node->is_public;
  node->is_mutable = src_node->is_mutable;
  node->is_static = src_node->is_static;
  node->fallible = src_node->fallible;
  node->export_c = src_node->export_c;
  return true;
}

static bool merge_graph_remove_node(ZProgramGraph *graph, const char *node_id) {
  if (!graph || !node_id) return false;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].id && strcmp(graph->nodes[i].id, node_id) == 0) {
      free(graph->nodes[i].id);
      free(graph->nodes[i].name);
      free(graph->nodes[i].type);
      free(graph->nodes[i].value);
      free(graph->nodes[i].path);
      free(graph->nodes[i].symbol_id);
      free(graph->nodes[i].type_id);
      free(graph->nodes[i].effect_id);
      free(graph->nodes[i].node_hash);
      for (size_t j = i; j < graph->node_len - 1; j++) {
        graph->nodes[j] = graph->nodes[j + 1];
      }
      graph->node_len--;
      return true;
    }
  }
  return false;
}

static bool merge_graph_modify_node(ZProgramGraph *graph, const char *node_id, const char *field, const char *value) {
  if (!graph || !node_id || !field) return false;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].id && strcmp(graph->nodes[i].id, node_id) == 0) {
      return merge_node_set_field(&graph->nodes[i], field, value);
    }
  }
  return false;
}

static bool merge_apply_diffs(ZProgramGraph *graph, const ZProgramGraph *base, const ZProgramGraph *diff_source, bool *added_count, bool *removed_count, bool *modified_count) {
  if (!graph || !base || !diff_source) return false;
  if (added_count) *added_count = false;
  if (removed_count) *removed_count = false;
  if (modified_count) *modified_count = false;

  ZProgramGraphDiff diff = {0};
  if (!z_program_graph_diff(base, diff_source, &diff) || !diff.ok) {
    return false;
  }

  for (size_t i = 0; i < diff.diff_len; i++) {
    ZProgramGraphDiffEntry *entry = &diff.diffs[i];
    if (entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_ADDED) {
      const ZProgramGraphNode *src_node = merge_find_node_by_id(diff_source, entry->node_id);
      if (src_node) {
        if (merge_graph_add_node(graph, src_node)) {
          if (added_count) (*added_count)++;
        }
      }
    } else if (entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_REMOVED) {
      if (merge_graph_remove_node(graph, entry->node_id)) {
        if (removed_count) (*removed_count)++;
      }
    } else if (entry->kind == Z_PROGRAM_GRAPH_DIFF_KIND_MODIFIED) {
      if (merge_graph_modify_node(graph, entry->node_id, entry->field, entry->right_value)) {
        if (modified_count) (*modified_count)++;
      }
    }
  }

  z_program_graph_diff_free(&diff);
  return true;
}

static ZProgramGraph *merge_copy_graph(const ZProgramGraph *src) {
  if (!src) return NULL;
  ZProgramGraph *graph = z_checked_calloc(1, sizeof(ZProgramGraph));
  graph->schema_version = src->schema_version;
  graph->validation_state = src->validation_state;
  graph->module_identity = z_strdup(src->module_identity);
  graph->graph_hash = z_strdup(src->graph_hash);
  graph->canonical_source = src->canonical_source;
  graph->next_id = src->next_id;

  if (src->node_len > 0) {
    graph->nodes = z_checked_calloc(src->node_len, sizeof(ZProgramGraphNode));
    graph->node_cap = src->node_len;
    graph->node_len = src->node_len;
    for (size_t i = 0; i < src->node_len; i++) {
      graph->nodes[i].id = z_strdup(src->nodes[i].id);
      graph->nodes[i].kind = src->nodes[i].kind;
      graph->nodes[i].name = z_strdup(src->nodes[i].name);
      graph->nodes[i].type = z_strdup(src->nodes[i].type);
      graph->nodes[i].value = z_strdup(src->nodes[i].value);
      graph->nodes[i].path = z_strdup(src->nodes[i].path);
      graph->nodes[i].symbol_id = z_strdup(src->nodes[i].symbol_id);
      graph->nodes[i].type_id = z_strdup(src->nodes[i].type_id);
      graph->nodes[i].effect_id = z_strdup(src->nodes[i].effect_id);
      graph->nodes[i].node_hash = z_strdup(src->nodes[i].node_hash);
      graph->nodes[i].line = src->nodes[i].line;
      graph->nodes[i].column = src->nodes[i].column;
      graph->nodes[i].is_public = src->nodes[i].is_public;
      graph->nodes[i].is_mutable = src->nodes[i].is_mutable;
      graph->nodes[i].is_static = src->nodes[i].is_static;
      graph->nodes[i].fallible = src->nodes[i].fallible;
      graph->nodes[i].export_c = src->nodes[i].export_c;
    }
  }

  if (src->edge_len > 0) {
    graph->edges = z_checked_calloc(src->edge_len, sizeof(ZProgramGraphEdge));
    graph->edge_cap = src->edge_len;
    graph->edge_len = src->edge_len;
    for (size_t i = 0; i < src->edge_len; i++) {
      graph->edges[i].from = z_strdup(src->edges[i].from);
      graph->edges[i].to = z_strdup(src->edges[i].to);
      graph->edges[i].kind = z_strdup(src->edges[i].kind);
      graph->edges[i].target = src->edges[i].target;
      graph->edges[i].order = src->edges[i].order;
    }
  }

  return graph;
}

bool z_program_graph_merge(
  const ZProgramGraph *base,
  const ZProgramGraph *ours,
  const ZProgramGraph *theirs,
  ZProgramGraphMergeResult *out
) {
  if (out) *out = (ZProgramGraphMergeResult){.ok = true, .merged = false, .graph = NULL};
  if (!base || !ours || !theirs) {
    if (out) out->ok = false;
    return false;
  }

  ZProgramGraphMergeReport report = {0};
  if (!z_program_graph_merge_detect(base, ours, theirs, &report)) {
    if (out) out->ok = false;
    return false;
  }

  if (out) {
    out->report = report;
  } else {
    z_program_graph_merge_report_free(&report);
  }

  if (report.has_conflicts) {
    out->ok = true;
    out->merged = false;
    out->graph = NULL;
    return true;
  }

  ZProgramGraph *merged = merge_copy_graph(base);
  if (!merged) {
    if (out) out->ok = false;
    return false;
  }

  bool added = false, removed = false, modified = false;
  if (!merge_apply_diffs(merged, base, ours, &added, &removed, &modified)) {
    free(merged);
    if (out) out->ok = false;
    return false;
  }

  if (!merge_apply_diffs(merged, base, theirs, &added, &removed, &modified)) {
    free(merged);
    if (out) out->ok = false;
    return false;
  }

  out->ok = true;
  out->merged = true;
  out->graph = merged;
  return true;
}

void z_program_graph_merge_result_free(ZProgramGraphMergeResult *result) {
  if (!result) return;
  if (result->graph) {
    z_program_graph_free(result->graph);
    free(result->graph);
    result->graph = NULL;
  }
  z_program_graph_merge_report_free(&result->report);
  *result = (ZProgramGraphMergeResult){0};
}
