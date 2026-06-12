#include "program_graph_adjacency.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int adjacency_text_cmp(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "");
}

static int adjacency_size_cmp(size_t left, size_t right) {
  if (left < right) return -1;
  return left > right ? 1 : 0;
}

static int adjacency_node_entry_cmp(const void *left, const void *right) {
  const ZProgramGraphAdjacencyNodeEntry *a = left;
  const ZProgramGraphAdjacencyNodeEntry *b = right;
  int cmp = adjacency_text_cmp(a->id, b->id);
  if (cmp != 0) return cmp;
  return adjacency_size_cmp(a->node_index, b->node_index);
}

static int adjacency_owner_entry_cmp(const void *left, const void *right) {
  const ZProgramGraphAdjacencyEdgeEntry *a = left;
  const ZProgramGraphAdjacencyEdgeEntry *b = right;
  int cmp = adjacency_size_cmp((size_t)a->edge->target, (size_t)b->edge->target);
  if (cmp == 0) cmp = adjacency_text_cmp(a->edge->from, b->edge->from);
  if (cmp == 0) cmp = adjacency_text_cmp(a->edge->kind, b->edge->kind);
  if (cmp == 0) cmp = adjacency_size_cmp(a->edge->order, b->edge->order);
  if (cmp == 0) cmp = adjacency_size_cmp(a->edge_index, b->edge_index);
  return cmp;
}

static int adjacency_child_entry_cmp(const void *left, const void *right) {
  const ZProgramGraphAdjacencyEdgeEntry *a = left;
  const ZProgramGraphAdjacencyEdgeEntry *b = right;
  int cmp = adjacency_text_cmp(a->edge->to, b->edge->to);
  if (cmp == 0) cmp = adjacency_text_cmp(a->edge->kind, b->edge->kind);
  if (cmp == 0) cmp = adjacency_size_cmp(a->edge_index, b->edge_index);
  return cmp;
}

ZProgramGraphAdjacencyNodeEntry *z_program_graph_id_index_build(const char *const *ids, size_t len) {
  ZProgramGraphAdjacencyNodeEntry *entries = z_checked_calloc(len ? len : 1, sizeof(ZProgramGraphAdjacencyNodeEntry));
  for (size_t i = 0; i < len; i++) {
    entries[i] = (ZProgramGraphAdjacencyNodeEntry){.id = ids ? ids[i] : NULL, .node_index = i};
  }
  qsort(entries, len, sizeof(ZProgramGraphAdjacencyNodeEntry), adjacency_node_entry_cmp);
  return entries;
}

static size_t id_index_lower_bound(const ZProgramGraphAdjacencyNodeEntry *entries, size_t len, const char *id) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (adjacency_text_cmp(entries[mid].id, id) < 0) low = mid + 1;
    else high = mid;
  }
  return low;
}

size_t z_program_graph_id_index_find(const ZProgramGraphAdjacencyNodeEntry *entries, size_t len, const char *id) {
  if (!entries || len == 0 || !id) return SIZE_MAX;
  size_t low = id_index_lower_bound(entries, len, id);
  if (low < len && adjacency_text_cmp(entries[low].id, id) == 0) return entries[low].node_index;
  return SIZE_MAX;
}

void z_program_graph_id_index_run(const ZProgramGraphAdjacencyNodeEntry *entries, size_t len, const char *id, size_t *start, size_t *run_len) {
  if (start) *start = 0;
  if (run_len) *run_len = 0;
  if (!entries || len == 0 || !id) return;
  size_t low = id_index_lower_bound(entries, len, id);
  size_t high = low;
  while (high < len && adjacency_text_cmp(entries[high].id, id) == 0) high++;
  if (start) *start = low;
  if (run_len) *run_len = high - low;
}

