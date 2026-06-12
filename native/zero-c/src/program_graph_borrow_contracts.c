#include "program_graph_contracts.h"

#include "program_graph_adjacency.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  char root[128];
  char path[256];
  char binding[128];
  const char *decl_path;
  int line;
  int column;
  bool mutable_borrow;
} ZGraphActiveBorrow;

static bool borrow_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool borrow_text_starts(const char *text, const char *prefix) {
  if (!text || !prefix) return false;
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

static const ZProgramGraphNode *borrow_child(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *node, const char *kind, size_t order) {
  if (!adjacency || !node || !kind) return NULL;
  size_t start = 0;
  size_t len = 0;
  z_program_graph_adjacency_owner_run(adjacency, node->id, kind, &start, &len);
  for (size_t i = start; i < start + len; i++) {
    const ZProgramGraphEdge *edge = z_program_graph_adjacency_owner_edge_at(adjacency, i);
    if (edge && edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->order == order) {
      return z_program_graph_adjacency_node(adjacency, edge->to);
    }
  }
  return NULL;
}

static bool borrow_append_path(char *path, size_t path_len, const char *field) {
  if (!path || path_len == 0 || !field || !field[0]) return false;
  size_t used = strlen(path);
  if (used > 0) {
    if (used + 1 >= path_len) return false;
    path[used++] = '.';
    path[used] = 0;
  }
  int written = snprintf(path + used, path_len - used, "%s", field);
  return written >= 0 && (size_t)written < path_len - used;
}

static bool borrow_place_from_expr(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *expr, char *root, size_t root_len, char *path, size_t path_len) {
  if (!expr || !root || !path || root_len == 0 || path_len == 0) return false;
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
    snprintf(root, root_len, "%s", expr->name ? expr->name : "");
    path[0] = 0;
    return root[0] != 0;
  }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) {
    const ZProgramGraphNode *left = borrow_child(adjacency, expr, "left", 0);
    if (!borrow_place_from_expr(adjacency, left, root, root_len, path, path_len)) return false;
    return borrow_append_path(path, path_len, expr->name);
  }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_BORROW) {
    return borrow_place_from_expr(adjacency, borrow_child(adjacency, expr, "left", 0), root, root_len, path, path_len);
  }
  return false;
}

static const ZGraphActiveBorrow *borrow_conflict(const ZGraphActiveBorrow *active, size_t active_len, const char *root, const char *path, bool mutable_op) {
  for (size_t i = 0; active && i < active_len; i++) {
    if (!borrow_text_eq(active[i].root, root) || !borrow_text_eq(active[i].path, path)) continue;
    if (mutable_op || active[i].mutable_borrow) return &active[i];
  }
  return NULL;
}

static void borrow_format_place(char *out, size_t out_len, const char *root, const char *path) {
  if (!out || out_len == 0) return;
  if (path && path[0]) snprintf(out, out_len, "%s.%s", root ? root : "", path);
  else snprintf(out, out_len, "%s", root ? root : "");
}

static bool fail_borrow_conflict(const ZProgramGraphNode *op, const ZGraphActiveBorrow *active, const char *root, const char *path, bool mutable_op, const char *fallback_path, ZDiag *diag) {
  if (diag) {
    char place[200];
    borrow_format_place(place, sizeof(place), root, path);
    *diag = (ZDiag){0};
    diag->code = 3029;
    diag->path = op && op->path && op->path[0] ? op->path : fallback_path;
    diag->line = op && op->line > 0 ? op->line : 1;
    diag->column = op && op->column > 0 ? op->column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "borrow conflicts with an active lexical borrow");
    snprintf(diag->expected, sizeof(diag->expected), "%s", mutable_op ? "unborrowed mutable lvalue" : "no active mutable borrow");
    snprintf(diag->actual, sizeof(diag->actual), "%.*s already has %s borrow", 80, place, active && active->mutable_borrow ? "a mutable" : "shared");
    snprintf(diag->help, sizeof(diag->help), "end the earlier borrow's scope before borrowing again");
    if (active) {
      diag->borrow_trace_count = 1;
      snprintf(diag->borrow_traces[0].root, sizeof(diag->borrow_traces[0].root), "%s", active->root);
      snprintf(diag->borrow_traces[0].path, sizeof(diag->borrow_traces[0].path), "%s", active->path);
      snprintf(diag->borrow_traces[0].kind, sizeof(diag->borrow_traces[0].kind), "%s", active->mutable_borrow ? "mutable" : "shared");
      snprintf(diag->borrow_traces[0].binding, sizeof(diag->borrow_traces[0].binding), "%s", active->binding);
      diag->borrow_traces[0].binding_decl_path = active->decl_path;
      diag->borrow_traces[0].binding_line = active->line;
      diag->borrow_traces[0].binding_column = active->column > 0 ? active->column : 1;
    }
    snprintf(diag->borrow_repair, sizeof(diag->borrow_repair), "move the conflicting borrow after the active borrow's lexical scope or put the earlier borrow in an inner block");
  }
  return false;
}

