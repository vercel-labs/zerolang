#include "program_graph_semantics.h"

#include "c_import.h"
#include "program_graph_resolve.h"
#include "program_graph_source_map.h"
#include "std_sig.h"
#include "std_source.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const ZProgramGraphResolutionReference *reference;
  const ZStdHelperInfo *helper;
  const ZStdSourceModule *source_module;
  const ZProgramGraphNode *c_import;
  const ZProgramGraphNode *target_node;
  const ZProgramGraphNode *target_function;
  const ZProgramGraphNode *world_binding;
  char c_return_type[128];
  char c_arg_types[Z_STD_HELPER_MAX_ARGS][128];
  int c_arg_count;
  bool world_write;
  bool fallible;
  bool present;
} ZGraphSemanticContract;

static bool graph_semantics_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool graph_semantics_text_present(const char *text) {
  return text && text[0];
}

static bool graph_semantics_text_starts_with(const char *text, const char *prefix) {
  size_t prefix_len = prefix ? strlen(prefix) : 0;
  return text && prefix && strncmp(text, prefix, prefix_len) == 0;
}

static bool graph_semantics_text_contains(const char *text, const char *needle) {
  return text && needle && strstr(text, needle) != NULL;
}

static void graph_semantics_capability_set(ZProgramGraphCapabilitySummary *caps, const char *capability, const ZProgramGraphNode *node) {
  if (!caps || !capability) return;
#define SET_GRAPH_CAP(field) do { caps->field = true; if (!caps->field##_node) caps->field##_node = node; } while (0)
  if (graph_semantics_text_eq(capability, "args")) SET_GRAPH_CAP(args);
  else if (graph_semantics_text_eq(capability, "env")) SET_GRAPH_CAP(env);
  else if (graph_semantics_text_eq(capability, "fs")) SET_GRAPH_CAP(fs);
  else if (graph_semantics_text_eq(capability, "memory")) SET_GRAPH_CAP(memory);
  else if (graph_semantics_text_eq(capability, "alloc")) {
    SET_GRAPH_CAP(alloc);
    SET_GRAPH_CAP(memory);
  } else if (graph_semantics_text_eq(capability, "path")) SET_GRAPH_CAP(path);
  else if (graph_semantics_text_eq(capability, "codec")) SET_GRAPH_CAP(codec);
  else if (graph_semantics_text_eq(capability, "parse")) SET_GRAPH_CAP(parse);
  else if (graph_semantics_text_eq(capability, "time")) SET_GRAPH_CAP(time);
  else if (graph_semantics_text_eq(capability, "rand")) SET_GRAPH_CAP(rand);
  else if (graph_semantics_text_eq(capability, "net")) SET_GRAPH_CAP(net);
  else if (graph_semantics_text_eq(capability, "proc")) SET_GRAPH_CAP(proc);
  else if (graph_semantics_text_eq(capability, "web")) SET_GRAPH_CAP(web);
  else if (graph_semantics_text_eq(capability, "world") || graph_semantics_text_eq(capability, "io")) SET_GRAPH_CAP(world);
#undef SET_GRAPH_CAP
}

static void graph_semantics_capability_set_for_type(ZProgramGraphCapabilitySummary *caps, const char *type, const ZProgramGraphNode *node) {
  if (!caps || !type) return;
  if (graph_semantics_text_eq(type, "World")) graph_semantics_capability_set(caps, "world", node);
  else if (graph_semantics_text_eq(type, "Fs")) graph_semantics_capability_set(caps, "fs", node);
  else if (graph_semantics_text_eq(type, "Net")) graph_semantics_capability_set(caps, "net", node);
  else if (graph_semantics_text_eq(type, "Proc")) graph_semantics_capability_set(caps, "proc", node);
  else if (graph_semantics_text_eq(type, "Clock")) graph_semantics_capability_set(caps, "time", node);
  else if (graph_semantics_text_eq(type, "Rand")) graph_semantics_capability_set(caps, "rand", node);
  else if (graph_semantics_text_eq(type, "Alloc") || graph_semantics_text_eq(type, "FixedBufAlloc") || graph_semantics_text_eq(type, "NullAlloc")) {
    graph_semantics_capability_set(caps, "alloc", node);
  }
  if (graph_semantics_text_contains(type, "Span<") || graph_semantics_text_contains(type, "MutSpan<") || graph_semantics_text_contains(type, "ByteBuf")) {
    graph_semantics_capability_set(caps, "memory", node);
  }
}


static void graph_semantics_append_quoted(ZBuf *buf, const char *text) {
  zbuf_append_char(buf, '"');
  for (const char *p = text ? text : ""; *p; p++) {
    unsigned char ch = (unsigned char)*p;
    switch (ch) {
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '"': zbuf_append(buf, "\\\""); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (ch < 0x20) {
          const char *hex = "0123456789abcdef";
          char escape[7] = {'\\', 'u', '0', '0', hex[ch >> 4], hex[ch & 0x0f], 0};
          zbuf_append(buf, escape);
        } else {
          zbuf_append_char(buf, (char)ch);
        }
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static void graph_semantics_append_source_range_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphNode *node) {
  z_program_graph_append_source_range_from_context_json(buf, sources, graph, node, node ? node->path : "");
}

static size_t graph_semantics_node_index(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (graph_semantics_text_eq(graph->nodes[i].id, id)) return i;
  }
  return SIZE_MAX;
}

static const ZProgramGraphNode *graph_semantics_node(const ZProgramGraph *graph, size_t index) {
  return graph && index < graph->node_len ? &graph->nodes[index] : NULL;
}

static const ZProgramGraphNode *graph_semantics_node_by_id(const ZProgramGraph *graph, const char *id) {
  return graph_semantics_node(graph, graph_semantics_node_index(graph, id));
}

static bool graph_semantics_node_is_call(const ZProgramGraphNode *node) {
  return node && (node->kind == Z_PROGRAM_GRAPH_NODE_CALL || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL);
}

static const ZProgramGraphResolutionReference *graph_semantics_reference_for_node(const ZProgramGraphResolutionFacts *resolution, const char *node_id, const char *kind) {
  for (size_t i = 0; resolution && node_id && i < resolution->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &resolution->references[i];
    if ((!kind || graph_semantics_text_eq(ref->kind, kind)) && graph_semantics_text_eq(ref->node_id, node_id)) return ref;
  }
  return NULL;
}

static const ZProgramGraphResolutionReference *graph_semantics_call_reference(const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *call) {
  return graph_semantics_reference_for_node(resolution, call ? call->id : NULL, "call");
}

static const ZProgramGraphNode *graph_semantics_resolution_target_node(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *node) {
  const ZProgramGraphResolutionReference *ref = graph_semantics_reference_for_node(resolution, node ? node->id : NULL, NULL);
  return ref && ref->target_node && ref->target_node[0] ? graph_semantics_node_by_id(graph, ref->target_node) : NULL;
}

static const char *graph_semantics_node_semantic_type(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *node) {
  if (node && graph_semantics_text_present(node->type)) return node->type;
  const ZProgramGraphNode *target = graph_semantics_resolution_target_node(graph, resolution, node);
  return target && target->type ? target->type : "";
}

static const char *graph_semantics_node_semantic_type_id(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *node) {
  if (node && graph_semantics_text_present(node->type_id)) return node->type_id;
  const ZProgramGraphNode *target = graph_semantics_resolution_target_node(graph, resolution, node);
  return target && target->type_id ? target->type_id : "";
}

static bool graph_semantics_span_type_element(const char *type, const char *name, char *out, size_t out_len) {
  if (!type || !name || !out || out_len == 0) return false;
  size_t name_len = strlen(name);
  if (strncmp(type, name, name_len) != 0 || type[name_len] != '<') return false;
  const char *start = type + name_len + 1;
  const char *end = strrchr(start, '>');
  if (!end || end[1] != 0 || end <= start) return false;
  size_t len = (size_t)(end - start);
  if (len >= out_len) return false;
  memcpy(out, start, len);
  out[len] = 0;
  return true;
}

static bool graph_semantics_fixed_array_element(const char *type, char *out, size_t out_len) {
  if (!type || type[0] != '[' || !out || out_len == 0) return false;
  const char *close = strchr(type, ']');
  if (!close || close[1] == 0) return false;
  const char *element = close + 1;
  size_t len = strlen(element);
  if (len >= out_len) return false;
  memcpy(out, element, len);
  out[len] = 0;
  return true;
}

static bool graph_semantics_fixed_array_matches_span(const char *actual, const char *expected) {
  char expected_element[128];
  char actual_element[128];
  bool expected_span = graph_semantics_span_type_element(expected, "Span", expected_element, sizeof(expected_element)) ||
                       graph_semantics_span_type_element(expected, "MutSpan", expected_element, sizeof(expected_element));
  return expected_span &&
         graph_semantics_fixed_array_element(actual, actual_element, sizeof(actual_element)) &&
         graph_semantics_text_eq(actual_element, expected_element);
}

static bool graph_semantics_copy_text(char *out, size_t out_len, const char *text) {
  if (!out || out_len == 0 || !text) return false;
  size_t len = strlen(text);
  if (len >= out_len) return false;
  memcpy(out, text, len);
  out[len] = 0;
  return true;
}

static const ZProgramGraphEdge *graph_semantics_owner_edge(const ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && node_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && graph_semantics_text_eq(edge->to, node_id)) return edge;
  }
  return NULL;
}