/*
 * Per-process memo of the most recent adjacency sort. Compiler commands
 * rebuild adjacency for the same loaded graph several times per invocation
 * (validation, stdlib reference scan, resolution facts, semantic checks);
 * the memo keeps the sorted order as table indexes and replays it with a
 * linear copy. Following the lowering index precedent, it revalidates
 * against the graph identity, table addresses, lengths, id allocation
 * cursor, and graph hash, so a rebuilt or mutated graph that happens to
 * reuse an address can never serve a stale order. Sort comparators tiebreak
 * on table index, so a replayed order is exactly what a fresh sort returns.
 */
typedef struct {
  const ZProgramGraph *graph;
  const ZProgramGraphNode *nodes;
  const ZProgramGraphEdge *edges;
  size_t node_len;
  size_t edge_len;
  size_t next_id;
  char *graph_hash;
  size_t *node_order;
  size_t *owner_order;
  size_t *child_order;
  size_t node_edge_len;
  bool present;
} AdjacencyMemo;

static AdjacencyMemo adjacency_memo;

static bool adjacency_memo_matches(const ZProgramGraph *graph) {
  return adjacency_memo.present && graph &&
         adjacency_memo.graph == graph &&
         adjacency_memo.nodes == graph->nodes &&
         adjacency_memo.edges == graph->edges &&
         adjacency_memo.node_len == graph->node_len &&
         adjacency_memo.edge_len == graph->edge_len &&
         adjacency_memo.next_id == graph->next_id &&
         adjacency_text_cmp(adjacency_memo.graph_hash, graph->graph_hash) == 0;
}

static void adjacency_memo_store(const ZProgramGraph *graph, const ZProgramGraphAdjacency *adjacency) {
  free(adjacency_memo.graph_hash);
  free(adjacency_memo.node_order);
  free(adjacency_memo.owner_order);
  free(adjacency_memo.child_order);
  adjacency_memo = (AdjacencyMemo){
    .graph = graph,
    .nodes = graph->nodes,
    .edges = graph->edges,
    .node_len = graph->node_len,
    .edge_len = graph->edge_len,
    .next_id = graph->next_id,
    .graph_hash = z_strdup(graph->graph_hash ? graph->graph_hash : ""),
    .node_order = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(size_t)),
    .owner_order = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t)),
    .child_order = z_checked_calloc(adjacency->node_edge_len ? adjacency->node_edge_len : 1, sizeof(size_t)),
    .node_edge_len = adjacency->node_edge_len,
    .present = true,
  };
  for (size_t i = 0; i < graph->node_len; i++) adjacency_memo.node_order[i] = adjacency->nodes_by_id[i].node_index;
  for (size_t i = 0; i < graph->edge_len; i++) adjacency_memo.owner_order[i] = adjacency->edges_by_owner[i].edge_index;
  for (size_t i = 0; i < adjacency->node_edge_len; i++) adjacency_memo.child_order[i] = adjacency->edges_by_child[i].edge_index;
}

static bool adjacency_memo_replay(ZProgramGraphAdjacency *adjacency, const ZProgramGraph *graph) {
  if (!adjacency_memo_matches(graph)) return false;
  size_t node_len = graph->node_len;
  size_t edge_len = graph->edge_len;
  adjacency->nodes_by_id = z_checked_calloc(node_len ? node_len : 1, sizeof(ZProgramGraphAdjacencyNodeEntry));
  for (size_t i = 0; i < node_len; i++) {
    size_t node_index = adjacency_memo.node_order[i];
    adjacency->nodes_by_id[i] = (ZProgramGraphAdjacencyNodeEntry){.id = graph->nodes[node_index].id, .node_index = node_index};
  }
  adjacency->edges_by_owner = z_checked_calloc(edge_len ? edge_len : 1, sizeof(ZProgramGraphAdjacencyEdgeEntry));
  for (size_t i = 0; i < edge_len; i++) {
    size_t edge_index = adjacency_memo.owner_order[i];
    adjacency->edges_by_owner[i] = (ZProgramGraphAdjacencyEdgeEntry){.edge = &graph->edges[edge_index], .edge_index = edge_index};
  }
  adjacency->node_edge_len = adjacency_memo.node_edge_len;
  adjacency->edges_by_child = z_checked_calloc(adjacency_memo.node_edge_len ? adjacency_memo.node_edge_len : 1, sizeof(ZProgramGraphAdjacencyEdgeEntry));
  for (size_t i = 0; i < adjacency_memo.node_edge_len; i++) {
    size_t edge_index = adjacency_memo.child_order[i];
    adjacency->edges_by_child[i] = (ZProgramGraphAdjacencyEdgeEntry){.edge = &graph->edges[edge_index], .edge_index = edge_index};
  }
  return true;
}

