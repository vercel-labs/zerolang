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

#endif
