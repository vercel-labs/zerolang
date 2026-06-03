#include "program_graph.h"

#include <stdlib.h>

void z_program_graph_init(ZProgramGraph *graph) {
  *graph = (ZProgramGraph){
    .schema_version = 1,
    .validation_state = Z_PROGRAM_GRAPH_VALIDATION_DECODED,
    .module_identity = z_strdup("module:main"),
  };
}

void z_program_graph_free(ZProgramGraph *graph) {
  if (!graph) return;
  for (size_t i = 0; i < graph->node_len; i++) {
    free(graph->nodes[i].id);
    free(graph->nodes[i].name);
    free(graph->nodes[i].type);
    free(graph->nodes[i].value);
    free(graph->nodes[i].path);
    free(graph->nodes[i].symbol_id);
    free(graph->nodes[i].type_id);
    free(graph->nodes[i].effect_id);
    free(graph->nodes[i].node_hash);
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    free(graph->edges[i].from);
    free(graph->edges[i].to);
    free(graph->edges[i].kind);
  }
  free(graph->module_identity);
  free(graph->graph_hash);
  free(graph->nodes);
  free(graph->edges);
  *graph = (ZProgramGraph){0};
}