void z_program_graph_adjacency_init(ZProgramGraphAdjacency *adjacency, const ZProgramGraph *graph) {
  if (!adjacency) return;
  *adjacency = (ZProgramGraphAdjacency){.graph = graph};
  if (graph && adjacency_memo_replay(adjacency, graph)) return;
  size_t node_len = graph ? graph->node_len : 0;
  size_t edge_len = graph ? graph->edge_len : 0;
  adjacency->nodes_by_id = z_checked_calloc(node_len ? node_len : 1, sizeof(ZProgramGraphAdjacencyNodeEntry));
  for (size_t i = 0; i < node_len; i++) {
    adjacency->nodes_by_id[i] = (ZProgramGraphAdjacencyNodeEntry){.id = graph->nodes[i].id, .node_index = i};
  }
  qsort(adjacency->nodes_by_id, node_len, sizeof(ZProgramGraphAdjacencyNodeEntry), adjacency_node_entry_cmp);
  adjacency->edges_by_owner = z_checked_calloc(edge_len ? edge_len : 1, sizeof(ZProgramGraphAdjacencyEdgeEntry));
  for (size_t i = 0; i < edge_len; i++) {
    adjacency->edges_by_owner[i] = (ZProgramGraphAdjacencyEdgeEntry){.edge = &graph->edges[i], .edge_index = i};
  }
  qsort(adjacency->edges_by_owner, edge_len, sizeof(ZProgramGraphAdjacencyEdgeEntry), adjacency_owner_entry_cmp);
  size_t node_edge_len = 0;
  while (node_edge_len < edge_len && adjacency->edges_by_owner[node_edge_len].edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) {
    node_edge_len++;
  }
  adjacency->node_edge_len = node_edge_len;
  adjacency->edges_by_child = z_checked_calloc(node_edge_len ? node_edge_len : 1, sizeof(ZProgramGraphAdjacencyEdgeEntry));
  for (size_t i = 0; i < node_edge_len; i++) {
    adjacency->edges_by_child[i] = adjacency->edges_by_owner[i];
  }
  qsort(adjacency->edges_by_child, node_edge_len, sizeof(ZProgramGraphAdjacencyEdgeEntry), adjacency_child_entry_cmp);
  if (graph && graph->node_len > 0) adjacency_memo_store(graph, adjacency);
}

void z_program_graph_adjacency_free(ZProgramGraphAdjacency *adjacency) {
  if (!adjacency) return;
  free(adjacency->nodes_by_id);
  free(adjacency->edges_by_owner);
  free(adjacency->edges_by_child);
  *adjacency = (ZProgramGraphAdjacency){0};
}

size_t z_program_graph_adjacency_node_index(const ZProgramGraphAdjacency *adjacency, const char *id) {
  if (!adjacency || !adjacency->graph || !id || !id[0]) return SIZE_MAX;
  size_t low = 0;
  size_t high = adjacency->graph->node_len;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (adjacency_text_cmp(adjacency->nodes_by_id[mid].id, id) < 0) low = mid + 1;
    else high = mid;
  }
  if (low < adjacency->graph->node_len && adjacency_text_cmp(adjacency->nodes_by_id[low].id, id) == 0) {
    return adjacency->nodes_by_id[low].node_index;
  }
  return SIZE_MAX;
}

