#ifndef ZERO_C_PROGRAM_GRAPH_REPOSITORY_MERGE_H
#define ZERO_C_PROGRAM_GRAPH_REPOSITORY_MERGE_H

#include "program_graph_store.h"

typedef struct {
  bool ok;
  bool wrote;
  char code[16];
  char message[160];
  char node_id[96];
  char source_path[256];
  char semantic_object[160];
  char field[64];
  char target_path[4096];
  size_t merged_nodes;
  size_t merged_edges;
  size_t left_changes;
  size_t right_changes;
  size_t conflicts;
} ZRepositoryGraphMergeResult;

bool z_repository_graph_merge_stores(
  const ZProgramGraphStore *base,
  const ZProgramGraphStore *left,
  const ZProgramGraphStore *right,
  const ZTargetInfo *target,
  const char *target_path,
  ZProgramGraphStore *merged,
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag
);

#endif