static const ZProgramGraphNode *graph_semantics_child(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *kind, size_t order) {
  for (size_t i = 0; graph && node && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        edge->order == order &&
        graph_semantics_text_eq(edge->from, node->id) &&
        graph_semantics_text_eq(edge->kind, kind)) {
      return graph_semantics_node(graph, graph_semantics_node_index(graph, edge->to));
    }
  }
  return NULL;
}

static size_t graph_semantics_child_count(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *kind) {
  size_t count = 0;
  for (size_t i = 0; graph && node && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        graph_semantics_text_eq(edge->from, node->id) &&
        graph_semantics_text_eq(edge->kind, kind)) {
      count++;
    }
  }
  return count;
}

static bool graph_semantics_is_under_kind(const ZProgramGraph *graph, const ZProgramGraphNode *node, ZProgramGraphNodeKind kind) {
  const char *current = node ? node->id : NULL;
  for (size_t depth = 0; graph && current && depth < graph->node_len; depth++) {
    const ZProgramGraphEdge *owner = graph_semantics_owner_edge(graph, current);
    if (!owner) return false;
    const ZProgramGraphNode *owner_node = graph_semantics_node(graph, graph_semantics_node_index(graph, owner->from));
    if (!owner_node) return false;
    if (owner_node->kind == kind) return true;
    current = owner_node->id;
  }
  return false;
}

static bool graph_semantics_node_has_type_fact(const ZProgramGraphNode *node) {
  return node && (graph_semantics_text_present(node->type) || graph_semantics_text_present(node->type_id));
}

static void graph_semantics_append_error_names_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  zbuf_append(buf, "[");
  bool first = true;
  size_t count = graph_semantics_child_count(graph, node, "error");
  for (size_t order = 0; order < count; order++) {
    const ZProgramGraphNode *error = graph_semantics_child(graph, node, "error", order);
    if (!error) continue;
    if (!first) zbuf_append(buf, ",");
    graph_semantics_append_quoted(buf, error->name);
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_effect_refs_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  zbuf_append(buf, "[");
  bool first = true;
  size_t count = graph_semantics_child_count(graph, node, "effect");
  for (size_t order = 0; order < count; order++) {
    const ZProgramGraphNode *effect = graph_semantics_child(graph, node, "effect", order);
    if (!effect) continue;
    if (!first) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"node\":");
    graph_semantics_append_quoted(buf, effect->id);
    zbuf_append(buf, ",\"name\":");
    graph_semantics_append_quoted(buf, effect->name);
    zbuf_append(buf, ",\"effectId\":");
    graph_semantics_append_quoted(buf, effect->effect_id);
    zbuf_append(buf, "}");
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_std_error_names_json(ZBuf *buf, const ZStdHelperInfo *helper) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; helper && i < Z_STD_HELPER_MAX_ERRORS; i++) {
    const char *name = z_std_helper_error_name(helper, i);
    if (!name) break;
    if (!first) zbuf_append(buf, ",");
    graph_semantics_append_quoted(buf, name);
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_std_arg_types_json(ZBuf *buf, const ZStdHelperInfo *helper) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; helper && helper->arg_count > 0 && i < (size_t)helper->arg_count && i < Z_STD_HELPER_MAX_ARGS; i++) {
    const char *type = helper->arg_types[i];
    if (!first) zbuf_append(buf, ",");
    if (type) graph_semantics_append_quoted(buf, type);
    else zbuf_append(buf, "null");
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_c_arg_types_json(ZBuf *buf, const ZGraphSemanticContract *contract) {
  zbuf_append(buf, "[");
  bool first = true;
  for (int i = 0; contract && i < contract->c_arg_count && i < (int)Z_STD_HELPER_MAX_ARGS; i++) {
    if (!first) zbuf_append(buf, ",");
    graph_semantics_append_quoted(buf, contract->c_arg_types[i]);
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_function_arg_types_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *function, bool skip_first) {
  zbuf_append(buf, "[");
  bool first = true;
  size_t count = graph_semantics_child_count(graph, function, "param");
  for (size_t order = skip_first ? 1 : 0; order < count; order++) {
    const ZProgramGraphNode *param = graph_semantics_child(graph, function, "param", order);
    if (!param) continue;
    if (!first) zbuf_append(buf, ",");
    graph_semantics_append_quoted(buf, param->type);
    first = false;
  }
  zbuf_append(buf, "]");
}

static const ZProgramGraphNode *graph_semantics_contract_param_node(const ZProgramGraph *graph, const ZGraphSemanticContract *contract, size_t order, bool implicit_receiver) {
  if (!contract || !contract->target_function) return NULL;
  size_t param_order = order + (implicit_receiver ? 1 : 0);
  return graph_semantics_child(graph, contract->target_function, "param", param_order);
}

static const char *graph_semantics_contract_expected_arg_type(const ZProgramGraph *graph, const ZGraphSemanticContract *contract, size_t order, bool implicit_receiver) {
  const ZProgramGraphNode *param = graph_semantics_contract_param_node(graph, contract, order, implicit_receiver);
  if (param && graph_semantics_text_present(param->type)) return param->type;
  if (contract && contract->helper && order < Z_STD_HELPER_MAX_ARGS && order < (size_t)contract->helper->arg_count) return contract->helper->arg_types[order];
  if (contract && order < Z_STD_HELPER_MAX_ARGS && contract->c_arg_count >= 0 && order < (size_t)contract->c_arg_count) return contract->c_arg_types[order];
  return "";
}

static const char *graph_semantics_effective_arg_type(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZGraphSemanticContract *contract, const ZProgramGraphNode *arg, size_t order, bool implicit_receiver) {
  const char *actual = graph_semantics_node_semantic_type(graph, resolution, arg);
  const char *expected = graph_semantics_contract_expected_arg_type(graph, contract, order, implicit_receiver);
  if (!graph_semantics_text_present(actual) && graph_semantics_text_present(expected)) return expected;
  if (graph_semantics_fixed_array_matches_span(actual, expected)) return expected;
  return actual;
}

static const char *graph_semantics_effective_arg_type_id(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZGraphSemanticContract *contract, const ZProgramGraphNode *arg, size_t order, bool implicit_receiver) {
  const char *actual = graph_semantics_node_semantic_type(graph, resolution, arg);
  const char *expected = graph_semantics_contract_expected_arg_type(graph, contract, order, implicit_receiver);
  if (graph_semantics_fixed_array_matches_span(actual, expected)) {
    const ZProgramGraphNode *param = graph_semantics_contract_param_node(graph, contract, order, implicit_receiver);
    return param && param->type_id ? param->type_id : "";
  }
  return graph_semantics_node_semantic_type_id(graph, resolution, arg);
}

static void graph_semantics_append_args_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphResolutionFacts *resolution, const ZGraphSemanticContract *contract, bool implicit_receiver, const ZProgramGraphNode *call) {
  zbuf_append(buf, "[");
  bool first = true;
  size_t count = graph_semantics_child_count(graph, call, "arg");
  for (size_t order = 0; order < count; order++) {
    const ZProgramGraphNode *arg = graph_semantics_child(graph, call, "arg", order);
    if (!arg) continue;
    if (!first) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"node\":");
    graph_semantics_append_quoted(buf, arg->id);
    zbuf_append(buf, ",\"kind\":");
    graph_semantics_append_quoted(buf, z_program_graph_node_kind_name(arg->kind));
    zbuf_append(buf, ",\"type\":");
    graph_semantics_append_quoted(buf, graph_semantics_effective_arg_type(graph, resolution, contract, arg, order, implicit_receiver));
    zbuf_append(buf, ",\"typeId\":");
    graph_semantics_append_quoted(buf, graph_semantics_effective_arg_type_id(graph, resolution, contract, arg, order, implicit_receiver));
    zbuf_appendf(buf, ",\"order\":%zu,\"sourceRange\":", order);
    graph_semantics_append_source_range_json(buf, graph, sources, arg);
    zbuf_append(buf, "}");
    first = false;
  }
  zbuf_append(buf, "]");
}