const ZProgramGraphNode *z_program_graph_adjacency_node(const ZProgramGraphAdjacency *adjacency, const char *id) {
  size_t index = z_program_graph_adjacency_node_index(adjacency, id);
  return index == SIZE_MAX ? NULL : &adjacency->graph->nodes[index];
}

static int adjacency_owner_query_cmp(const ZProgramGraphAdjacencyEdgeEntry *entry, const char *from, const char *kind) {
  int cmp = adjacency_text_cmp(entry->edge->from, from);
  if (cmp == 0 && kind) cmp = adjacency_text_cmp(entry->edge->kind, kind);
  return cmp;
}

void z_program_graph_adjacency_owner_run(const ZProgramGraphAdjacency *adjacency, const char *from, const char *kind, size_t *start, size_t *len) {
  if (start) *start = 0;
  if (len) *len = 0;
  if (!adjacency || !from) return;
  size_t low = 0;
  size_t high = adjacency->node_edge_len;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (adjacency_owner_query_cmp(&adjacency->edges_by_owner[mid], from, kind) < 0) low = mid + 1;
    else high = mid;
  }
  size_t first = low;
  high = adjacency->node_edge_len;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (adjacency_owner_query_cmp(&adjacency->edges_by_owner[mid], from, kind) <= 0) low = mid + 1;
    else high = mid;
  }
  if (start) *start = first;
  if (len) *len = low - first;
}

const ZProgramGraphEdge *z_program_graph_adjacency_owner_edge_at(const ZProgramGraphAdjacency *adjacency, size_t position) {
  if (!adjacency || position >= (adjacency->graph ? adjacency->graph->edge_len : 0)) return NULL;
  return adjacency->edges_by_owner[position].edge;
}

static int adjacency_child_query_cmp(const ZProgramGraphAdjacencyEdgeEntry *entry, const char *to, const char *kind) {
  int cmp = adjacency_text_cmp(entry->edge->to, to);
  if (cmp == 0 && kind) cmp = adjacency_text_cmp(entry->edge->kind, kind);
  return cmp;
}

void z_program_graph_adjacency_child_run(const ZProgramGraphAdjacency *adjacency, const char *to, const char *kind, size_t *start, size_t *len) {
  if (start) *start = 0;
  if (len) *len = 0;
  if (!adjacency || !to) return;
  size_t low = 0;
  size_t high = adjacency->node_edge_len;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (adjacency_child_query_cmp(&adjacency->edges_by_child[mid], to, kind) < 0) low = mid + 1;
    else high = mid;
  }
  size_t first = low;
  high = adjacency->node_edge_len;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (adjacency_child_query_cmp(&adjacency->edges_by_child[mid], to, kind) <= 0) low = mid + 1;
    else high = mid;
  }
  if (start) *start = first;
  if (len) *len = low - first;
}

const ZProgramGraphEdge *z_program_graph_adjacency_child_edge_at(const ZProgramGraphAdjacency *adjacency, size_t position) {
  if (!adjacency || position >= adjacency->node_edge_len) return NULL;
  return adjacency->edges_by_child[position].edge;
}

const ZProgramGraphEdge *z_program_graph_adjacency_first_child_edge(const ZProgramGraphAdjacency *adjacency, const char *to) {
  size_t start = 0;
  size_t len = 0;
  z_program_graph_adjacency_child_run(adjacency, to, NULL, &start, &len);
  const ZProgramGraphEdge *first = NULL;
  size_t first_index = SIZE_MAX;
  for (size_t i = start; i < start + len; i++) {
    if (adjacency->edges_by_child[i].edge_index < first_index) {
      first_index = adjacency->edges_by_child[i].edge_index;
      first = adjacency->edges_by_child[i].edge;
    }
  }
  return first;
}

bool z_program_graph_adjacency_has_child_edge(const ZProgramGraphAdjacency *adjacency, const char *to, const char *kind) {
  size_t start = 0;
  size_t len = 0;
  if (!kind) return false;
  z_program_graph_adjacency_child_run(adjacency, to, kind, &start, &len);
  return len > 0;
}
