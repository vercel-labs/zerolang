#include "program_graph_query.h"
#include "program_graph_query_internal.h"
#include "program_graph_patch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { QUERY_TEXT_MATCH_LIMIT = 200 };

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

static void query_print_child_edges(const ZProgramGraph *graph, const char *node_id, size_t depth, size_t indent_level) {
  size_t child_count = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!z_program_graph_query_text_eq(edge->from, node_id)) continue;
    for (size_t pad = 0; pad < indent_level; pad++) printf("  ");
    query_print_edge_line(graph, edge, false);
    child_count++;
    if (depth > 1 && edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) {
      query_print_child_edges(graph, edge->to, depth - 1, indent_level + 1);
    }
  }
  if (child_count == 0 && indent_level == 0) printf("  (none)\n");
}

static void query_print_node_neighborhood(const ZProgramGraph *graph, const char *node_id, size_t depth) {
  if (!node_id || !node_id[0]) return;
  const ZProgramGraphNode *node = z_program_graph_query_node_by_id(graph, node_id);
  printf("\nnode:\n");
  if (!node) {
    printf("  (not found) %s\n", node_id);
    printf("  tip: run zero query --find <text> to locate node ids, or zero view --fn <name> for one function's source\n");
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
  query_print_child_edges(graph, node_id, depth ? depth : 1, 0);
}

static void query_print_compact_header(const ZProgramGraph *graph) {
  printf("module: %s hash: %s\n", graph && graph->module_identity ? graph->module_identity : "", graph && graph->graph_hash ? graph->graph_hash : "");
}

static void query_print_scoped_node_text(const ZProgramGraph *graph, const ZProgramGraphQueryRequest *request) {
  size_t depth = request->node_depth ? request->node_depth : 1;
  printf("query: node:%s depth:%zu\n", request->node ? request->node : "", depth);
  query_print_compact_header(graph);
  query_print_node_neighborhood(graph, request->node, depth);
  printf("\ntip: use --depth <n> for a deeper subtree, --full for the whole-module report, or zero view --fn <name> for one function's source\n");
}

static void query_print_function_signature_line(const ZProgramGraph *graph, const ZProgramGraphNode *node, const ZProgramGraphNode *body) {
  printf("  %s(", node->name ? node->name : "");
  size_t param_count = z_program_graph_query_child_count(graph, node->id, "param");
  for (size_t order = 0; order < param_count; order++) {
    const ZProgramGraphNode *param = z_program_graph_query_child_node(graph, node->id, "param", order);
    if (!param) continue;
    if (order > 0) printf(", ");
    printf("%s: %s", param->name ? param->name : "", param->type ? param->type : "Unknown");
  }
  printf(") -> %s%s%s %s body:%s\n", node->type ? node->type : "Unknown", node->fallible ? " raises" : "", node->value ? " test" : "", node->id ? node->id : "", body && body->id ? body->id : "");
}

static void query_print_function_handles(const ZProgramGraph *graph, const ZProgramGraphNode *node, const ZProgramGraphNode *body) {
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
}

typedef struct {
  const char *name;
  const char *target_node;
  size_t count;
} QueryCallSummary;

static void query_print_function_calls_summary(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *function, const char *filter) {
  QueryCallSummary *items = NULL;
  size_t item_len = 0;
  size_t item_cap = 0;
  for (size_t i = 0; resolution && function && i < resolution->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &resolution->references[i];
    if (!z_program_graph_query_text_eq(ref->kind, "call")) continue;
    const ZProgramGraphNode *owner = z_program_graph_query_enclosing_function(graph, ref->node_id);
    if (!owner || !z_program_graph_query_text_eq(owner->id, function->id)) continue;
    if (!z_program_graph_query_reference_matches(ref, filter)) continue;
    const char *name = ref->qualified_name && ref->qualified_name[0] ? ref->qualified_name : ref->name;
    bool found = false;
    for (size_t known = 0; known < item_len; known++) {
      if (z_program_graph_query_text_eq(items[known].name, name) && z_program_graph_query_text_eq(items[known].target_node, ref->target_node)) {
        items[known].count++;
        found = true;
        break;
      }
    }
    if (found) continue;
    if (item_len == item_cap) {
      item_cap = item_cap ? item_cap * 2 : 16;
      items = z_checked_reallocarray(items, item_cap, sizeof(QueryCallSummary));
    }
    items[item_len].name = name;
    items[item_len].target_node = ref->target_node;
    items[item_len].count = 1;
    item_len++;
  }
  if (item_len > 0) {
    printf("    calls:\n");
    for (size_t i = 0; i < item_len; i++) {
      printf("      %s", items[i].name ? items[i].name : "");
      if (items[i].count > 1) printf(" x%zu", items[i].count);
      if (items[i].target_node && items[i].target_node[0]) printf(" -> %s", items[i].target_node);
      printf("\n");
    }
  }
  free(items);
}

static size_t query_print_functions_section(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphQueryRequest *request, bool show_handles) {
  printf("functions:\n");
  size_t printed_functions = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) continue;
    if (!z_program_graph_query_function_selected(node, request->function) || !z_program_graph_query_function_contains_match(graph, node, request->find)) continue;
    const ZProgramGraphNode *body = z_program_graph_query_child_node(graph, node->id, "body", 0);
    printed_functions++;
    query_print_function_signature_line(graph, node, body);
    if (show_handles) query_print_function_handles(graph, node, body);
    if (request->function || request->calls) {
      if (show_handles) z_program_graph_query_print_function_calls_text(graph, resolution, node, request->calls, "    ");
      else query_print_function_calls_summary(graph, resolution, node, request->calls);
    }
  }
  if (printed_functions == 0) printf("  (none)\n");
  return printed_functions;
}