static const ZProgramGraphNode *graph_semantics_call_member_base(const ZProgramGraph *graph, const ZProgramGraphNode *call) {
  const ZProgramGraphNode *left = graph_semantics_child(graph, call, "left", 0);
  if (left && left->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) return graph_semantics_child(graph, left, "left", 0);
  return NULL;
}

static bool graph_semantics_reference_is_type_like(const ZProgramGraphResolutionReference *ref) {
  if (!ref) return false;
  return graph_semantics_text_eq(ref->target_kind, "shape") ||
         graph_semantics_text_eq(ref->target_kind, "interface") ||
         graph_semantics_text_eq(ref->target_kind, "enum") ||
         graph_semantics_text_eq(ref->target_kind, "choice") ||
         graph_semantics_text_eq(ref->target_kind, "type") ||
         graph_semantics_text_eq(ref->target_kind, "typeAlias") ||
         graph_semantics_text_eq(ref->target_kind, "typeParam") ||
         graph_semantics_text_eq(ref->target_kind, "builtinType") ||
         graph_semantics_text_eq(ref->target_kind, "stdlibNamespace") ||
         graph_semantics_text_eq(ref->target_kind, "targetNamespace");
}

static bool graph_semantics_call_has_implicit_receiver(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *call, const ZGraphSemanticContract *contract) {
  if (!graph ||
      !call ||
      call->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL ||
      !contract ||
      !contract->target_function ||
      (!graph_semantics_text_eq(contract->reference ? contract->reference->target_kind : "", "method") &&
       !graph_semantics_text_eq(contract->reference ? contract->reference->target_kind : "", "interfaceMethod"))) {
    return false;
  }
  size_t param_count = graph_semantics_child_count(graph, contract->target_function, "param");
  size_t arg_count = graph_semantics_child_count(graph, call, "arg");
  if (param_count == 0 || param_count != arg_count + 1) return false;
  const ZProgramGraphNode *base = graph_semantics_call_member_base(graph, call);
  if (!base) return false;
  const ZProgramGraphResolutionReference *base_ref = graph_semantics_reference_for_node(resolution, base->id, NULL);
  return !graph_semantics_reference_is_type_like(base_ref);
}

static void graph_semantics_append_receiver_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *call, bool implicit_receiver) {
  if (!implicit_receiver) {
    zbuf_append(buf, "null");
    return;
  }
  const ZProgramGraphNode *receiver = graph_semantics_call_member_base(graph, call);
  zbuf_append(buf, "{\"node\":");
  graph_semantics_append_quoted(buf, receiver ? receiver->id : "");
  zbuf_append(buf, ",\"kind\":");
  graph_semantics_append_quoted(buf, receiver ? z_program_graph_node_kind_name(receiver->kind) : "");
  zbuf_append(buf, ",\"type\":");
  graph_semantics_append_quoted(buf, graph_semantics_node_semantic_type(graph, resolution, receiver));
  zbuf_append(buf, ",\"typeId\":");
  graph_semantics_append_quoted(buf, graph_semantics_node_semantic_type_id(graph, resolution, receiver));
  zbuf_append(buf, ",\"implicit\":true,\"sourceRange\":");
  graph_semantics_append_source_range_json(buf, graph, sources, receiver);
  zbuf_append(buf, "}");
}

static bool graph_semantics_reference_target_kind(const ZProgramGraphResolutionReference *ref, const char *kind) {
  return ref && graph_semantics_text_eq(ref->target_kind, kind);
}

static bool graph_semantics_contract_world_write(const ZProgramGraphNode *call, const ZProgramGraphResolutionReference *ref, const ZProgramGraphNode *target) {
  return call &&
         ref &&
         ref->resolved &&
         graph_semantics_text_eq(call->name, "write") &&
         graph_semantics_text_contains(ref->qualified_name, ".out.write") &&
         (target && (target->kind == Z_PROGRAM_GRAPH_NODE_PARAM || target->kind == Z_PROGRAM_GRAPH_NODE_LET)) &&
         graph_semantics_text_eq(target->type, "World");
}

static const char *graph_semantics_c_import_symbol(const ZProgramGraphNode *c_import, const ZProgramGraphResolutionReference *ref) {
  const char *qualified = ref ? ref->qualified_name : NULL;
  const char *alias = c_import ? c_import->name : NULL;
  if (!graph_semantics_text_present(qualified)) return "";
  if (graph_semantics_text_present(alias)) {
    size_t alias_len = strlen(alias);
    if (strncmp(qualified, alias, alias_len) == 0 && qualified[alias_len] == '.') return qualified + alias_len + 1;
  }
  const char *dot = strrchr(qualified, '.');
  return dot && dot[1] ? dot + 1 : qualified;
}

static void graph_semantics_load_c_import_contract(ZGraphSemanticContract *contract) {
  if (!contract || !contract->c_import || !contract->reference) return;
  const char *header_path = contract->c_import->value;
  const char *symbol = graph_semantics_c_import_symbol(contract->c_import, contract->reference);
  if (!graph_semantics_text_present(header_path) || !graph_semantics_text_present(symbol)) return;
  ZDiag diag = {0};
  char *header = z_read_file(header_path, &diag);
  if (!header) return;
  ZCImportFunctionVec functions = {0};
  z_c_header_parse_functions(header, &functions);
  free(header);
  for (size_t i = 0; i < functions.len; i++) {
    ZCImportFunction *function = &functions.items[i];
    if (!graph_semantics_text_eq(function->name, symbol)) continue;
    graph_semantics_copy_text(contract->c_return_type, sizeof(contract->c_return_type), function->return_zero_type);
    contract->c_arg_count = function->param_len > Z_STD_HELPER_MAX_ARGS ? Z_STD_HELPER_MAX_ARGS : (int)function->param_len;
    for (int arg = 0; arg < contract->c_arg_count; arg++) {
      graph_semantics_copy_text(contract->c_arg_types[arg], sizeof(contract->c_arg_types[arg]), function->params[arg].zero_type);
    }
    break;
  }
  z_c_import_function_vec_free(&functions);
}

