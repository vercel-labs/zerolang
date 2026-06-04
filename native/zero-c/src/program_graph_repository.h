#ifndef ZERO_C_PROGRAM_GRAPH_REPOSITORY_H
#define ZERO_C_PROGRAM_GRAPH_REPOSITORY_H

#include "program_graph.h"

#include <stdbool.h>

int z_repository_graph_status_command(const char *input, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph, const ZDiag *source_graph_diag);
int z_repository_graph_verify_sync_command(const char *input, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph);
int z_repository_graph_sync_command(const char *input, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph);
bool z_repository_graph_needs_source_graph(const char *kind, const char *input, bool from_graph, bool from_source);
bool z_repository_graph_source_graph_optional(const char *kind, bool from_graph, bool from_source);
int z_repository_graph_maybe_command(const char *kind, const char *input, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph, const ZDiag *source_graph_diag, bool *handled);

#endif
