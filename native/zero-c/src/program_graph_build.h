#ifndef ZERO_C_PROGRAM_GRAPH_BUILD_H
#define ZERO_C_PROGRAM_GRAPH_BUILD_H

#include "program_graph.h"

typedef struct {
  const char *artifact;
  const char *graph_hash;
  const char *module_identity;
  const char *lowering;
  const char *source_projection_state;
  bool canonical_source;
} ZProgramGraphArtifactSource;

bool z_program_graph_artifact_source_present(const ZProgramGraphArtifactSource *source);
bool z_program_graph_has_direct_entry_function(const ZProgramGraph *graph);
bool z_program_graph_merge_embedded_std_graph_modules(ZProgramGraph *graph, const SourceInput *input, ZDiag *diag);
bool z_program_graph_merge_embedded_std_graph_modules_timed(ZProgramGraph *graph, SourceInput *input, ZDiag *diag);
bool z_program_graph_prepare_artifact_mir_input(const char *artifact_path, const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag);
bool z_program_graph_prepare_repository_store_mir_input(const char *store_path, const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, bool require_checked_program, bool write_mapped_mir_cache, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag);
#endif
