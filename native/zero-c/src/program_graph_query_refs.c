#include "program_graph_query_internal.h"

#include <stdio.h>
#include <string.h>

static bool query_ref_text_contains(const char *text, const char *needle) {
  return !needle || !needle[0] || (text && strstr(text, needle));
}

static bool query_ref_text_present(const char *text) {
  return text && text[0];
}

static void query_refs_append_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; p++) {
    unsigned char ch = *p;
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

static void query_refs_append_json_string_or_null(ZBuf *buf, const char *value) {
  if (value) query_refs_append_json_string(buf, value);
  else zbuf_append(buf, "null");
}

static const ZProgramGraphEdge *query_refs_owner_edge(const ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && node_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && z_program_graph_query_text_eq(edge->to, node_id)) return edge;
  }
  return NULL;
}

const ZProgramGraphNode *z_program_graph_query_enclosing_function(const ZProgramGraph *graph, const char *node_id) {
  const char *current = node_id;
  for (size_t depth = 0; graph && current && depth < graph->node_len; depth++) {
    const ZProgramGraphEdge *owner = query_refs_owner_edge(graph, current);
    if (!owner) return NULL;
    const ZProgramGraphNode *node = z_program_graph_query_node_by_id(graph, owner->from);
    if (!node) return NULL;
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION) return node;
    current = node->id;
  }
  return NULL;
}

bool z_program_graph_query_reference_matches(const ZProgramGraphResolutionReference *ref, const char *filter) {
  if (!filter || !filter[0]) return true;
  return ref &&
         (query_ref_text_contains(ref->kind, filter) ||
          query_ref_text_contains(ref->name, filter) ||
          query_ref_text_contains(ref->qualified_name, filter) ||
          query_ref_text_contains(ref->target_kind, filter) ||
          query_ref_text_contains(ref->target_node, filter) ||
          query_ref_text_contains(ref->symbol_id, filter) ||
          query_ref_text_contains(ref->via_import, filter));
}

static bool query_refs_function_matches(const ZProgramGraph *graph, const ZProgramGraphResolutionReference *ref, const char *function_filter) {
  if (!function_filter || !function_filter[0]) return true;
  const ZProgramGraphNode *function = z_program_graph_query_enclosing_function(graph, ref ? ref->node_id : NULL);
  return z_program_graph_query_function_selected(function, function_filter);
}

static bool query_refs_under_function(const ZProgramGraph *graph, const ZProgramGraphResolutionReference *ref, const ZProgramGraphNode *function) {
  const ZProgramGraphNode *owner = z_program_graph_query_enclosing_function(graph, ref ? ref->node_id : NULL);
  return owner && function && z_program_graph_query_text_eq(owner->id, function->id);
}

static bool query_refs_include(const ZProgramGraph *graph, const ZProgramGraphResolutionReference *ref, const char *filter, bool calls_only, const char *function_filter) {
  return ref &&
         (!calls_only || z_program_graph_query_text_eq(ref->kind, "call")) &&
         query_refs_function_matches(graph, ref, function_filter) &&
         z_program_graph_query_reference_matches(ref, filter);
}

static void query_refs_append_one_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphResolutionReference *ref) {
  const ZProgramGraphNode *function = z_program_graph_query_enclosing_function(graph, ref ? ref->node_id : NULL);
  zbuf_append(buf, "{\"node\":");
  query_refs_append_json_string_or_null(buf, ref ? ref->node_id : NULL);
  zbuf_append(buf, ",\"kind\":");
  query_refs_append_json_string_or_null(buf, ref ? ref->kind : NULL);
  zbuf_append(buf, ",\"name\":");
  query_refs_append_json_string_or_null(buf, ref ? ref->name : NULL);
  zbuf_append(buf, ",\"qualifiedName\":");
  query_refs_append_json_string_or_null(buf, ref ? ref->qualified_name : NULL);
  zbuf_append(buf, ",\"function\":");
  query_refs_append_json_string_or_null(buf, function ? function->name : NULL);
  zbuf_append(buf, ",\"targetKind\":");
  query_refs_append_json_string_or_null(buf, ref ? ref->target_kind : NULL);
  zbuf_append(buf, ",\"targetNode\":");
  query_refs_append_json_string_or_null(buf, ref ? ref->target_node : NULL);
  zbuf_append(buf, ",\"symbolId\":");
  query_refs_append_json_string_or_null(buf, ref ? ref->symbol_id : NULL);
  zbuf_append(buf, ",\"viaImport\":");
  query_refs_append_json_string_or_null(buf, ref ? ref->via_import : NULL);
  zbuf_appendf(buf, ",\"resolved\":%s,\"ambiguous\":%s}", ref && ref->resolved ? "true" : "false", ref && ref->ambiguous ? "true" : "false");
}

