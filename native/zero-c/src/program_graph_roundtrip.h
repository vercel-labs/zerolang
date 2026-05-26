#ifndef ZERO_C_PROGRAM_GRAPH_ROUNDTRIP_H
#define ZERO_C_PROGRAM_GRAPH_ROUNDTRIP_H

#include "program_graph_compare.h"

typedef struct {
  ZProgramGraph original;
  ZProgramGraph roundtrip;
  ZProgramGraphCompare comparison;
} ZProgramGraphDirectRoundtrip;

bool z_program_graph_direct_roundtrip_graph(const ZProgramGraph *original, const char *source_path, ZProgramGraph *roundtrip, ZProgramGraphCompare *comparison, ZDiag *diag);
bool z_program_graph_direct_roundtrip_file(const char *artifact_path, const char *out_path, ZProgramGraphDirectRoundtrip *result, ZDiag *diag);
void z_program_graph_direct_roundtrip_free(ZProgramGraphDirectRoundtrip *result);
#endif
