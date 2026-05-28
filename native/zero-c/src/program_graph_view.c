#include "program_graph_view.h"

#include "canonical_text.h"
#include "program_graph_lower.h"

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

bool z_program_graph_append_view(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, ZDiag *diag) {
  if (!buf || !graph) return false;
  Program program = {0};
  SourceInput input = {0};
  const char *path = source_path && source_path[0] ? source_path : "<program-graph>";
  bool ok = z_program_graph_lower_to_program_for_roundtrip(graph, path, &program, &input, diag);
  if (ok) view_drop_flattened_module_imports(&program, &input);
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