static ZGraphSemanticContract graph_semantics_contract(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *call) {
  ZGraphSemanticContract contract = {0};
  contract.c_arg_count = -1;
  const ZProgramGraphResolutionReference *ref = graph_semantics_call_reference(resolution, call);
  contract.reference = ref;
  if (!ref) return contract;
  contract.target_node = graph_semantics_node_by_id(graph, ref->target_node);
  if (graph_semantics_reference_target_kind(ref, "stdlib") || graph_semantics_reference_target_kind(ref, "graphBackedStdlib")) {
    contract.helper = z_std_helper_find(ref->qualified_name);
  }
  if (graph_semantics_reference_target_kind(ref, "graphBackedStdlib")) contract.source_module = z_std_source_module_for_public_call(ref->qualified_name);
  if (graph_semantics_reference_target_kind(ref, "cFunction") || (contract.target_node && contract.target_node->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT)) {
    contract.c_import = contract.target_node;
    graph_semantics_load_c_import_contract(&contract);
  }
  if (contract.target_node && contract.target_node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION) contract.target_function = contract.target_node;
  contract.world_binding = graph_semantics_contract_world_write(call, ref, contract.target_node) ? contract.target_node : NULL;
  contract.world_write = contract.world_binding != NULL;
  contract.fallible = contract.world_write || (contract.helper && z_std_helper_is_fallible(contract.helper)) || (contract.target_function && contract.target_function->fallible);
  contract.present = ref->resolved &&
    (contract.helper ||
     contract.c_import ||
     contract.source_module ||
     contract.target_function ||
     contract.target_node ||
     contract.world_write ||
     graph_semantics_text_present(ref->target_kind));
  return contract;
}

static const char *graph_semantics_contract_kind(const ZGraphSemanticContract *contract) {
  if (!contract) return "language";
  if (contract->world_write) return "worldStreamWrite";
  if (contract->c_import) return "cAbi";
  if (contract->source_module) return "graphBackedStdlib";
  if (contract->helper) return "stdlib";
  if (contract->reference && graph_semantics_text_present(contract->reference->target_kind)) return contract->reference->target_kind;
  return "language";
}

static const char *graph_semantics_contract_capability(const ZGraphSemanticContract *contract) {
  if (!contract) return "";
  if (contract->world_write) return "io";
  if (contract->c_import) return "c-abi";
  return contract->helper ? contract->helper->capability : "";
}

static const char *graph_semantics_contract_target_support(const ZGraphSemanticContract *contract) {
  if (!contract) return "";
  if (contract->world_write) return "world-io";
  if (contract->c_import) return "host-c-abi";
  return contract->helper ? contract->helper->target_support : "";
}

static const char *graph_semantics_contract_allocation(const ZGraphSemanticContract *contract) {
  if (!contract || !contract->helper) return "";
  return contract->helper->allocation_behavior;
}

static const char *graph_semantics_contract_return_type(const ZGraphSemanticContract *contract, const ZProgramGraphNode *call) {
  if (contract && contract->world_write) return "Void";
  if (contract && contract->helper && contract->helper->return_type) return contract->helper->return_type;
  if (contract && graph_semantics_text_present(contract->c_return_type)) return contract->c_return_type;
  if (contract && contract->target_function) return contract->target_function->type;
  return call && call->type ? call->type : "";
}

static bool graph_semantics_contract_has_target_requirement(const ZGraphSemanticContract *contract) {
  const char *support = graph_semantics_contract_target_support(contract);
  return contract && contract->present &&
    (contract->world_write ||
     contract->c_import ||
     (support && support[0] && !graph_semantics_text_eq(support, "target-neutral")));
}

static bool graph_semantics_contract_has_repair(const ZGraphSemanticContract *contract) {
  return contract && contract->fallible;
}

static void graph_semantics_append_repair_json(ZBuf *buf, const ZGraphSemanticContract *contract, bool checked) {
  if (!graph_semantics_contract_has_repair(contract)) {
    zbuf_append(buf, "null");
    return;
  }
  zbuf_append(buf, "{\"id\":\"check-fallible-call\",\"appliesWhen\":");
  graph_semantics_append_quoted(buf, checked ? "alreadyChecked" : "requiresCheck");
  zbuf_append(buf, ",\"summary\":\"wrap this fallible call in check or rescue before using its value\"}");
}

static void graph_semantics_append_contract_target_node_json(ZBuf *buf, const ZGraphSemanticContract *contract) {
  if (contract && contract->reference) {
    graph_semantics_append_quoted(buf, contract->reference->target_node);
    return;
  }
  graph_semantics_append_quoted(buf, contract && contract->target_node ? contract->target_node->id : "");
}

static void graph_semantics_append_contract_symbol_json(ZBuf *buf, const ZGraphSemanticContract *contract, const char *qualified) {
  if (contract && contract->reference) {
    graph_semantics_append_quoted(buf, contract->reference->symbol_id);
    return;
  }
  if (contract && contract->world_binding) {
    graph_semantics_append_quoted(buf, contract->world_binding->symbol_id);
    return;
  }
  if (contract && contract->c_import) {
    graph_semantics_append_quoted(buf, contract->c_import->symbol_id);
    return;
  }
  if (contract && contract->target_function) {
    graph_semantics_append_quoted(buf, contract->target_function->symbol_id);
    return;
  }
  if (contract && contract->helper) {
    zbuf_append(buf, "\"stdlib:");
    for (const char *p = qualified ? qualified : ""; *p; p++) {
      if (*p == '"' || *p == '\\') zbuf_append_char(buf, '_');
      else zbuf_append_char(buf, *p);
    }
    zbuf_append_char(buf, '"');
    return;
  }
  graph_semantics_append_quoted(buf, "");
}

static void graph_semantics_append_resolution_json(ZBuf *buf, const ZGraphSemanticContract *contract) {
  const ZProgramGraphResolutionReference *ref = contract ? contract->reference : NULL;
  zbuf_append(buf, "{\"referenceKind\":\"call\",\"qualifiedName\":");
  graph_semantics_append_quoted(buf, ref ? ref->qualified_name : "");
  zbuf_appendf(buf, ",\"resolved\":%s,\"ambiguous\":%s", ref && ref->resolved ? "true" : "false", ref && ref->ambiguous ? "true" : "false");
  zbuf_append(buf, ",\"targetKind\":");
  graph_semantics_append_quoted(buf, ref ? ref->target_kind : "");
  zbuf_append(buf, ",\"targetNode\":");
  graph_semantics_append_quoted(buf, ref ? ref->target_node : "");
  zbuf_append(buf, ",\"symbolId\":");
  graph_semantics_append_quoted(buf, ref ? ref->symbol_id : "");
  zbuf_append(buf, "}");
}

