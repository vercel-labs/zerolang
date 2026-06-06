#include "program_graph.h"
#include "program_graph_order.h"

#include <stdio.h>
#include <string.h>

typedef const char *(*GraphIdField)(const ZProgramGraphNode *node);

static const char *graph_node_id_field(const ZProgramGraphNode *node) { return node->id; }
static const char *graph_symbol_id_field(const ZProgramGraphNode *node) { return node->symbol_id; }
static const char *graph_type_id_field(const ZProgramGraphNode *node) { return node->type_id; }
static const char *graph_effect_id_field(const ZProgramGraphNode *node) { return node->effect_id; }

static bool graph_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static const ZProgramGraphNode *graph_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (graph_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

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

static bool graph_node_kind_valid(ZProgramGraphNodeKind kind) {
  return kind >= Z_PROGRAM_GRAPH_NODE_MODULE && kind <= Z_PROGRAM_GRAPH_NODE_STATEMENT;
}

static bool graph_node_is_stmt(const ZProgramGraphNode *node) {
  if (!node) return false;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_LET:
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT:
    case Z_PROGRAM_GRAPH_NODE_DEFER:
    case Z_PROGRAM_GRAPH_NODE_CHECK:
    case Z_PROGRAM_GRAPH_NODE_RETURN:
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT:
    case Z_PROGRAM_GRAPH_NODE_IF:
    case Z_PROGRAM_GRAPH_NODE_WHILE:
    case Z_PROGRAM_GRAPH_NODE_FOR:
    case Z_PROGRAM_GRAPH_NODE_BREAK:
    case Z_PROGRAM_GRAPH_NODE_CONTINUE:
    case Z_PROGRAM_GRAPH_NODE_MATCH:
    case Z_PROGRAM_GRAPH_NODE_RAISE:
    case Z_PROGRAM_GRAPH_NODE_STATEMENT:
      return true;
    default:
      return false;
  }
}

static bool graph_node_is_expr(const ZProgramGraphNode *node) {
  if (!node) return false;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_SLICE:
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
    case Z_PROGRAM_GRAPH_NODE_CAST:
    case Z_PROGRAM_GRAPH_NODE_BORROW:
    case Z_PROGRAM_GRAPH_NODE_CHECK:
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
    case Z_PROGRAM_GRAPH_NODE_META:
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION:
      return true;
    default:
      return false;
  }
}

