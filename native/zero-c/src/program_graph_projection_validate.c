#include "program_graph_projection.h"

#include "canonical_text.h"
#include "program_graph_import.h"
#include "program_graph_mir_std.h"
#include "program_graph_view.h"
#include "std_source.h"
#include "zero.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool projection_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool projection_starts_with(const char *text, const char *prefix) {
  size_t len = prefix ? strlen(prefix) : 0;
  return text && prefix && strncmp(text, prefix, len) == 0;
}

static char *projection_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left && left[0] ? left : ".");
  if (buf.len > 0 && buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right && right[0] ? right : "");
  return buf.data;
}

static char *projection_dirname_of(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static bool projection_path_is_absolute(const char *path) {
  if (!path || !path[0]) return false;
  if (path[0] == '/') return true;
  return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':';
}

static void projection_source_set_package_identity(SourceInput *input, const char *identity) {
  const char *prefix = "package:";
  size_t prefix_len = strlen(prefix);
  if (!input || !identity || strncmp(identity, prefix, prefix_len) != 0) return;
  const char *name = identity + prefix_len;
  const char *version = strrchr(name, '@');
  if (version && version > name) {
    input->package_name = z_strndup(name, (size_t)(version - name));
    input->package_version = z_strdup(version + 1);
  } else {
    input->package_name = z_strdup(name);
  }
}

static void projection_seed_package_metadata(SourceInput *input, const ZProgramGraphStore *store) {
  if (!input || !store || !projection_starts_with(store->graph.module_identity, "package:")) return;
  projection_source_set_package_identity(input, store->graph.module_identity);
  input->package_root = z_strdup(store->root && store->root[0] ? store->root : ".");
  char *manifest_path = projection_join_path(input->package_root, "zero.json");
  if (z_program_graph_store_file_exists(manifest_path)) {
    input->manifest_path = manifest_path;
  } else {
    free(manifest_path);
  }
}

static char *projection_resolve_c_import_header_path(const SourceInput *input, const char *header) {
  if (!header || !header[0]) return z_strdup(header ? header : "");
  if (projection_path_is_absolute(header)) return z_strdup(header);
  if (input && input->package_root && input->package_root[0]) {
    char *package_path = projection_join_path(input->package_root, header);
    if (z_program_graph_store_file_exists(package_path)) return package_path;
    free(package_path);
  }
  if (input && input->source_file && input->source_file[0]) {
    char *dir = projection_dirname_of(input->source_file);
    char *source_path = projection_join_path(dir, header);
    free(dir);
    if (z_program_graph_store_file_exists(source_path)) return source_path;
    free(source_path);
  }
  if (z_program_graph_store_file_exists(header)) return z_strdup(header);
  return z_strdup(header);
}

static void projection_resolve_program_c_import_header_paths(const SourceInput *input, Program *program) {
  for (size_t i = 0; program && i < program->c_imports.len; i++) {
    CImport *item = &program->c_imports.items[i];
    char *resolved = projection_resolve_c_import_header_path(input, item->header);
    free(item->resolved_header);
    item->resolved_header = resolved;
  }
}

static bool projection_diag(const ZProgramGraphStore *store, ZDiag *diag, const char *message, const char *actual) {
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 1002;
    diag->path = store && store->path ? store->path : "zero.graph";
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "repository graph source projection does not match stored graph");
    snprintf(diag->expected, sizeof(diag->expected), "source projection text whose ProgramGraph matches zero.graph");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "projection graph mismatch");
  }
  return false;
}

static void projection_input_push_string(char ***items, size_t *len, const char *text) {
  *items = z_checked_reallocarray(*items, *len + 1, sizeof(char *));
  (*items)[(*len)++] = z_strdup(text ? text : "");
}

static void projection_input_push_source_line(SourceInput *input, const char *path, int line) {
  input->source_line_paths = z_checked_reallocarray(input->source_line_paths, input->source_line_count + 1, sizeof(char *));
  input->source_line_numbers = z_checked_reallocarray(input->source_line_numbers, input->source_line_count + 1, sizeof(int));
  input->source_line_paths[input->source_line_count] = z_strdup(path ? path : "");
  input->source_line_numbers[input->source_line_count] = line > 0 ? line : 1;
  input->source_line_count++;
}

