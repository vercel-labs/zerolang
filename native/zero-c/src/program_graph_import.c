#include "program_graph_import.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *graph_source_path(const SourceInput *input, int parser_line) {
  if (!input || parser_line <= 0) return input && input->source_file ? input->source_file : "";
  size_t index = (size_t)parser_line - 1;
  if (index < input->source_line_count && input->source_line_paths[index]) return input->source_line_paths[index];
  return input->source_file ? input->source_file : "";
}

static int graph_source_line(const SourceInput *input, int parser_line) {
  if (parser_line <= 0) return 0;
  if (input) {
    size_t index = (size_t)parser_line - 1;
    if (index < input->source_line_count && input->source_line_numbers[index] > 0) return input->source_line_numbers[index];
  }
  return parser_line;
}

static const char *graph_module_name_for_path(const SourceInput *input, const char *path) {
  if (!input || input->module_count == 0) return "main";
  if (!path) return input->module_names[0] ? input->module_names[0] : "main";
  for (size_t i = 0; i < input->module_count; i++) {
    if (input->module_paths[i] && strcmp(input->module_paths[i], path) == 0) return input->module_names[i];
  }
  return input->module_names[0] ? input->module_names[0] : "main";
}

static char *graph_next_node_id(ZProgramGraph *graph) {
  char id[32];
  snprintf(id, sizeof(id), "node:%06zu", ++graph->next_id);
  return z_strdup(id);
}

static char *graph_module_identity(const SourceInput *input) {
  ZBuf identity;
  zbuf_init(&identity);
  if (input && input->package_name && input->package_name[0]) {
    zbuf_append(&identity, "package:");
    zbuf_append(&identity, input->package_name);
    if (input->package_version && input->package_version[0]) {
      zbuf_append_char(&identity, '@');
      zbuf_append(&identity, input->package_version);
    }
  } else {
    zbuf_append(&identity, "module:");
    zbuf_append(&identity, input && input->module_count == 1 && input->module_names[0] ? input->module_names[0] : "main");
  }
  return identity.data ? identity.data : z_strdup("module:main");
}

static ZProgramGraphNode *graph_add_node(ZProgramGraph *graph, ZProgramGraphNodeKind kind, const char *name, const char *type, const char *value, const char *path, int line, int column) {
  if (graph->node_len == graph->node_cap) {
    size_t next = z_grow_capacity(graph->node_cap, graph->node_len + 1, 32);
    graph->nodes = z_checked_reallocarray(graph->nodes, next, sizeof(ZProgramGraphNode));
    for (size_t i = graph->node_cap; i < next; i++) graph->nodes[i] = (ZProgramGraphNode){0};
    graph->node_cap = next;
  }
  ZProgramGraphNode *node = &graph->nodes[graph->node_len++];
  node->id = graph_next_node_id(graph);
  node->kind = kind;
  node->name = name ? z_strdup(name) : NULL;
  node->type = type ? z_strdup(type) : NULL;
  node->value = value ? z_strdup(value) : NULL;
  node->path = path ? z_strdup(path) : NULL;
  node->line = line;
  node->column = column;
  return node;
}

static void graph_add_edge_target(ZProgramGraph *graph, const char *from, const char *to, const char *kind, ZProgramGraphEdgeTarget target, size_t order) {
  if (!from || !to || !kind) return;
  if (graph->edge_len == graph->edge_cap) {
    size_t next = z_grow_capacity(graph->edge_cap, graph->edge_len + 1, 64);
    graph->edges = z_checked_reallocarray(graph->edges, next, sizeof(ZProgramGraphEdge));
    for (size_t i = graph->edge_cap; i < next; i++) graph->edges[i] = (ZProgramGraphEdge){0};
    graph->edge_cap = next;
  }
  ZProgramGraphEdge *edge = &graph->edges[graph->edge_len++];
  edge->from = z_strdup(from);
  edge->to = z_strdup(to);
  edge->kind = z_strdup(kind);
  edge->target = target;
  edge->order = order;
}

static void graph_add_edge(ZProgramGraph *graph, const char *from, const char *to, const char *kind, size_t order) {
  graph_add_edge_target(graph, from, to, kind, Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, order);
}

