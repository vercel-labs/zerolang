#ifndef ZERO_PROGRAM_GRAPH_QUERY_H
#define ZERO_PROGRAM_GRAPH_QUERY_H

#include "program_graph.h"
#include "zero.h"

void z_program_graph_append_query_json(ZBuf *buf, const ZProgramGraph *graph, const char *input, const char *artifact, const char *input_kind, const char *query_function, const char *query_find, const char *query_refs, const char *query_calls, const char *query_node);
void z_program_graph_print_query_text(const ZProgramGraph *graph, const char *input, const char *artifact, const char *input_kind, const char *query_function, const char *query_find, const char *query_refs, const char *query_calls, const char *query_node);

#endif
