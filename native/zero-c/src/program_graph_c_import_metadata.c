#include "program_graph_c_import_metadata.h"

#include <stdlib.h>
#include <string.h>

static bool graph_c_meta_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

void z_program_graph_clear_c_import_metadata(SourceInput *input) {
  if (!input) return;
  for (size_t i = 0; i < input->direct_c_import_header_count; i++) {
    free(input->direct_c_import_headers[i]);
    free(input->direct_c_import_resolved_headers[i]);
  }
  free(input->direct_c_import_headers);
  free(input->direct_c_import_resolved_headers);
  input->direct_c_import_headers = NULL;
  input->direct_c_import_resolved_headers = NULL;
  input->direct_c_import_header_count = 0;
  input->direct_c_import_call_count = 0;
  input->direct_c_import_symbol_count = 0;
}

static bool graph_c_meta_has_header(const SourceInput *input, const char *header) {
  for (size_t i = 0; input && header && i < input->direct_c_import_header_count; i++) {
    if (graph_c_meta_text_eq(input->direct_c_import_headers[i], header) ||
        graph_c_meta_text_eq(input->direct_c_import_resolved_headers[i], header)) return true;
  }
  return false;
}

static void graph_c_meta_record_header(SourceInput *input, const char *header) {
  if (!input || !header || !header[0] || graph_c_meta_has_header(input, header)) return;
  size_t next = input->direct_c_import_header_count + 1;
  input->direct_c_import_headers = z_checked_reallocarray(input->direct_c_import_headers, next, sizeof(char *));
  input->direct_c_import_resolved_headers = z_checked_reallocarray(input->direct_c_import_resolved_headers, next, sizeof(char *));
  input->direct_c_import_headers[input->direct_c_import_header_count] = z_strdup(header);
  input->direct_c_import_resolved_headers[input->direct_c_import_header_count] = NULL;
  input->direct_c_import_header_count = next;
}

static const ZProgramGraphNode *graph_c_meta_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (graph_c_meta_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphNode *graph_c_meta_child(const ZProgramGraph *graph, const ZProgramGraphNode *parent, const char *kind, size_t order) {
  for (size_t i = 0; graph && parent && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!graph_c_meta_text_eq(edge->from, parent->id) || !graph_c_meta_text_eq(edge->kind, kind) || edge->order != order) continue;
    return graph_c_meta_find_node(graph, edge->to);
  }
  return NULL;
}

static bool graph_c_meta_has_alias(const ZProgramGraph *graph, const char *alias) {
  for (size_t i = 0; graph && alias && alias[0] && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT && graph_c_meta_text_eq(node->name, alias)) return true;
  }
  return false;
}

static const char *graph_c_meta_root_identifier(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (!node) return NULL;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) return node->name;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) return graph_c_meta_root_identifier(graph, graph_c_meta_child(graph, node, "left", 0));
  return NULL;
}

static size_t graph_c_meta_count_calls(const ZProgramGraph *graph) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL) continue;
    const char *root = graph_c_meta_root_identifier(graph, graph_c_meta_child(graph, node, "left", 0));
    if (graph_c_meta_has_alias(graph, root)) count++;
  }
  return count;
}

void z_program_graph_seed_c_import_metadata(SourceInput *input, const ZProgramGraph *graph) {
  if (!input || !graph) return;
  bool has_c_import = false;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT) {
      has_c_import = true;
      break;
    }
  }
  if (!has_c_import) {
    input->direct_c_import_call_count = 0;
    input->direct_c_import_symbol_count = 0;
    return;
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!graph_c_meta_text_eq(edge->kind, "cImport")) continue;
    const ZProgramGraphNode *node = graph_c_meta_find_node(graph, edge->to);
    if (node && node->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT) graph_c_meta_record_header(input, node->value);
  }
  input->direct_c_import_symbol_count = input->direct_c_import_header_count;
  input->direct_c_import_call_count = graph_c_meta_count_calls(graph);
}