static void query_print_matches_section(const ZProgramGraph *graph, const char *query_find) {
  printf("\nmatches:\n");
  size_t match_count = 0;
  size_t total_count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!z_program_graph_query_node_matches_find(node, query_find)) continue;
    total_count++;
    if (match_count >= QUERY_TEXT_MATCH_LIMIT) continue;
    query_print_match_line(node);
    match_count++;
  }
  if (match_count == 0) printf("  (none)\n");
  if (total_count > match_count) printf("  ... +%zu more matches; refine --find <text> or use --json for the full list\n", total_count - match_count);
}

static void query_print_patch_footer(void) {
  printf("\npatch help:\n");
  printf("  zero patch --op help\n");
  printf("common checked edits:\n");
  printf("  zero patch --op 'set node=\"#node_id\" field=\"value\" expect=\"old\" value=\"new\"'\n");
  printf("  zero patch --op 'insert node=\"#new_id\" kind=\"Literal\" parent=\"#parent\" edge=\"arg\" order=\"0\" type=\"String\" value=\"text\"'\n");
  printf("  zero patch --op 'replace node=\"#node_id\" expect=\"nodehash:abc123\" kind=\"Literal\" type=\"String\" value=\"text\"'\n");
  printf("  zero patch --op 'rename node=\"#node_id\" expect=\"old\" value=\"new\"'\n");
  printf("  zero patch --op 'delete node=\"#node_id\"'\n");
  printf("larger edits:\n  write zero-program-graph-patch v1 text under /tmp or pass --patch-text; all supported operations are:\n");
  const char *const *ops = z_program_graph_patch_operation_examples();
  for (size_t i = 0; ops[i]; i++) printf("  - %s\n", ops[i]);
}

static const ZProgramGraphNode *query_overview_entry_module(const ZProgramGraph *graph) {
  const ZProgramGraphNode *fallback = NULL;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_MODULE) fallback = &graph->nodes[i];
  }
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || !z_program_graph_query_text_eq(node->name, "main")) continue;
    for (size_t j = 0; j < graph->edge_len; j++) {
      const ZProgramGraphEdge *edge = &graph->edges[j];
      if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !z_program_graph_query_text_eq(edge->kind, "function") || !z_program_graph_query_text_eq(edge->to, node->id)) continue;
      const ZProgramGraphNode *module = z_program_graph_query_node_by_id(graph, edge->from);
      if (module && module->kind == Z_PROGRAM_GRAPH_NODE_MODULE) return module;
    }
  }
  return fallback;
}

/*
 * Compact bare-overview report: module list with per-module function counts,
 * entry-module signatures only, and a short usage footer. Scoped reports and
 * --full keep the detailed sections, and --json is unchanged.
 */
