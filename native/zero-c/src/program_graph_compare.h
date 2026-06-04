#ifndef ZERO_C_PROGRAM_GRAPH_COMPARE_H
#define ZERO_C_PROGRAM_GRAPH_COMPARE_H

#include "program_graph.h"

typedef enum {
  Z_PROGRAM_GRAPH_DIFF_KIND_ADDED,
  Z_PROGRAM_GRAPH_DIFF_KIND_REMOVED,
  Z_PROGRAM_GRAPH_DIFF_KIND_MODIFIED,
} ZProgramGraphDiffKind;

typedef struct {
  ZProgramGraphDiffKind kind;
  char node_id[64];
  char node_kind[32];
  char field[32];
  char left_value[128];
  char right_value[128];
} ZProgramGraphDiffEntry;

typedef struct {
  bool ok;
  char code[16];
  char message[160];
  char field[32];
  size_t left_index;
  size_t right_index;
  size_t left_count;
  size_t right_count;
  size_t left_semantic_nodes;
  size_t right_semantic_nodes;
  size_t left_semantic_edges;
  size_t right_semantic_edges;
} ZProgramGraphCompare;

typedef struct {
  bool ok;
  size_t left_node_count;
  size_t right_node_count;
  ZProgramGraphDiffEntry *diffs;
  size_t diff_len;
  size_t diff_cap;
} ZProgramGraphDiff;

bool z_program_graph_semantic_compare(const ZProgramGraph *left, const ZProgramGraph *right, ZProgramGraphCompare *out);
bool z_program_graph_diff(const ZProgramGraph *left, const ZProgramGraph *right, ZProgramGraphDiff *out);
void z_program_graph_diff_free(ZProgramGraphDiff *diff);

#endif
