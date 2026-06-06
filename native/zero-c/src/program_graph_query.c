#include "program_graph_query.h"
#include "program_graph_query_internal.h"
#include "program_graph_patch.h"

#include <string.h>

bool z_program_graph_query_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

const ZProgramGraphNode *z_program_graph_query_node_by_id(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (z_program_graph_query_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static void query_append_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  const unsigned char *p = (const unsigned char *)(value ? value : "");
  while (*p) {
    unsigned char ch = *p++;
    switch (ch) {
      case '"': zbuf_append(buf, "\\\""); break;
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (ch < 0x20) zbuf_appendf(buf, "\\u%04x", ch);
        else zbuf_append_char(buf, (char)ch);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static void query_append_json_string_or_null(ZBuf *buf, const char *value) {
  if (value) query_append_json_string(buf, value);
  else zbuf_append(buf, "null");
}

static const ZProgramGraphEdge *query_child_edge(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        edge->order == order &&
        z_program_graph_query_text_eq(edge->from, from) &&
        z_program_graph_query_text_eq(edge->kind, kind)) {
      return edge;
    }
  }
  return NULL;
}

const ZProgramGraphNode *z_program_graph_query_child_node(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  const ZProgramGraphEdge *edge = query_child_edge(graph, from, kind, order);
  return edge ? z_program_graph_query_node_by_id(graph, edge->to) : NULL;
}

size_t z_program_graph_query_child_count(const ZProgramGraph *graph, const char *from, const char *kind) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        z_program_graph_query_text_eq(edge->from, from) &&
        z_program_graph_query_text_eq(edge->kind, kind)) {
      count++;
    }
  }
  return count;
}

static bool query_text_contains(const char *text, const char *needle) {
  return !needle || !needle[0] || (text && strstr(text, needle));
}

bool z_program_graph_query_node_matches_find(const ZProgramGraphNode *node, const char *find) {
  if (!find || !find[0]) return true;
  return node &&
         (query_text_contains(node->id, find) ||
          query_text_contains(node->name, find) ||
          query_text_contains(node->type, find) ||
          query_text_contains(node->value, find) ||
          query_text_contains(node->path, find) ||
          query_text_contains(z_program_graph_node_kind_name(node->kind), find));
}

bool z_program_graph_query_function_selected(const ZProgramGraphNode *function, const char *filter) {
  if (!filter || !filter[0]) return true;
  return function &&
         (query_text_contains(function->name, filter) ||
          query_text_contains(function->id, filter) ||
          query_text_contains(function->value, filter));
}

static bool query_subtree_contains_match(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *find, unsigned depth) {
  if (!node || depth > 64) return false;
  if (z_program_graph_query_node_matches_find(node, find)) return true;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !z_program_graph_query_text_eq(edge->from, node->id)) continue;
    if (query_subtree_contains_match(graph, z_program_graph_query_node_by_id(graph, edge->to), find, depth + 1)) return true;
  }
  return false;
}

bool z_program_graph_query_function_contains_match(const ZProgramGraph *graph, const ZProgramGraphNode *function, const char *find) {
  if (!find || !find[0]) return true;
  if (z_program_graph_query_node_matches_find(function, find)) return true;
  size_t param_count = z_program_graph_query_child_count(graph, function ? function->id : NULL, "param");
  for (size_t order = 0; order < param_count; order++) {
    if (z_program_graph_query_node_matches_find(z_program_graph_query_child_node(graph, function ? function->id : NULL, "param", order), find)) return true;
  }
  const ZProgramGraphNode *body = z_program_graph_query_child_node(graph, function ? function->id : NULL, "body", 0);
  return query_subtree_contains_match(graph, body, find, 0);
}

static void query_append_params_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  zbuf_append_char(buf, '[');
  bool first = true;
  size_t count = z_program_graph_query_child_count(graph, function ? function->id : NULL, "param");
  for (size_t order = 0; order < count; order++) {
    const ZProgramGraphNode *param = z_program_graph_query_child_node(graph, function ? function->id : NULL, "param", order);
    if (!param) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    zbuf_append(buf, "{\"id\":");
    query_append_json_string(buf, param->id);
    zbuf_append(buf, ",\"name\":");
    query_append_json_string(buf, param->name);
    zbuf_append(buf, ",\"type\":");
    query_append_json_string(buf, param->type);
    zbuf_appendf(buf, ",\"order\":%zu}", order);
  }
  zbuf_append_char(buf, ']');
}

