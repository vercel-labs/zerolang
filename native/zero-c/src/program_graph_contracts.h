#ifndef ZERO_C_PROGRAM_GRAPH_CONTRACTS_H
#define ZERO_C_PROGRAM_GRAPH_CONTRACTS_H

#include "program_graph.h"
#include "program_graph_resolve.h"

bool z_program_graph_name_contracts_ok(const ZProgramGraph *graph, const char *path, ZDiag *diag);
bool z_program_graph_semantic_contracts_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *path, ZDiag *diag);
bool z_program_graph_effect_contracts_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *path, ZDiag *diag);
bool z_program_graph_memory_contracts_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *path, ZDiag *diag);
bool z_program_graph_fixed_array_length_contracts_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *path, ZDiag *diag);
bool z_program_graph_borrow_contracts_ok(const ZProgramGraph *graph, const char *path, ZDiag *diag);

#endif