static void graph_semantics_append_contract_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *call, bool checked, bool implicit_receiver, bool *fallible_out, ZGraphSemanticContract *contract_out) {
  ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, call);
  if (fallible_out) *fallible_out = contract.fallible;
  if (contract_out) *contract_out = contract;
  zbuf_append(buf, "{\"kind\":");
  graph_semantics_append_quoted(buf, graph_semantics_contract_kind(&contract));
  zbuf_appendf(buf, ",\"fallible\":%s,\"checked\":%s,\"requiresCheck\":%s", contract.fallible ? "true" : "false", checked ? "true" : "false", contract.fallible && !checked ? "true" : "false");
  zbuf_append(buf, ",\"targetNode\":");
  graph_semantics_append_contract_target_node_json(buf, &contract);
  zbuf_append(buf, ",\"symbolId\":");
  graph_semantics_append_contract_symbol_json(buf, &contract, contract.reference ? contract.reference->qualified_name : (call ? call->name : ""));
  zbuf_append(buf, ",\"returnType\":");
  graph_semantics_append_quoted(buf, graph_semantics_contract_return_type(&contract, call));
  zbuf_append(buf, ",\"capability\":");
  graph_semantics_append_quoted(buf, graph_semantics_contract_capability(&contract));
  zbuf_append(buf, ",\"targetSupport\":");
  graph_semantics_append_quoted(buf, graph_semantics_contract_target_support(&contract));
  zbuf_append(buf, ",\"allocation\":");
  graph_semantics_append_quoted(buf, graph_semantics_contract_allocation(&contract));
  zbuf_append(buf, ",\"sourceModule\":");
  graph_semantics_append_quoted(buf, contract.source_module ? contract.source_module->module : "");
  zbuf_append(buf, ",\"expectedArgCount\":");
  if (contract.helper && contract.helper->arg_count >= 0) zbuf_appendf(buf, "%d", contract.helper->arg_count);
  else if (contract.c_arg_count >= 0) zbuf_appendf(buf, "%d", contract.c_arg_count);
  else if (contract.target_function) {
    size_t count = graph_semantics_child_count(graph, contract.target_function, "param");
    zbuf_appendf(buf, "%zu", implicit_receiver && count > 0 ? count - 1 : count);
  }
  else zbuf_append(buf, "null");
  zbuf_append(buf, ",\"expectedArgTypes\":");
  if (contract.helper) graph_semantics_append_std_arg_types_json(buf, contract.helper);
  else if (contract.c_arg_count >= 0) graph_semantics_append_c_arg_types_json(buf, &contract);
  else if (contract.target_function) graph_semantics_append_function_arg_types_json(buf, graph, contract.target_function, implicit_receiver);
  else zbuf_append(buf, "[]");
  zbuf_append(buf, ",\"errors\":");
  if (contract.helper) graph_semantics_append_std_error_names_json(buf, contract.helper);
  else if (contract.target_function) graph_semantics_append_error_names_json(buf, graph, contract.target_function);
  else zbuf_append(buf, "[]");
  zbuf_append(buf, ",\"repair\":");
  graph_semantics_append_repair_json(buf, &contract, checked);
  zbuf_append(buf, "}");
}

static size_t graph_semantics_typed_node_count(const ZProgramGraph *graph) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph_semantics_node_has_type_fact(&graph->nodes[i])) count++;
  }
  return count;
}

static size_t graph_semantics_function_count(const ZProgramGraph *graph) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_FUNCTION) count++;
  }
  return count;
}

static size_t graph_semantics_call_count(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph_semantics_node_is_call(&graph->nodes[i]) && graph_semantics_call_reference(resolution, &graph->nodes[i])) count++;
  }
  return count;
}

static size_t graph_semantics_effect_fact_count(const ZProgramGraph *graph) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && (node->fallible || graph_semantics_child_count(graph, node, "effect") > 0 || graph_semantics_child_count(graph, node, "error") > 0)) count++;
    else if (node->kind == Z_PROGRAM_GRAPH_NODE_CHECK || node->kind == Z_PROGRAM_GRAPH_NODE_RAISE || node->kind == Z_PROGRAM_GRAPH_NODE_RESCUE) count++;
  }
  return count;
}

static void graph_semantics_call_summary_counts(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, size_t *fallible_calls, size_t *contracts) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!graph_semantics_node_is_call(node) || !graph_semantics_call_reference(resolution, node)) continue;
    ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, node);
    if (contract.fallible && fallible_calls) (*fallible_calls)++;
    if (contract.present && contracts) (*contracts)++;
  }
}

static bool graph_semantics_type_is_borrow(const char *type) {
  return graph_semantics_text_starts_with(type, "ref<") ||
         graph_semantics_text_starts_with(type, "mutref<") ||
         graph_semantics_text_starts_with(type, "Span<") ||
         graph_semantics_text_starts_with(type, "MutSpan<") ||
         graph_semantics_text_contains(type, "<ref<") ||
         graph_semantics_text_contains(type, "<mutref<") ||
         graph_semantics_text_contains(type, "<Span<") ||
         graph_semantics_text_contains(type, "<MutSpan<");
}

static bool graph_semantics_type_identifier_char(unsigned char ch) {
  return isalnum(ch) || ch == '_' || ch == '.';
}

static bool graph_semantics_node_is_user_type_decl(const ZProgramGraphNode *node) {
  return node &&
         (node->kind == Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS ||
          node->kind == Z_PROGRAM_GRAPH_NODE_SHAPE ||
          node->kind == Z_PROGRAM_GRAPH_NODE_INTERFACE ||
          node->kind == Z_PROGRAM_GRAPH_NODE_ENUM ||
          node->kind == Z_PROGRAM_GRAPH_NODE_CHOICE);
}

static bool graph_semantics_graph_declares_type_name(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (graph_semantics_node_is_user_type_decl(node) && graph_semantics_text_eq(node->name, name)) return true;
  }
  return false;
}

static bool graph_semantics_type_id_resolves_to_user_type_name(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *type_id, const char *name) {
  if (!graph_semantics_text_present(type_id) || !graph_semantics_text_present(name)) return false;
  for (size_t i = 0; resolution && i < resolution->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &resolution->references[i];
    if (!ref->resolved || !graph_semantics_text_eq(ref->kind, "type") || !graph_semantics_text_eq(ref->name, name)) continue;
    const ZProgramGraphNode *type_ref = graph_semantics_node_by_id(graph, ref->node_id);
    if (!type_ref || !graph_semantics_text_eq(type_ref->type_id, type_id)) continue;
    const ZProgramGraphNode *target = graph_semantics_node_by_id(graph, ref->target_node);
    if (graph_semantics_node_is_user_type_decl(target)) return true;
  }
  return false;
}

static bool graph_semantics_type_token_is_owned_wrapped(const char *type, const char *token_start) {
  if (!type || !token_start || token_start <= type) return false;
  const char *cursor = token_start;
  size_t depth = 0;
  while (cursor > type) {
    cursor--;
    if (*cursor == '>') {
      depth++;
      continue;
    }
    if (*cursor != '<') continue;
    if (depth > 0) {
      depth--;
      continue;
    }
    const char *name_end = cursor;
    const char *name_start = name_end;
    while (name_start > type && graph_semantics_type_identifier_char((unsigned char)name_start[-1])) name_start--;
    return (size_t)(name_end - name_start) == strlen("owned") && strncmp(name_start, "owned", strlen("owned")) == 0;
  }
  return false;
}

static bool graph_semantics_resource_name_shadowed(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *type, const char *type_id, const char *name, const char *token_start) {
  if (!graph_semantics_graph_declares_type_name(graph, name)) return false;
  if (graph_semantics_type_id_resolves_to_user_type_name(graph, resolution, type_id, name)) return true;
  if (graph_semantics_text_eq(type, name)) return true;
  return !graph_semantics_type_token_is_owned_wrapped(type, token_start);
}

static bool graph_semantics_known_resource_type_name(const char *name) {
  static const char *const names[] = {
    "File",
    "ByteBuf",
    "Alloc",
    "NullAlloc",
    "FixedBufAlloc",
    "PageAlloc",
    "GeneralAlloc",
    "BufferedReader",
    "BufferedWriter",
    "FixedReader",
    "FixedWriter",
    "Fs",
    "World",
    "WorldStream",
    NULL
  };
  for (size_t i = 0; name && names[i]; i++) {
    if (graph_semantics_text_eq(name, names[i])) return true;
  }
  return false;
}

static bool graph_semantics_type_has_resource_name(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *type, const char *type_id, const char *name) {
  for (const char *cursor = type ? type : ""; *cursor;) {
    while (*cursor && !graph_semantics_type_identifier_char((unsigned char)*cursor)) cursor++;
    const char *start = cursor;
    while (*cursor && graph_semantics_type_identifier_char((unsigned char)*cursor)) cursor++;
    if (start == cursor) continue;
    size_t len = (size_t)(cursor - start);
    if (strlen(name) == len && strncmp(start, name, len) == 0 && !graph_semantics_resource_name_shadowed(graph, resolution, type, type_id, name, start)) return true;
  }
  return false;
}