static bool graph_edge_child_allowed(const ZProgramGraphNode *owner, const char *kind, const ZProgramGraphNode *child) {
  if (!owner || !kind || !child) return false;
  switch (owner->kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE:
      return (graph_text_eq(kind, "import") && child->kind == Z_PROGRAM_GRAPH_NODE_IMPORT) ||
             (graph_text_eq(kind, "cImport") && child->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT) ||
             (graph_text_eq(kind, "const") && child->kind == Z_PROGRAM_GRAPH_NODE_CONST) ||
             (graph_text_eq(kind, "alias") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS) ||
             (graph_text_eq(kind, "shape") && child->kind == Z_PROGRAM_GRAPH_NODE_SHAPE) ||
             (graph_text_eq(kind, "interface") && child->kind == Z_PROGRAM_GRAPH_NODE_INTERFACE) ||
             (graph_text_eq(kind, "enum") && child->kind == Z_PROGRAM_GRAPH_NODE_ENUM) ||
             (graph_text_eq(kind, "choice") && child->kind == Z_PROGRAM_GRAPH_NODE_CHOICE) ||
             (graph_text_eq(kind, "function") && child->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION);
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
      return (graph_text_eq(kind, "typeParam") && child->kind == Z_PROGRAM_GRAPH_NODE_PARAM) ||
             (graph_text_eq(kind, "field") && child->kind == Z_PROGRAM_GRAPH_NODE_FIELD) ||
             (graph_text_eq(kind, "method") && child->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION);
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
      return (graph_text_eq(kind, "typeParam") && child->kind == Z_PROGRAM_GRAPH_NODE_PARAM) ||
             (graph_text_eq(kind, "method") && child->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION);
    case Z_PROGRAM_GRAPH_NODE_ENUM:
      return graph_text_eq(kind, "case") && child->kind == Z_PROGRAM_GRAPH_NODE_ENUM_CASE;
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
      return graph_text_eq(kind, "case") && child->kind == Z_PROGRAM_GRAPH_NODE_CHOICE_CASE;
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
      return (graph_text_eq(kind, "typeParam") && child->kind == Z_PROGRAM_GRAPH_NODE_PARAM) ||
             (graph_text_eq(kind, "param") && child->kind == Z_PROGRAM_GRAPH_NODE_PARAM) ||
             (graph_text_eq(kind, "returnType") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF) ||
             (graph_text_eq(kind, "effect") && child->kind == Z_PROGRAM_GRAPH_NODE_EFFECT_REF) ||
             (graph_text_eq(kind, "error") && child->kind == Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT) ||
             (graph_text_eq(kind, "body") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_PARAM:
    case Z_PROGRAM_GRAPH_NODE_FIELD:
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
      return (graph_text_eq(kind, "type") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF) ||
             (graph_text_eq(kind, "default") && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_CONST:
      return graph_text_eq(kind, "value") && graph_node_is_expr(child);
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
      return graph_text_eq(kind, "target") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF;
    case Z_PROGRAM_GRAPH_NODE_BLOCK:
      return graph_text_eq(kind, "statement") && graph_node_is_stmt(child);
    case Z_PROGRAM_GRAPH_NODE_LET:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "declaredType") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF);
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT:
      return (graph_text_eq(kind, "target") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "expr") && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_DEFER:
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT:
    case Z_PROGRAM_GRAPH_NODE_RETURN:
      return graph_text_eq(kind, "expr") && graph_node_is_expr(child);
    case Z_PROGRAM_GRAPH_NODE_CHECK:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "left") && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_IF:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "then") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK) ||
             (graph_text_eq(kind, "else") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_WHILE:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "then") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_FOR:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "rangeEnd") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "then") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_MATCH:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "arm") && child->kind == Z_PROGRAM_GRAPH_NODE_MATCH_ARM);
    case Z_PROGRAM_GRAPH_NODE_MATCH_ARM:
      return (graph_text_eq(kind, "rangeEnd") && child->kind == Z_PROGRAM_GRAPH_NODE_LITERAL) ||
             (graph_text_eq(kind, "guard") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "body") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
      return graph_text_eq(kind, "typeArg") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF;
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
      return (graph_text_eq(kind, "left") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "typeArg") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF);
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
      return ((graph_text_eq(kind, "left") || graph_text_eq(kind, "right")) && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_SLICE:
      return ((graph_text_eq(kind, "left") || graph_text_eq(kind, "arg")) && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
      return ((graph_text_eq(kind, "left") || graph_text_eq(kind, "right") || graph_text_eq(kind, "arg")) && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "typeArg") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF);
    case Z_PROGRAM_GRAPH_NODE_CAST:
    case Z_PROGRAM_GRAPH_NODE_BORROW:
    case Z_PROGRAM_GRAPH_NODE_META:
      return graph_text_eq(kind, "left") && graph_node_is_expr(child);
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
      return ((graph_text_eq(kind, "left") || graph_text_eq(kind, "right")) && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
      return graph_text_eq(kind, "field") && child->kind == Z_PROGRAM_GRAPH_NODE_FIELD_INIT;
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL:
      return graph_text_eq(kind, "arg") && graph_node_is_expr(child);
    case Z_PROGRAM_GRAPH_NODE_FIELD_INIT:
      return graph_text_eq(kind, "value") && graph_node_is_expr(child);
    default:
      return false;
  }
}

static size_t graph_count_node_edges(const ZProgramGraph *graph, const char *from, const char *kind) {
  size_t count = 0;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && graph_text_eq(edge->from, from) && graph_text_eq(edge->kind, kind)) count++;
  }
  return count;
}

static bool graph_has_node_edge_order(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->order == order && graph_text_eq(edge->from, from) && graph_text_eq(edge->kind, kind)) return true;
  }
  return false;
}

