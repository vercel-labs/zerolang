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

typedef enum {
  Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_EDIT_EDIT,
  Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_EDIT_DELETE,
  Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_ADD_ADD,
  Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_ANCESTOR_COUPLING,
  Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_MOVE_MOVE_CYCLE,
  Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_PARENT_MOVED_CHILD_EDITED,
  Z_PROGRAM_GRAPH_MERGE_CONFLICT_KIND_AUTO_RESOLVED,
} ZProgramGraphMergeConflictKind;

typedef struct {
  char code[16];
  ZProgramGraphMergeConflictKind kind;
  char node_id[64];
  char node_kind[32];
  char name[64];
  char field[32];
  char left_value[128];
  char right_value[128];
  char ancestor_value[128];
  char our_move[128];
  char their_move[128];
} ZProgramGraphMergeConflictEntry;

typedef struct {
  bool ok;
  bool has_conflicts;
  ZProgramGraphMergeConflictEntry *conflicts;
  size_t conflict_len;
  size_t conflict_cap;
  size_t left_changes;
  size_t right_changes;
  size_t auto_resolved_count;
} ZProgramGraphMergeReport;

typedef struct {
  bool ok;
  bool merged;
  ZProgramGraph *graph;
  ZProgramGraphMergeReport report;
} ZProgramGraphMergeResult;

bool z_program_graph_semantic_compare(const ZProgramGraph *left, const ZProgramGraph *right, ZProgramGraphCompare *out);
bool z_program_graph_diff(const ZProgramGraph *left, const ZProgramGraph *right, ZProgramGraphDiff *out);
void z_program_graph_diff_free(ZProgramGraphDiff *diff);
bool z_program_graph_merge_detect(const ZProgramGraph *base, const ZProgramGraph *ours, const ZProgramGraph *theirs, ZProgramGraphMergeReport *out);
void z_program_graph_merge_report_free(ZProgramGraphMergeReport *report);
bool z_program_graph_merge(const ZProgramGraph *base, const ZProgramGraph *ours, const ZProgramGraph *theirs, ZProgramGraphMergeResult *out);
void z_program_graph_merge_result_free(ZProgramGraphMergeResult *result);

#endif
