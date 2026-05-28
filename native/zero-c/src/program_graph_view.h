#ifndef ZERO_C_PROGRAM_GRAPH_VIEW_H
#define ZERO_C_PROGRAM_GRAPH_VIEW_H

#include "program_graph.h"

bool z_program_graph_append_view(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, ZDiag *diag);

#endif
