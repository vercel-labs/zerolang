#include "program_graph_test_caps.h"
#include "std_sig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool pgtc_eq(const char *left, const char *right) {
  const char *a = left ? left : "";
  const char *b = right ? right : "";
  size_t a_len = strlen(a);
  return a_len == strlen(b) && memcmp(a, b, a_len) == 0;
}

static bool pgtc_starts(const char *text, const char *prefix) {
  return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

static const ZProgramGraphNode *pgtc_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) if (pgtc_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  return NULL;
}

static const ZProgramGraphEdge *pgtc_next_edge(const ZProgramGraph *graph, const char *from, const char *kind, bool have_last, size_t last_order) {
  const ZProgramGraphEdge *best = NULL;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !pgtc_eq(edge->from, from) || !pgtc_eq(edge->kind, kind) || (have_last && edge->order <= last_order)) continue;
    if (!best || edge->order < best->order) best = edge;
  }
  return best;
}

static const ZProgramGraphNode *pgtc_child(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->order == order && pgtc_eq(edge->from, from) && pgtc_eq(edge->kind, kind)) return pgtc_node(graph, edge->to);
  }
  return NULL;
}

static bool pgtc_expr_name(const ZProgramGraph *graph, const ZProgramGraphNode *expr, ZBuf *out) {
  if (!expr) return false;
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) { zbuf_append(out, expr->name ? expr->name : ""); return true; }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) {
    if (!pgtc_expr_name(graph, pgtc_child(graph, expr->id, "left", 0), out)) return false;
    zbuf_append_char(out, '.');
    zbuf_append(out, expr->name ? expr->name : "");
    return true;
  }
  if ((expr->kind == Z_PROGRAM_GRAPH_NODE_CALL || expr->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) && expr->name) {
    const ZProgramGraphNode *left = pgtc_child(graph, expr->id, "left", 0);
    if (left && pgtc_expr_name(graph, left, out)) return true;
    zbuf_append(out, expr->name);
    return true;
  }
  return false;
}

static const ZProgramGraphNode *pgtc_function(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && pgtc_eq(node->name, name)) return node;
  }
  return NULL;
}

static bool pgtc_is_test_function(const ZProgramGraphNode *node) {
  return node && node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && pgtc_starts(node->name, "__zero_test_");
}

static const char *pgtc_test_name(const ZProgramGraphNode *fun) {
  return fun && fun->value && fun->value[0] ? fun->value : (fun && fun->name ? fun->name : "");
}

static size_t pgtc_node_index(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  return graph && node && node >= graph->nodes && node < graph->nodes + graph->node_len ? (size_t)(node - graph->nodes) : graph ? graph->node_len : 0;
}

static void pgtc_capability_set(ZProgramGraphCapabilitySummary *caps, const char *capability, const ZProgramGraphNode *node) {
  if (!caps || !capability) return;
#define PGTC_SET_CAP(field) do { caps->field = true; if (!caps->field##_node) caps->field##_node = node; } while (0)
  if (pgtc_eq(capability, "args")) PGTC_SET_CAP(args);
  else if (pgtc_eq(capability, "env")) PGTC_SET_CAP(env);
  else if (pgtc_eq(capability, "fs")) PGTC_SET_CAP(fs);
  else if (pgtc_eq(capability, "memory")) PGTC_SET_CAP(memory);
  else if (pgtc_eq(capability, "alloc")) PGTC_SET_CAP(alloc);
  else if (pgtc_eq(capability, "path")) PGTC_SET_CAP(path);
  else if (pgtc_eq(capability, "codec")) PGTC_SET_CAP(codec);
  else if (pgtc_eq(capability, "parse")) PGTC_SET_CAP(parse);
  else if (pgtc_eq(capability, "time")) PGTC_SET_CAP(time);
  else if (pgtc_eq(capability, "rand")) PGTC_SET_CAP(rand);
  else if (pgtc_eq(capability, "net")) PGTC_SET_CAP(net);
  else if (pgtc_eq(capability, "proc")) PGTC_SET_CAP(proc);
  else if (pgtc_eq(capability, "web")) PGTC_SET_CAP(web);
  else if (pgtc_eq(capability, "world") || pgtc_eq(capability, "io") || pgtc_eq(capability, "stdio")) PGTC_SET_CAP(world);
#undef PGTC_SET_CAP
}

static void pgtc_capability_set_for_type(ZProgramGraphCapabilitySummary *caps, const char *type, const ZProgramGraphNode *node) {
  if (pgtc_eq(type, "World")) pgtc_capability_set(caps, "world", node);
  else if (pgtc_eq(type, "Fs")) pgtc_capability_set(caps, "fs", node);
  else if (pgtc_eq(type, "Net")) pgtc_capability_set(caps, "net", node);
  else if (pgtc_eq(type, "Proc")) pgtc_capability_set(caps, "proc", node);
  else if (pgtc_eq(type, "Clock")) pgtc_capability_set(caps, "time", node);
  else if (pgtc_eq(type, "Rand")) pgtc_capability_set(caps, "rand", node);
}