static void query_append_statements_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *body) {
  zbuf_append_char(buf, '[');
  bool first = true;
  size_t count = z_program_graph_query_child_count(graph, body ? body->id : NULL, "statement");
  for (size_t order = 0; order < count; order++) {
    const ZProgramGraphNode *stmt = z_program_graph_query_child_node(graph, body ? body->id : NULL, "statement", order);
    if (!stmt) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    zbuf_append(buf, "{\"id\":");
    query_append_json_string(buf, stmt->id);
    zbuf_append(buf, ",\"kind\":");
    query_append_json_string(buf, z_program_graph_node_kind_name(stmt->kind));
    zbuf_append(buf, ",\"name\":");
    query_append_json_string_or_null(buf, stmt->name);
    zbuf_append(buf, ",\"type\":");
    query_append_json_string_or_null(buf, stmt->type);
    zbuf_append(buf, ",\"value\":");
    query_append_json_string_or_null(buf, stmt->value);
    zbuf_appendf(buf, ",\"order\":%zu}", order);
  }
  zbuf_append_char(buf, ']');
}

static void query_append_match_json(ZBuf *buf, const ZProgramGraphNode *node) {
  zbuf_append(buf, "{\"id\":");
  query_append_json_string(buf, node ? node->id : "");
  zbuf_append(buf, ",\"kind\":");
  query_append_json_string(buf, node ? z_program_graph_node_kind_name(node->kind) : "");
  zbuf_append(buf, ",\"name\":");
  query_append_json_string_or_null(buf, node ? node->name : NULL);
  zbuf_append(buf, ",\"type\":");
  query_append_json_string_or_null(buf, node ? node->type : NULL);
  zbuf_append(buf, ",\"value\":");
  query_append_json_string_or_null(buf, node ? node->value : NULL);
  zbuf_append(buf, ",\"path\":");
  query_append_json_string_or_null(buf, node ? node->path : NULL);
  zbuf_appendf(buf, ",\"line\":%d,\"column\":%d}", node ? node->line : 0, node ? node->column : 0);
}

static void query_append_matches_json(ZBuf *buf, const ZProgramGraph *graph, const char *find) {
  zbuf_append_char(buf, '[');
  if (!find || !find[0]) {
    zbuf_append_char(buf, ']');
    return;
  }
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!z_program_graph_query_node_matches_find(node, find)) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    query_append_match_json(buf, node);
  }
  zbuf_append_char(buf, ']');
}

static void query_append_edge_ref_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphEdge *edge, bool parent_edge) {
  zbuf_append(buf, "{\"kind\":");
  query_append_json_string(buf, edge ? edge->kind : "");
  zbuf_appendf(buf, ",\"order\":%zu,\"target\":", edge ? edge->order : 0);
  query_append_json_string(buf, edge ? z_program_graph_edge_target_name(edge->target) : "");
  zbuf_append(buf, ",\"from\":");
  query_append_json_string(buf, edge ? edge->from : "");
  zbuf_append(buf, ",\"to\":");
  query_append_json_string(buf, edge ? edge->to : "");
  const ZProgramGraphNode *other = NULL;
  if (edge && edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) other = z_program_graph_query_node_by_id(graph, parent_edge ? edge->from : edge->to);
  zbuf_append(buf, ",\"node\":");
  if (other) query_append_match_json(buf, other);
  else zbuf_append(buf, "null");
  zbuf_append_char(buf, '}');
}

static void query_append_node_neighborhood_json(ZBuf *buf, const ZProgramGraph *graph, const char *node_id) {
  const ZProgramGraphNode *node = z_program_graph_query_node_by_id(graph, node_id);
  if (!node) {
    zbuf_append(buf, "null");
    return;
  }
  zbuf_append(buf, "{\"selected\":");
  query_append_match_json(buf, node);
  zbuf_append(buf, ",\"parents\":[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !z_program_graph_query_text_eq(edge->to, node_id)) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    query_append_edge_ref_json(buf, graph, edge, true);
  }
  zbuf_append(buf, "],\"children\":[");
  first = true;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!z_program_graph_query_text_eq(edge->from, node_id)) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    query_append_edge_ref_json(buf, graph, edge, false);
  }
  zbuf_append(buf, "]}");
}

