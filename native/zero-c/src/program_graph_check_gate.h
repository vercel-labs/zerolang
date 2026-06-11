#ifndef ZERO_C_PROGRAM_GRAPH_CHECK_GATE_H
#define ZERO_C_PROGRAM_GRAPH_CHECK_GATE_H

#include "program_graph.h"

bool z_check_gate_diag_is_buildability_blocker(const ZDiag *diag);
bool z_check_gate_diag_is_known_construct(const ZProgramGraph *graph, const ZDiag *diag);
bool z_check_gate_scan_finds_known_construct(const ZProgramGraph *graph);
#endif
