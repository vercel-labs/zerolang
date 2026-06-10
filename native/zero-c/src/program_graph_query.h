#ifndef ZERO_PROGRAM_GRAPH_QUERY_H
#define ZERO_PROGRAM_GRAPH_QUERY_H

#include "program_graph.h"
#include "zero.h"

typedef struct {
  const char *function;
  const char *find;
  const char *refs;
  const char *calls;
  const char *node;
  size_t node_depth;
  bool full_module;
  const char *bare_argument;
} ZProgramGraphQueryRequest;

void z_program_graph_query_request_init(ZProgramGraphQueryRequest *request);
void z_program_graph_append_query_json(ZBuf *buf, const ZProgramGraph *graph, const char *input, const char *artifact, const char *input_kind, const ZProgramGraphQueryRequest *request);
void z_program_graph_print_query_text(const ZProgramGraph *graph, const char *input, const char *artifact, const char *input_kind, const ZProgramGraphQueryRequest *request);

#endif