static size_t graph_next_edge_order(const ZProgramGraph *graph, const char *from, const char *kind) {
  size_t order = 0;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->from && edge->kind &&
        strcmp(edge->from, from) == 0 && strcmp(edge->kind, kind) == 0) {
      order++;
    }
  }
  return order;
}

static const char *graph_find_module_id(const ZProgramGraph *graph, const char *module_name) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && node->name && module_name && strcmp(node->name, module_name) == 0) return node->id;
  }
  return graph && graph->node_len > 0 ? graph->nodes[0].id : NULL;
}

static const char *graph_module_id_for_line(const ZProgramGraph *graph, const SourceInput *input, int parser_line) {
  const char *path = graph_source_path(input, parser_line);
  const char *module = graph_module_name_for_path(input, path);
  return graph_find_module_id(graph, module);
}

static const char *graph_add_type_ref(ZProgramGraph *graph, const char *owner_id, const char *edge_kind, const char *type, const char *path, int line, int column, size_t order) {
  if (!type || !type[0]) return NULL;
  ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_TYPE_REF, NULL, type, NULL, path, line, column);
  graph_add_edge(graph, owner_id, node->id, edge_kind, order);
  return node->id;
}

static const char *graph_expr_name(const Expr *expr) {
  if (!expr) return NULL;
  if (expr->kind == EXPR_STRING || expr->kind == EXPR_CHAR || expr->kind == EXPR_NUMBER || expr->kind == EXPR_BOOL || expr->kind == EXPR_NULL) return NULL;
  if (expr->kind == EXPR_CALL && expr->left) return graph_expr_name(expr->left);
  if (expr->text) return expr->text;
  return NULL;
}

static const char *graph_expr_value(const Expr *expr) {
  if (!expr) return NULL;
  if (expr->kind == EXPR_BOOL) return expr->bool_value ? "true" : "false";
  if (expr->kind == EXPR_NULL) return "null";
  if (expr->kind == EXPR_CALL && expr->prefix_deref) return "prefix-deref";
  if (expr->kind == EXPR_ARRAY_LITERAL && expr->array_repeat) return "repeat";
  if (expr->kind == EXPR_STRING || expr->kind == EXPR_CHAR || expr->kind == EXPR_NUMBER) return expr->text;
  return NULL;
}

static const char *graph_expr_type(const Expr *expr) {
  if (!expr) return NULL;
  if (expr->resolved_type && expr->resolved_type[0]) return expr->resolved_type;
  if (expr->kind == EXPR_STRING) return "String";
  return expr->kind == EXPR_CHAR ? "char" : NULL;
}

static ZProgramGraphNodeKind graph_expr_kind(const Expr *expr) {
  if (!expr) return Z_PROGRAM_GRAPH_NODE_EXPRESSION;
  switch (expr->kind) {
    case EXPR_IDENT: return Z_PROGRAM_GRAPH_NODE_IDENTIFIER;
    case EXPR_STRING:
    case EXPR_CHAR:
    case EXPR_NUMBER:
    case EXPR_BOOL:
    case EXPR_NULL: return Z_PROGRAM_GRAPH_NODE_LITERAL;
    case EXPR_MEMBER: return Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS;
    case EXPR_INDEX: return Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS;
    case EXPR_SLICE: return Z_PROGRAM_GRAPH_NODE_SLICE;
    case EXPR_CALL: return expr->left && expr->left->kind == EXPR_MEMBER ? Z_PROGRAM_GRAPH_NODE_METHOD_CALL : Z_PROGRAM_GRAPH_NODE_CALL;
    case EXPR_BINARY: return Z_PROGRAM_GRAPH_NODE_CALL;
    case EXPR_CAST: return Z_PROGRAM_GRAPH_NODE_CAST;
    case EXPR_BORROW: return Z_PROGRAM_GRAPH_NODE_BORROW;
    case EXPR_CHECK: return Z_PROGRAM_GRAPH_NODE_CHECK;
    case EXPR_RESCUE: return Z_PROGRAM_GRAPH_NODE_RESCUE;
    case EXPR_META: return Z_PROGRAM_GRAPH_NODE_META;
    case EXPR_SHAPE_LITERAL: return Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL;
    case EXPR_ARRAY_LITERAL: return Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL;
  }
  return Z_PROGRAM_GRAPH_NODE_EXPRESSION;
}

static const char *graph_build_expr(ZProgramGraph *graph, const SourceInput *input, const Expr *expr);

