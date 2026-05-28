#include "program_graph_view.h"

#include "canonical_text.h"
#include "program_graph_lower.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool view_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool view_starts_with(const char *text, const char *prefix) {
  return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool view_import_targets_rendered_module(const SourceInput *source, const char *module) {
  if (!source || !module || !module[0] || view_starts_with(module, "std.")) return false;
  for (size_t i = 0; i < source->module_count; i++) {
    if (view_text_eq(source->module_names[i], module)) return true;
  }
  return false;
}

static void view_drop_flattened_module_imports(Program *program, const SourceInput *source) {
  if (!program || !source || source->module_count <= 1) return;
  size_t write = 0;
  for (size_t read = 0; read < program->use_imports.len; read++) {
    UseImport item = program->use_imports.items[read];
    if (view_import_targets_rendered_module(source, item.module)) {
      free(item.module);
      free(item.alias);
      continue;
    }
    if (write != read) program->use_imports.items[write] = item;
    write++;
  }
  program->use_imports.len = write;
}

static void view_copy_node(ZProgramGraph *dst, const ZProgramGraphNode *src) {
  dst->nodes = z_checked_reallocarray(dst->nodes, dst->node_len + 1, sizeof(ZProgramGraphNode));
  ZProgramGraphNode *node = &dst->nodes[dst->node_len++];
  *node = (ZProgramGraphNode){
    .kind = src->kind,
    .line = src->line,
    .column = src->column,
    .is_public = src->is_public,
    .is_mutable = src->is_mutable,
    .is_static = src->is_static,
    .fallible = src->fallible,
    .export_c = src->export_c,
  };
  node->id = z_strdup(src->id ? src->id : "");
  node->name = z_strdup(src->name ? src->name : "");
  node->type = z_strdup(src->type ? src->type : "");
  node->value = z_strdup(src->value ? src->value : "");
  node->path = z_strdup(src->path ? src->path : "");
  node->symbol_id = z_strdup(src->symbol_id ? src->symbol_id : "");
  node->type_id = z_strdup(src->type_id ? src->type_id : "");
  node->effect_id = z_strdup(src->effect_id ? src->effect_id : "");
  node->node_hash = z_strdup(src->node_hash ? src->node_hash : "");
}

static void view_copy_edge(ZProgramGraph *dst, const ZProgramGraphEdge *src) {
  dst->edges = z_checked_reallocarray(dst->edges, dst->edge_len + 1, sizeof(ZProgramGraphEdge));
  ZProgramGraphEdge *edge = &dst->edges[dst->edge_len++];
  *edge = (ZProgramGraphEdge){
    .target = src->target,
    .order = src->order,
  };
  edge->from = z_strdup(src->from ? src->from : "");
  edge->to = z_strdup(src->to ? src->to : "");
  edge->kind = z_strdup(src->kind ? src->kind : "");
}

static ptrdiff_t view_find_node_index(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (view_text_eq(graph->nodes[i].id, id)) return (ptrdiff_t)i;
  }
  return -1;
}

static bool view_included_node_id(const ZProgramGraph *graph, const bool *included, const char *id) {
  ptrdiff_t index = view_find_node_index(graph, id);
  return index >= 0 && included[index];
}

static void view_include_reachable_nodes(const ZProgramGraph *graph, bool *included, size_t index) {
  if (!graph || !included || index >= graph->node_len || included[index]) return;
  included[index] = true;
  const char *id = graph->nodes[index].id;
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !view_text_eq(edge->from, id)) continue;
    ptrdiff_t target = view_find_node_index(graph, edge->to);
    if (target >= 0) view_include_reachable_nodes(graph, included, (size_t)target);
  }
}

static bool view_graph_has_symbol(const ZProgramGraph *graph, const bool *included, const char *symbol_id) {
  for (size_t i = 0; graph && symbol_id && i < graph->node_len; i++) {
    if (included[i] && view_text_eq(graph->nodes[i].symbol_id, symbol_id)) return true;
  }
  return false;
}

static bool view_graph_has_type(const ZProgramGraph *graph, const bool *included, const char *type_id) {
  for (size_t i = 0; graph && type_id && i < graph->node_len; i++) {
    if (included[i] && view_text_eq(graph->nodes[i].type_id, type_id)) return true;
  }
  return false;
}

