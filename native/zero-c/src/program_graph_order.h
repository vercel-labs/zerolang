#ifndef ZERO_PROGRAM_GRAPH_ORDER_H
#define ZERO_PROGRAM_GRAPH_ORDER_H

#include "program_graph.h"

bool z_program_graph_order_must_be_contiguous(const ZProgramGraphNode *owner, const char *kind);
void z_program_graph_compact_ordered_edges(ZProgramGraph *graph);

#endif
