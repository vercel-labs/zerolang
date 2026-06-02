#include "program_graph.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int graph_text_cmp(const char *left, const char *right) {
  const unsigned char *a = (const unsigned char *)(left ? left : "");
  const unsigned char *b = (const unsigned char *)(right ? right : "");
  while (*a && *b && *a == *b) {
    a++;
    b++;
  }
  return (int)*a - (int)*b;
}

static bool graph_text_eq(const char *left, const char *right) {
  return graph_text_cmp(left, right) == 0;
}

static uint64_t graph_hash_text(uint64_t hash, const char *text) {
  const unsigned char *p = (const unsigned char *)(text ? text : "");
  while (*p) {
    hash ^= (uint64_t)*p++;
    hash *= 1099511628211ull;
  }
  hash ^= 0xffu;
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t graph_hash_u64(uint64_t hash, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) {
    hash ^= (value >> (i * 8)) & 0xffu;
    hash *= 1099511628211ull;
  }
  return hash;
}

bool z_program_graph_node_id_valid(const char *id) {
  if (!id || id[0] != '#' || !id[1]) return false;
  for (const char *cursor = id + 1; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (!((ch >= 'a' && ch <= 'z') ||
          (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') ||
          ch == '_' || ch == '-' || ch == '.')) {
      return false;
    }
  }
  return true;
}

static const char *graph_node_id_domain(const ZProgramGraphNode *node) {
  if (!node) return "node";
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE: return "mod";
    case Z_PROGRAM_GRAPH_NODE_IMPORT: return "imp";
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT: return "cimp";
    case Z_PROGRAM_GRAPH_NODE_CONST:
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
    case Z_PROGRAM_GRAPH_NODE_ENUM:
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
      return "decl";
    case Z_PROGRAM_GRAPH_NODE_PARAM: return "param";
    case Z_PROGRAM_GRAPH_NODE_FIELD: return "field";
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
      return "case";
    case Z_PROGRAM_GRAPH_NODE_BLOCK: return "block";
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
    case Z_PROGRAM_GRAPH_NODE_MATCH_ARM:
    case Z_PROGRAM_GRAPH_NODE_STATEMENT:
      return "stmt";
    case Z_PROGRAM_GRAPH_NODE_TYPE_REF: return "type";
    case Z_PROGRAM_GRAPH_NODE_EFFECT_REF: return "effect";
    case Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT: return "err";
    default:
      return "expr";
  }
}

static bool graph_edge_order_participates(const ZProgramGraphNode *node, const ZProgramGraphEdge *edge) {
  if (!edge) return false;
  if (node && node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && graph_text_eq(edge->kind, "function")) return false;
  if (graph_text_eq(edge->kind, "statement")) return false;
  return true;
}

static uint64_t graph_node_id_hash_value(const ZProgramGraph *graph, const ZProgramGraphNode *node, const ZProgramGraphEdge *owner_edge, const char *owner_new_id) {
  uint64_t hash = 1469598103934665603ull;
  hash = graph_hash_text(hash, graph && graph->module_identity ? graph->module_identity : "");
  hash = graph_hash_text(hash, graph_node_id_domain(node));
  hash = graph_hash_text(hash, z_program_graph_node_kind_name(node->kind));
  hash = graph_hash_text(hash, node->type);
  hash = graph_hash_u64(hash, node->is_public ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_mutable ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_static ? 1 : 0);
  hash = graph_hash_u64(hash, node->fallible ? 1 : 0);
  hash = graph_hash_u64(hash, node->export_c ? 1 : 0);
  if (owner_edge) {
    hash = graph_hash_text(hash, owner_new_id);
    hash = graph_hash_text(hash, owner_edge->kind);
    if (graph_edge_order_participates(node, owner_edge)) hash = graph_hash_u64(hash, (uint64_t)owner_edge->order);
  } else if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE) {
    hash = graph_hash_text(hash, node->name);
  }
  return hash;
}

static bool graph_id_is_used(char **ids, size_t len, const char *id) {
  for (size_t i = 0; i < len; i++) {
    if (ids[i] && id && graph_text_eq(ids[i], id)) return true;
  }
  return false;
}