static void graph_build_expr_edges(ZProgramGraph *graph, const SourceInput *input, const Expr *expr, const char *node_id) {
  if (expr->left && expr->kind != EXPR_SHAPE_LITERAL) graph_add_edge(graph, node_id, graph_build_expr(graph, input, expr->left), "left", 0);
  if (expr->right) graph_add_edge(graph, node_id, graph_build_expr(graph, input, expr->right), "right", 1);
  for (size_t i = 0; i < expr->args.len; i++) graph_add_edge(graph, node_id, graph_build_expr(graph, input, expr->args.items[i]), "arg", i);
  for (size_t i = 0; i < expr->fields.len; i++) {
    const FieldInit *field = &expr->fields.items[i];
    const char *field_value = graph_build_expr(graph, input, field->value);
    ZProgramGraphNode *field_node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_FIELD_INIT, field->name, NULL, NULL, graph_source_path(input, field->line), graph_source_line(input, field->line), field->column);
    graph_add_edge(graph, field_node->id, field_value, "value", 0);
    graph_add_edge(graph, node_id, field_node->id, "field", i);
  }
  for (size_t i = 0; i < expr->type_args.len; i++) {
    const TypeArg *arg = &expr->type_args.items[i];
    graph_add_type_ref(graph, node_id, "typeArg", arg->type, graph_source_path(input, arg->line), graph_source_line(input, arg->line), arg->column, i);
  }
}

static const char *graph_build_expr(ZProgramGraph *graph, const SourceInput *input, const Expr *expr) {
  if (!expr) return NULL;
  const char *path = graph_source_path(input, expr->line);
  ZProgramGraphNode *node = graph_add_node(graph, graph_expr_kind(expr), graph_expr_name(expr), graph_expr_type(expr), graph_expr_value(expr), path, graph_source_line(input, expr->line), expr->column);
  const char *node_id = node->id;
  if (expr->kind == EXPR_BORROW) node->is_mutable = expr->mutable_borrow;
  graph_build_expr_edges(graph, input, expr, node_id);
  return node_id;
}

static ZProgramGraphNodeKind graph_stmt_kind(const Stmt *stmt) {
  if (!stmt) return Z_PROGRAM_GRAPH_NODE_STATEMENT;
  switch (stmt->kind) {
    case STMT_LET: return Z_PROGRAM_GRAPH_NODE_LET;
    case STMT_ASSIGN: return Z_PROGRAM_GRAPH_NODE_ASSIGNMENT;
    case STMT_DEFER: return Z_PROGRAM_GRAPH_NODE_DEFER;
    case STMT_CHECK: return Z_PROGRAM_GRAPH_NODE_CHECK;
    case STMT_RETURN: return Z_PROGRAM_GRAPH_NODE_RETURN;
    case STMT_EXPR: return Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT;
    case STMT_IF: return Z_PROGRAM_GRAPH_NODE_IF;
    case STMT_WHILE: return Z_PROGRAM_GRAPH_NODE_WHILE;
    case STMT_FOR: return Z_PROGRAM_GRAPH_NODE_FOR;
    case STMT_BREAK: return Z_PROGRAM_GRAPH_NODE_BREAK;
    case STMT_CONTINUE: return Z_PROGRAM_GRAPH_NODE_CONTINUE;
    case STMT_MATCH: return Z_PROGRAM_GRAPH_NODE_MATCH;
    case STMT_RAISE: return Z_PROGRAM_GRAPH_NODE_RAISE;
  }
  return Z_PROGRAM_GRAPH_NODE_STATEMENT;
}

static const char *graph_build_block(ZProgramGraph *graph, const SourceInput *input, const StmtVec *body, const char *name, const char *path, int line, int column);

static void graph_build_match_arms(ZProgramGraph *graph, const SourceInput *input, const Stmt *stmt, const char *stmt_id) {
  for (size_t i = 0; i < stmt->match_arms.len; i++) {
    const MatchArm *arm = &stmt->match_arms.items[i];
    const char *path = graph_source_path(input, arm->line);
    ZProgramGraphNode *arm_node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_MATCH_ARM, arm->case_name, NULL, arm->payload_name, path, graph_source_line(input, arm->line), arm->column);
    const char *arm_id = arm_node->id;
    graph_add_edge(graph, stmt_id, arm_id, "arm", i);
    if (arm->range_end) {
      ZProgramGraphNode *range_end = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_LITERAL, NULL, NULL, arm->range_end, path, graph_source_line(input, arm->line), arm->column);
      graph_add_edge(graph, arm_id, range_end->id, "rangeEnd", 0);
    }
    if (arm->guard) graph_add_edge(graph, arm_id, graph_build_expr(graph, input, arm->guard), "guard", 0);
    const char *body = graph_build_block(graph, input, &arm->body, "body", path, arm->line, arm->column);
    graph_add_edge(graph, arm_id, body, "body", 0);
  }
}

