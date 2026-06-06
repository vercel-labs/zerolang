#include "program_graph_query.h"
#include "program_graph_query_internal.h"

#include <stdio.h>

static void query_print_escaped(const char *text) {
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
    if (*p == '\n') printf("\\n");
    else if (*p == '\r') printf("\\r");
    else if (*p == '\t') printf("\\t");
    else if (*p == '\\') printf("\\\\");
    else if (*p == '"') printf("\\\"");
    else if (*p < 32 || *p == 127) printf("\\x%02x", *p);
    else putchar(*p);
  }
}

static void query_print_text_field(const char *label, const char *value) {
  if (!value) return;
  printf(" %s:", label);
  query_print_escaped(value);
}

static void query_print_match_line(const ZProgramGraphNode *node) {
  printf("  %s %s", node ? z_program_graph_node_kind_name(node->kind) : "Unknown", node && node->id ? node->id : "");
  if (node) query_print_text_field("name", node->name);
  if (node) query_print_text_field("type", node->type);
  if (node) query_print_text_field("value", node->value);
  if (node && node->path) printf(" path:%s:%d:%d", node->path, node->line, node->column);
  printf("\n");
}

static void query_print_edge_line(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, bool parent_edge) {
  const ZProgramGraphNode *other = edge && edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE ? z_program_graph_query_node_by_id(graph, parent_edge ? edge->from : edge->to) : NULL;
  printf("  %s order:%zu %s -> %s", edge ? edge->kind : "", edge ? edge->order : 0, edge && edge->from ? edge->from : "", edge && edge->to ? edge->to : "");
  if (edge) printf(" target:%s", z_program_graph_edge_target_name(edge->target));
  if (other) printf(" node:%s %s", z_program_graph_node_kind_name(other->kind), other->name ? other->name : "");
  printf("\n");
}

static void query_print_node_neighborhood(const ZProgramGraph *graph, const char *node_id) {
  if (!node_id || !node_id[0]) return;
  const ZProgramGraphNode *node = z_program_graph_query_node_by_id(graph, node_id);
  printf("\nnode:\n");
  if (!node) {
    printf("  (not found) %s\n", node_id);
    return;
  }
  query_print_match_line(node);
  printf("parents:\n");
  size_t parent_count = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !z_program_graph_query_text_eq(edge->to, node_id)) continue;
    query_print_edge_line(graph, edge, true);
    parent_count++;
  }
  if (parent_count == 0) printf("  (none)\n");
  printf("children:\n");
  size_t child_count = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!z_program_graph_query_text_eq(edge->from, node_id)) continue;
    query_print_edge_line(graph, edge, false);
    child_count++;
  }
  if (child_count == 0) printf("  (none)\n");
}

void z_program_graph_print_query_text(const ZProgramGraph *graph, const char *input, const char *artifact, const char *input_kind, const char *query_function, const char *query_find, const char *query_refs, const char *query_calls, const char *query_node) {
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  bool resolved = z_program_graph_collect_resolution_facts(graph, &resolution);
  printf("program graph query\n");
  printf("input: %s\n", input ? input : "");
  printf("source: %s\n", input_kind ? input_kind : "");
  if (artifact && input && !z_program_graph_query_text_eq(artifact, input)) printf("artifact: %s\n", artifact);
  printf("module: %s\n", graph && graph->module_identity ? graph->module_identity : "");
  printf("hash: %s\n", graph && graph->graph_hash ? graph->graph_hash : "");
  printf("counts: %zu nodes, %zu edges\n\n", graph ? graph->node_len : 0, graph ? graph->edge_len : 0);
  printf("resolution: %s, %zu refs\n", resolved ? "ok" : "failed", resolution.reference_len);
  if (query_function || query_find || query_refs || query_calls || query_node) {
    printf("query:");
    if (query_function) printf(" fn:%s", query_function);
    if (query_find) printf(" find:%s", query_find);
    if (query_refs) printf(" refs:%s", query_refs);
    if (query_calls) printf(" calls:%s", query_calls);
    if (query_node) printf(" node:%s", query_node);
    printf("\n\n");
  }
  printf("modules:\n");
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE) printf("  %s %s path:%s\n", node->name ? node->name : "", node->id ? node->id : "", node->path ? node->path : "");
  }
  printf("\nfunctions:\n");
  size_t printed_functions = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) continue;
    if (!z_program_graph_query_function_selected(node, query_function) || !z_program_graph_query_function_contains_match(graph, node, query_find)) continue;
    const ZProgramGraphNode *body = z_program_graph_query_child_node(graph, node->id, "body", 0);
    printed_functions++;
    printf("  %s %s -> %s%s%s body:%s\n", node->name ? node->name : "", node->id ? node->id : "", node->type ? node->type : "Unknown", node->fallible ? " raises" : "", node->value ? " test" : "", body && body->id ? body->id : "");
    size_t param_count = z_program_graph_query_child_count(graph, node->id, "param");
    for (size_t order = 0; order < param_count; order++) {
      const ZProgramGraphNode *param = z_program_graph_query_child_node(graph, node->id, "param", order);
      if (param) printf("    param[%zu] %s: %s %s\n", order, param->name ? param->name : "", param->type ? param->type : "Unknown", param->id ? param->id : "");
    }
    size_t statement_count = z_program_graph_query_child_count(graph, body ? body->id : NULL, "statement");
    for (size_t order = 0; order < statement_count; order++) {
      const ZProgramGraphNode *stmt = z_program_graph_query_child_node(graph, body ? body->id : NULL, "statement", order);
      if (!stmt) continue;
      printf("    stmt[%zu] %s %s", order, z_program_graph_node_kind_name(stmt->kind), stmt->id ? stmt->id : "");
      query_print_text_field("name", stmt->name);
      query_print_text_field("type", stmt->type);
      query_print_text_field("value", stmt->value);
      printf("\n");
    }
    if (query_function || query_calls) z_program_graph_query_print_function_calls_text(graph, &resolution, node, query_calls, "    ");
  }
  if (printed_functions == 0) printf("  (none)\n");
  if (query_find && query_find[0]) {
    printf("\nmatches:\n");
    size_t match_count = 0;
    for (size_t i = 0; graph && i < graph->node_len; i++) {
      const ZProgramGraphNode *node = &graph->nodes[i];
      if (!z_program_graph_query_node_matches_find(node, query_find)) continue;
      query_print_match_line(node);
      match_count++;
    }
    if (match_count == 0) printf("  (none)\n");
  }
  z_program_graph_query_print_reference_section_text(graph, &resolution, "calls", query_calls, true, query_function);
  z_program_graph_query_print_reference_section_text(graph, &resolution, "references", query_refs, false, query_function);
  query_print_node_neighborhood(graph, query_node);
  printf("\npatch help:\n");
  printf("  zero graph patch --op help\n");
  printf("common checked edits:\n");
  printf("  zero graph patch --op 'set node=\"#node_id\" field=\"value\" expect=\"old\" value=\"new\"'\n");
  printf("  zero graph patch --op 'rename node=\"#node_id\" expect=\"old\" value=\"new\"'\n");
  printf("  zero graph patch --op 'delete node=\"#node_id\"'\n");
  z_program_graph_resolution_facts_free(&resolution);
}
