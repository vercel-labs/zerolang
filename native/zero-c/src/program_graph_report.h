#ifndef ZERO_C_PROGRAM_GRAPH_REPORT_H
#define ZERO_C_PROGRAM_GRAPH_REPORT_H

#include "capability_summary.h"
#include "program_graph.h"
#include "program_graph_store.h"

typedef struct {
  ZProgramGraph graph;
  ZProgramGraphStore store;
  bool graph_loaded;
  bool store_loaded;
} ZProgramGraphReportLoad;

CapabilitySummary z_program_graph_report_capabilities(const ZProgramGraph *graph);
void z_program_graph_report_mark_used_std_helpers(const ZProgramGraph *graph, bool *used, size_t used_len);
const ZProgramGraph *z_program_graph_report_load_source(const char *artifact, bool repository_store, ZProgramGraphReportLoad *loaded);
void z_program_graph_report_load_free(ZProgramGraphReportLoad *loaded);

#endif
