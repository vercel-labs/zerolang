#include "program_graph_size.h"
#include "program_graph_c_import_metadata.h"
#include <stdlib.h>
#include <string.h>
static void graph_size_push_string(char ***items, size_t *len, const char *value) {
  *items = z_checked_reallocarray(*items, *len + 1, sizeof(char *)); (*items)[(*len)++] = z_strdup(value ? value : "");
}

static bool graph_size_text_eq(const char *left, const char *right) {
  const unsigned char *a = (const unsigned char *)(left ? left : "");
  const unsigned char *b = (const unsigned char *)(right ? right : "");
  while (*a || *b) {
    if (*a != *b) return false;
    a++;
    b++;
  }
  return true;
}
static char *graph_size_dirname_of(const char *path) {
  if (!path || !path[0]) return z_strdup(".");
  const char *slash = strrchr(path, '/');
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static char *graph_size_manifest_for_source_path(const char *path) {
  char *dir = graph_size_dirname_of(path);
  while (dir && dir[0]) {
    char *manifest = z_manifest_path_for_root(dir);
    if (manifest) {
      free(dir);
      return manifest;
    }
    if (graph_size_text_eq(dir, ".") || graph_size_text_eq(dir, "/")) break;
    char *parent = graph_size_dirname_of(dir);
    if (graph_size_text_eq(parent, dir)) {
      free(parent);
      break;
    }
    free(dir);
    dir = parent;
  }
  free(dir);
  return NULL;
}

static bool graph_size_source_file_seen(const SourceInput *input, const char *path) {
  for (size_t i = 0; input && path && i < input->source_file_count; i++) {
    if (graph_size_text_eq(input->source_files[i], path)) return true;
  }
  return false;
}

static void graph_size_record_source_file(SourceInput *input, const char *path) {
  if (!input || !path || !path[0] || graph_size_source_file_seen(input, path)) return;
  graph_size_push_string(&input->source_files, &input->source_file_count, path);
}

static void graph_size_seed_manifest_from_path(SourceInput *input, const char *path) {
  if (!input || input->manifest_path || !path || !path[0]) return;
  char *manifest = graph_size_manifest_for_source_path(path);
  if (!manifest) return;
  input->manifest_path = manifest;
  if (!input->package_root) input->package_root = graph_size_dirname_of(manifest);
}

static const char *graph_size_module_path_for_name(const SourceInput *input, const char *name) {
  for (size_t i = 0; input && name && i < input->module_count; i++) {
    if (graph_size_text_eq(input->module_names[i], name)) return input->module_paths[i];
  }
  return "";
}

static void graph_size_record_module(SourceInput *input, const char *name, const char *path) {
  for (size_t i = 0; input && name && i < input->module_count; i++) {
    if (graph_size_text_eq(input->module_names[i], name)) return;
  }
  graph_size_push_string(&input->module_names, &input->module_count, name ? name : "");
  input->module_paths = z_checked_reallocarray(input->module_paths, input->module_count, sizeof(char *));
  input->module_paths[input->module_count - 1] = z_strdup(path ? path : "");
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

static void graph_size_clear_module_metadata(SourceInput *input) {
  if (!input) return;
  for (size_t i = 0; i < input->module_count; i++) {
    free(input->module_names[i]);
    free(input->module_paths[i]);
  }
  free(input->module_names);
  free(input->module_paths);
  input->module_names = NULL;
  input->module_paths = NULL;
  input->module_count = 0;
}

static void graph_size_clear_import_metadata(SourceInput *input) {
  if (!input) return;
  for (size_t i = 0; i < input->import_count; i++) free(input->imports[i]);
  free(input->imports);
  input->imports = NULL;
  input->import_count = 0;

  for (size_t i = 0; i < input->import_edge_count; i++) {
    free(input->import_from[i]);
    free(input->import_to[i]);
    free(input->import_paths[i]);
    free(input->import_source_paths[i]);
  }
  free(input->import_from);
  free(input->import_to);
  free(input->import_paths);
  free(input->import_source_paths);
  free(input->import_lines);
  free(input->import_columns);
  free(input->import_lengths);
  input->import_from = NULL;
  input->import_to = NULL;
  input->import_paths = NULL;
  input->import_source_paths = NULL;
  input->import_lines = NULL;
  input->import_columns = NULL;
  input->import_lengths = NULL;
  input->import_edge_count = 0;
}

static void graph_size_clear_symbol_metadata(SourceInput *input) {
  if (!input) return;
  for (size_t i = 0; i < input->symbol_count; i++) {
    free(input->symbol_names[i]);
    free(input->symbol_modules[i]);
    free(input->symbol_kinds[i]);
  }
  free(input->symbol_names);
  free(input->symbol_modules);
  free(input->symbol_kinds);
  free(input->symbol_public);
  input->symbol_names = NULL;
  input->symbol_modules = NULL;
  input->symbol_kinds = NULL;
  input->symbol_public = NULL;
  input->symbol_count = 0;
}

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
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE) graph_size_record_module(input, node->name && node->name[0] ? node->name : "main", node->path);
  }
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
      graph_size_record_import(input, module_name, name, graph_size_module_path_for_name(input, name), node->path, line, node->column, (int)strlen(name));
      continue;
    }

    const char *kind = graph_size_owned_symbol_kind(edge, node);
    if (kind) graph_size_record_symbol(input, module_name, kind, node->name, node->is_public);
  }
}

void z_program_graph_seed_source_metadata(SourceInput *input, const ZProgramGraph *graph) {
  if (!input || !graph) return;
  free(input->source);
  input->source = z_strdup(graph->graph_hash ? graph->graph_hash : "");
  z_program_graph_seed_source_metadata_facts(input, graph);
}

void z_program_graph_seed_source_metadata_facts(SourceInput *input, const ZProgramGraph *graph) {
  if (!input || !graph) return;
  graph_size_clear_module_metadata(input);
  graph_size_clear_import_metadata(input);
  graph_size_clear_symbol_metadata(input);
  z_program_graph_clear_c_import_metadata(input);
  graph_size_seed_from_graph(input, graph);
  z_program_graph_seed_c_import_metadata(input, graph);
}

void z_program_graph_seed_artifact_source_paths(SourceInput *input, const ZProgramGraph *graph, const char *artifact_path) {
  if (!input || !graph) return;
  if (!input->source_file) input->source_file = z_strdup(artifact_path && artifact_path[0] ? artifact_path : "<program-graph>");
  graph_size_seed_manifest_from_path(input, artifact_path);
  for (size_t i = 0; i < graph->node_len; i++) {
    const char *path = graph->nodes[i].path;
    if (!path || !path[0]) continue;
    graph_size_record_source_file(input, path);
    graph_size_seed_manifest_from_path(input, path);
  }
  if (input->source_file_count == 0) graph_size_record_source_file(input, input->source_file);
}
