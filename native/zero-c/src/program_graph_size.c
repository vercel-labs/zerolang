#include "program_graph_size.h"

#include <stdlib.h>
#include <string.h>

static void graph_size_push_string(char ***items, size_t *len, const char *value) {
  *items = z_checked_reallocarray(*items, *len + 1, sizeof(char *)); (*items)[(*len)++] = z_strdup(value ? value : "");
}

static const char *graph_size_module_path_for_name(const SourceInput *input, const char *name) {
  for (size_t i = 0; input && name && i < input->module_count; i++) {
    if (input->module_names[i] && strcmp(input->module_names[i], name) == 0) return input->module_paths[i];
  }
  return "";
}

static void graph_size_record_import(SourceInput *input, const char *from, const char *to, const char *path, const char *source_path, int line, int column, int length) {
  const char *import_name = to ? to : "";
  graph_size_push_string(&input->imports, &input->import_count, import_name);
  input->import_from = z_checked_reallocarray(input->import_from, input->import_edge_count + 1, sizeof(char *));
  input->import_to = z_checked_reallocarray(input->import_to, input->import_edge_count + 1, sizeof(char *));
  input->import_paths = z_checked_reallocarray(input->import_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_source_paths = z_checked_reallocarray(input->import_source_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_lines = z_checked_reallocarray(input->import_lines, input->import_edge_count + 1, sizeof(int));
  input->import_columns = z_checked_reallocarray(input->import_columns, input->import_edge_count + 1, sizeof(int));
  input->import_lengths = z_checked_reallocarray(input->import_lengths, input->import_edge_count + 1, sizeof(int));
  input->import_from[input->import_edge_count] = z_strdup(from ? from : "");
  input->import_to[input->import_edge_count] = z_strdup(import_name);
  input->import_paths[input->import_edge_count] = z_strdup(path ? path : "");
  input->import_source_paths[input->import_edge_count] = z_strdup(source_path ? source_path : "");
  input->import_lines[input->import_edge_count] = line > 0 ? line : 1;
  input->import_columns[input->import_edge_count] = column > 0 ? column : 1;
  input->import_lengths[input->import_edge_count] = length > 0 ? length : (import_name[0] ? (int)strlen(import_name) : 1);
  input->import_edge_count++;
}

static void graph_size_record_symbol(SourceInput *input, const char *module, const char *kind, const char *name, bool is_public) {
  input->symbol_names = z_checked_reallocarray(input->symbol_names, input->symbol_count + 1, sizeof(char *));
  input->symbol_modules = z_checked_reallocarray(input->symbol_modules, input->symbol_count + 1, sizeof(char *));
  input->symbol_kinds = z_checked_reallocarray(input->symbol_kinds, input->symbol_count + 1, sizeof(char *));
  input->symbol_public = z_checked_reallocarray(input->symbol_public, input->symbol_count + 1, sizeof(bool));
  input->symbol_names[input->symbol_count] = z_strdup(name ? name : "");
  input->symbol_modules[input->symbol_count] = z_strdup(module ? module : "");
  input->symbol_kinds[input->symbol_count] = z_strdup(kind ? kind : "");
  input->symbol_public[input->symbol_count++] = is_public;
}

static bool graph_size_text_eq(const char *left, const char *right) { return strcmp(left ? left : "", right ? right : "") == 0; }

static const ZProgramGraphNode *graph_size_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (graph_size_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static const char *graph_size_owned_symbol_kind(const ZProgramGraphEdge *edge, const ZProgramGraphNode *node) {
  if (!edge || !node) return NULL;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_CONST: return graph_size_text_eq(edge->kind, "const") ? "const" : NULL;
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS: return graph_size_text_eq(edge->kind, "alias") ? "type-alias" : NULL;
    case Z_PROGRAM_GRAPH_NODE_SHAPE: return graph_size_text_eq(edge->kind, "shape") ? "shape" : NULL;
    case Z_PROGRAM_GRAPH_NODE_INTERFACE: return graph_size_text_eq(edge->kind, "interface") ? "interface" : NULL;
    case Z_PROGRAM_GRAPH_NODE_ENUM: return graph_size_text_eq(edge->kind, "enum") ? "enum" : NULL;
    case Z_PROGRAM_GRAPH_NODE_CHOICE: return graph_size_text_eq(edge->kind, "choice") ? "choice" : NULL;
    case Z_PROGRAM_GRAPH_NODE_FUNCTION: return graph_size_text_eq(edge->kind, "function") ? "function" : NULL;
    default: return NULL;
  }
}

static void graph_size_seed_from_graph(SourceInput *input, const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    const ZProgramGraphNode *module = graph_size_find_node(graph, edge->from);
    if (!module || module->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    const ZProgramGraphNode *node = graph_size_find_node(graph, edge->to);
    if (!node) continue;
    const char *module_name = module->name && module->name[0] ? module->name : "main";

    if (node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT && graph_size_text_eq(edge->kind, "import")) {
      const char *name = node->name ? node->name : "";
      if (strncmp(name, "std.", 4) == 0) continue;
      int line = node->line > 0 ? node->line : 1;
      graph_size_record_import(input,
                               module_name,
                               name,
                               graph_size_module_path_for_name(input, name),
                               node->path,
                               line,
                               node->column,
                               (int)strlen(name));
      continue;
    }

    const char *kind = graph_size_owned_symbol_kind(edge, node);
    if (kind) graph_size_record_symbol(input, module_name, kind, node->name, node->is_public);
  }
}

void z_program_graph_seed_size_source_metadata(SourceInput *input, const ZProgramGraph *graph) {
  if (!input || !graph) return;
  free(input->source);
  input->source = z_strdup(graph->graph_hash ? graph->graph_hash : "");
  graph_size_seed_from_graph(input, graph);
}