static const char *graph_build_stmt(ZProgramGraph *graph, const SourceInput *input, const Stmt *stmt) {
  const char *path = graph_source_path(input, stmt ? stmt->line : 0);
  ZProgramGraphNode *node = graph_add_node(graph, graph_stmt_kind(stmt), stmt ? stmt->name : NULL, stmt ? (stmt->type ? stmt->type : stmt->resolved_type) : NULL, NULL, path, graph_source_line(input, stmt ? stmt->line : 0), stmt ? stmt->column : 0);
  const char *node_id = node->id;
  if (!stmt) return node_id;
  node->is_mutable = stmt->mutable_binding;
  if (stmt->target) graph_add_edge(graph, node_id, graph_build_expr(graph, input, stmt->target), "target", 0);
  if (stmt->expr) graph_add_edge(graph, node_id, graph_build_expr(graph, input, stmt->expr), "expr", 0);
  if (stmt->range_end) graph_add_edge(graph, node_id, graph_build_expr(graph, input, stmt->range_end), "rangeEnd", 1);
  if (stmt->type) graph_add_type_ref(graph, node_id, "declaredType", stmt->type, path, graph_source_line(input, stmt->line), stmt->column, 0);
  if (stmt->then_body.len > 0) graph_add_edge(graph, node_id, graph_build_block(graph, input, &stmt->then_body, "then", path, stmt->line, stmt->column), "then", 0);
  if (stmt->else_body.len > 0) graph_add_edge(graph, node_id, graph_build_block(graph, input, &stmt->else_body, "else", path, stmt->line, stmt->column), "else", 1);
  graph_build_match_arms(graph, input, stmt, node_id);
  return node_id;
}

static const char *graph_build_block(ZProgramGraph *graph, const SourceInput *input, const StmtVec *body, const char *name, const char *path, int line, int column) {
  ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_BLOCK, name, NULL, NULL, path, graph_source_line(input, line), column);
  const char *node_id = node->id;
  for (size_t i = 0; body && i < body->len; i++) graph_add_edge(graph, node_id, graph_build_stmt(graph, input, body->items[i]), "statement", i);
  return node_id;
}

static void graph_build_params(ZProgramGraph *graph, const SourceInput *input, const ParamVec *params, const char *owner_id, const char *edge_kind) {
  for (size_t i = 0; params && i < params->len; i++) {
    const Param *param = &params->items[i];
    const char *path = graph_source_path(input, param->line);
    ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_PARAM, param->name, param->type, NULL, path, graph_source_line(input, param->line), param->column);
    const char *node_id = node->id;
    node->is_static = param->is_static;
    graph_add_edge(graph, owner_id, node_id, edge_kind, i);
    if (param->type) graph_add_type_ref(graph, node_id, "type", param->type, path, graph_source_line(input, param->line), param->column, 0);
    if (param->default_value) graph_add_edge(graph, node_id, graph_build_expr(graph, input, param->default_value), "default", 0);
  }
}

static void graph_build_function(ZProgramGraph *graph, const SourceInput *input, const Function *fun, const char *owner_id, const char *edge_kind, size_t order) {
  const char *path = graph_source_path(input, fun->line);
  ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_FUNCTION, fun->name, fun->return_type, fun->test_name, path, graph_source_line(input, fun->line), fun->column);
  const char *node_id = node->id;
  node->is_public = fun->is_public;
  node->fallible = fun->raises;
  node->export_c = fun->export_c;
  graph_add_edge(graph, owner_id, node_id, edge_kind, order);
  graph_build_params(graph, input, &fun->type_params, node_id, "typeParam");
  graph_build_params(graph, input, &fun->params, node_id, "param");
  graph_add_type_ref(graph, node_id, "returnType", fun->return_type, path, graph_source_line(input, fun->line), fun->column, 0);
  if (fun->raises) graph_add_edge(graph, node_id, graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_EFFECT_REF, "error", NULL, NULL, path, graph_source_line(input, fun->line), fun->column)->id, "effect", 0);
  for (size_t i = 0; i < fun->errors.len; i++) {
    const Param *error = &fun->errors.items[i];
    ZProgramGraphNode *error_node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT, error->name, error->type, NULL, graph_source_path(input, error->line), graph_source_line(input, error->line), error->column);
    graph_add_edge(graph, node_id, error_node->id, "error", i);
  }
  graph_add_edge(graph, node_id, graph_build_block(graph, input, &fun->body, "body", path, fun->line, fun->column), "body", 0);
}

