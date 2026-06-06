#include "program_graph_build.h"
#include "program_graph_import.h"
#include "program_graph_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool graph_compile_text_eq(const char *left, const char *right) { return strcmp(left ? left : "", right ? right : "") == 0; }

bool z_program_graph_source_command_uses_graph_mir(const char *command) {
  if (!command || graph_compile_text_eq(command, "fix") || graph_compile_text_eq(command, "doc")) return false;
  if (graph_compile_text_eq(command, "dev") || graph_compile_text_eq(command, "time") || graph_compile_text_eq(command, "abi")) return false;
  return true;
}

static bool graph_compile_should_try_typed_mir(const ZProgramGraph *graph) {
  return graph && graph->node_len <= 1024 && graph->edge_len <= 1024;
}

static bool graph_compile_diag(ZDiag *diag, const char *path, const char *message, const char *actual) {
  if (!diag) return false;
  *diag = (ZDiag){0};
  diag->code = 2002; diag->path = path; diag->line = 1; diag->column = 1; diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "failed to prepare source program graph");
  snprintf(diag->expected, sizeof(diag->expected), "source-imported ProgramGraph"); snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "invalid graph");
  snprintf(diag->help, sizeof(diag->help), "run zero check to inspect the source graph");
  return false;
}

bool z_program_graph_prepare_source_mir_input(const char *source_path, const ZTargetInfo *target, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag) {
  if (!program || !input || !ir) return graph_compile_diag(diag, source_path, "failed to prepare source program graph", "missing compiler input");
  const char *path = input->source_file ? input->source_file : source_path;
  ZProgramGraph graph = {0};
  if (!z_program_graph_from_program(input, program, &graph)) return graph_compile_diag(diag, path, "failed to build source program graph", "source import failed");
  graph.canonical_source = input->canonical_text_source;

  IrProgram graph_ir = {0};
  bool graph_mir_valid = false;
  if (graph_compile_should_try_typed_mir(&graph)) {
    graph_ir = z_lower_program_graph_with_source(&graph, input, target);
    graph_mir_valid = graph_ir.mir_valid;
  }
  if (graph_mir_valid) *ir = graph_ir;
  else { z_free_ir_program(&graph_ir); *ir = z_lower_program_with_source(program, input, target); }
  input->program_graph_hash = z_strdup(graph.graph_hash ? graph.graph_hash : "");
  input->program_graph_module_identity = z_strdup(graph.module_identity ? graph.module_identity : "");
  if (source) {
    source->artifact = path;
    source->graph_hash = input->program_graph_hash;
    source->module_identity = input->program_graph_module_identity;
    source->lowering = graph_mir_valid ? "typed-program-graph-mir" : "program-graph-ast-mir";
    source->canonical_source = graph.canonical_source;
  }
  z_program_graph_free(&graph);
  return true;
}