static void pgtc_capability_set_for_call(ZProgramGraphCapabilitySummary *caps, const char *callee, const ZProgramGraphNode *node) {
  if (!callee) return;
  const ZStdHelperInfo *helper = z_std_helper_find(callee);
  if (helper && helper->capability && !pgtc_eq(helper->capability, "none")) {
    pgtc_capability_set(caps, helper->capability, node);
    return;
  }
  if (pgtc_starts(callee, "std.args.")) pgtc_capability_set(caps, "args", node);
  else if (pgtc_starts(callee, "std.env.")) pgtc_capability_set(caps, "env", node);
  else if (pgtc_starts(callee, "std.fs.")) pgtc_capability_set(caps, "fs", node);
  else if (pgtc_starts(callee, "std.time.")) pgtc_capability_set(caps, "time", node);
  else if (pgtc_starts(callee, "std.rand.")) pgtc_capability_set(caps, "rand", node);
  else if (pgtc_starts(callee, "std.net.")) pgtc_capability_set(caps, "net", node);
  else if (pgtc_starts(callee, "std.proc.")) pgtc_capability_set(caps, "proc", node);
  else if (pgtc_starts(callee, "std.web.")) pgtc_capability_set(caps, "web", node);
  else if (pgtc_starts(callee, "std.parse.")) pgtc_capability_set(caps, "parse", node);
  else if (pgtc_starts(callee, "std.path.")) pgtc_capability_set(caps, "path", node);
  else if (pgtc_starts(callee, "std.fmt.") || pgtc_starts(callee, "std.mem.") || pgtc_starts(callee, "std.testing.")) pgtc_capability_set(caps, "memory", node);
}

static void pgtc_collect_caps_from_node(const ZProgramGraph *graph, const ZProgramGraphNode *node, bool *visited, ZProgramGraphCapabilitySummary *caps);

static void pgtc_collect_caps_from_function(const ZProgramGraph *graph, const ZProgramGraphNode *fun, bool *visited, ZProgramGraphCapabilitySummary *caps) {
  size_t index = pgtc_node_index(graph, fun);
  if (!graph || !fun || index >= graph->node_len || visited[index]) return;
  visited[index] = true;
  for (const ZProgramGraphEdge *edge = pgtc_next_edge(graph, fun->id, "param", false, 0); edge; edge = pgtc_next_edge(graph, fun->id, "param", true, edge->order)) {
    const ZProgramGraphNode *param = pgtc_node(graph, edge->to);
    pgtc_capability_set_for_type(caps, param ? param->type : NULL, param);
  }
  pgtc_collect_caps_from_node(graph, pgtc_child(graph, fun->id, "body", 0), visited, caps);
}

static void pgtc_collect_caps_from_node(const ZProgramGraph *graph, const ZProgramGraphNode *node, bool *visited, ZProgramGraphCapabilitySummary *caps) {
  if (!graph || !node) return;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_CALL || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) {
    ZBuf name;
    zbuf_init(&name);
    if (!pgtc_expr_name(graph, pgtc_child(graph, node->id, "left", 0), &name) && node->name) zbuf_append(&name, node->name);
    const char *callee = name.data ? name.data : "";
    pgtc_capability_set_for_call(caps, callee, node);
    pgtc_collect_caps_from_function(graph, pgtc_function(graph, callee), visited, caps);
    zbuf_free(&name);
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && pgtc_eq(edge->from, node->id)) pgtc_collect_caps_from_node(graph, pgtc_node(graph, edge->to), visited, caps);
  }
}

static void pgtc_collect_selected_capabilities(const ZProgramGraph *graph, const char *filter, ZProgramGraphCapabilitySummary *caps) {
  *caps = (ZProgramGraphCapabilitySummary){0};
  bool *visited = z_checked_calloc(graph && graph->node_len ? graph->node_len : 1, sizeof(bool));
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *fun = &graph->nodes[i];
    if (pgtc_is_test_function(fun) && (!filter || !filter[0] || strstr(pgtc_test_name(fun), filter))) pgtc_collect_caps_from_function(graph, fun, visited, caps);
  }
  free(visited);
}

static bool pgtc_target_has_capability(const ZTargetInfo *target, const char *capability) {
  if (!capability) return true;
  if (pgtc_eq(capability, "alloc") || pgtc_eq(capability, "path") || pgtc_eq(capability, "codec") || pgtc_eq(capability, "parse") || pgtc_eq(capability, "memory")) return true;
  if (pgtc_eq(capability, "world")) return z_target_has_capability(target, "stdio");
  return z_target_has_capability(target, capability);
}

bool z_program_graph_test_target_capabilities_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZTargetInfo *target, const char *filter, ZDiag *diag) {
  ZProgramGraphCapabilitySummary all_caps;
  z_program_graph_collect_capabilities(graph, resolution, &all_caps);
  (void)all_caps;
  ZProgramGraphCapabilitySummary caps;
  pgtc_collect_selected_capabilities(graph, filter, &caps);
#define PGTC_DENY_CAP(field, cap) do { \
    if (caps.field && !pgtc_target_has_capability(target, cap)) { \
      diag->code = 6002; diag->path = caps.field##_node ? caps.field##_node->path : NULL; diag->line = caps.field##_node ? caps.field##_node->line : 1; diag->column = caps.field##_node ? caps.field##_node->column : 1; diag->length = 1; \
      snprintf(diag->message, sizeof(diag->message), "target does not provide required %s capability", cap); snprintf(diag->expected, sizeof(diag->expected), "target with %s capability", cap); snprintf(diag->actual, sizeof(diag->actual), "target %s lacks %s", target ? target->name : "unknown", cap); return false; \
    } \
  } while (0)
  PGTC_DENY_CAP(args, "args");
  PGTC_DENY_CAP(env, "env");
  PGTC_DENY_CAP(fs, "fs");
  PGTC_DENY_CAP(time, "time");
  PGTC_DENY_CAP(rand, "rand");
  PGTC_DENY_CAP(net, "net");
  PGTC_DENY_CAP(proc, "proc");
  PGTC_DENY_CAP(web, "web");
  PGTC_DENY_CAP(world, "world");
#undef PGTC_DENY_CAP
  return true;
}
