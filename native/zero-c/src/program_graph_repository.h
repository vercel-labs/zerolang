#ifndef ZERO_C_PROGRAM_GRAPH_REPOSITORY_H
#define ZERO_C_PROGRAM_GRAPH_REPOSITORY_H

#include <stdbool.h>

int z_repository_graph_status_command(const char *input, bool json, bool from_graph, bool from_source);
int z_repository_graph_verify_sync_command(const char *input, bool json, bool from_graph, bool from_source);
int z_repository_graph_sync_command(const char *input, bool json, bool from_graph, bool from_source);
int z_repository_graph_maybe_command(const char *kind, const char *input, bool json, bool from_graph, bool from_source, bool *handled);

#endif