static bool view_graph_has_effect(const ZProgramGraph *graph, const bool *included, const char *effect_id) {
  for (size_t i = 0; graph && effect_id && i < graph->node_len; i++) {
    if (included[i] && view_text_eq(graph->nodes[i].effect_id, effect_id)) return true;
  }
  return false;
}

static bool view_edge_target_included(const ZProgramGraph *graph, const bool *included, const ZProgramGraphEdge *edge) {
  switch (edge->target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
      return view_included_node_id(graph, included, edge->to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
      return view_graph_has_symbol(graph, included, edge->to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
      return view_graph_has_type(graph, included, edge->to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
      return view_graph_has_effect(graph, included, edge->to);
  }
  return false;
}

static bool view_slice_graph_for_source(const ZProgramGraph *graph, const char *source_path, ZProgramGraph *out, ZDiag *diag) {
  if (!graph || !source_path || !source_path[0]) return false;
  bool *included = calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
  if (!included) {
    if (diag) {
      diag->code = 2001;
      diag->path = source_path;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "out of memory while rendering source-backed graph view");
    }
    return false;
  }

  bool found_module = false;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_MODULE && view_text_eq(graph->nodes[i].path, source_path)) {
      found_module = true;
      view_include_reachable_nodes(graph, included, i);
    }
  }
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_MODULE) included[i] = true;
  }

  if (!found_module) {
    free(included);
    if (diag) {
      diag->code = 2002;
      diag->path = source_path;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "source-backed graph has no module for input file");
      snprintf(diag->expected, sizeof(diag->expected), "Module node with matching source path");
      snprintf(diag->actual, sizeof(diag->actual), "missing module");
      snprintf(diag->help, sizeof(diag->help), "dump the graph and patch a node that belongs to the input .0 file");
    }
    return false;
  }

  z_program_graph_init(out);
  free(out->module_identity);
  out->module_identity = z_strdup(graph->module_identity ? graph->module_identity : "module:main");
  out->validation_state = graph->validation_state;
  out->canonical_source = graph->canonical_source;
  out->next_id = graph->next_id;
  if (graph->graph_hash) out->graph_hash = z_strdup(graph->graph_hash);
  for (size_t i = 0; i < graph->node_len; i++) {
    if (included[i]) view_copy_node(out, &graph->nodes[i]);
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!view_included_node_id(graph, included, edge->from)) continue;
    if (!view_edge_target_included(graph, included, edge)) continue;
    view_copy_edge(out, edge);
  }
  free(included);
  return true;
}

static bool view_append_program(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, bool drop_flattened_imports, ZDiag *diag) {
  if (!buf || !graph) return false;
  Program program = {0};
  SourceInput input = {0};
  const char *path = source_path && source_path[0] ? source_path : "<program-graph>";
  bool ok = z_program_graph_lower_to_program_for_roundtrip(graph, path, &program, &input, diag);
  if (ok && drop_flattened_imports) view_drop_flattened_module_imports(&program, &input);
  if (ok) ok = z_canonical_text_write_program(&program, buf, diag);
  if (ok) {
    Program parsed = {0};
    ZDiag parse_diag = {0};
    ok = z_parse_canonical_text_program_source(buf->data ? buf->data : "", &parsed, &parse_diag);
    if (!ok && diag) {
      *diag = parse_diag;
      if (!diag->path) diag->path = path;
    }
    z_free_program(&parsed);
  }
  if (!ok && diag && diag->code == 0) {
    diag->code = 2002;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "failed to render program graph as canonical source");
    snprintf(diag->expected, sizeof(diag->expected), "lowerable ProgramGraph");
    snprintf(diag->actual, sizeof(diag->actual), "invalid graph view");
    snprintf(diag->help, sizeof(diag->help), "run zero graph check to inspect graph lowering diagnostics");
  }
  z_free_program(&program);
  z_free_source(&input);
  return ok;
}

bool z_program_graph_append_view(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, ZDiag *diag) {
  return view_append_program(buf, graph, source_path, true, diag);
}

bool z_program_graph_append_source_view(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, ZDiag *diag) {
  ZProgramGraph sliced = {0};
  if (!view_slice_graph_for_source(graph, source_path, &sliced, diag)) return false;
  bool ok = view_append_program(buf, &sliced, source_path, false, diag);
  z_program_graph_free(&sliced);
  return ok;
}
