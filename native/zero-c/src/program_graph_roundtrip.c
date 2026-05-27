#include "program_graph_roundtrip.h"
#include "program_graph_format.h"
#include "program_graph_import.h"
#include "program_graph_lower.h"

#include <stdio.h>
#include <string.h>

bool z_program_graph_direct_roundtrip_graph(const ZProgramGraph *original, const char *source_path, ZProgramGraph *roundtrip, ZProgramGraphCompare *comparison, ZDiag *diag) {
  if (!original || !roundtrip || !comparison) return false;
  *roundtrip = (ZProgramGraph){0};
  *comparison = (ZProgramGraphCompare){0};

  Program lowered_program = {0};
  SourceInput lowered_input = {0};
  bool ok = z_program_graph_lower_to_program_for_roundtrip(original, source_path, &lowered_program, &lowered_input, diag) &&
            z_program_graph_from_program(&lowered_input, &lowered_program, roundtrip);

  if (ok) {
    roundtrip->canonical_source = original->canonical_source;
    z_program_graph_semantic_compare(original, roundtrip, comparison);
  }
  else if (diag && diag->code == 0) {
    diag->code = 2002;
    diag->path = source_path;
    diag->line = diag->column = diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "failed to rebuild program graph after direct lowering");
  }

  z_free_program(&lowered_program);
  z_free_source(&lowered_input);
  return ok;
}

bool z_program_graph_direct_roundtrip_file(const char *artifact_path, const char *out_path, ZProgramGraphDirectRoundtrip *result, ZDiag *diag) {
  if (!result) return false;
  *result = (ZProgramGraphDirectRoundtrip){0};

  bool ok = z_program_graph_load(artifact_path, &result->original, diag) &&
            z_program_graph_direct_roundtrip_graph(&result->original, artifact_path, &result->roundtrip, &result->comparison, diag);

  if (ok && out_path) {
    z_program_graph_apply_storage_metadata(out_path, &result->roundtrip);
    if (result->roundtrip.canonical_source) result->original.canonical_source = true;
    if (!z_program_graph_save(out_path, &result->roundtrip, diag)) ok = false;
  }
  return ok;
}

void z_program_graph_direct_roundtrip_free(ZProgramGraphDirectRoundtrip *result) {
  if (!result) return;
  z_program_graph_free(&result->roundtrip);
  z_program_graph_free(&result->original);
  memset(result, 0, sizeof(*result));
}