static bool graph_node_has_incoming_node_edge(const ZProgramGraph *graph, const char *to, const char *kind) {
  for (size_t i = 0; graph && to && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && graph_text_eq(edge->to, to) && graph_text_eq(edge->kind, kind)) return true;
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

static bool graph_required_text_present(const char *text) { return text && text[0]; }

static bool graph_required_value_present(const char *text) { return text != NULL; }

static bool graph_validate_node_payload(const ZProgramGraphNode *node, ZProgramGraphValidation *validation) {
  if (!node) return true;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE:
    case Z_PROGRAM_GRAPH_NODE_IMPORT:
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT:
    case Z_PROGRAM_GRAPH_NODE_CONST:
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
    case Z_PROGRAM_GRAPH_NODE_ENUM:
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
    case Z_PROGRAM_GRAPH_NODE_PARAM:
    case Z_PROGRAM_GRAPH_NODE_FIELD:
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
    case Z_PROGRAM_GRAPH_NODE_LET:
    case Z_PROGRAM_GRAPH_NODE_FOR:
    case Z_PROGRAM_GRAPH_NODE_RAISE:
    case Z_PROGRAM_GRAPH_NODE_MATCH_ARM:
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_FIELD_INIT:
    case Z_PROGRAM_GRAPH_NODE_EFFECT_REF:
    case Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT:
      if (!graph_required_text_present(node->name)) return graph_validation_fail(validation, "GRF014", "node is missing required name", node->id, NULL, NULL, NULL);
      break;
    default:
      break;
  }
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT:
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
      if (!graph_required_value_present(node->value)) return graph_validation_fail(validation, "GRF014", "node is missing required value", node->id, NULL, NULL, NULL);
      break;
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
    case Z_PROGRAM_GRAPH_NODE_FIELD:
    case Z_PROGRAM_GRAPH_NODE_TYPE_REF:
      if (!graph_required_text_present(node->type)) return graph_validation_fail(validation, "GRF014", "node is missing required type", node->id, NULL, NULL, NULL);
      break;
    default:
      break;
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && graph_required_text_present(node->name)) {
    return graph_validation_fail(validation, "GRF015", "literal node must not carry a name payload", node->id, NULL, NULL, NULL);
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_CAST && !graph_required_text_present(node->name) && !graph_required_text_present(node->type)) {
    return graph_validation_fail(validation, "GRF014", "cast node is missing target type", node->id, NULL, NULL, NULL);
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF && (graph_required_text_present(node->name) || graph_required_value_present(node->value))) {
    return graph_validation_fail(validation, "GRF015", "type reference node has illegal payload", node->id, NULL, NULL, NULL);
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_EFFECT_REF && (graph_required_text_present(node->type) || graph_required_value_present(node->value))) {
    return graph_validation_fail(validation, "GRF015", "effect reference node has illegal payload", node->id, NULL, NULL, NULL);
  }
  return true;
}

static bool graph_validate_required_edge_count(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *kind, size_t min, size_t max, ZProgramGraphValidation *validation) {
  size_t count = graph_count_node_edges(graph, node ? node->id : NULL, kind);
  if (count >= min && count <= max) return true;
  char message[160];
  if (min == max && min == 1) snprintf(message, sizeof(message), "node is missing required %s edge", kind ? kind : "child");
  else snprintf(message, sizeof(message), "node has invalid %s edge count", kind ? kind : "child");
  return graph_validation_fail(validation, "GRF016", message, node ? node->id : NULL, node ? node->id : NULL, NULL, "node");
}

static bool graph_validate_optional_edge_count(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *kind, size_t max, ZProgramGraphValidation *validation) {
  return graph_validate_required_edge_count(graph, node, kind, 0, max, validation);
}

static bool graph_validate_edge_order_range(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *kind, size_t max_exclusive, ZProgramGraphValidation *validation) {
  for (size_t i = 0; graph && node && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        graph_text_eq(edge->from, node->id) &&
        graph_text_eq(edge->kind, kind) &&
        edge->order >= max_exclusive) {
      char message[160];
      snprintf(message, sizeof(message), "node has invalid %s edge order", kind);
      return graph_validation_fail(validation, "GRF016", message, node->id, edge->from, edge->to, "node");
    }
  }
  return true;
}

static bool graph_validate_edge_order(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *kind, size_t order, ZProgramGraphValidation *validation) {
  if (graph_count_node_edges(graph, node ? node->id : NULL, kind) == 0) return true;
  if (graph_has_node_edge_order(graph, node ? node->id : NULL, kind, order)) return true;
  char message[160];
  snprintf(message, sizeof(message), "node has invalid %s edge order", kind ? kind : "child");
  return graph_validation_fail(validation, "GRF016", message, node ? node->id : NULL, node ? node->id : NULL, NULL, "node");
}

static bool graph_validate_required_edge_at_order(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *kind, size_t order, ZProgramGraphValidation *validation) {
  return graph_validate_required_edge_count(graph, node, kind, 1, 1, validation) &&
         graph_validate_edge_order(graph, node, kind, order, validation);
}

static bool graph_validate_optional_edge_at_order(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *kind, size_t order, ZProgramGraphValidation *validation) {
  return graph_validate_optional_edge_count(graph, node, kind, 1, validation) &&
         graph_validate_edge_order(graph, node, kind, order, validation);
}

