#ifndef ZERO_PROGRAM_GRAPH_QUERY_INTERNAL_H
#define ZERO_PROGRAM_GRAPH_QUERY_INTERNAL_H

#include "program_graph.h"
#include "program_graph_resolve.h"

bool z_program_graph_query_text_eq(const char *left, const char *right);
const ZProgramGraphNode *z_program_graph_query_node_by_id(const ZProgramGraph *graph, const char *id);
const ZProgramGraphNode *z_program_graph_query_child_node(const ZProgramGraph *graph, const char *from, const char *kind, size_t order);
size_t z_program_graph_query_child_count(const ZProgramGraph *graph, const char *from, const char *kind);
bool z_program_graph_query_node_matches_find(const ZProgramGraphNode *node, const char *find);
bool z_program_graph_query_function_selected(const ZProgramGraphNode *function, const char *filter);
bool z_program_graph_query_function_contains_match(const ZProgramGraph *graph, const ZProgramGraphNode *function, const char *find);
const ZProgramGraphNode *z_program_graph_query_enclosing_function(const ZProgramGraph *graph, const char *node_id);
bool z_program_graph_query_reference_matches(const ZProgramGraphResolutionReference *ref, const char *filter);
void z_program_graph_query_append_function_calls_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *function, const char *filter);
void z_program_graph_query_append_reference_list_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *filter, bool calls_only, const char *function_filter);
void z_program_graph_query_print_function_calls_text(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *function, const char *filter, const char *indent);
void z_program_graph_query_print_reference_section_text(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *title, const char *filter, bool calls_only, const char *function_filter);
#endif
