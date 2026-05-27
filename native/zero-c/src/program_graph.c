#include "program_graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef const char *(*GraphIdField)(const ZProgramGraphNode *node);

static const char *graph_node_id_field(const ZProgramGraphNode *node) { return node->id; }
static const char *graph_symbol_id_field(const ZProgramGraphNode *node) { return node->symbol_id; }
static const char *graph_type_id_field(const ZProgramGraphNode *node) { return node->type_id; }
static const char *graph_effect_id_field(const ZProgramGraphNode *node) { return node->effect_id; }

static bool graph_has_id(const ZProgramGraph *graph, const char *id, GraphIdField field) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const char *candidate = field(&graph->nodes[i]);
    if (candidate && candidate[0] && id && id[0] && strcmp(candidate, id) == 0) return true;
  }
  return false;
}

static bool graph_edge_target_exists(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  switch (edge->target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE: return graph_has_id(graph, edge->to, graph_node_id_field);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL: return graph_has_id(graph, edge->to, graph_symbol_id_field);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE: return graph_has_id(graph, edge->to, graph_type_id_field);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT: return graph_has_id(graph, edge->to, graph_effect_id_field);
  }
  return false;
}

static bool graph_edge_target_valid(ZProgramGraphEdgeTarget target) {
  switch (target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
      return true;
  }
  return false;
}

static bool graph_validation_fail(ZProgramGraphValidation *validation, const char *code, const char *message, const char *node_id, const char *edge_from, const char *edge_to, const char *edge_target) {
  if (validation) {
    validation->ok = false;
    validation->state = Z_PROGRAM_GRAPH_VALIDATION_DECODED;
    snprintf(validation->code, sizeof(validation->code), "%s", code ? code : "GRF000");
    snprintf(validation->message, sizeof(validation->message), "%s", message ? message : "program graph validation failed");
    snprintf(validation->node_id, sizeof(validation->node_id), "%s", node_id ? node_id : "");
    snprintf(validation->edge_from, sizeof(validation->edge_from), "%s", edge_from ? edge_from : "");
    snprintf(validation->edge_to, sizeof(validation->edge_to), "%s", edge_to ? edge_to : "");
    snprintf(validation->edge_target, sizeof(validation->edge_target), "%s", edge_target ? edge_target : "");
  }
  return false;
}

bool z_program_graph_validate(const ZProgramGraph *graph, ZProgramGraphValidation *validation) {
  if (validation) *validation = (ZProgramGraphValidation){.ok = true, .state = Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID};
  if (!graph) return graph_validation_fail(validation, "GRF001", "program graph is missing", NULL, NULL, NULL, NULL);
  if (!graph->module_identity || !graph->module_identity[0]) return graph_validation_fail(validation, "GRF009", "program graph is missing module identity", NULL, NULL, NULL, NULL);
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!graph->nodes[i].id || !graph->nodes[i].id[0]) return graph_validation_fail(validation, "GRF002", "node is missing required identity", graph->nodes[i].id, NULL, NULL, NULL);
    if (!graph->nodes[i].node_hash) return graph_validation_fail(validation, "GRF007", "node is missing content hash", graph->nodes[i].id, NULL, NULL, NULL);
    for (size_t j = i + 1; j < graph->node_len; j++) {
      if (graph->nodes[j].id && strcmp(graph->nodes[i].id, graph->nodes[j].id) == 0) return graph_validation_fail(validation, "GRF003", "duplicate node id", graph->nodes[i].id, NULL, NULL, NULL);
    }
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    const char *edge_target = z_program_graph_edge_target_name(edge->target);
    if (!graph_edge_target_valid(edge->target)) return graph_validation_fail(validation, "GRF008", "edge target domain is invalid", NULL, edge->from, edge->to, edge_target);
    if (!graph_has_id(graph, edge->from, graph_node_id_field)) return graph_validation_fail(validation, "GRF004", "edge source is missing", NULL, edge->from, edge->to, edge_target);
    if (!graph_edge_target_exists(graph, edge)) return graph_validation_fail(validation, "GRF005", "edge target is missing from declared domain", NULL, edge->from, edge->to, edge_target);
    for (size_t j = i + 1; j < graph->edge_len; j++) {
      const ZProgramGraphEdge *other = &graph->edges[j];
      if (edge->from && other->from && edge->kind && other->kind &&
          edge->target == other->target && strcmp(edge->from, other->from) == 0 && strcmp(edge->kind, other->kind) == 0 && edge->order == other->order) {
        return graph_validation_fail(validation, "GRF006", "duplicate ordered edge", NULL, edge->from, edge->to, edge_target);
      }
    }
  }
  return true;
}
