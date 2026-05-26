#include "program_graph_size.h"

#include <stdlib.h>
#include <string.h>

static const ZProgramGraphNode *graph_size_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (graph->nodes[i].id && strcmp(graph->nodes[i].id, id) == 0) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphNode *graph_size_find_module_named(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && node->name && strcmp(node->name, name) == 0) return node;
  }
  return NULL;
}

static const char *graph_size_symbol_kind(ZProgramGraphNodeKind kind) {
  switch (kind) {
    case Z_PROGRAM_GRAPH_NODE_CONST: return "const";
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS: return "type-alias";
    case Z_PROGRAM_GRAPH_NODE_SHAPE: return "shape";
    case Z_PROGRAM_GRAPH_NODE_INTERFACE: return "interface";
    case Z_PROGRAM_GRAPH_NODE_ENUM: return "enum";
    case Z_PROGRAM_GRAPH_NODE_CHOICE: return "choice";
    case Z_PROGRAM_GRAPH_NODE_FUNCTION: return "function";
    default: return NULL;
  }
}

static void graph_size_push_string(char ***items, size_t *len, const char *value) {
  *items = z_checked_reallocarray(*items, *len + 1, sizeof(char *));
  (*items)[(*len)++] = z_strdup(value ? value : "");
}

static void graph_size_record_import_edge(SourceInput *input, const ZProgramGraphNode *module, const ZProgramGraphNode *node, const char *import_path) {
  const char *import_name = node && node->name ? node->name : "";
  graph_size_push_string(&input->imports, &input->import_count, import_name);
  input->import_from = z_checked_reallocarray(input->import_from, input->import_edge_count + 1, sizeof(char *));
  input->import_to = z_checked_reallocarray(input->import_to, input->import_edge_count + 1, sizeof(char *));
  input->import_paths = z_checked_reallocarray(input->import_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_source_paths = z_checked_reallocarray(input->import_source_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_lines = z_checked_reallocarray(input->import_lines, input->import_edge_count + 1, sizeof(int));
  input->import_columns = z_checked_reallocarray(input->import_columns, input->import_edge_count + 1, sizeof(int));
  input->import_lengths = z_checked_reallocarray(input->import_lengths, input->import_edge_count + 1, sizeof(int));
  input->import_from[input->import_edge_count] = z_strdup(module && module->name ? module->name : "");
  input->import_to[input->import_edge_count] = z_strdup(import_name);
  input->import_paths[input->import_edge_count] = z_strdup(import_path ? import_path : "");
  input->import_source_paths[input->import_edge_count] = z_strdup(node && node->path ? node->path : "");
  input->import_lines[input->import_edge_count] = node && node->line > 0 ? node->line : 1;
  input->import_columns[input->import_edge_count] = node && node->column > 0 ? node->column : 1;
  input->import_lengths[input->import_edge_count] = import_name[0] ? (int)strlen(import_name) : 1;
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

void z_program_graph_seed_size_source_metadata(SourceInput *input, const ZProgramGraph *graph) {
  if (!input || !graph) return;
  free(input->source);
  input->source = z_strdup(graph->graph_hash ? graph->graph_hash : "");
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    const ZProgramGraphNode *module = graph_size_find_node(graph, edge->from);
    const ZProgramGraphNode *node = graph_size_find_node(graph, edge->to);
    if (!module || module->kind != Z_PROGRAM_GRAPH_NODE_MODULE || !node) continue;
    if (node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT && strcmp(edge->kind, "import") == 0) {
      const char *import_name = node->name ? node->name : "";
      const ZProgramGraphNode *import_module = strncmp(import_name, "std.", 4) == 0 ? NULL : graph_size_find_module_named(graph, import_name);
      if (strncmp(import_name, "std.", 4) != 0) graph_size_record_import_edge(input, module, node, import_module && import_module->path ? import_module->path : "");
      continue;
    }
    const char *symbol_kind = graph_size_symbol_kind(node->kind);
    if (symbol_kind && node->name && node->name[0]) graph_size_record_symbol(input, module->name ? module->name : "", symbol_kind, node->name, node->is_public);
  }
}