static bool graph_semantics_type_has_known_resource(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *type, const char *type_id) {
  for (const char *cursor = type ? type : ""; *cursor;) {
    while (*cursor && !graph_semantics_type_identifier_char((unsigned char)*cursor)) cursor++;
    const char *start = cursor;
    while (*cursor && graph_semantics_type_identifier_char((unsigned char)*cursor)) cursor++;
    if (start == cursor) continue;
    char name[64];
    size_t len = (size_t)(cursor - start);
    if (len >= sizeof(name)) continue;
    memcpy(name, start, len);
    name[len] = 0;
    if (graph_semantics_known_resource_type_name(name) && !graph_semantics_resource_name_shadowed(graph, resolution, type, type_id, name, start)) return true;
  }
  return false;
}

static bool graph_semantics_type_is_resource(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *type, const char *type_id) {
  return graph_semantics_type_has_known_resource(graph, resolution, type, type_id);
}

static bool graph_semantics_type_is_ownership_fact(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *type, const char *type_id) {
  return graph_semantics_text_contains(type, "owned<") ||
         graph_semantics_type_is_borrow(type) ||
         graph_semantics_type_is_resource(graph, resolution, type, type_id);
}

static const char *graph_semantics_ownership_kind(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *type, const char *type_id) {
  if (graph_semantics_text_contains(type, "owned<")) return "owned";
  if (graph_semantics_text_starts_with(type, "mutref<") || graph_semantics_text_contains(type, "<mutref<")) return "mut-borrow";
  if (graph_semantics_text_starts_with(type, "ref<") || graph_semantics_text_contains(type, "<ref<")) return "borrow";
  if (graph_semantics_text_starts_with(type, "MutSpan<") || graph_semantics_text_contains(type, "<MutSpan<")) return "mut-view";
  if (graph_semantics_text_starts_with(type, "Span<") || graph_semantics_text_contains(type, "<Span<")) return "view";
  if (graph_semantics_type_is_resource(graph, resolution, type, type_id)) return "resource-handle";
  return "value";
}

static const char *graph_semantics_resource_kind(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *type, const char *type_id, const char *capability) {
  if (graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "File")) return "file";
  if (graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "ByteBuf")) return "byte-buffer";
  if (graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "Alloc") ||
      graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "NullAlloc") ||
      graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "FixedBufAlloc") ||
      graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "PageAlloc") ||
      graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "GeneralAlloc")) {
    return "allocator";
  }
  if (graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "Fs") || graph_semantics_text_eq(capability, "fs")) return "filesystem";
  if (graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "World") ||
      graph_semantics_type_has_resource_name(graph, resolution, type, type_id, "WorldStream") ||
      graph_semantics_text_eq(capability, "io")) {
    return "world-io";
  }
  if (graph_semantics_text_eq(capability, "c-abi")) return "c-abi";
  if (graph_semantics_text_eq(capability, "args")) return "process-args";
  if (graph_semantics_text_eq(capability, "env")) return "process-env";
  if (capability && capability[0]) return capability;
  return "resource";
}

static bool graph_semantics_contract_is_resource(const ZGraphSemanticContract *contract) {
  const char *capability = graph_semantics_contract_capability(contract);
  const char *allocation = graph_semantics_contract_allocation(contract);
  bool allocation_resource = allocation && allocation[0] &&
    !graph_semantics_text_eq(allocation, "no allocation") &&
    !graph_semantics_text_contains(allocation, "no allocation") &&
    !graph_semantics_text_contains(allocation, "without allocation") &&
    (graph_semantics_text_contains(allocation, "alloc") ||
     graph_semantics_text_contains(allocation, "caller storage") ||
     graph_semantics_text_contains(allocation, "buffer"));
  return contract && contract->present &&
    ((capability && capability[0] && !graph_semantics_text_eq(capability, "none") &&
      !graph_semantics_text_eq(capability, "memory") &&
      !graph_semantics_text_eq(capability, "parse") &&
      !graph_semantics_text_eq(capability, "path") &&
      !graph_semantics_text_eq(capability, "codec")) ||
     allocation_resource);
}

static size_t graph_semantics_ownership_fact_count(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph_semantics_node_has_type_fact(&graph->nodes[i]) && graph_semantics_type_is_ownership_fact(graph, resolution, graph->nodes[i].type, graph->nodes[i].type_id)) count++;
  }
  return count;
}

static size_t graph_semantics_borrowing_fact_count(const ZProgramGraph *graph) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_BORROW || graph_semantics_type_is_borrow(node->type)) count++;
  }
  return count;
}

static size_t graph_semantics_resource_fact_count(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (graph_semantics_node_has_type_fact(node) && graph_semantics_type_is_resource(graph, resolution, node->type, node->type_id)) count++;
    if (!graph_semantics_node_is_call(node) || !graph_semantics_call_reference(resolution, node)) continue;
    ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, node);
    if (graph_semantics_contract_is_resource(&contract)) count++;
  }
  return count;
}

static size_t graph_semantics_target_requirement_count(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!graph_semantics_node_is_call(node) || !graph_semantics_call_reference(resolution, node)) continue;
    ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, node);
    if (graph_semantics_contract_has_target_requirement(&contract)) count++;
  }
  return count;
}

void z_program_graph_collect_capabilities(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, ZProgramGraphCapabilitySummary *out) {
  if (!out) return;
  *out = (ZProgramGraphCapabilitySummary){0};
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_PARAM) graph_semantics_capability_set_for_type(out, node->type, node);
    if (!graph_semantics_node_is_call(node) || !graph_semantics_call_reference(resolution, node)) continue;
    ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, node);
    if (!contract.present) continue;
    graph_semantics_capability_set(out, graph_semantics_contract_capability(&contract), node);
  }
}

static size_t graph_semantics_repair_fact_count(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!graph_semantics_node_is_call(node) || !graph_semantics_call_reference(resolution, node)) continue;
    ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, node);
    if (graph_semantics_contract_has_repair(&contract)) count++;
  }
  return count;
}

