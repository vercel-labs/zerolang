#include "program_graph_build.h"

#include "program_graph_format.h"
#include "program_graph_lower.h"
#include "program_graph_size.h"

bool z_program_graph_artifact_source_present(const ZProgramGraphArtifactSource *source) {
  return source && source->graph_hash && source->graph_hash[0];
}

bool z_program_graph_prepare_artifact_input(const char *artifact_path, const ZTargetInfo *target, Program *program, SourceInput *input, ZProgramGraphArtifactSource *source, ZDiag *diag) {
  ZProgramGraph graph = {0};
  if (!z_program_graph_load(artifact_path, &graph, diag)) return false;

  bool ok = z_program_graph_lower_to_program_with_source(&graph, artifact_path, program, input, diag);
  if (ok) {
    z_set_check_target(target);
    ok = z_check_program(program, diag);
  }
  if (!ok) {
    if (input && input->source_file) z_map_source_diag(input, diag);
    if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : artifact_path;
    z_program_graph_free(&graph);
    return false;
  }

  z_program_graph_seed_source_metadata(input, &graph);
  if (source) {
    source->artifact = artifact_path;
    source->graph_hash = z_strdup(graph.graph_hash ? graph.graph_hash : "");
    source->module_identity = z_strdup(graph.module_identity ? graph.module_identity : "");
    source->lowering = "direct-program-graph";
    source->canonical_source = graph.canonical_source;
  }
  z_program_graph_free(&graph);
  return true;
}