void z_program_graph_query_append_reference_list_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *filter, bool calls_only, const char *function_filter) {
  zbuf_append_char(buf, '[');
  if ((!filter || !filter[0]) && (!function_filter || !function_filter[0])) {
    zbuf_append_char(buf, ']');
    return;
  }
  bool first = true;
  for (size_t i = 0; resolution && i < resolution->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &resolution->references[i];
    if (!query_refs_include(graph, ref, filter, calls_only, function_filter)) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    query_refs_append_one_json(buf, graph, ref);
  }
  zbuf_append_char(buf, ']');
}

void z_program_graph_query_append_function_calls_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *function, const char *filter) {
  zbuf_append_char(buf, '[');
  bool first = true;
  for (size_t i = 0; resolution && function && i < resolution->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &resolution->references[i];
    if (!z_program_graph_query_text_eq(ref->kind, "call") || !query_refs_under_function(graph, ref, function) || !z_program_graph_query_reference_matches(ref, filter)) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    query_refs_append_one_json(buf, graph, ref);
  }
  zbuf_append_char(buf, ']');
}

static void query_refs_print_one(const ZProgramGraph *graph, const ZProgramGraphResolutionReference *ref, const char *indent) {
  const ZProgramGraphNode *function = z_program_graph_query_enclosing_function(graph, ref ? ref->node_id : NULL);
  printf("%s%s %s", indent ? indent : "", ref && ref->kind ? ref->kind : "ref", ref && ref->node_id ? ref->node_id : "");
  if (function && query_ref_text_present(function->name)) printf(" fn:%s", function->name);
  if (ref && query_ref_text_present(ref->name)) printf(" name:%s", ref->name);
  if (ref && query_ref_text_present(ref->qualified_name)) printf(" qualified:%s", ref->qualified_name);
  if (ref && query_ref_text_present(ref->target_kind)) printf(" target:%s", ref->target_kind);
  if (ref && query_ref_text_present(ref->target_node)) printf(" node:%s", ref->target_node);
  if (ref && query_ref_text_present(ref->symbol_id)) printf(" symbol:%s", ref->symbol_id);
  if (ref && query_ref_text_present(ref->via_import)) printf(" import:%s", ref->via_import);
  printf(" resolved:%s", ref && ref->resolved ? "true" : "false");
  if (ref && ref->ambiguous) printf(" ambiguous:true");
  printf("\n");
}

void z_program_graph_query_print_function_calls_text(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *function, const char *filter, const char *indent) {
  size_t count = 0;
  for (size_t i = 0; resolution && function && i < resolution->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &resolution->references[i];
    if (!z_program_graph_query_text_eq(ref->kind, "call") || !query_refs_under_function(graph, ref, function) || !z_program_graph_query_reference_matches(ref, filter)) continue;
    if (count == 0) printf("%scalls:\n", indent ? indent : "");
    query_refs_print_one(graph, ref, indent && indent[0] ? "      " : "  ");
    count++;
  }
}

void z_program_graph_query_print_reference_section_text(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *title, const char *filter, bool calls_only, const char *function_filter) {
  if (!filter || !filter[0]) return;
  printf("\n%s:\n", title ? title : "references");
  size_t count = 0;
  for (size_t i = 0; resolution && i < resolution->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &resolution->references[i];
    if (!query_refs_include(graph, ref, filter, calls_only, function_filter)) continue;
    query_refs_print_one(graph, ref, "  ");
    count++;
  }
  if (count == 0) printf("  (none)\n");
}