static void graph_semantics_append_types_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!graph_semantics_node_has_type_fact(node)) continue;
    if (!first) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"node\":");
    graph_semantics_append_quoted(buf, node->id);
    zbuf_append(buf, ",\"kind\":");
    graph_semantics_append_quoted(buf, z_program_graph_node_kind_name(node->kind));
    zbuf_append(buf, ",\"name\":");
    graph_semantics_append_quoted(buf, node->name);
    zbuf_append(buf, ",\"type\":");
    graph_semantics_append_quoted(buf, node->type);
    zbuf_append(buf, ",\"typeId\":");
    graph_semantics_append_quoted(buf, node->type_id);
    zbuf_append(buf, ",\"sourceRange\":");
    graph_semantics_append_source_range_json(buf, graph, sources, node);
    zbuf_append(buf, "}");
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_function_params_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphNode *function) {
  zbuf_append(buf, "[");
  bool first = true;
  size_t count = graph_semantics_child_count(graph, function, "param");
  for (size_t order = 0; order < count; order++) {
    const ZProgramGraphNode *param = graph_semantics_child(graph, function, "param", order);
    if (!param) continue;
    if (!first) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"node\":");
    graph_semantics_append_quoted(buf, param->id);
    zbuf_append(buf, ",\"name\":");
    graph_semantics_append_quoted(buf, param->name);
    zbuf_append(buf, ",\"type\":");
    graph_semantics_append_quoted(buf, param->type);
    zbuf_append(buf, ",\"typeId\":");
    graph_semantics_append_quoted(buf, param->type_id);
    zbuf_appendf(buf, ",\"mutable\":%s,\"static\":%s,\"order\":%zu,\"sourceRange\":", param->is_mutable ? "true" : "false", param->is_static ? "true" : "false", order);
    graph_semantics_append_source_range_json(buf, graph, sources, param);
    zbuf_append(buf, "}");
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_functions_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) continue;
    if (!first) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"node\":");
    graph_semantics_append_quoted(buf, node->id);
    zbuf_append(buf, ",\"name\":");
    graph_semantics_append_quoted(buf, node->name);
    zbuf_append(buf, ",\"symbolId\":");
    graph_semantics_append_quoted(buf, node->symbol_id);
    zbuf_append(buf, ",\"returnType\":");
    graph_semantics_append_quoted(buf, node->type);
    zbuf_append(buf, ",\"returnTypeId\":");
    graph_semantics_append_quoted(buf, node->type_id);
    zbuf_appendf(buf, ",\"public\":%s,\"fallible\":%s,\"exportC\":%s", node->is_public ? "true" : "false", node->fallible ? "true" : "false", node->export_c ? "true" : "false");
    zbuf_append(buf, ",\"params\":");
    graph_semantics_append_function_params_json(buf, graph, sources, node);
    zbuf_append(buf, ",\"effects\":");
    graph_semantics_append_effect_refs_json(buf, graph, node);
    zbuf_append(buf, ",\"errors\":");
    graph_semantics_append_error_names_json(buf, graph, node);
    zbuf_append(buf, ",\"sourceRange\":");
    graph_semantics_append_source_range_json(buf, graph, sources, node);
    zbuf_append(buf, "}");
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_calls_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphResolutionFacts *resolution) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!graph_semantics_node_is_call(node)) continue;
    const ZProgramGraphResolutionReference *ref = graph_semantics_call_reference(resolution, node);
    if (!ref) continue;
    bool checked = graph_semantics_is_under_kind(graph, node, Z_PROGRAM_GRAPH_NODE_CHECK) ||
                   graph_semantics_is_under_kind(graph, node, Z_PROGRAM_GRAPH_NODE_RESCUE);
    bool fallible = false;
    ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, node);
    bool implicit_receiver = graph_semantics_call_has_implicit_receiver(graph, resolution, node, &contract);
    if (!first) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"node\":");
    graph_semantics_append_quoted(buf, node->id);
    zbuf_append(buf, ",\"kind\":");
    graph_semantics_append_quoted(buf, z_program_graph_node_kind_name(node->kind));
    zbuf_append(buf, ",\"name\":");
    graph_semantics_append_quoted(buf, node->name);
    zbuf_append(buf, ",\"qualifiedName\":");
    graph_semantics_append_quoted(buf, ref->qualified_name);
    zbuf_append(buf, ",\"returnType\":");
    graph_semantics_append_quoted(buf, graph_semantics_contract_return_type(&contract, node));
    zbuf_append(buf, ",\"returnTypeId\":");
    graph_semantics_append_quoted(buf, node->type_id);
    zbuf_append(buf, ",\"receiver\":");
    graph_semantics_append_receiver_json(buf, graph, sources, resolution, node, implicit_receiver);
    zbuf_append(buf, ",\"args\":");
    graph_semantics_append_args_json(buf, graph, sources, resolution, &contract, implicit_receiver, node);
    zbuf_append(buf, ",\"contract\":");
    graph_semantics_append_contract_json(buf, graph, resolution, node, checked, implicit_receiver, &fallible, &contract);
    zbuf_append(buf, ",\"resolution\":");
    graph_semantics_append_resolution_json(buf, &contract);
    zbuf_appendf(buf, ",\"fallible\":%s,\"checked\":%s,\"sourceRange\":", fallible ? "true" : "false", checked ? "true" : "false");
    graph_semantics_append_source_range_json(buf, graph, sources, node);
    zbuf_append(buf, "}");
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_effects_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    bool include_function = node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION &&
      (node->fallible || graph_semantics_child_count(graph, node, "effect") > 0 || graph_semantics_child_count(graph, node, "error") > 0);
    bool include_statement = node->kind == Z_PROGRAM_GRAPH_NODE_CHECK || node->kind == Z_PROGRAM_GRAPH_NODE_RAISE || node->kind == Z_PROGRAM_GRAPH_NODE_RESCUE;
    if (!include_function && !include_statement) continue;
    if (!first) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"node\":");
    graph_semantics_append_quoted(buf, node->id);
    zbuf_append(buf, ",\"kind\":");
    graph_semantics_append_quoted(buf, z_program_graph_node_kind_name(node->kind));
    zbuf_appendf(buf, ",\"fallible\":%s", node->fallible ? "true" : "false");
    zbuf_append(buf, ",\"effects\":");
    graph_semantics_append_effect_refs_json(buf, graph, node);
    zbuf_append(buf, ",\"errors\":");
    graph_semantics_append_error_names_json(buf, graph, node);
    zbuf_append(buf, ",\"sourceRange\":");
    graph_semantics_append_source_range_json(buf, graph, sources, node);
    zbuf_append(buf, "}");
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_ownership_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphResolutionFacts *resolution) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!graph_semantics_node_has_type_fact(node) || !graph_semantics_type_is_ownership_fact(graph, resolution, node->type, node->type_id)) continue;
    if (!first) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"node\":");
    graph_semantics_append_quoted(buf, node->id);
    zbuf_append(buf, ",\"kind\":");
    graph_semantics_append_quoted(buf, z_program_graph_node_kind_name(node->kind));
    zbuf_append(buf, ",\"name\":");
    graph_semantics_append_quoted(buf, node->name);
    zbuf_append(buf, ",\"type\":");
    graph_semantics_append_quoted(buf, node->type);
    zbuf_append(buf, ",\"ownership\":");
    graph_semantics_append_quoted(buf, graph_semantics_ownership_kind(graph, resolution, node->type, node->type_id));
    zbuf_appendf(buf, ",\"mutable\":%s,\"resource\":%s,\"sourceRange\":", node->is_mutable ? "true" : "false", graph_semantics_type_is_resource(graph, resolution, node->type, node->type_id) ? "true" : "false");
    graph_semantics_append_source_range_json(buf, graph, sources, node);
    zbuf_append(buf, "}");
    first = false;
  }
  zbuf_append(buf, "]");
}

static bool graph_semantics_borrow_type(char *out, size_t out_len, const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *borrow, const ZProgramGraphNode *target) {
  if (!out || out_len == 0) return false;
  out[0] = 0;
  if (!borrow) return false;
  if (graph_semantics_text_present(borrow->type)) return graph_semantics_copy_text(out, out_len, borrow->type);
  if (borrow->kind != Z_PROGRAM_GRAPH_NODE_BORROW || !target) return false;
  const char *target_type = graph_semantics_node_semantic_type(graph, resolution, target);
  if (!graph_semantics_text_present(target_type)) return false;
  char owned_inner[160];
  if (graph_semantics_span_type_element(target_type, "owned", owned_inner, sizeof(owned_inner))) target_type = owned_inner;
  int written = snprintf(out, out_len, "%s<%s>", borrow->is_mutable ? "mutref" : "ref", target_type);
  return written > 0 && (size_t)written < out_len;
}