static void graph_build_top_level_params(ZProgramGraph *graph, const SourceInput *input, const ParamVec *items, const char *owner_id, const char *edge_kind, ZProgramGraphNodeKind node_kind) {
  for (size_t i = 0; items && i < items->len; i++) {
    const Param *item = &items->items[i];
    ZProgramGraphNode *node = graph_add_node(graph, node_kind, item->name, item->type, NULL, graph_source_path(input, item->line), graph_source_line(input, item->line), item->column);
    const char *node_id = node->id;
    graph_add_edge(graph, owner_id, node_id, edge_kind, i);
    if (item->type) graph_add_type_ref(graph, node_id, "type", item->type, graph_source_path(input, item->line), graph_source_line(input, item->line), item->column, 0);
    if (item->default_value) graph_add_edge(graph, node_id, graph_build_expr(graph, input, item->default_value), "default", 0);
  }
}

static void graph_build_modules(ZProgramGraph *graph, const SourceInput *input) {
  if (input && input->module_count > 0) {
    for (size_t i = 0; i < input->module_count; i++) {
      graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_MODULE, input->module_names[i], NULL, NULL, input->module_paths[i], 1, 1);
    }
    return;
  }
  graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_MODULE, "main", NULL, NULL, input ? input->source_file : "", 1, 1);
}

static void graph_build_imports(ZProgramGraph *graph, const SourceInput *input, const Program *program) {
  for (size_t i = 0; program && i < program->c_imports.len; i++) {
    const CImport *item = &program->c_imports.items[i];
    const char *path = graph_source_path(input, item->line);
    ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_C_IMPORT, item->alias, NULL, item->header, path, graph_source_line(input, item->line), item->column);
    const char *module_id = graph_module_id_for_line(graph, input, item->line);
    graph_add_edge(graph, module_id, node->id, "cImport", graph_next_edge_order(graph, module_id, "cImport"));
  }
  for (size_t i = 0; program && i < program->use_imports.len; i++) {
    const UseImport *item = &program->use_imports.items[i];
    const char *path = graph_source_path(input, item->line);
    ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_IMPORT, item->module, NULL, item->alias, path, graph_source_line(input, item->line), item->column);
    const char *module_id = graph_module_id_for_line(graph, input, item->line);
    graph_add_edge(graph, module_id, node->id, "import", graph_next_edge_order(graph, module_id, "import"));
  }
}

static void graph_build_consts_and_aliases(ZProgramGraph *graph, const SourceInput *input, const Program *program) {
  for (size_t i = 0; program && i < program->consts.len; i++) {
    const ConstDecl *item = &program->consts.items[i];
    ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_CONST, item->name, item->type, NULL, graph_source_path(input, item->line), graph_source_line(input, item->line), item->column);
    const char *node_id = node->id;
    node->is_public = item->is_public;
    const char *module_id = graph_module_id_for_line(graph, input, item->line);
    graph_add_edge(graph, module_id, node_id, "const", graph_next_edge_order(graph, module_id, "const"));
    if (item->expr) graph_add_edge(graph, node_id, graph_build_expr(graph, input, item->expr), "value", 0);
  }
  for (size_t i = 0; program && i < program->aliases.len; i++) {
    const TypeAlias *item = &program->aliases.items[i];
    ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS, item->name, item->target, NULL, graph_source_path(input, item->line), graph_source_line(input, item->line), item->column);
    const char *node_id = node->id;
    node->is_public = item->is_public;
    const char *module_id = graph_module_id_for_line(graph, input, item->line);
    graph_add_edge(graph, module_id, node_id, "alias", graph_next_edge_order(graph, module_id, "alias"));
    graph_add_type_ref(graph, node_id, "target", item->target, graph_source_path(input, item->line), graph_source_line(input, item->line), item->column, 0);
  }
}