static void query_print_overview_text(const ZProgramGraph *graph, const char *input, const char *artifact, const char *input_kind) {
  printf("program graph query\n");
  printf("input: %s\n", input ? input : "");
  printf("source: %s\n", input_kind ? input_kind : "");
  if (artifact && input && !z_program_graph_query_text_eq(artifact, input)) printf("artifact: %s\n", artifact);
  printf("module: %s\n", graph && graph->module_identity ? graph->module_identity : "");
  printf("hash: %s\n", graph && graph->graph_hash ? graph->graph_hash : "");
  const ZProgramGraphNode *entry = query_overview_entry_module(graph);
  printf("\nmodules:\n");
  size_t module_count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    module_count++;
    printf("  %s path:%s functions:%zu%s\n", node->name ? node->name : "", node->path ? node->path : "", z_program_graph_query_child_count(graph, node->id, "function"), entry == node ? " (entry)" : "");
  }
  if (module_count == 0) printf("  (none)\n");
  printf("\nfunctions (module %s):\n", entry && entry->name ? entry->name : "");
  size_t printed = 0;
  size_t entry_function_count = entry ? z_program_graph_query_child_count(graph, entry->id, "function") : 0;
  for (size_t order = 0; order < entry_function_count; order++) {
    const ZProgramGraphNode *node = z_program_graph_query_child_node(graph, entry->id, "function", order);
    if (!node) continue;
    query_print_function_signature_line(graph, node, z_program_graph_query_child_node(graph, node->id, "body", 0));
    printed++;
  }
  if (printed == 0) printf("  (none)\n");
  printf("\ntips: zero query --fn <name> | --find <text> | --calls <name> | --refs <name> scope the report\n");
  printf("  zero view --fn <name> prints one function's source; zero view --outline <module> lists signatures\n");
  printf("  zero patch --op help lists checked edit operations\n");
}

void z_program_graph_print_query_text(const ZProgramGraph *graph, const char *input, const char *artifact, const char *input_kind, const ZProgramGraphQueryRequest *request) {
  static const ZProgramGraphQueryRequest empty_request = {0};
  if (!request) request = &empty_request;
  const char *query_function = request->function;
  const char *query_find = request->find;
  const char *query_refs = request->refs;
  const char *query_calls = request->calls;
  const char *query_node = request->node;
  if (query_node && !request->full_module) {
    query_print_scoped_node_text(graph, request);
    return;
  }
  if (!request->full_module && !query_function && !query_find && !query_refs && !query_calls) {
    query_print_overview_text(graph, input, artifact, input_kind);
    return;
  }
  const bool scoped = (query_function || query_find || query_refs || query_calls || query_node) && !request->full_module;
  const bool show_handles = request->handles || request->full_module;
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  bool resolved = z_program_graph_collect_resolution_facts(graph, &resolution);
  if (scoped) {
    printf("query:");
    if (query_function) printf(" fn:%s", query_function);
    if (query_find) printf(" find:%s", query_find);
    if (query_refs) printf(" refs:%s", query_refs);
    if (query_calls) printf(" calls:%s", query_calls);
    printf("\n");
    query_print_compact_header(graph);
    if (!resolved) printf("resolution: failed, %zu refs\n", resolution.reference_len);
    printf("\n");
    if (request->bare_argument) {
      printf("argument: %s is not an existing path; treated as --find %s\n", request->bare_argument, request->bare_argument);
      printf("tip: zero view --fn <name> prints one function's source\n\n");
    }
    if (query_function || query_find) {
      size_t printed = query_print_functions_section(graph, &resolution, request, show_handles);
      if (query_function && printed > 0 && !show_handles) printf("\ntip: add --handles to list stmt and param patch handles; patch ops: zero patch --op help\n");
    }
    if (query_find && query_find[0]) query_print_matches_section(graph, query_find);
    z_program_graph_query_print_reference_section_text(graph, &resolution, "calls", query_calls, true, query_function);
    z_program_graph_query_print_reference_section_text(graph, &resolution, "references", query_refs, false, query_function);
    if (show_handles && request->handles) query_print_patch_footer();
    z_program_graph_resolution_facts_free(&resolution);
    return;
  }
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
    printf("\n");
  }
  printf("\nmodules:\n");
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE) printf("  %s %s path:%s\n", node->name ? node->name : "", node->id ? node->id : "", node->path ? node->path : "");
  }
  printf("\n");
  query_print_functions_section(graph, &resolution, request, show_handles);
  if (query_find && query_find[0]) query_print_matches_section(graph, query_find);
  z_program_graph_query_print_reference_section_text(graph, &resolution, "calls", query_calls, true, query_function);
  z_program_graph_query_print_reference_section_text(graph, &resolution, "references", query_refs, false, query_function);
  query_print_node_neighborhood(graph, query_node, request->node_depth ? request->node_depth : 1);
  if (show_handles) query_print_patch_footer();
  else printf("\ntips: zero query --fn <name> for one function's facts, --handles for stmt and param patch handles, zero view --fn <name> for source; patch ops: zero patch --op help\n");
  z_program_graph_resolution_facts_free(&resolution);
}