static void projection_input_push_module(SourceInput *input, const char *name, const char *path) {
  input->module_names = z_checked_reallocarray(input->module_names, input->module_count + 1, sizeof(char *));
  input->module_paths = z_checked_reallocarray(input->module_paths, input->module_count + 1, sizeof(char *));
  input->module_names[input->module_count] = z_strdup(name && name[0] ? name : "main");
  input->module_paths[input->module_count] = z_strdup(path ? path : "");
  input->module_count++;
}

static void projection_append_source(SourceInput *input, ZBuf *combined, const char *path, const char *source) {
  const char *line = source ? source : "";
  int original_line = 1;
  if (!*line) {
    zbuf_append_char(combined, '\n');
    projection_input_push_source_line(input, path, original_line);
    return;
  }
  while (*line) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    zbuf_appendf(combined, "%.*s\n", (int)len, line);
    projection_input_push_source_line(input, path, original_line++);
    if (!end) break;
    line = end + 1;
  }
  zbuf_append_char(combined, '\n');
  projection_input_push_source_line(input, path, original_line);
}

static size_t projection_index_for_path(const ZProgramGraphStore *store, const char *path) {
  for (size_t i = 0; store && path && i < store->projection_len; i++) {
    if (projection_text_eq(store->projection_paths[i], path)) return i;
  }
  return SIZE_MAX;
}

static const char *projection_module_name_for_path(const ZProgramGraphStore *store, const char *path) {
  for (size_t i = 0; store && i < store->graph.node_len; i++) {
    const ZProgramGraphNode *node = &store->graph.nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && projection_text_eq(node->path, path)) return node->name;
  }
  return "main";
}

static const char *projection_basename(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}

static bool projection_std_module_path_matches(const ZStdSourceModule *module, const char *path) {
  return module && path &&
         (projection_text_eq(module->path, path) ||
          projection_text_eq(projection_basename(module->path), projection_basename(path)));
}

static bool projection_source_matches_embedded_std(const char *path, const char *source) {
  for (size_t i = 0; i < z_std_source_module_count(); i++) {
    const ZStdSourceModule *module = z_std_source_module_at(i);
    if (!projection_std_module_path_matches(module, path)) continue;
    char *embedded = z_std_source_module_copy_source(module);
    bool matches = embedded && projection_text_eq(source, embedded);
    free(embedded);
    if (matches) return true;
  }
  return false;
}

static bool projection_path_is_embedded_std(const char *path) {
  for (size_t i = 0; i < z_std_source_module_count(); i++) {
    if (projection_std_module_path_matches(z_std_source_module_at(i), path)) return true;
  }
  return false;
}

static bool projection_store_is_embedded_std_library(const ZProgramGraphStore *store) {
  if (!store || store->projection_len == 0) return false;
  for (size_t i = 0; i < store->projection_len; i++) {
    if (!projection_source_matches_embedded_std(store->projection_paths[i], store->projection_texts[i])) return false;
  }
  return true;
}

static bool projection_append_path_source(const ZProgramGraphStore *store, SourceInput *input, ZBuf *combined, const char *path, bool *used) {
  size_t index = projection_index_for_path(store, path);
  if (index == SIZE_MAX) return false;
  if (used && used[index]) return true;
  projection_input_push_string(&input->source_files, &input->source_file_count, path);
  projection_append_source(input, combined, path, store->projection_texts[index]);
  if (used) used[index] = true;
  return true;
}

static bool projection_source_input_from_store(const ZProgramGraphStore *store, SourceInput *input, ZDiag *diag) {
  if (!store || store->projection_len == 0) return projection_diag(store, diag, "repository graph store has no source projections", "empty projection table");
  *input = (SourceInput){0};
  input->canonical_text_source = true;
  input->source_file = z_strdup(store->projection_paths[0]);
  projection_seed_package_metadata(input, store);

  ZBuf combined;
  zbuf_init(&combined);
  bool *used = z_checked_calloc(store->projection_len, sizeof(bool));
  for (size_t i = 0; i < store->graph.node_len; i++) {
    const ZProgramGraphNode *node = &store->graph.nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    size_t index = projection_index_for_path(store, node->path);
    if (index == SIZE_MAX) continue;
    projection_input_push_module(input, node->name, node->path);
    projection_append_path_source(store, input, &combined, node->path, used);
  }
  for (size_t i = 0; i < store->projection_len; i++) {
    if (used[i]) continue;
    projection_input_push_module(input, projection_module_name_for_path(store, store->projection_paths[i]), store->projection_paths[i]);
    projection_append_path_source(store, input, &combined, store->projection_paths[i], used);
  }
  free(used);
  input->source = combined.data ? combined.data : z_strdup("");
  return true;
}

