#include "program_graph_order.h"

#include "program_graph_adjacency.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  ZProgramGraphEdge *edge;
  size_t edge_index;
} GraphOrderEdgeRef;

static int graph_order_text_cmp(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "");
}

static bool graph_order_text_eq(const char *left, const char *right) {
  return graph_order_text_cmp(left, right) == 0;
}

static int graph_order_size_cmp(size_t left, size_t right) {
  if (left < right) return -1;
  return left > right ? 1 : 0;
}

static int graph_order_edge_ref_cmp(const void *left, const void *right) {
  const GraphOrderEdgeRef *a = left;
  const GraphOrderEdgeRef *b = right;
  int cmp = graph_order_text_cmp(a->edge->from, b->edge->from);
  if (cmp == 0) cmp = graph_order_text_cmp(a->edge->kind, b->edge->kind);
  if (cmp == 0) cmp = graph_order_size_cmp(a->edge->order, b->edge->order);
  if (cmp == 0) cmp = graph_order_size_cmp(a->edge_index, b->edge_index);
  return cmp;
}

bool z_program_graph_order_must_be_contiguous(const ZProgramGraphNode *owner, const char *kind) {
  if (!owner || !kind) return false;
  if (graph_order_text_eq(kind, "arg") && owner->kind == Z_PROGRAM_GRAPH_NODE_SLICE) return false;
  return graph_order_text_eq(kind, "import") || graph_order_text_eq(kind, "cImport") ||
         graph_order_text_eq(kind, "const") || graph_order_text_eq(kind, "alias") ||
         graph_order_text_eq(kind, "shape") || graph_order_text_eq(kind, "interface") ||
         graph_order_text_eq(kind, "enum") || graph_order_text_eq(kind, "choice") ||
         graph_order_text_eq(kind, "function") || graph_order_text_eq(kind, "method") ||
         graph_order_text_eq(kind, "typeParam") || graph_order_text_eq(kind, "param") ||
         graph_order_text_eq(kind, "error") || graph_order_text_eq(kind, "field") ||
         graph_order_text_eq(kind, "case") || graph_order_text_eq(kind, "statement") ||
         graph_order_text_eq(kind, "arg") || graph_order_text_eq(kind, "typeArg") ||
         graph_order_text_eq(kind, "arm");
}

static void graph_order_compact_ref_group(GraphOrderEdgeRef *refs, size_t start, size_t end) {
  size_t next_order = 0;
  for (size_t i = start; i < end; i++) refs[i].edge->order = next_order++;
}

static void graph_order_index_nodes(const ZProgramGraph *graph, const char ***out_ids, ZProgramGraphAdjacencyNodeEntry **out_index) {
  const char **ids = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(const char *));
  for (size_t i = 0; i < graph->node_len; i++) ids[i] = graph->nodes[i].id;
  *out_ids = ids;
  *out_index = z_program_graph_id_index_build(ids, graph->node_len);
}

static bool graph_order_edge_requires_compaction(const ZProgramGraph *graph, const ZProgramGraphAdjacencyNodeEntry *node_index, const ZProgramGraphEdge *edge) {
  if (!graph || !edge || edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) return false;
  size_t owner_index = z_program_graph_id_index_find(node_index, graph->node_len, edge->from);
  const ZProgramGraphNode *owner = owner_index == SIZE_MAX ? NULL : &graph->nodes[owner_index];
  return z_program_graph_order_must_be_contiguous(owner, edge->kind);
}

static size_t graph_order_collect_refs(ZProgramGraph *graph, const ZProgramGraphAdjacencyNodeEntry *node_index, GraphOrderEdgeRef *refs) {
  size_t len = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    if (!graph_order_edge_requires_compaction(graph, node_index, &graph->edges[i])) continue;
    refs[len++] = (GraphOrderEdgeRef){.edge = &graph->edges[i], .edge_index = i};
  }
  return len;
}

void z_program_graph_compact_ordered_edges(ZProgramGraph *graph) {
  if (!graph || graph->edge_len == 0) return;
  const char **node_ids = NULL;
  ZProgramGraphAdjacencyNodeEntry *node_index = NULL;
  graph_order_index_nodes(graph, &node_ids, &node_index);
  GraphOrderEdgeRef *refs = z_checked_calloc(graph->edge_len, sizeof(GraphOrderEdgeRef));
  size_t len = graph_order_collect_refs(graph, node_index, refs);
  qsort(refs, len, sizeof(GraphOrderEdgeRef), graph_order_edge_ref_cmp);
  for (size_t start = 0; start < len;) {
    size_t end = start + 1;
    while (end < len && graph_order_text_eq(refs[start].edge->from, refs[end].edge->from) && graph_order_text_eq(refs[start].edge->kind, refs[end].edge->kind)) end++;
    graph_order_compact_ref_group(refs, start, end);
    start = end;
  }
  free(refs); free(node_index); free(node_ids);
}