static void graph_semantics_append_borrowing_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphResolutionFacts *resolution) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_BORROW && !graph_semantics_type_is_borrow(node->type)) continue;
    if (!first) zbuf_append(buf, ",");
    const ZProgramGraphNode *target = node->kind == Z_PROGRAM_GRAPH_NODE_BORROW ? graph_semantics_child(graph, node, "left", 0) : NULL;
    char borrow_type[192];
    if (!graph_semantics_borrow_type(borrow_type, sizeof(borrow_type), graph, resolution, node, target)) borrow_type[0] = 0;
    zbuf_append(buf, "{\"node\":");
    graph_semantics_append_quoted(buf, node->id);
    zbuf_append(buf, ",\"kind\":");
    graph_semantics_append_quoted(buf, z_program_graph_node_kind_name(node->kind));
    zbuf_append(buf, ",\"type\":");
    graph_semantics_append_quoted(buf, borrow_type);
    zbuf_append(buf, ",\"borrowKind\":");
    graph_semantics_append_quoted(buf, graph_semantics_ownership_kind(graph, resolution, borrow_type, node->type_id));
    zbuf_appendf(buf, ",\"mutable\":%s,\"target\":", (node->is_mutable || graph_semantics_text_contains(borrow_type, "mut")) ? "true" : "false");
    graph_semantics_append_quoted(buf, target ? target->id : "");
    zbuf_append(buf, ",\"sourceRange\":");
    graph_semantics_append_source_range_json(buf, graph, sources, node);
    zbuf_append(buf, "}");
    first = false;
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_resources_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphResolutionFacts *resolution) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (graph_semantics_node_has_type_fact(node) && graph_semantics_type_is_resource(graph, resolution, node->type, node->type_id)) {
      if (!first) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"node\":");
      graph_semantics_append_quoted(buf, node->id);
      zbuf_append(buf, ",\"kind\":\"binding\",\"resourceKind\":");
      graph_semantics_append_quoted(buf, graph_semantics_resource_kind(graph, resolution, node->type, node->type_id, ""));
      zbuf_append(buf, ",\"type\":");
      graph_semantics_append_quoted(buf, node->type);
      zbuf_append(buf, ",\"sourceRange\":");
      graph_semantics_append_source_range_json(buf, graph, sources, node);
      zbuf_append(buf, "}");
      first = false;
    }
    if (!graph_semantics_node_is_call(node)) continue;
    const ZProgramGraphResolutionReference *ref = graph_semantics_call_reference(resolution, node);
    if (!ref) continue;
    ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, node);
    if (graph_semantics_contract_is_resource(&contract)) {
      if (!first) zbuf_append(buf, ",");
      const char *capability = graph_semantics_contract_capability(&contract);
      zbuf_append(buf, "{\"node\":");
      graph_semantics_append_quoted(buf, node->id);
      zbuf_append(buf, ",\"kind\":\"capabilityUse\",\"resourceKind\":");
      graph_semantics_append_quoted(buf, graph_semantics_resource_kind(graph, resolution, node->type, node->type_id, capability));
      zbuf_append(buf, ",\"capability\":");
      graph_semantics_append_quoted(buf, capability);
      zbuf_append(buf, ",\"qualifiedName\":");
      graph_semantics_append_quoted(buf, ref->qualified_name);
      zbuf_append(buf, ",\"sourceRange\":");
      graph_semantics_append_source_range_json(buf, graph, sources, node);
      zbuf_append(buf, "}");
      first = false;
    }
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_target_requirements_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphResolutionFacts *resolution) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!graph_semantics_node_is_call(node)) continue;
    const ZProgramGraphResolutionReference *ref = graph_semantics_call_reference(resolution, node);
    if (!ref) continue;
    ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, node);
    if (graph_semantics_contract_has_target_requirement(&contract)) {
      if (!first) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"node\":");
      graph_semantics_append_quoted(buf, node->id);
      zbuf_append(buf, ",\"qualifiedName\":");
      graph_semantics_append_quoted(buf, ref->qualified_name);
      zbuf_append(buf, ",\"contractKind\":");
      graph_semantics_append_quoted(buf, graph_semantics_contract_kind(&contract));
      zbuf_append(buf, ",\"capability\":");
      graph_semantics_append_quoted(buf, graph_semantics_contract_capability(&contract));
      zbuf_append(buf, ",\"targetSupport\":");
      graph_semantics_append_quoted(buf, graph_semantics_contract_target_support(&contract));
      zbuf_append(buf, ",\"sourceRange\":");
      graph_semantics_append_source_range_json(buf, graph, sources, node);
      zbuf_append(buf, "}");
      first = false;
    }
  }
  zbuf_append(buf, "]");
}

static void graph_semantics_append_repairs_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphSourceRangeContext *sources, const ZProgramGraphResolutionFacts *resolution) {
  zbuf_append(buf, "[");
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!graph_semantics_node_is_call(node)) continue;
    const ZProgramGraphResolutionReference *ref = graph_semantics_call_reference(resolution, node);
    if (!ref) continue;
    bool checked = graph_semantics_is_under_kind(graph, node, Z_PROGRAM_GRAPH_NODE_CHECK) ||
                   graph_semantics_is_under_kind(graph, node, Z_PROGRAM_GRAPH_NODE_RESCUE);
    ZGraphSemanticContract contract = graph_semantics_contract(graph, resolution, node);
    if (graph_semantics_contract_has_repair(&contract)) {
      if (!first) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"node\":");
      graph_semantics_append_quoted(buf, node->id);
      zbuf_append(buf, ",\"qualifiedName\":");
      graph_semantics_append_quoted(buf, ref->qualified_name);
      zbuf_appendf(buf, ",\"requiresCheck\":%s,\"repair\":", contract.fallible && !checked ? "true" : "false");
      graph_semantics_append_repair_json(buf, &contract, checked);
      zbuf_append(buf, ",\"sourceRange\":");
      graph_semantics_append_source_range_json(buf, graph, sources, node);
      zbuf_append(buf, "}");
      first = false;
    }
  }
  zbuf_append(buf, "]");
}

void z_program_graph_append_semantics_json(ZBuf *buf, const ZProgramGraph *graph) {
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  z_program_graph_collect_resolution_facts(graph, &resolution);
  size_t fallible_calls = 0;
  size_t contracts = 0;
  graph_semantics_call_summary_counts(graph, &resolution, &fallible_calls, &contracts);
  ZProgramGraphSourceRangeContext sources;
  z_program_graph_source_range_context_init(&sources, graph);
  zbuf_append(buf, "{\"state\":\"typed-facts\",\"ok\":true");
  zbuf_appendf(buf,
               ",\"counts\":{\"typedNodes\":%zu,\"functions\":%zu,\"calls\":%zu,\"fallibleCalls\":%zu,\"effects\":%zu,\"contracts\":%zu,\"ownership\":%zu,\"borrowing\":%zu,\"resources\":%zu,\"targetRequirements\":%zu,\"repairs\":%zu,\"diagnostics\":0}",
               graph_semantics_typed_node_count(graph),
               graph_semantics_function_count(graph),
               graph_semantics_call_count(graph, &resolution),
               fallible_calls,
               graph_semantics_effect_fact_count(graph),
               contracts,
               graph_semantics_ownership_fact_count(graph, &resolution),
               graph_semantics_borrowing_fact_count(graph),
               graph_semantics_resource_fact_count(graph, &resolution),
               graph_semantics_target_requirement_count(graph, &resolution),
               graph_semantics_repair_fact_count(graph, &resolution));
  zbuf_append(buf, ",\"types\":");
  graph_semantics_append_types_json(buf, graph, &sources);
  zbuf_append(buf, ",\"functions\":");
  graph_semantics_append_functions_json(buf, graph, &sources);
  zbuf_append(buf, ",\"calls\":");
  graph_semantics_append_calls_json(buf, graph, &sources, &resolution);
  zbuf_append(buf, ",\"effects\":");
  graph_semantics_append_effects_json(buf, graph, &sources);
  zbuf_append(buf, ",\"ownership\":");
  graph_semantics_append_ownership_json(buf, graph, &sources, &resolution);
  zbuf_append(buf, ",\"borrowing\":");
  graph_semantics_append_borrowing_json(buf, graph, &sources, &resolution);
  zbuf_append(buf, ",\"resources\":");
  graph_semantics_append_resources_json(buf, graph, &sources, &resolution);
  zbuf_append(buf, ",\"targetRequirements\":");
  graph_semantics_append_target_requirements_json(buf, graph, &sources, &resolution);
  zbuf_append(buf, ",\"repairs\":");
  graph_semantics_append_repairs_json(buf, graph, &sources, &resolution);
  zbuf_append(buf, ",\"diagnostics\":[]");
  zbuf_append(buf, "}");
  z_program_graph_source_range_context_free(&sources);
  z_program_graph_resolution_facts_free(&resolution);
}