static size_t graph_find_old_id(char **old_ids, size_t len, const char *id) {
  for (size_t i = 0; old_ids && id && i < len; i++) {
    if (old_ids[i] && graph_text_eq(old_ids[i], id)) return i;
  }
  return SIZE_MAX;
}

static const ZProgramGraphEdge *graph_owner_edge_for_node(const ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && node_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && graph_text_eq(edge->to, node_id)) return edge;
  }
  return NULL;
}

static char *graph_source_node_base_id(const ZProgramGraph *graph, const ZProgramGraphNode *node, const ZProgramGraphEdge *owner_edge, const char *owner_new_id) {
  uint64_t hash = graph_node_id_hash_value(graph, node, owner_edge, owner_new_id);
  ZBuf base;
  zbuf_init(&base);
  zbuf_appendf(&base, "#%s_%08llx", graph_node_id_domain(node), (unsigned long long)(hash & 0xffffffffull));
  return base.data ? base.data : z_strdup("#node_00000000");
}

static uint64_t graph_collision_hash_value(const ZProgramGraphNode *node) {
  uint64_t hash = 1469598103934665603ull;
  hash = graph_hash_text(hash, graph_node_id_domain(node));
  hash = graph_hash_text(hash, z_program_graph_node_kind_name(node->kind));
  hash = graph_hash_text(hash, node->name);
  hash = graph_hash_text(hash, node->type);
  hash = graph_hash_text(hash, node->value);
  hash = graph_hash_text(hash, node->path);
  hash = graph_hash_u64(hash, (uint64_t)(node->line > 0 ? node->line : 0));
  hash = graph_hash_u64(hash, (uint64_t)(node->column > 0 ? node->column : 0));
  hash = graph_hash_u64(hash, node->is_public ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_mutable ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_static ? 1 : 0);
  hash = graph_hash_u64(hash, node->fallible ? 1 : 0);
  hash = graph_hash_u64(hash, node->export_c ? 1 : 0);
  return hash;
}

static char *graph_source_node_collision_id(const ZProgramGraphNode *node, const char *base_id, char **ids, size_t id_len, size_t rank) {
  ZBuf base;
  zbuf_init(&base);
  zbuf_append(&base, base_id);
  if (!graph_id_is_used(ids, id_len, base.data)) return base.data ? base.data : z_strdup("#node_00000000");
  uint64_t collision = graph_collision_hash_value(node);
  for (size_t attempt = 0;; attempt++) {
    ZBuf unique;
    zbuf_init(&unique);
    zbuf_append(&unique, base.data);
    if (attempt == 0 && rank == 0) zbuf_appendf(&unique, "-%04llx", (unsigned long long)(collision & 0xffffull));
    else zbuf_appendf(&unique, "-%04llx-%zu", (unsigned long long)(collision & 0xffffull), attempt + rank);
    if (graph_id_is_used(ids, id_len, unique.data)) {
      zbuf_free(&unique);
      continue;
    }
    zbuf_free(&base);
    return unique.data ? unique.data : z_strdup("#node_00000000");
  }
}

static const char *graph_remapped_id(const char *old_id, char **old_ids, char **new_ids, size_t len) {
  size_t index = graph_find_old_id(old_ids, len, old_id);
  return index == SIZE_MAX ? old_id : new_ids[index];
}

static int graph_collision_node_cmp(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  int cmp = graph_text_cmp(left ? left->name : NULL, right ? right->name : NULL);
  if (cmp != 0) return cmp;
  cmp = graph_text_cmp(left ? left->value : NULL, right ? right->value : NULL);
  if (cmp != 0) return cmp;
  cmp = graph_text_cmp(left ? left->type : NULL, right ? right->type : NULL);
  if (cmp != 0) return cmp;
  cmp = graph_text_cmp(left ? left->path : NULL, right ? right->path : NULL);
  if (cmp != 0) return cmp;
  int left_line = left && left->line > 0 ? left->line : 0;
  int right_line = right && right->line > 0 ? right->line : 0;
  if (left_line != right_line) return left_line < right_line ? -1 : 1;
  int left_column = left && left->column > 0 ? left->column : 0;
  int right_column = right && right->column > 0 ? right->column : 0;
  if (left_column != right_column) return left_column < right_column ? -1 : 1;
  return 0;
}