static void graph_build_aggregate_decls(ZProgramGraph *graph, const SourceInput *input, const Program *program) {
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    const Shape *shape = &program->shapes.items[i];
    ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_SHAPE, shape->name, NULL, shape->layout, graph_source_path(input, shape->line), graph_source_line(input, shape->line), shape->column);
    const char *node_id = node->id;
    node->is_public = shape->is_public;
    const char *module_id = graph_module_id_for_line(graph, input, shape->line);
    graph_add_edge(graph, module_id, node_id, "shape", graph_next_edge_order(graph, module_id, "shape"));
    graph_build_params(graph, input, &shape->type_params, node_id, "typeParam");
    graph_build_top_level_params(graph, input, &shape->fields, node_id, "field", Z_PROGRAM_GRAPH_NODE_FIELD);
    for (size_t method = 0; method < shape->methods.len; method++) graph_build_function(graph, input, &shape->methods.items[method], node_id, "method", method);
  }
  for (size_t i = 0; program && i < program->interfaces.len; i++) {
    const InterfaceDecl *item = &program->interfaces.items[i];
    ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_INTERFACE, item->name, NULL, NULL, graph_source_path(input, item->line), graph_source_line(input, item->line), item->column);
    const char *node_id = node->id;
    node->is_public = item->is_public;
    const char *module_id = graph_module_id_for_line(graph, input, item->line);
    graph_add_edge(graph, module_id, node_id, "interface", graph_next_edge_order(graph, module_id, "interface"));
    graph_build_params(graph, input, &item->type_params, node_id, "typeParam");
    for (size_t method = 0; method < item->methods.len; method++) graph_build_function(graph, input, &item->methods.items[method], node_id, "method", method);
  }
}

static void graph_build_sum_decls(ZProgramGraph *graph, const SourceInput *input, const Program *program) {
  for (size_t i = 0; program && i < program->enums.len; i++) {
    const EnumDecl *item = &program->enums.items[i];
    ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_ENUM, item->name, item->type, NULL, graph_source_path(input, item->line), graph_source_line(input, item->line), item->column);
    const char *node_id = node->id;
    node->is_public = item->is_public;
    const char *module_id = graph_module_id_for_line(graph, input, item->line);
    graph_add_edge(graph, module_id, node_id, "enum", graph_next_edge_order(graph, module_id, "enum"));
    graph_build_top_level_params(graph, input, &item->cases, node_id, "case", Z_PROGRAM_GRAPH_NODE_ENUM_CASE);
  }
  for (size_t i = 0; program && i < program->choices.len; i++) {
    const Choice *item = &program->choices.items[i];
    ZProgramGraphNode *node = graph_add_node(graph, Z_PROGRAM_GRAPH_NODE_CHOICE, item->name, NULL, NULL, graph_source_path(input, item->line), graph_source_line(input, item->line), item->column);
    const char *node_id = node->id;
    node->is_public = item->is_public;
    const char *module_id = graph_module_id_for_line(graph, input, item->line);
    graph_add_edge(graph, module_id, node_id, "choice", graph_next_edge_order(graph, module_id, "choice"));
    graph_build_top_level_params(graph, input, &item->cases, node_id, "case", Z_PROGRAM_GRAPH_NODE_CHOICE_CASE);
  }
}

bool z_program_graph_from_program(const SourceInput *input, const Program *program, ZProgramGraph *graph) {
  if (!graph) return false;
  z_program_graph_init(graph);
  free(graph->module_identity);
  graph->module_identity = graph_module_identity(input);
  graph_build_modules(graph, input);
  graph_build_imports(graph, input, program);
  graph_build_consts_and_aliases(graph, input, program);
  graph_build_aggregate_decls(graph, input, program);
  graph_build_sum_decls(graph, input, program);
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    const char *module_id = graph_module_id_for_line(graph, input, fun->line);
    graph_build_function(graph, input, fun, module_id, "function", graph_next_edge_order(graph, module_id, "function"));
  }
  z_program_graph_assign_source_node_ids(graph);
  z_program_graph_finalize_identities(graph);
  return true;
}