static bool graph_validate_required_edges(const ZProgramGraph *graph, const ZProgramGraphNode *node, ZProgramGraphValidation *validation) {
  if (!node) return true;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_PARAM:
      if (!graph_required_text_present(node->type) &&
          (graph_node_has_incoming_node_edge(graph, node->id, "param") || !graph_node_has_incoming_node_edge(graph, node->id, "typeParam")))
        return graph_validation_fail(validation, "GRF014", "node is missing required type", node->id, NULL, NULL, NULL);
      return graph_validate_optional_edge_at_order(graph, node, "type", 0, validation) &&
             graph_validate_optional_edge_at_order(graph, node, "default", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_FIELD:
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
      return graph_validate_optional_edge_at_order(graph, node, "type", 0, validation) &&
             graph_validate_optional_edge_at_order(graph, node, "default", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_CONST:
      return graph_validate_required_edge_at_order(graph, node, "value", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
      return graph_validate_required_edge_at_order(graph, node, "target", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
      return graph_validate_optional_edge_at_order(graph, node, "returnType", 0, validation) &&
             graph_validate_optional_edge_at_order(graph, node, "effect", 0, validation) &&
             graph_validate_required_edge_at_order(graph, node, "body", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_LET:
      return graph_validate_required_edge_at_order(graph, node, "expr", 0, validation) &&
             graph_validate_optional_edge_at_order(graph, node, "declaredType", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT:
      return graph_validate_required_edge_at_order(graph, node, "target", 0, validation) &&
             graph_validate_required_edge_at_order(graph, node, "expr", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_DEFER:
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT:
      return graph_validate_required_edge_at_order(graph, node, "expr", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_CHECK: {
      bool is_statement = graph_node_has_incoming_node_edge(graph, node->id, "statement");
      if (is_statement) {
        if (graph_count_node_edges(graph, node->id, "left") != 0) {
          return graph_validation_fail(validation, "GRF016", "statement check node must use expr edge", node->id, node->id, NULL, "node");
        }
        return graph_validate_required_edge_at_order(graph, node, "expr", 0, validation);
      }
      if (graph_count_node_edges(graph, node->id, "expr") != 0) {
        return graph_validation_fail(validation, "GRF016", "expression check node must use left edge", node->id, node->id, NULL, "node");
      }
      if (graph_validate_required_edge_at_order(graph, node, "left", 0, validation)) return true;
      return graph_validation_fail(validation, "GRF016", "check node requires exactly one checked expression edge", node->id, node->id, NULL, "node");
    }
    case Z_PROGRAM_GRAPH_NODE_RETURN:
      return graph_validate_optional_edge_at_order(graph, node, "expr", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_IF:
      return graph_validate_required_edge_at_order(graph, node, "expr", 0, validation) &&
             graph_validate_optional_edge_at_order(graph, node, "then", 0, validation) &&
             graph_validate_optional_edge_at_order(graph, node, "else", 1, validation);
    case Z_PROGRAM_GRAPH_NODE_WHILE:
      return graph_validate_required_edge_at_order(graph, node, "expr", 0, validation) &&
             graph_validate_optional_edge_at_order(graph, node, "then", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_FOR:
      return graph_validate_required_edge_at_order(graph, node, "expr", 0, validation) &&
             graph_validate_required_edge_at_order(graph, node, "rangeEnd", 1, validation) &&
             graph_validate_optional_edge_at_order(graph, node, "then", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_MATCH:
      return graph_validate_required_edge_at_order(graph, node, "expr", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_MATCH_ARM:
      return graph_validate_optional_edge_at_order(graph, node, "rangeEnd", 0, validation) &&
             graph_validate_optional_edge_at_order(graph, node, "guard", 0, validation) &&
             graph_validate_required_edge_at_order(graph, node, "body", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_CAST:
    case Z_PROGRAM_GRAPH_NODE_BORROW:
    case Z_PROGRAM_GRAPH_NODE_META:
      return graph_validate_required_edge_at_order(graph, node, "left", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
      return graph_validate_required_edge_at_order(graph, node, "left", 0, validation) &&
             graph_validate_required_edge_at_order(graph, node, "right", 1, validation);
    case Z_PROGRAM_GRAPH_NODE_SLICE:
      return graph_validate_required_edge_at_order(graph, node, "left", 0, validation) &&
             graph_validate_optional_edge_count(graph, node, "arg", 2, validation) &&
             graph_validate_edge_order_range(graph, node, "arg", 2, validation);
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
      if (!graph_validate_optional_edge_at_order(graph, node, "left", 0, validation) ||
          !graph_validate_optional_edge_at_order(graph, node, "right", 1, validation)) {
        return false;
      }
      if (graph_count_node_edges(graph, node->id, "right") > 0 && graph_count_node_edges(graph, node->id, "left") == 0) {
        return graph_validation_fail(validation, "GRF016", "binary call node is missing left operand", node->id, node->id, NULL, "node");
      }
      if (graph_required_text_present(node->name) || graph_count_node_edges(graph, node->id, "left") == 1) return true;
      return graph_validation_fail(validation, "GRF016", "call node is missing callee", node->id, node->id, NULL, "node");
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
      return graph_validate_required_edge_at_order(graph, node, "left", 0, validation) &&
             graph_validate_required_edge_at_order(graph, node, "right", 1, validation);
    case Z_PROGRAM_GRAPH_NODE_FIELD_INIT:
      return graph_validate_required_edge_at_order(graph, node, "value", 0, validation);
    default:
      return true;
  }
}

static bool graph_validate_order_groups(const ZProgramGraph *graph, ZProgramGraphValidation *validation) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
    const ZProgramGraphNode *owner = graph_find_node(graph, edge->from);
    if (!z_program_graph_order_must_be_contiguous(owner, edge->kind)) continue;
    bool first = true;
    for (size_t j = 0; j < i; j++) {
      const ZProgramGraphEdge *prior = &graph->edges[j];
      if (prior->target == edge->target && graph_text_eq(prior->from, edge->from) && graph_text_eq(prior->kind, edge->kind)) {
        first = false;
        break;
      }
    }
    if (!first) continue;
    size_t count = graph_count_node_edges(graph, edge->from, edge->kind);
    for (size_t order = 0; order < count; order++) {
      if (!graph_has_node_edge_order(graph, edge->from, edge->kind, order)) {
        return graph_validation_fail(validation, "GRF013", "ordered edge group is sparse", NULL, edge->from, edge->to, "node");
      }
    }
  }
  return true;
}

bool z_program_graph_validate(const ZProgramGraph *graph, ZProgramGraphValidation *validation) {
  if (validation) *validation = (ZProgramGraphValidation){.ok = true, .state = Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID};
  if (!graph) return graph_validation_fail(validation, "GRF001", "program graph is missing", NULL, NULL, NULL, NULL);
  if (!graph->module_identity || !graph->module_identity[0]) return graph_validation_fail(validation, "GRF009", "program graph is missing module identity", NULL, NULL, NULL, NULL);
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!graph_node_kind_valid(graph->nodes[i].kind)) return graph_validation_fail(validation, "GRF011", "node kind is invalid", graph->nodes[i].id, NULL, NULL, NULL);
    if (!graph->nodes[i].id || !graph->nodes[i].id[0]) return graph_validation_fail(validation, "GRF002", "node is missing required identity", graph->nodes[i].id, NULL, NULL, NULL);
    if (!z_program_graph_node_id_valid(graph->nodes[i].id)) return graph_validation_fail(validation, "GRF010", "node identity is malformed", graph->nodes[i].id, NULL, NULL, NULL);
    if (!graph->nodes[i].node_hash) return graph_validation_fail(validation, "GRF007", "node is missing content hash", graph->nodes[i].id, NULL, NULL, NULL);
    if (!graph_validate_node_payload(&graph->nodes[i], validation)) return false;
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
    if (!edge->kind || !edge->kind[0]) return graph_validation_fail(validation, "GRF011", "edge kind is missing", NULL, edge->from, edge->to, edge_target);
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) {
      const ZProgramGraphNode *owner = graph_find_node(graph, edge->from);
      const ZProgramGraphNode *child = graph_find_node(graph, edge->to);
      if (!graph_edge_child_allowed(owner, edge->kind, child)) {
        return graph_validation_fail(validation, "GRF012", "edge child kind is invalid for source node", NULL, edge->from, edge->to, edge_target);
      }
    }
    for (size_t j = i + 1; j < graph->edge_len; j++) {
      const ZProgramGraphEdge *other = &graph->edges[j];
      if (edge->from && other->from && edge->kind && other->kind &&
          edge->target == other->target && strcmp(edge->from, other->from) == 0 && strcmp(edge->kind, other->kind) == 0 && edge->order == other->order) {
        return graph_validation_fail(validation, "GRF006", "duplicate ordered edge", NULL, edge->from, edge->to, edge_target);
      }
    }
  }
  if (!graph_validate_order_groups(graph, validation)) return false;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!graph_validate_required_edges(graph, &graph->nodes[i], validation)) return false;
  }
  return true;
}