static void graph_sort_collision_group(const ZProgramGraph *graph, size_t *items, size_t len) {
  for (size_t i = 1; i < len; i++) {
    size_t item = items[i];
    size_t cursor = i;
    while (cursor > 0 && graph_collision_node_cmp(&graph->nodes[item], &graph->nodes[items[cursor - 1]]) < 0) {
      items[cursor] = items[cursor - 1];
      cursor--;
    }
    items[cursor] = item;
  }
}

void z_program_graph_assign_source_node_ids(ZProgramGraph *graph) {
  if (!graph || graph->node_len == 0) return;
  char **old_ids = z_checked_calloc(graph->node_len, sizeof(char *));
  char **new_ids = z_checked_calloc(graph->node_len, sizeof(char *));
  char **base_ids = z_checked_calloc(graph->node_len, sizeof(char *));
  bool *assigned = z_checked_calloc(graph->node_len, sizeof(bool));
  size_t *ready = z_checked_calloc(graph->node_len, sizeof(size_t));
  bool *ready_used = z_checked_calloc(graph->node_len, sizeof(bool));
  size_t *group = z_checked_calloc(graph->node_len, sizeof(size_t));
  for (size_t i = 0; i < graph->node_len; i++) old_ids[i] = z_strdup(graph->nodes[i].id ? graph->nodes[i].id : "");

  size_t assigned_count = 0;
  while (assigned_count < graph->node_len) {
    size_t ready_len = 0;
    for (size_t i = 0; i < graph->node_len; i++) {
      if (assigned[i]) continue;
      const ZProgramGraphEdge *owner_edge = graph_owner_edge_for_node(graph, old_ids[i]);
      const char *owner_new_id = NULL;
      if (owner_edge) {
        size_t owner_index = graph_find_old_id(old_ids, graph->node_len, owner_edge->from);
        if (owner_index != SIZE_MAX) {
          if (!assigned[owner_index]) continue;
          owner_new_id = new_ids[owner_index];
        }
      }
      free(base_ids[i]);
      base_ids[i] = graph_source_node_base_id(graph, &graph->nodes[i], owner_edge, owner_new_id);
      ready[ready_len++] = i;
    }
    if (ready_len == 0) {
      for (size_t i = 0; i < graph->node_len; i++) {
        if (assigned[i]) continue;
        free(base_ids[i]);
        base_ids[i] = graph_source_node_base_id(graph, &graph->nodes[i], NULL, NULL);
        ready[ready_len++] = i;
      }
    }

    for (size_t i = 0; i < ready_len; i++) ready_used[i] = false;
    for (size_t i = 0; i < ready_len; i++) {
      if (ready_used[i]) continue;
      size_t group_len = 0;
      for (size_t j = i; j < ready_len; j++) {
        if (ready_used[j] || !graph_text_eq(base_ids[ready[i]], base_ids[ready[j]])) continue;
        ready_used[j] = true;
        group[group_len++] = ready[j];
      }
      graph_sort_collision_group(graph, group, group_len);
      for (size_t j = 0; j < group_len; j++) {
        size_t index = group[j];
        new_ids[index] = graph_source_node_collision_id(&graph->nodes[index], base_ids[index], new_ids, graph->node_len, j);
        assigned[index] = true;
        assigned_count++;
      }
    }
  }

  for (size_t i = 0; i < graph->node_len; i++) {
    free(graph->nodes[i].id);
    graph->nodes[i].id = z_strdup(new_ids[i]);
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    const char *from = graph_remapped_id(graph->edges[i].from, old_ids, new_ids, graph->node_len);
    free(graph->edges[i].from);
    graph->edges[i].from = z_strdup(from);
    if (graph->edges[i].target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) {
      const char *to = graph_remapped_id(graph->edges[i].to, old_ids, new_ids, graph->node_len);
      free(graph->edges[i].to);
      graph->edges[i].to = z_strdup(to);
    }
  }
  for (size_t i = 0; i < graph->node_len; i++) {
    free(old_ids[i]);
    free(new_ids[i]);
    free(base_ids[i]);
  }
  free(group);
  free(ready_used);
  free(ready);
  free(assigned);
  free(base_ids);
  free(old_ids);
  free(new_ids);
}
