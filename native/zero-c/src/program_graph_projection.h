#ifndef ZERO_C_PROGRAM_GRAPH_PROJECTION_H
#define ZERO_C_PROGRAM_GRAPH_PROJECTION_H

#include "program_graph_store.h"

typedef struct {
  char **changed_paths;
  size_t changed_len;
  size_t changed_cap;
  size_t source_count;
  size_t unchanged_count;
} ZProgramGraphProjection;

typedef enum {
  Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN = 0,
  Z_PROGRAM_GRAPH_PROJECTION_SYNC_SOURCE_NEWER,
  Z_PROGRAM_GRAPH_PROJECTION_SYNC_STORE_NEWER,
  Z_PROGRAM_GRAPH_PROJECTION_SYNC_DIVERGED,
} ZProgramGraphProjectionSourceSync;

void z_program_graph_projection_init(ZProgramGraphProjection *projection);
void z_program_graph_projection_free(ZProgramGraphProjection *projection);
bool z_program_graph_projection_sources_match(const ZProgramGraphStore *store, const ZTargetInfo *target, bool *matches, ZDiag *diag);
bool z_program_graph_projection_source_files_match(const ZProgramGraphStore *store, bool *matches, ZDiag *diag);
bool z_program_graph_projection_source_sync_state(const ZProgramGraphStore *store, ZProgramGraphProjectionSourceSync *sync, ZDiag *diag);
bool z_program_graph_projection_sources_missing(const ZProgramGraphStore *store);
const char *z_program_graph_projection_state_label(const ZProgramGraphStore *store, const ZTargetInfo *target, bool *checked, bool *current, ZDiag *diag);
bool z_program_graph_projection_write_sources(const ZProgramGraphStore *store, const ZTargetInfo *target, ZProgramGraphProjection *projection, ZDiag *diag);
bool z_program_graph_projection_graph_from_store(const ZProgramGraphStore *store, const ZTargetInfo *target, ZProgramGraph *graph, ZDiag *diag);
bool z_program_graph_projection_store_matches_graph(const ZProgramGraphStore *store, const ZTargetInfo *target, ZDiag *diag);
void z_program_graph_projection_match_verdicts_flush(void);
bool z_program_graph_check_semantics_verdict_known(const ZProgramGraphStore *store, const ZTargetInfo *target, const char *manifest_text);
void z_program_graph_check_semantics_verdict_remember(const ZProgramGraphStore *store, const ZTargetInfo *target, const char *manifest_text);

#endif
