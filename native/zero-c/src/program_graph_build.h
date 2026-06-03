#ifndef ZERO_C_PROGRAM_GRAPH_BUILD_H
#define ZERO_C_PROGRAM_GRAPH_BUILD_H

#include "program_graph.h"

typedef struct {
  const char *artifact;
  char *graph_hash;
  char *module_identity;
  const char *lowering;
  bool canonical_source;
} ZProgramGraphArtifactSource;

bool z_program_graph_artifact_source_present(const ZProgramGraphArtifactSource *source);
bool z_program_graph_prepare_artifact_input(const char *artifact_path, const ZTargetInfo *target, Program *program, SourceInput *input, ZProgramGraphArtifactSource *source, ZDiag *diag);
bool z_program_graph_prepare_artifact_mir_input(const char *artifact_path, const ZTargetInfo *target, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag);
bool z_program_graph_prepare_source_mir_input(const char *source_path, const ZTargetInfo *target, const Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag);
bool z_program_graph_source_command_uses_graph_mir(const char *command);

#endif