void z_program_graph_append_query_json(ZBuf *buf, const ZProgramGraph *graph, const char *input, const char *artifact, const char *input_kind, const char *query_function, const char *query_find, const char *query_refs, const char *query_calls, const char *query_node) {
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  bool resolved = z_program_graph_collect_resolution_facts(graph, &resolution);
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"input\": ");
  query_append_json_string(buf, input);
  zbuf_append(buf, ",\n  \"artifact\": ");
  query_append_json_string(buf, artifact);
  zbuf_append(buf, ",\n  \"inputKind\": ");
  query_append_json_string(buf, input_kind);
  zbuf_append(buf, ",\n  \"moduleIdentity\": ");
  query_append_json_string(buf, graph ? graph->module_identity : "");
  zbuf_append(buf, ",\n  \"graphHash\": ");
  query_append_json_string(buf, graph ? graph->graph_hash : "");
  zbuf_appendf(buf, ",\n  \"counts\": {\"nodes\": %zu, \"edges\": %zu}", graph ? graph->node_len : 0, graph ? graph->edge_len : 0);
  zbuf_append(buf, ",\n  \"query\": {\"function\": ");
  query_append_json_string_or_null(buf, query_function);
  zbuf_append(buf, ", \"find\": ");
  query_append_json_string_or_null(buf, query_find);
  zbuf_append(buf, ", \"refs\": ");
  query_append_json_string_or_null(buf, query_refs);
  zbuf_append(buf, ", \"calls\": ");
  query_append_json_string_or_null(buf, query_calls);
  zbuf_append(buf, ", \"node\": ");
  query_append_json_string_or_null(buf, query_node);
  zbuf_append(buf, "}");
  zbuf_appendf(buf, ",\n  \"resolution\": {\"ok\": %s, \"references\": %zu}", resolved ? "true" : "false", resolution.reference_len);
  zbuf_append(buf, ",\n  \"modules\": [");
  bool first_module = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    if (!first_module) zbuf_append(buf, ", ");
    first_module = false;
    zbuf_append(buf, "{\"id\":");
    query_append_json_string(buf, node->id);
    zbuf_append(buf, ",\"name\":");
    query_append_json_string(buf, node->name);
    zbuf_append(buf, ",\"path\":");
    query_append_json_string(buf, node->path);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"functions\": [");
  bool first_function = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) continue;
    if (!z_program_graph_query_function_selected(node, query_function) || !z_program_graph_query_function_contains_match(graph, node, query_find)) continue;
    const ZProgramGraphNode *body = z_program_graph_query_child_node(graph, node->id, "body", 0);
    if (!first_function) zbuf_append(buf, ", ");
    first_function = false;
    zbuf_append(buf, "{\"id\":");
    query_append_json_string(buf, node->id);
    zbuf_append(buf, ",\"name\":");
    query_append_json_string(buf, node->name);
    zbuf_append(buf, ",\"returnType\":");
    query_append_json_string_or_null(buf, node->type);
    zbuf_appendf(buf, ",\"public\":%s,\"fallible\":%s,\"test\":", node->is_public ? "true" : "false", node->fallible ? "true" : "false");
    query_append_json_string_or_null(buf, node->value);
    zbuf_append(buf, ",\"body\":");
    query_append_json_string_or_null(buf, body ? body->id : NULL);
    zbuf_append(buf, ",\"params\":");
    query_append_params_json(buf, graph, node);
    zbuf_append(buf, ",\"statements\":");
    query_append_statements_json(buf, graph, body);
    zbuf_append(buf, ",\"calls\":");
    if (query_function || query_calls) z_program_graph_query_append_function_calls_json(buf, graph, &resolution, node, query_calls);
    else zbuf_append(buf, "[]");
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"matches\": ");
  query_append_matches_json(buf, graph, query_find);
  zbuf_append(buf, ",\n  \"references\": ");
  z_program_graph_query_append_reference_list_json(buf, graph, &resolution, query_refs, false, query_refs ? query_function : NULL);
  zbuf_append(buf, ",\n  \"calls\": ");
  z_program_graph_query_append_reference_list_json(buf, graph, &resolution, query_calls, true, query_calls ? query_function : NULL);
  zbuf_append(buf, ",\n  \"node\": ");
  query_append_node_neighborhood_json(buf, graph, query_node);
  zbuf_append(buf, ",\n  \"patchOperations\": [");
  const char *const *ops = z_program_graph_patch_operation_examples();
  for (size_t i = 0; ops[i]; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    query_append_json_string(buf, ops[i]);
  }
  zbuf_append(buf, "]\n}\n");
  z_program_graph_resolution_facts_free(&resolution);
}