bool z_program_graph_projection_graph_from_store(const ZProgramGraphStore *store, const ZTargetInfo *target, ZProgramGraph *graph, ZDiag *diag) {
  if (graph) z_program_graph_init(graph);
  SourceInput input = {0};
  if (!projection_source_input_from_store(store, &input, diag)) return false;

  Program program = {0};
  ZDiag parse_diag = {0};
  bool ok = z_parse_canonical_text_program_source(input.source, &program, &parse_diag);
  if (!ok) {
    const char *actual = parse_diag.message[0] ? parse_diag.message : "projection source parse failed";
    z_free_source(&input);
    return projection_diag(store, diag, "repository graph source projection does not parse", actual);
  }
  if (target) z_set_check_target(target);
  projection_resolve_program_c_import_header_paths(&input, &program);
  ZDiag check_diag = {0};
  if (!projection_store_is_embedded_std_library(store)) {
    size_t appended_std_functions = 0;
    if (!z_program_graph_append_source_std_functions(&program, &appended_std_functions, &check_diag)) {
      const char *actual = check_diag.message[0] ? check_diag.message : "projection std helper expansion failed";
      z_free_program(&program);
      z_free_source(&input);
      return projection_diag(store, diag, "repository graph source projection does not check", actual);
    }
  }
  ok = projection_store_is_embedded_std_library(store) ? z_check_program_library(&program, &check_diag) : z_check_program(&program, &check_diag);
  if (!ok) {
    const char *actual = check_diag.message[0] ? check_diag.message : "projection source check failed";
    z_free_program(&program);
    z_free_source(&input);
    return projection_diag(store, diag, "repository graph source projection does not check", actual);
  }

  ok = graph && z_program_graph_from_program(&input, &program, graph);
  if (ok) {
    graph->canonical_source = true;
  }
  z_free_program(&program);
  z_free_source(&input);
  return ok || (diag && diag->message[0] ? false : projection_diag(store, diag, "repository graph source projection does not match stored graph", "projection graph mismatch"));
}

bool z_program_graph_projection_store_matches_graph(const ZProgramGraphStore *store, const ZTargetInfo *target, ZDiag *diag) {
  if (!store || store->projection_len == 0) return projection_diag(store, diag, "repository graph store has no source projections", "empty projection table");
  for (size_t i = 0; i < store->source_path_len; i++) {
    const char *path = store->source_paths[i];
    if (!z_program_graph_store_source_path_is_local(path)) continue;
    const char *stored = z_program_graph_store_projection_text(store, path);
    if (!stored && projection_path_is_embedded_std(path)) continue;
    if (!stored) return projection_diag(store, diag, "repository graph projection table is missing a source path", path);
  }
  if (projection_store_is_embedded_std_library(store)) return true;
  bool generated_ok = true;
  bool generated_matches = true;
  for (size_t i = 0; i < store->projection_len; i++) {
    const char *path = store->projection_paths[i];
    ZBuf generated;
    zbuf_init(&generated);
    ZDiag generated_diag = {0};
    bool ok = z_program_graph_append_source_view(&generated, &store->graph, path, &generated_diag);
    if (!ok) {
      generated_ok = false;
      zbuf_free(&generated);
      break;
    }
    bool matches = projection_text_eq(generated.data ? generated.data : "", store->projection_texts[i]);
    zbuf_free(&generated);
    if (!matches) {
      generated_matches = false;
      break;
    }
  }
  if (generated_ok && generated_matches) return true;

  ZProgramGraph projection_graph = {0};
  ZDiag projection_diag_value = {0};
  if (!z_program_graph_projection_graph_from_store(store, target, &projection_graph, &projection_diag_value)) {
    if (diag) *diag = projection_diag_value;
    return false;
  }
  bool matches = z_program_graph_store_graph_matches_source(store, &projection_graph);
  z_program_graph_free(&projection_graph);
  if (matches) return true;
  return projection_diag(store, diag, "repository graph source projection does not match stored graph", "projection graph mismatch");
}
