#include "program_graph_order.h"

#include <string.h>

static bool graph_order_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static const ZProgramGraphNode *graph_order_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (graph_order_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
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

static bool graph_order_edge_group_seen_before(const ZProgramGraph *graph, size_t index) {
  const ZProgramGraphEdge *edge = &graph->edges[index];
  for (size_t i = 0; i < index; i++) {
    const ZProgramGraphEdge *prior = &graph->edges[i];
    if (prior->target == edge->target && graph_order_text_eq(prior->from, edge->from) && graph_order_text_eq(prior->kind, edge->kind)) return true;
  }
  return false;
}

static void graph_order_compact_group(ZProgramGraph *graph, const char *from, const char *kind) {
  size_t max_order = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !graph_order_text_eq(edge->from, from) || !graph_order_text_eq(edge->kind, kind)) continue;
    if (edge->order > max_order) max_order = edge->order;
  }
  size_t next_order = 0;
  for (size_t old_order = 0; old_order <= max_order; old_order++) {
    for (size_t i = 0; graph && i < graph->edge_len; i++) {
      ZProgramGraphEdge *edge = &graph->edges[i];
      if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !graph_order_text_eq(edge->from, from) || !graph_order_text_eq(edge->kind, kind) || edge->order != old_order) continue;
      edge->order = next_order++;
    }
  }
}

void z_program_graph_compact_ordered_edges(ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || graph_order_edge_group_seen_before(graph, i)) continue;
    const ZProgramGraphNode *owner = graph_order_find_node(graph, edge->from);
    if (!z_program_graph_order_must_be_contiguous(owner, edge->kind)) continue;
    graph_order_compact_group(graph, edge->from, edge->kind);
  }
}