static bool borrow_let_contract_ok(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *stmt, ZGraphActiveBorrow *active, size_t *active_len, const char *path, ZDiag *diag) {
  const ZProgramGraphNode *expr = borrow_child(adjacency, stmt, "expr", 0);
  if (!expr || expr->kind != Z_PROGRAM_GRAPH_NODE_BORROW) return true;
  char root[128] = {0};
  char place[256] = {0};
  if (!borrow_place_from_expr(adjacency, expr, root, sizeof(root), place, sizeof(place))) return true;
  bool mutable_borrow = expr->is_mutable || borrow_text_starts(stmt->type, "mutref<");
  const ZGraphActiveBorrow *conflict = borrow_conflict(active, active_len ? *active_len : 0, root, place, mutable_borrow);
  if (conflict) return fail_borrow_conflict(expr, conflict, root, place, mutable_borrow, path, diag);
  if (active && active_len && *active_len < 64) {
    ZGraphActiveBorrow *slot = &active[(*active_len)++];
    snprintf(slot->root, sizeof(slot->root), "%s", root);
    snprintf(slot->path, sizeof(slot->path), "%s", place);
    snprintf(slot->binding, sizeof(slot->binding), "%s", stmt->name ? stmt->name : "");
    slot->decl_path = stmt->path;
    slot->line = stmt->line;
    slot->column = stmt->column;
    slot->mutable_borrow = mutable_borrow;
  }
  return true;
}

static bool borrow_assignment_contract_ok(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *stmt, const ZGraphActiveBorrow *active, size_t active_len, const char *path, ZDiag *diag) {
  const ZProgramGraphNode *target = borrow_child(adjacency, stmt, "target", 0);
  char root[128] = {0};
  char place[256] = {0};
  if (!borrow_place_from_expr(adjacency, target, root, sizeof(root), place, sizeof(place))) return true;
  const ZGraphActiveBorrow *conflict = borrow_conflict(active, active_len, root, place, true);
  return conflict ? fail_borrow_conflict(target, conflict, root, place, true, path, diag) : true;
}

static bool borrow_block_contract_ok(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *block, const char *path, ZDiag *diag) {
  ZGraphActiveBorrow active[64] = {0};
  size_t active_len = 0;
  size_t start = 0;
  size_t len = 0;
  z_program_graph_adjacency_owner_run(adjacency, block->id, "statement", &start, &len);
  for (size_t i = start; i < start + len; i++) {
    const ZProgramGraphEdge *edge = z_program_graph_adjacency_owner_edge_at(adjacency, i);
    if (!edge || edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
    const ZProgramGraphNode *stmt = z_program_graph_adjacency_node(adjacency, edge->to);
    if (!stmt) continue;
    if (stmt->kind == Z_PROGRAM_GRAPH_NODE_LET && !borrow_let_contract_ok(adjacency, stmt, active, &active_len, path, diag)) return false;
    if (stmt->kind == Z_PROGRAM_GRAPH_NODE_ASSIGNMENT && !borrow_assignment_contract_ok(adjacency, stmt, active, active_len, path, diag)) return false;
  }
  return true;
}

bool z_program_graph_borrow_contracts_ok(const ZProgramGraph *graph, const char *path, ZDiag *diag) {
  if (!graph || graph->node_len == 0) return true;
  ZProgramGraphAdjacency adjacency;
  z_program_graph_adjacency_init(&adjacency, graph);
  bool ok = true;
  for (size_t i = 0; ok && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_BLOCK) ok = borrow_block_contract_ok(&adjacency, &graph->nodes[i], path, diag);
  }
  z_program_graph_adjacency_free(&adjacency);
  return ok;
}
