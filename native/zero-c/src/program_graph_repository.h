#ifndef ZERO_C_PROGRAM_GRAPH_REPOSITORY_H
#define ZERO_C_PROGRAM_GRAPH_REPOSITORY_H

#include "program_graph.h"
#include "program_graph_store.h"

#include <stdbool.h>

typedef bool (*ZRepositoryGraphLoadSourceGraphFn)(void *ctx, ZProgramGraph *graph, ZDiag *diag);

int z_repository_graph_status_command(const char *input, const ZTargetInfo *target, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph, const ZDiag *source_graph_diag);
int z_repository_graph_verify_sync_command(const char *input, const ZTargetInfo *target, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph);
int z_repository_graph_sync_command(const char *input, const ZTargetInfo *target, bool json, bool from_graph, bool from_source, const char *store_format, const ZProgramGraph *source_graph, const ZDiag *source_graph_diag, ZRepositoryGraphLoadSourceGraphFn load_source_graph, void *load_source_graph_ctx);
int z_repository_graph_merge_command(const char *input, const ZTargetInfo *target, const char *base_path, const char *left_path, const char *right_path, const char *store_format, bool json);
bool z_repository_graph_needs_source_graph(const char *kind, const char *input, const ZTargetInfo *target, bool from_graph, bool from_source);
bool z_repository_graph_source_graph_optional(const char *kind, bool from_graph, bool from_source);
int z_repository_graph_maybe_command(const char *kind, const char *input, const ZTargetInfo *target, bool json, bool from_graph, bool from_source, const char *merge_base, const char *merge_left, const char *merge_right, const char *store_format, const ZProgramGraph *source_graph, const ZDiag *source_graph_diag, ZRepositoryGraphLoadSourceGraphFn load_source_graph, void *load_source_graph_ctx, bool *handled);

#endif
